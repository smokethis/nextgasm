// main.cpp — Main entry point for the Nextgasm project
// Based on code from protogasm: https://github.com/night-howler/protogasm
//
// This file handles:
//   1. Defining global variables (the "real" copies that extern points to)
//   2. setup() — one-time hardware initialization
//   3. loop() — the 60Hz main tick that orchestrates everything
//
// The loop now has TWO layers of state:
//
//   AppState (menu.h)  — which "screen" are we on?
//     APP_MENU     → main menu, nav up/down/center to pick
//     APP_RUNNING  → device is operational, existing mode cycling
//     APP_SETTINGS → settings screen (placeholder)
//     APP_DEMO     → demo/attract mode (placeholder)
//
//   operationalState (config.h)  — within APP_RUNNING, which mode?
//     STANDBY, MANUAL, AUTO, OPT_SPEED, etc.
//
// NAV_UP always means "go back up one level":
//   - In APP_RUNNING/SETTINGS/DEMO → return to APP_MENU
//   - In APP_MENU → moves cursor up (handled by menu module)
//
// In Python terms, the structure is like:
//
//   while True:
//       if app_state == "menu":
//           app_state = menu.handle_input(nav)
//       elif app_state == "running":
//           if nav == UP:
//               app_state = "menu"
//           else:
//               operational_state_machine.tick(nav)
//       elif app_state == "settings":
//           ...

#include <Arduino.h>
#include <Encoder.h>
#include <EEPROM.h>
#include "FastLED.h"

#include "config.h"
#include "globals.h"
#include "leds.h"
#include "motor.h"
#include "pressure.h"
#include "buttons.h"
#include "modes.h"
#include "state.h"
#include "serial_report.h"
#include "oleddisplay.h"
#include "nav_switch.h"
#include "HT1632C_Display.h"
#include "menu.h"

// ============================================================
// File-scope objects
// ============================================================
HT1632C_Display ledMatrix;

// Convert operational mode constant to a display string for the LED matrix.
static const char* mode_to_string(uint8_t mode)
{
    switch (mode) {
        case MANUAL:        return "MANUAL";
        case AUTO:          return "AUTO";
        case OPT_SPEED:     return "SPEED";
        case OPT_RAMPSPD:   return "RAMP";
        case OPT_BEEP:      return "BEEP";
        case OPT_PRES:      return "PRES";
        case OPT_USER_MODE: return "MODE";
        case STANDBY:       return "STANDBY";
        default:            return "STANDBY";
    }
}

// ============================================================
// Global variable DEFINITIONS
// ============================================================
Encoder myEnc(3, 2);

int pressure = 0;
int averagePressure = 25;
float motorSpeed = 0;
int maxMotorSpeed = 255;
int sensitivity = 0;
int pressureLimit = 600;

int rampUp = 10;
int userMode = 6;
int userModeTotal = 6;
int pressureStep = 1;

int cooldown = 120;
int cooldownStep = 1;
int cooldownFlag = 1;
int maxCooldown = 180;
int minimumcooldown = 1;

// ============================================================
// Setup
// ============================================================
void setup()
{
    button_init();
    motor_init();
    pressure_init();
    nav_init();
    menu_init();

    pinMode(BUTTPIN, INPUT);
    analogReadResolution(12);

    delay(3000);  // Recovery delay for FastLED

    Serial.begin(115200);

    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS)
        .setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(BRIGHTNESS);

    display_init();
    ledMatrix.begin();

    // Recall saved settings from EEPROM
    sensitivity = EEPROM.read(SENSITIVITY_ADDR);
    maxMotorSpeed = min(EEPROM.read(MAX_SPEED_ADDR), MOT_MAX);

    beep_motor(1047, 1396, 2093);  // Power-on beep
}

// ============================================================
// Main loop — runs at 60Hz
// ============================================================
void loop()
{
    // ── Top-level app state ────────────────────────────────────────────
    // This is the "which screen" variable. It starts at APP_MENU so 
    // the device boots into the main menu rather than immediately 
    // entering operational mode.
    static AppState appState = APP_MENU;

    // ── Operational state (only matters when appState == APP_RUNNING) ──
    static uint8_t operationalState = STANDBY;

    // ── Shared loop state ──────────────────────────────────────────────
    static int sampleTick = 0;
    static unsigned long lastTick = 0;
    static NavDirection lastNavDir = NAV_NONE;

    // ── 60Hz tick gate ─────────────────────────────────────────────────
    if (millis() - lastTick < UPDATE_PERIOD_MS) return;
    lastTick = millis();
    sampleTick++;

    // Read the nav switch (debounced by the nav module)
    NavDirection navDir = nav_read();

    // Edge detection: did the direction just change this tick?
    // This prevents held directions from firing repeatedly.
    // In Python terms: navChanged = (navDir != lastNavDir)
    bool navChanged = (navDir != lastNavDir);

    // ── Dispatch based on app state ────────────────────────────────────
    // Each case is like a separate "screen" or "scene" with its own 
    // input handling, display updates, and peripheral control.

    switch (appState)
    {
        // ────────────────────────────────────────────────────────────────
        // MAIN MENU
        // ────────────────────────────────────────────────────────────────
        // The menu module handles its own cursor movement and selection.
        // We just pass it the nav input and check if it wants to 
        // transition to a different app state.
        case APP_MENU:
        {
            AppState nextAppState = menu_update(navDir);
            menu_render();
            ledMatrix.scrollText("NEXTGASM");

            // If the menu told us to go somewhere, set up for it
            if (nextAppState != APP_MENU)
            {
                appState = nextAppState;

                // When entering operational mode, start in STANDBY 
                // with the motor off — the user then uses left/right 
                // to navigate to the mode they want.
                if (appState == APP_RUNNING)
                {
                    operationalState = STANDBY;
                    motorSpeed = 0;
                    motor_write(0);
                }
            }
            break;
        }

        // ────────────────────────────────────────────────────────────────
        // OPERATIONAL MODE (the existing state machine)
        // ────────────────────────────────────────────────────────────────
        // This is where all the original protogasm functionality lives.
        // NAV_LEFT/RIGHT cycles through modes, NAV_CENTER → STANDBY,
        // and NAV_UP is our escape hatch back to the menu.
        case APP_RUNNING:
        {
            // ── NAV_UP: return to main menu ────────────────────────────
            // Safety first — stop the motor before leaving operational 
            // mode. We don't want the vibrator running unattended while 
            // the user is browsing the menu.
            if (navDir == NAV_UP && navChanged)
            {
                motorSpeed = 0;
                motor_write(0);
                menu_reset_cursor();
                appState = APP_MENU;
                break;  // Skip the rest of this tick
            }

            // ── Pressure sensing ───────────────────────────────────────
            update_pressure(sampleTick);

            // ── LED fade (creates trailing light effect) ───────────────
            fadeToBlackBy(leds, NUM_LEDS, 20);

            // ── Run current operational mode ───────────────────────────
            run_state_machine(operationalState);

            // ── Handle nav for mode cycling ────────────────────────────
            // This is the same logic that was in the old main.cpp, just 
            // using the shared navChanged flag instead of comparing to 
            // a separate lastNavDir.
            if (navChanged)
            {
                uint8_t nextState;
                switch (navDir) {
                    case NAV_LEFT:
                        nextState = get_previous_state(operationalState);
                        operationalState = nextState;
                        run_state_machine(operationalState);
                        break;

                    case NAV_RIGHT:
                        nextState = get_next_state(operationalState);
                        operationalState = nextState;
                        run_state_machine(operationalState);
                        break;

                    case NAV_CENTER:
                        operationalState = STANDBY;
                        run_state_machine(operationalState);
                        break;

                    default:
                        break;
                }
            }

            // ── Update outputs ─────────────────────────────────────────
            FastLED.show();
            ledMatrix.scrollText(mode_to_string(operationalState));
            display_update(operationalState, motorSpeed, pressure, averagePressure, navDir);

            // Warn if pressure sensor is railing (trimpot needs adjustment)
            if (pressure > 4030) beep_motor(2093, 2093, 2093);

            // Report data over USB
            report_serial();
            break;
        }

        // ────────────────────────────────────────────────────────────────
        // SETTINGS (placeholder)
        // ────────────────────────────────────────────────────────────────
        // For now, just shows a "coming soon" message. NAV_UP returns 
        // to the menu. This will eventually become its own submenu 
        // with configurable options.
        case APP_SETTINGS:
        {
            if (navDir == NAV_UP && navChanged)
            {
                menu_reset_cursor();
                appState = APP_MENU;
                break;
            }
            display_message("SETTINGS", "Coming soon...");
            ledMatrix.scrollText("SETTINGS");
            break;
        }

        // ────────────────────────────────────────────────────────────────
        // DEMO / ATTRACT MODE (placeholder)
        // ────────────────────────────────────────────────────────────────
        // Will eventually run a simulated session across all displays 
        // and outputs. For now, just a placeholder screen.
        case APP_DEMO:
        {
            if (navDir == NAV_UP && navChanged)
            {
                menu_reset_cursor();
                appState = APP_MENU;
                break;
            }
            display_message("DEMO", "Coming soon...");
            ledMatrix.scrollText("DEMO");
            break;
        }
    }

    // ── Update edge detection state ────────────────────────────────────
    // This MUST happen after all the switch cases, so every case can 
    // use the navChanged flag consistently. If we updated lastNavDir 
    // inside the cases, later cases in the same tick might see stale data.
    // (Not an issue with switch/break, but good practice for maintainability.)
    lastNavDir = navDir;
}
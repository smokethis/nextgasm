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
#include "colour_lcd.h"
#include "fire_effect.h"
#include "matrix_graph.h"
#include "sim_session.h"
#include "alphanum_display.h"

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

// ── Alphanumeric display helper ────────────────────────────────────────
// Shows the most useful at-a-glance debug value for each operational 
// mode. This runs every tick — the HT16K33 handles it fine since 
// the I2C transfer is only ~10 bytes (~0.2ms at 400kHz).
//
// What you see on the 4-digit display depends on which mode you're in:
//   STANDBY:       "STBY"
//   MANUAL:        "M" + motor speed as percentage (0-100)
//   AUTO:          "d" + pressure delta (what triggers edge detection)
//   OPT_SPEED:     "S" + current max speed setting (0-255)
//   OPT_PRES:      "P" + raw pressure reading
//   OPT_USER_MODE: "U" + current user mode number (1-6)
//   Other:         "----"
//
// In Python terms, this is like a dictionary dispatch:
//   display_map = {
//       STANDBY: lambda: show_text("STBY"),
//       MANUAL:  lambda: show_labeled('M', motor_pct),
//       AUTO:    lambda: show_labeled('d', delta),
//   }
//   display_map.get(mode, lambda: show_text("----"))()

static void alphanum_update_running(uint8_t mode)
{
    switch (mode) {
        case STANDBY:
            alphanum_show_text("STBY");
            break;

        case MANUAL:
        {
            // Show motor speed as a percentage (0-100%).
            // More intuitive than raw 0-255 at a glance.
            int speedPct = (int)(motorSpeed / MOT_MAX * 100);
            alphanum_show_labeled('M', speedPct);
            break;
        }

        case AUTO:
        {
            // Show pressure delta — this is THE key value for 
            // understanding what the edging algorithm is seeing.
            // When this exceeds pressureLimit, the motor cuts off.
            int delta = pressure - averagePressure;
            alphanum_show_labeled('d', delta);
            break;
        }

        case OPT_SPEED:
            alphanum_show_labeled('S', maxMotorSpeed);
            break;

        case OPT_PRES:
        {
            // Raw pressure — useful when adjusting the trimpot.
            // Divides by 4 to fit in 3 digits (max ~1023).
            int rawDisplay = analogRead(BUTTPIN) / 4;
            alphanum_show_labeled('P', rawDisplay);
            break;
        }

        case OPT_USER_MODE:
            alphanum_show_labeled('U', userMode);
            break;

        default:
            alphanum_show_text("----");
            break;
    }
}

// ── Alphanumeric demo mode display helper ──────────────────────
// Alternates between two "pages" on the 4-digit display:
//
//   Page 1 (3 seconds):  "A" + arousal value        e.g. "A 42"
//   Page 2 (3 seconds):  "H" + BPM + dot on beat    e.g. "H 72" → "H 72."
//
// The dot on the last digit flashes for exactly one tick (1/60th 
// second) when a simulated heartbeat occurs — a tiny visual pulse, 
// like the LED on a heart rate monitor.
//
// The alternation uses a simple tick counter. At 60Hz and 3 seconds 
// per page, each page shows for 180 ticks. In Python terms:
//
//   page = (tick_count // 180) % 2
//   if page == 0: show_arousal()
//   else:         show_heartrate_with_beat_dot()

static void alphanum_demo_tick()
{
    // 'static' variables persist between calls — they're like 
    // instance variables on a Python class. The tick counter keeps 
    // counting across calls, driving the page alternation.
    static unsigned int demoDisplayTick = 0;
    demoDisplayTick++;

    // ── Smoothed BPM for display ───────────────────────────────────
    // The raw sim_bpm jitters by ±1-2 each tick, which makes the 
    // number bounce distractingly on a 4-digit display. We smooth 
    // it with an exponential moving average (EMA) — the same idea 
    // as the pressure running average, but much simpler to implement.
    //
    // EMA formula:  smoothed = α × new + (1 - α) × smoothed
    //
    // α (alpha) controls how quickly the average responds to changes:
    //   α = 1.0 → no smoothing (just the raw value)
    //   α = 0.0 → never updates (stuck at initial value)
    //   α ≈ 0.065 → roughly equivalent to averaging the last 30 samples
    //
    // Why 30 samples? At 60Hz, 30 samples = 500ms — enough to smooth 
    // out tick-to-tick noise while still tracking genuine BPM changes 
    // (which happen over seconds, not milliseconds).
    //
    // The equivalent in Python would be:
    //   smoothed_bpm = 0.065 * sim_bpm + 0.935 * smoothed_bpm
    //
    // Unlike a simple moving average (which needs a buffer of N past 
    // values), an EMA needs just one float. The tradeoff is that older 
    // values never fully disappear — they just fade exponentially. For 
    // display smoothing, that's actually ideal.
    static float smoothedBpm = 0.0;
    constexpr float BPM_ALPHA = 0.065;  // ≈ 2/(30+1), ~500ms window

    // On first call, seed the EMA with the current value so it 
    // doesn't have to "ramp up" from zero.
    if (demoDisplayTick == 0) {
        smoothedBpm = (float)sim_bpm;
    } else {
        smoothedBpm = BPM_ALPHA * sim_bpm + (1.0 - BPM_ALPHA) * smoothedBpm;
    }

    // 3 seconds per page at 60Hz = 180 ticks per page.
    constexpr unsigned int TICKS_PER_PAGE = 180;
    unsigned int page = (demoDisplayTick / TICKS_PER_PAGE) % 2;

    if (sim_beat) {
        alphanum_set_dot(0);
    }

    if (page == 0)
    {
        // ── Page 1: Arousal ────────────────────────────────────────
        alphanum_show_labeled('A', sim_arousal);
    }
    else
    {
        // ── Page 2: Heart rate with beat indicator ─────────────────
        // Show "H" + 3-digit BPM, then layer the dot on beat frames.
        // alphanum_show_labeled writes all 4 digits and flushes.
        // alphanum_set_dot adds the dot on top and re-flushes.
        // Two I2C writes per beat frame (~0.4ms total) is negligible.
        alphanum_show_labeled('H', (int)(smoothedBpm + 0.5));  // Round to nearest int
    }
    // ── Beat dot persistence ───────────────────────────────────────
    // sim_beat is only true for one tick, but the dot needs to stay 
    // visible long enough to actually see. We use a countdown: beat 
    // sets it to N, then every tick we re-apply dots until it expires.
    static int beatDotTimer = 0;

    if (sim_beat) {
        beatDotTimer = 4;
    }

    // Apply dots AFTER the character write so they survive the flush
    if (beatDotTimer > 0) {
        for (uint8_t i = 0; i < 4; i++) {
            alphanum_set_dot(i);
        }
        beatDotTimer--;
    }
}

// ── Fire heartbeat helper ──────────────────────
// Adds heat to the fire displayed on every heartbeat.
//
static void add_heat() {

    static float beatHeat = 0.0f;

    // When the sim pulses a beat, inject heat
    if (sim_beat) {
        beatHeat += 6.0f;           // The "bump" — tune this for visual punch
    }

    // Exponential decay every tick — the heat fades smoothly between beats
    // At 0.92 per tick and 60Hz, the half-life is about 8 ticks (~130ms),
    // so the pulse is visible but doesn't linger past the next beat.
    beatHeat *= 0.92f;

    // GSR lifts the entire intensity range as the session deepens.
    // sim_gsr ranges ~0.15 (resting) to ~0.85 (deep session + phasic).
    //
    // Early session (GSR ≈ 0.15):  baseline range  8 → 24  (cool, modest)
    // Deep session  (GSR ≈ 0.70):  baseline range 12 → 30  (hotter, taller)
    //
    // The multiplier (8.0) controls how much "lift" GSR provides.
    // At max GSR, it adds about 6 heat units — enough to visibly shift
    // the fire's colour palette from orange toward yellow/white.
    float gsrLift = sim_gsr * 8.0f;
    float baseline = map(constrain(sim_arousal, 0, 600), 0, 600, 8, 24) + gsrLift;

    // Combine and clamp to palette range (0–36)
    uint8_t totalHeat = constrain((int)(baseline + beatHeat + 0.5f), 0, 36);
    fire_set_intensity(totalHeat);

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
    matrix_graph_init();
    // sim_arousal_init();
    lcd_init();
    fire_init();    // Seed the fire buffer
    alphanum_init();  // Quad alphanumeric display (I2C 0x70)

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

    // -- Set up previousAppState and declare it to be APP_MENU
    static AppState prevAppState = APP_MENU;

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
            alphanum_show_text("MENU");

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
            alphanum_update_running(operationalState);

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
            alphanum_show_text("SET");
            break;
        }

        // ────────────────────────────────────────────────────────────────
        // DEMO / ATTRACT MODE
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

            // display_message("DEMO", "Watch the screens...");
            display_demo_water(sim_gsr);
            
            // Show arousal value on OLED, deprecated for alphanumeric display
            // char arousal[32];
            // sprintf(arousal, "Arousal: %d", simDelta);
            alphanum_demo_tick();
            // Feed simulated arousal data to the matrix graph
            matrix_graph_tick(sim_arousal, MAX_PRESSURE_LIMIT, ledMatrix);
            
            // Advance the simulation by one tick
            sim_tick();

            // Render fire to LCD
            add_heat();
            fire_tick();
            break;
        }
    }

    // --- Detect change of state and take one-time actions
    if (appState != prevAppState)
        // ── On-enter actions for the NEW state ─────────────────────
        switch (appState)
        {
            case APP_MENU:
                ledMatrix.clear();
                ledMatrix.flush();
                SPI.beginTransaction(SPISettings(24000000, MSBFIRST, SPI_MODE3));
                lcd_fill(0x0001);
                SPI.endTransaction();
                break;

            case APP_RUNNING:
                // could reset displays here too
                break;

            case APP_DEMO:
                // reinit the sim, clear displays, etc.
                break;

            default:
                break;
        }
    
    // -- Update prevAppState to be current appState
    prevAppState = appState;

    // ── Update edge detection state ────────────────────────────────────
    // This MUST happen after all the switch cases, so every case can 
    // use the navChanged flag consistently. If we updated lastNavDir 
    // inside the cases, later cases in the same tick might see stale data.
    // (Not an issue with switch/break, but good practice for maintainability.)
    lastNavDir = navDir;
}
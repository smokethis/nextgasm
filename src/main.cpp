// main.cpp — Main entry point for the Nextgasm project
// Based on code from protogasm: https://github.com/night-howler/protogasm
//
// This file now only handles:
//   1. Defining global variables (the "real" copies that extern points to)
//   2. setup() — one-time hardware initialization
//   3. loop() — the 60Hz main tick that orchestrates everything

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

// ============================================================
// File-scope objects
// ============================================================
// The LED matrix lives here at file scope so it persists for the 
// lifetime of the program. Previously it was declared inside setup(),
// which meant C++ destroyed it when setup() returned — like a local 
// variable going out of scope in a Python function. The object still 
// existed in memory (embedded systems don't reclaim stack frames the 
// same way), but it was technically "dead" and any future use would 
// be undefined behaviour.
HT1632C_Display ledMatrix;

// Convert mode constant to a display string.
// There's an identical copy in oleddisplay.cpp (as a static function).
// We could factor this into a shared header, but for a tiny switch 
// statement the duplication is harmless and avoids creating a new 
// module just for one helper.
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

    pinMode(BUTTPIN, INPUT);

    analogReadResolution(12);

    delay(3000);  // Recovery delay for FastLED

    Serial.begin(115200);

    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS)
        .setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(BRIGHTNESS);

    display_init();

    // Initialize the LED matrix — now using the file-scope instance
    // instead of a local that would vanish after setup().
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
    static uint8_t state = STANDBY;
    static int sampleTick = 0;
    static unsigned long lastTick = 0;
    static uint8_t nextState;
    static NavDirection lastNavDir;

    // Track previous state so we only redraw the matrix when 
    // the mode actually changes. The HT1632C bit-bang write is 
    // fast (~50µs for 48 bytes at Teensy 4.0 speeds), but 
    // there's no point redrawing identical content 60 times 
    // per second. This is the same idea as React's "only 
    // re-render when state changes" philosophy.
    static uint8_t lastDisplayedState = 255;  // Invalid initial value forces first draw

    if (millis() - lastTick >= UPDATE_PERIOD_MS) {
        lastTick = millis();
        sampleTick++;

        // Update pressure reading and running average
        update_pressure(sampleTick);

        // Read 5-way nav switch (debounced)
        NavDirection navDir = nav_read();

        // Fade LED buffer (creates trailing light effect)
        fadeToBlackBy(leds, NUM_LEDS, 20);

        // DEPRECATE USE OF ENCODER BUTTON - Handle button input and state transitions
        // uint8_t btnState = check_button();
        // state = set_state(btnState, state);

        // Run state machine 
        run_state_machine(state);

        // Check if direction has been pressed and take action if so. 
        // Be sure to only activate once when changing modes.
        switch (navDir) {
            case NAV_LEFT:
                if (lastNavDir != NAV_LEFT) {
                    nextState = get_previous_state(state);
                    run_state_machine(nextState);
                    state = nextState;
                    lastNavDir = navDir;
                }
                break;
            case NAV_RIGHT:
                if (lastNavDir != NAV_RIGHT) {
                    nextState = get_next_state(state);
                    run_state_machine(nextState);
                    state = nextState;
                    lastNavDir = navDir;
                }
                break;
            case NAV_UP:
                if (lastNavDir != NAV_UP) {
                    lastNavDir = navDir;
                }
                break;
            case NAV_DOWN:
                if (lastNavDir != NAV_DOWN) {
                    lastNavDir = navDir;
                }
                break;
            case NAV_CENTER: 
                state = STANDBY;
                run_state_machine(state);
                lastNavDir = navDir;
                break;
            case NAV_NONE:
                lastNavDir = navDir;
                break;
        }

        // Push LED buffer to hardware
        FastLED.show();

        // Update LED matrix when mode changes
        if (state != lastDisplayedState) {
            ledMatrix.clear();
            ledMatrix.drawString(0, mode_to_string(state));
            ledMatrix.flush();
            lastDisplayedState = state;
        }
        
        // Run OLED display update routine (now includes nav direction)
        display_update(state, motorSpeed, pressure, averagePressure, navDir);

        // Warn if pressure sensor is railing (trimpot needs adjustment)
        if (pressure > 4030) beep_motor(2093, 2093, 2093);

        // Report data over USB
        report_serial();
    }
}
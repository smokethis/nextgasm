// main.cpp — Main entry point for the Nextgasm project
// Forked from protogasm: https://github.com/night-howler/protogasm
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

// ── LED matrix display ─────────────────────────────────────────────────
// 'static' at file scope means "visible only in this file" — like a
// Python module-level variable with a leading underscore. It persists
// for the lifetime of the program, unlike the local variable we had
// before which died at the end of setup().
static HT1632C_Display ledMatrix;

// Throttle matrix updates separately from the OLED. The HT1632C is
// faster than I2C (bit-banged at GPIO speed) but we still don't need
// to refresh it every single 60Hz tick.
constexpr unsigned long MATRIX_UPDATE_INTERVAL_MS = 50;  // 20Hz
static unsigned long lastMatrixUpdate = 0;

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

    // Initialise the HT1632C LED matrix
    ledMatrix.begin();

    // Brief startup test: fill all LEDs, pause, then clear.
    // If you see the matrix flash fully lit then go dark, the
    // wiring and initialisation are working correctly.
    ledMatrix.fill();
    ledMatrix.flush();
    delay(500);
    ledMatrix.clear();
    ledMatrix.drawString(0, "READY");
    ledMatrix.flush();

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
    static uint8_t state = MANUAL;
    static int sampleTick = 0;
    static unsigned long lastTick = 0;

    if (millis() - lastTick >= UPDATE_PERIOD_MS) {
        lastTick = millis();
        sampleTick++;

        // Update pressure reading and running average
        update_pressure(sampleTick);

        // Read 5-way nav switch (debounced)
        NavDirection navDir = nav_read();

        // Fade LED buffer (creates trailing light effect)
        fadeToBlackBy(leds, NUM_LEDS, 20);

        // Handle button input and state transitions
        uint8_t btnState = check_button();
        state = set_state(btnState, state);
        run_state_machine(state);

        // Push LED buffer to hardware
        FastLED.show();
        
        // Run OLED display update routine
        display_update(state, motorSpeed, pressure, averagePressure, navDir);

        // ── LED matrix update ──────────────────────────────────────
        // Show the nav switch direction on the matrix as a quick test.
        // Later this will display arousal history, patterns, etc.
        unsigned long now = millis();
        if (now - lastMatrixUpdate >= MATRIX_UPDATE_INTERVAL_MS) {
            lastMatrixUpdate = now;

            ledMatrix.clear();

            if (navDir != NAV_NONE) {
                // Show direction name: "Up", "Down", "Left", etc.
                // The 24-column display fits exactly 4 characters at
                // 6 pixels each (5px char + 1px gap), so "Down" and
                // "Left" fit perfectly, "Right" gets clipped to "Righ"
                // and "Press" to "Pres" — fine for testing purposes.
                ledMatrix.drawString(0, nav_direction_name(navDir));
            } else {
                // When nothing pressed, show a small idle dot in the
                // center so you can tell the display is alive vs stuck.
                // Column 12 (middle), row 4 (middle) — one lonely pixel.
                ledMatrix.setPixel(12, 4);
            }

            ledMatrix.flush();
        }

        // Warn if pressure sensor is railing (trimpot needs adjustment)
        if (pressure > 4030) beep_motor(2093, 2093, 2093);

        // Report data over USB
        report_serial();
    }
}
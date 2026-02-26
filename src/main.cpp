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

// ============================================================
// Setup
// ============================================================
void setup()
{
    button_init();
    motor_init();
    pressure_init();
    nav_init();       // Set up 5-way navigation switch pins

    pinMode(BUTTPIN, INPUT);

    analogReadResolution(12);

    delay(3000);  // Recovery delay for FastLED

    Serial.begin(115200);

    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS)
        .setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(BRIGHTNESS);

    display_init();

    // Create display instance with default pins (CS=6, WR=7, DATA=8)
    HT1632C_Display ledMatrix;
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
        
        // Run OLED display update routine (now includes nav direction)
        display_update(state, motorSpeed, pressure, averagePressure, navDir);

        // Warn if pressure sensor is railing (trimpot needs adjustment)
        if (pressure > 4030) beep_motor(2093, 2093, 2093);

        // Report data over USB
        report_serial();
    }
}
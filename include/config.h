// config.h — Central configuration for the nextgasm project
// 
#pragma once
#include <Arduino.h>

// --- Hardware Pins ---
// These are simple numeric constants. constexpr gives them a 
// proper type, so the compiler can warn you if you accidentally 
// pass a pin number where a speed value was expected, for example.

constexpr uint8_t NUM_LEDS = 24;
constexpr uint8_t LED_PIN = 10;
constexpr uint8_t BRIGHTNESS = 50;

constexpr uint8_t ENC_SW = 5;
constexpr bool ENC_SW_UP = HIGH;
constexpr bool ENC_SW_DOWN = LOW;

constexpr uint8_t MOTPIN = 9;
constexpr uint8_t BUTTPIN = A0;

// --- ADC / Sampling ---
constexpr uint8_t OVERSAMPLE = 4;
constexpr uint16_t ADC_MAX = 4095;  // Updated for 12-bit Teensy ADC

// --- Timing ---
constexpr uint8_t FREQUENCY = 60;
constexpr uint16_t LONG_PRESS_MS = 600;
constexpr uint16_t V_LONG_PRESS_MS = 2500;
constexpr unsigned long UPDATE_PERIOD_MS = 1000 / FREQUENCY;

// --- Pressure averaging ---
constexpr uint8_t RA_HIST_SECONDS = 2;
constexpr uint8_t RA_FREQUENCY = 6;
constexpr uint8_t RA_TICK_PERIOD = FREQUENCY / RA_FREQUENCY;

// --- Motor limits ---
constexpr uint8_t MOT_MAX = 255;
constexpr uint8_t MOT_MIN = 20;
constexpr uint16_t MAX_PRESSURE_LIMIT = 600;

// --- EEPROM addresses ---
constexpr uint8_t BEEP_ADDR = 1;
constexpr uint8_t MAX_SPEED_ADDR = 2;
constexpr uint8_t SENSITIVITY_ADDR = 3;

// --- State machine modes ---
constexpr uint8_t STANDBY = 1;
constexpr uint8_t MANUAL = 2;
constexpr uint8_t AUTO = 3;
constexpr uint8_t OPT_SPEED = 4;
constexpr uint8_t OPT_RAMPSPD = 5;
constexpr uint8_t OPT_BEEP = 6;
constexpr uint8_t OPT_PRES = 7;
constexpr uint8_t OPT_USER_MODE = 8;

// --- Button states ---
constexpr uint8_t BTN_NONE = 0;
constexpr uint8_t BTN_SHORT = 1;
constexpr uint8_t BTN_LONG = 2;
constexpr uint8_t BTN_V_LONG = 3;

// --- Debug options ---
constexpr bool DEBUG_MODE = true; // Enter general debug mode - don't print rolling output to serial
constexpr bool DEBUG_PRESSURE = false; // Print pressure values to serial
constexpr bool DEBUG_MOTOR = false; // Print motor values to serial
constexpr bool DEBUG_BUTTONS = true; // Print button actions to serial

// --- FastLED configuration ---
// These MUST stay as #defines. FastLED uses them as template 
// parameters, which require the values to be substituted at the 
// preprocessor level before the compiler sees them. 
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
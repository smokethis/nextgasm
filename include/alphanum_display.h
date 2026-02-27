// alphanum_display.h — Adafruit Quad Alphanumeric Display (HT16K33) interface
//
// Drives a 4-character 14-segment LED display over I2C. This is the
// Adafruit product #2158 (yellow 0.54" digits) with HT16K33 backpack.
//
// The HT16K33 is an I2C LED matrix driver that handles all the 
// multiplexing internally — we just tell it which segments to light
// and it takes care of refreshing them. Much simpler than the 
// HT1632C bit-banging we did for the LED matrix!
//
// This display shares the I2C bus with the OLED (SH1106 at 0x3C).
// The HT16K33 defaults to address 0x70, so there's no conflict.
// In Python terms, it's like two objects both using the same serial 
// port but listening for different "addresses" — only one responds 
// to each message.
//
// GREAT FOR DEBUG because:
//   - 14-segment displays can show full alphanumerics (A-Z, 0-9)
//   - No refresh rate concerns — the HT16K33 handles multiplexing
//   - I2C writes are fast enough to update every single 60Hz tick
//   - You can glance at it without needing serial monitor open
//
// PIN CONNECTIONS:
//   Display VCC  → 3.3V rail
//   Display GND  → GND rail  
//   Display SDA  → Teensy pin 18 (shared with OLED)
//   Display SCL  → Teensy pin 19 (shared with OLED)

#pragma once

#include <Arduino.h>

// Default I2C address for the HT16K33 backpack.
// Changeable via solder jumpers A0/A1/A2 on the back of the board.
// Address = 0x70 + A2*4 + A1*2 + A0*1, giving range 0x70–0x77.
constexpr uint8_t ALPHANUM_I2C_ADDR = 0x70;

// ── Initialisation ─────────────────────────────────────────────────────

// Set up the display. Call once from setup().
// Returns true if the display was found on the I2C bus.
bool alphanum_init();

// ── Display functions ──────────────────────────────────────────────────

// Show a 4-character string. If shorter than 4 chars, remaining 
// positions are blanked. If longer, only first 4 chars are shown.
//
// Example: alphanum_show_text("AUTO")  →  displays "AUTO"
//          alphanum_show_text("Hi")    →  displays "Hi  "
void alphanum_show_text(const char* text);

// Show an integer value, right-aligned. Handles negative numbers.
// Good for displaying pressure delta, motor speed, etc.
//
// Examples: alphanum_show_int(42)    →  displays "  42"
//           alphanum_show_int(-7)    →  displays "  -7"
//           alphanum_show_int(1234)  →  displays "1234"
//           alphanum_show_int(99999) →  displays "9999" (clamped)
void alphanum_show_int(int value);

// Show a label (1 char) and a value (up to 3 digits), right-aligned.
// Perfect for debug: a letter identifying what you're showing, plus 
// the numeric value.
//
// Examples: alphanum_show_labeled('d', 42)   →  displays "d 42"
//           alphanum_show_labeled('M', 255)  →  displays "M255"
//           alphanum_show_labeled('P', 7)    →  displays "P  7"
void alphanum_show_labeled(char label, int value);

// Clear the display (all segments off).
void alphanum_clear();

// Set brightness (0 = dimmest, 15 = brightest).
// Default after init is 8 (mid-range).
void alphanum_set_brightness(uint8_t level);
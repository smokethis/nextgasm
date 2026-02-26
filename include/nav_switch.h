// nav_switch.h — 5-way navigation switch interface
//
// Reads a 5-way tactile nav switch (up/down/left/right/center press).
// Designed as a parallel input system alongside the rotary encoder —
// the encoder stays for "dial-like" adjustments, while this handles
// menu navigation and selection.
//
// The switch works like 5 independent buttons sharing a common pin.
// Pressing a direction connects that pin to COM (ground), so we use
// INPUT_PULLUP and read LOW = pressed, HIGH = released.

#pragma once

#include <Arduino.h>

// ── Pin assignments ────────────────────────────────────────────────────
// Chosen to avoid conflicts with existing I/O. All on the right-hand
// side of the Teensy 4.0 board for tidy wiring.
//
// Already in use:
//   2,3   = Encoder quadrature    5     = Encoder button
//   6,7,8 = HT1632C display       9     = Motor PWM
//   10    = NeoPixel               14/A0 = Pressure sensor
//   18    = I2C SDA                19    = I2C SCL
//
// Free and physically adjacent on the right edge:
constexpr uint8_t NAV_PIN_UP     = 15;
constexpr uint8_t NAV_PIN_DOWN   = 16;
constexpr uint8_t NAV_PIN_LEFT   = 17;
constexpr uint8_t NAV_PIN_RIGHT  = 20;
constexpr uint8_t NAV_PIN_CENTER = 21;

// ── Direction constants ────────────────────────────────────────────────
// These are like a Python enum. Each direction gets a unique value,
// with NAV_NONE meaning "nothing pressed." Using an enum instead of
// bare ints means the compiler catches typos — if you write NAV_DOWNN
// it's an error, whereas with #define 2 it would silently be wrong.
enum NavDirection : uint8_t {
    NAV_NONE   = 0,
    NAV_UP     = 1,
    NAV_DOWN   = 2,
    NAV_LEFT   = 3,
    NAV_RIGHT  = 4,
    NAV_CENTER = 5
};

// ── Public interface ───────────────────────────────────────────────────

// Configure pins. Call once from setup().
void nav_init();

// Poll the switch and return which direction is currently pressed.
// Returns NAV_NONE if nothing is pressed.
// Call this once per main loop tick — the 60Hz sample rate provides
// natural debouncing since mechanical switch bounce is typically <10ms
// and our sample period is ~16ms.
NavDirection nav_read();

// Get a human-readable string for a direction (for display/debug).
// Returns a pointer to a string literal, e.g. "Up", "Center", "None".
const char* nav_direction_name(NavDirection dir);
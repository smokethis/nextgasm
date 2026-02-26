// nav_switch.cpp — 5-way navigation switch implementation
//
// Each direction on the switch is just a normally-open momentary button
// that connects its pin to the common (COM) pin when pressed. We wire
// COM to GND, and use the Teensy's internal pull-up resistors to keep
// the pins HIGH when not pressed. So:
//
//   Pin reads HIGH → button released (pull-up holds it up)
//   Pin reads LOW  → button pressed  (switch connects pin to GND)
//
// This "active low" pattern is extremely common in embedded systems.
// It's the opposite of what you might expect (pressed = LOW not HIGH),
// but it's standard because pull-up resistors are built into most MCUs
// while pull-downs often aren't.
//
// DEBOUNCING:
// Mechanical switches don't make clean transitions — the metal contacts
// literally bounce for a few milliseconds, producing rapid on-off-on
// noise. We handle this with a simple approach: require the same reading
// for DEBOUNCE_TICKS consecutive polls before accepting it. At 60Hz,
// 3 ticks = 50ms, which filters out bounce without feeling laggy.
//
// In Python terms, it's like:
//   if all(readings[-3:]) == same_value:
//       confirmed_state = same_value

#include "nav_switch.h"
#include "config.h"

// How many consecutive identical readings before we accept a change.
// At 60Hz: 3 ticks ≈ 50ms. Enough to filter bounce, fast enough
// to feel instant to a human.
constexpr uint8_t DEBOUNCE_TICKS = 3;

// ── Internal state ─────────────────────────────────────────────────────
// These are "file-scope" statics — visible only inside this .cpp file.
// In Python terms, they're module-private variables (like _underscore).

static NavDirection stableDirection = NAV_NONE;   // Last confirmed direction
static NavDirection candidateDirection = NAV_NONE; // What we're currently seeing
static uint8_t candidateCount = 0;                 // How many ticks we've seen it

// ── Pin setup ──────────────────────────────────────────────────────────

void nav_init()
{
    // INPUT_PULLUP enables the internal ~22kΩ pull-up resistor on each
    // pin. This means we don't need any external resistors — the Teensy
    // handles it internally. Each pin sits at 3.3V until the switch
    // pulls it to GND.
    //
    // In Python/RPi.GPIO terms: GPIO.setup(pin, GPIO.IN, pull_up_down=GPIO.PUD_UP)
    pinMode(NAV_PIN_UP,     INPUT_PULLUP);
    pinMode(NAV_PIN_DOWN,   INPUT_PULLUP);
    pinMode(NAV_PIN_LEFT,   INPUT_PULLUP);
    pinMode(NAV_PIN_RIGHT,  INPUT_PULLUP);
    pinMode(NAV_PIN_CENTER, INPUT_PULLUP);
}

// ── Read the raw switch state (no debouncing) ──────────────────────────
// Checks each pin and returns the first one found pressed.
// If multiple directions are pressed simultaneously (unlikely with a
// 5-way nav switch, but possible), we just take the first one — this
// is called "priority encoding."

static NavDirection raw_read()
{
    // Remember: LOW = pressed (active low)
    if (digitalRead(NAV_PIN_UP)     == LOW) return NAV_UP;
    if (digitalRead(NAV_PIN_DOWN)   == LOW) return NAV_DOWN;
    if (digitalRead(NAV_PIN_LEFT)   == LOW) return NAV_LEFT;
    if (digitalRead(NAV_PIN_RIGHT)  == LOW) return NAV_RIGHT;
    if (digitalRead(NAV_PIN_CENTER) == LOW) return NAV_CENTER;
    return NAV_NONE;
}

// ── Debounced read ─────────────────────────────────────────────────────

NavDirection nav_read()
{
    NavDirection current = raw_read();

    if (current == candidateDirection)
    {
        // Same reading as last tick — increment the stability counter
        if (candidateCount < DEBOUNCE_TICKS)
            candidateCount++;

        // Once we've seen the same value for enough ticks, accept it
        if (candidateCount >= DEBOUNCE_TICKS)
            stableDirection = candidateDirection;
    }
    else
    {
        // Reading changed — start counting again from this new value
        candidateDirection = current;
        candidateCount = 1;
    }

    return stableDirection;
}

// ── Human-readable direction names ─────────────────────────────────────
// Returns a pointer to a string literal. These live in flash memory
// and never need freeing — safe to pass around freely.
//
// In Python you'd just return "Up" etc. In C++ it's the same thing
// but the return type is const char* (pointer to read-only characters)
// instead of str.

const char* nav_direction_name(NavDirection dir)
{
    switch (dir) {
        case NAV_UP:     return "Up";
        case NAV_DOWN:   return "Down";
        case NAV_LEFT:   return "Left";
        case NAV_RIGHT:  return "Right";
        case NAV_CENTER: return "Press";
        default:         return "-";
    }
}
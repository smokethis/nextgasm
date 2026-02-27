// alphanum_display.cpp — Adafruit Quad Alphanumeric Display implementation
//
// Uses the Adafruit LED Backpack library, which handles all the low-
// level I2C communication with the HT16K33 chip. The library provides 
// the Adafruit_AlphaNum4 class with methods for writing characters 
// and raw segment bitmasks.
//
// HOW IT WORKS UNDER THE HOOD:
//
// The HT16K33 is an I2C device with 16 bytes of display RAM. Each 
// of the 4 digit positions gets a 16-bit word in that RAM, where 
// each bit maps to one of the 14 segments (plus decimal point and 
// colon). The Adafruit library has a built-in font table that maps 
// ASCII characters to the right combination of segment bits.
//
// The workflow is:
//   1. Write characters into the library's internal buffer
//   2. Call writeDisplay() to push the buffer over I2C
//
// In Python terms, it's like:
//   display.buffer[0] = font_table['A']   # Set digit 0 to 'A'
//   display.buffer[1] = font_table['B']   # Set digit 1 to 'B'
//   display.flush()                        # Send buffer to hardware
//
// The I2C transfer for 4 digits is only ~10 bytes, which takes about 
// 0.2ms at 400kHz — easily fits within a 60Hz tick (16.7ms budget).

#include "alphanum_display.h"
#include <Wire.h>
#include <Adafruit_LEDBackpack.h>

// The display object. Adafruit's library manages the I2C communication
// and provides the font table for 14-segment characters.
//
// 'static' here means this object is only visible in this file —
// other modules interact with it through our public functions.
// In Python terms: _alpha4 = Adafruit_AlphaNum4()  (private module var)
static Adafruit_AlphaNum4 alpha4 = Adafruit_AlphaNum4();

// Track whether init succeeded, so we can skip writes if the 
// display isn't connected. Avoids I2C errors clogging the bus.
static bool displayReady = false;


// ════════════════════════════════════════════════════════════════════════
// Initialisation
// ════════════════════════════════════════════════════════════════════════

bool alphanum_init()
{
    // begin() initialises the HT16K33 over I2C:
    //   - Turns on the internal oscillator
    //   - Sets the display to "on" mode
    //   - Clears all segment data
    //
    // It returns true if the device responded on the I2C bus.
    // If the display isn't connected, this returns false and we 
    // silently skip all future writes — the rest of the system 
    // keeps running fine without it.
    displayReady = alpha4.begin(ALPHANUM_I2C_ADDR);

    if (displayReady) {
        alpha4.setBrightness(8);   // Mid-range brightness
        alpha4.clear();
        alpha4.writeDisplay();     // Push the cleared state to hardware
        Serial.println("[AlphaNum] Init OK at 0x70");
    } else {
        Serial.println("[AlphaNum] Not found at 0x70!");
    }

    return displayReady;
}


// ════════════════════════════════════════════════════════════════════════
// Display Functions
// ════════════════════════════════════════════════════════════════════════

void alphanum_show_text(const char* text)
{
    if (!displayReady) return;

    alpha4.clear();

    // Write up to 4 characters. If the string is shorter, the 
    // remaining digits stay cleared (blank).
    for (uint8_t i = 0; i < 4 && text[i] != '\0'; i++) {
        alpha4.writeDigitAscii(i, text[i]);
    }

    // writeDisplay() sends the 4-digit buffer to the HT16K33 over 
    // I2C. Until you call this, changes are only in local memory.
    // It's the same pattern as our HT1632C flush() or U8g2's 
    // sendBuffer() — buffer locally, then push to hardware.
    alpha4.writeDisplay();
}


void alphanum_show_int(int value)
{
    if (!displayReady) return;

    // Clamp to what fits in 4 characters (with sign)
    // -999 to 9999 for positive, -999 for negative
    if (value > 9999) value = 9999;
    if (value < -999) value = -999;

    // snprintf formats the number into a string, right-aligned 
    // in a 4-character field. The %4d format pads with spaces.
    //
    // In Python terms: f"{value:4d}" or f"{value:>4}"
    //
    // We use a 5-char buffer (4 digits + null terminator).
    // C strings always need one extra byte for the '\0' at the end.
    char buf[5];
    snprintf(buf, sizeof(buf), "%4d", value);

    alphanum_show_text(buf);
}


void alphanum_show_labeled(char label, int value)
{
    if (!displayReady) return;

    // Clamp value to 3 digits (what fits after the label character)
    if (value > 999) value = 999;
    if (value < -99) value = -99;

    // Format: label char + 3-digit right-aligned number
    // e.g. label='d', value=42  →  "d 42"
    //      label='M', value=255 →  "M255"
    char buf[5];
    snprintf(buf, sizeof(buf), "%c%3d", label, value);

    alphanum_show_text(buf);
}


void alphanum_clear()
{
    if (!displayReady) return;

    alpha4.clear();
    alpha4.writeDisplay();
}


void alphanum_set_brightness(uint8_t level)
{
    if (!displayReady) return;

    // The HT16K33 has 16 brightness levels (0-15).
    // This controls the duty cycle of the LED multiplexing — 
    // similar to PWM but handled by the chip internally.
    if (level > 15) level = 15;
    alpha4.setBrightness(level);
}


void alphanum_set_dot(uint8_t digit)
{
    if (!displayReady || digit > 3) return;

    // The Adafruit library stores each digit as a 16-bit value in 
    // its displaybuffer[] array. Each bit maps to one segment of the 
    // 14-segment display. Bit 14 is the decimal point.
    //
    // By OR-ing in that bit, we add the dot WITHOUT clearing the 
    // character that's already there. Then we flush to hardware.
    //
    // In Python terms:
    //   display.buffer[digit] |= (1 << 14)   # set the dot bit
    //   display.flush()
    //
    // The displaybuffer is a public member of Adafruit_LEDBackpack 
    // (the parent class of Adafruit_AlphaNum4), so we can access 
    // it directly. This is one of those cases where reaching into 
    // the library's internals is the cleanest solution.
    for (uint8_t i = 0; i < 4; i++) {
        alpha4.displaybuffer[i] |= (1 << 14);
    }
    alpha4.writeDisplay();  // Single I2C transaction
}
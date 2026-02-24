// HT1632C_Display.cpp
// Implementation of the HT1632C driver for DFRobot FireBeetle 24×8 LED Matrix
//
// See HT1632C_Display.h for an overview of how the protocol works.
// ═══════════════════════════════════════════════════════════════════════
// WIRING (Teensy 4.0 → HT1632C display board)
// ═══════════════════════════════════════════════════════════════════════
//
// The DFRobot FireBeetle display board should have test pads or a
// connector for the HT1632C signals. Look for these labels on the PCB:
//
//   Teensy Pin 6  ──→  CS   (Chip Select)
//   Teensy Pin 7  ──→  WR   (Write Clock)
//   Teensy Pin 8  ──→  DATA (Data In)
//   Teensy 3.3V   ──→  VCC  (The HT1632C runs at 3–5V; 3.3V is fine)
//   Teensy GND    ──→  GND
//
// These default pins were chosen to avoid conflicts with existing I/O:
//   Pin 2,3 = Encoder      Pin 5  = Encoder button
//   Pin 9   = Motor PWM    Pin 10 = NeoPixel ring
//   Pin A0  = Pressure     Pin 6,7,8 = FREE → used for display
//
// IMPORTANT: The Teensy 4.0 is 3.3V logic. The HT1632C accepts 3.3V
// just fine on its inputs. If your display board has its own 5V rail for
// the LEDs, that's fine — the logic interface still works at 3.3V.
//
// ═══════════════════════════════════════════════════════════════════════

#include "HT1632C_Display.h"

// ── Basic 5×7 Font ─────────────────────────────────────────────────────
// Each character is 5 bytes wide. Each byte is a column, with bit 0 at
// the top. This is a very common format for small LED matrix fonts.
//
// For example, the letter 'A' looks like this on the grid:
//
//   Col:  0     1     2     3     4
//         .XX.  X..X  XXXX  X..X  X..X
//  Bit 0  0     1     1     1     0       → 0x7E
//  Bit 1  1     0     0     0     1       ...etc
//  (rendered transposed as column bytes)
//
// We only include ASCII 32 (space) through 90 (Z) to save memory.
// Expand as needed — each character costs just 5 bytes.

static const uint8_t FONT_5X7[][5] PROGMEM = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // 32: space
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // 33: !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // 34: "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // 35: #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // 36: $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // 37: %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // 38: &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // 39: '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // 40: (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // 41: )
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // 42: *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // 43: +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // 44: ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // 45: -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // 46: .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // 47: /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 48: 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 49: 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 50: 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 51: 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 52: 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 53: 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 54: 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 55: 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 56: 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 57: 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // 58: :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // 59: ;
    {0x08, 0x14, 0x22, 0x41, 0x00}, // 60: <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // 61: =
    {0x00, 0x41, 0x22, 0x14, 0x08}, // 62: >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // 63: ?
    {0x3E, 0x41, 0x5D, 0x55, 0x1E}, // 64: @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // 65: A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // 66: B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // 67: C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 68: D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // 69: E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // 70: F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // 71: G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 72: H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // 73: I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // 74: J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // 75: K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // 76: L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // 77: M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 78: N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 79: O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // 80: P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 81: Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // 82: R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // 83: S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // 84: T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 85: U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 86: V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // 87: W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // 88: X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // 89: Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // 90: Z
};

// Number of characters in the font table
#define FONT_FIRST_CHAR 32
#define FONT_LAST_CHAR  90


// ════════════════════════════════════════════════════════════════════════
// Constructor
// ════════════════════════════════════════════════════════════════════════

// The colon syntax below is an "initialiser list" — a C++ way of setting
// member variables before the constructor body runs. It's more efficient
// than assigning inside the braces for simple types.

HT1632C_Display::HT1632C_Display(uint8_t pinCS, uint8_t pinWR, uint8_t pinDATA)
    : _pinCS(pinCS), _pinWR(pinWR), _pinDATA(pinDATA)
{
    // Zero the framebuffer.
    // memset is like: for i in range(24): self.buffer[i] = 0
    memset(_buffer, 0, HT1632C_WIDTH);
}


// ════════════════════════════════════════════════════════════════════════
// Low-level bit-banging
// ════════════════════════════════════════════════════════════════════════

// Clock out `numBits` from `data`, MSB first.
//
// This is the fundamental building block of the protocol. Every command
// and data write goes through here. The timing works like this:
//
//   1. Set DATA pin to the bit value (high or low)
//   2. Pull WR low  (chip: "I see you're about to give me a bit...")
//   3. Pull WR high (chip: "Got it! I latched that bit on this rising edge")
//   4. Repeat for next bit
//
// The HT1632C is very tolerant on timing — the datasheet minimum is just
// 250ns per half-cycle, and digitalWriteFast on Teensy 4.0 is ~2ns, so
// even without delays we're fine. If you ported this to a much faster MCU
// you might need a small delay.

void HT1632C_Display::_writeBits(uint16_t data, uint8_t numBits) {
    for (uint8_t i = numBits; i > 0; i--) {
        // Isolate the current bit (MSB first).
        // In Python: bit = (data >> (i-1)) & 1
        uint8_t bit = (data >> (i - 1)) & 1;

        digitalWriteFast(_pinDATA, bit);   // Set data line
        digitalWriteFast(_pinWR, LOW);     // WR low — prepare
        digitalWriteFast(_pinWR, HIGH);    // WR high — chip latches bit
    }
}


// Send a command to the HT1632C.
//
// Command frame format (12 bits total):
//   [100] [CCCCCCCC] [X]
//    ^^^   ^^^^^^^^    ^
//    ID    command     "don't care" bit (protocol requires it)
//
// The 3-bit ID "100" means "this is a command".

void HT1632C_Display::_sendCommand(uint8_t cmd) {
    digitalWriteFast(_pinCS, LOW);       // Begin transaction

    _writeBits(0b100, 3);               // Command ID: 100
    _writeBits(cmd, 8);                 // 8-bit command
    _writeBits(0, 1);                   // Extra "don't care" bit

    digitalWriteFast(_pinCS, HIGH);      // End transaction
}


// Write one byte of pixel data to a specific address in display RAM.
//
// Write frame format:
//   [101] [AAAAAAA] [DDDD] [DDDD]
//    ^^^   ^^^^^^^   ^^^^   ^^^^
//    ID    7-bit     high   low nibble
//          address   nibble of data
//
// The HT1632C addresses RAM in nibbles (4 bits), but we send a full byte
// (two nibbles) in one go by continuing to clock data after the first
// nibble. The address auto-increments, so the second nibble lands at
// addr+1. This means each call writes 8 bits = 8 LEDs = one full column.
//
// Address mapping for 24×8 mode:
//   Column 0 → address 0  (high nibble at addr 0, low nibble at addr 1)
//   Column 1 → address 2
//   Column N → address N*2

void HT1632C_Display::_writeDataAt(uint8_t addr, uint8_t data) {
    digitalWriteFast(_pinCS, LOW);

    _writeBits(0b101, 3);               // Write ID: 101
    _writeBits(addr, 7);                // 7-bit RAM address
    _writeBits(data, 8);                // 8 bits of pixel data (2 nibbles)

    digitalWriteFast(_pinCS, HIGH);
}


// ════════════════════════════════════════════════════════════════════════
// Initialisation & Shutdown
// ════════════════════════════════════════════════════════════════════════

void HT1632C_Display::begin() {
    // Configure pins as outputs
    pinMode(_pinCS, OUTPUT);
    pinMode(_pinWR, OUTPUT);
    pinMode(_pinDATA, OUTPUT);

    // Start with CS and WR high (idle state)
    digitalWriteFast(_pinCS, HIGH);
    digitalWriteFast(_pinWR, HIGH);

    // Boot sequence — order matters here!
    // It's like calling a series of setup functions on the chip:
    _sendCommand(HT1632C_CMD_SYS_EN);      // 1. Wake up the oscillator
    _sendCommand(HT1632C_CMD_N_MOS_COM8);  // 2. Configure for 24×8 NMOS mode
    _sendCommand(HT1632C_CMD_INT_RC);      // 3. Use internal RC clock
    _sendCommand(HT1632C_CMD_LED_ON);      // 4. Enable the LED driver
    _sendCommand(HT1632C_CMD_BLINK_OFF);   // 5. No blinking
    setBrightness(8);                       // 6. Mid brightness (~50%)

    // Clear the display RAM on the chip (not just our local buffer)
    clear();
    flush();
}

void HT1632C_Display::shutdown() {
    clear();
    flush();
    _sendCommand(HT1632C_CMD_LED_OFF);
    _sendCommand(HT1632C_CMD_SYS_DIS);
}


// ════════════════════════════════════════════════════════════════════════
// Framebuffer Operations
// ════════════════════════════════════════════════════════════════════════
// We maintain a local copy of the display contents (_buffer) and only
// push it to the chip when flush() is called. This is like the FastLED
// pattern in the main code: you modify the leds[] array, then call
// FastLED.show() to push it out.

void HT1632C_Display::clear() {
    memset(_buffer, 0x00, HT1632C_WIDTH);
}

void HT1632C_Display::fill() {
    memset(_buffer, 0xFF, HT1632C_WIDTH);
}

void HT1632C_Display::setPixel(uint8_t x, uint8_t y) {
    if (x >= HT1632C_WIDTH || y >= HT1632C_HEIGHT) return;  // Bounds check

    // Set bit `y` in column `x`.
    // In Python: self.buffer[x] |= (1 << y)
    // The |= is a bitwise OR-assign — it turns on that one bit without
    // disturbing the others. Like flipping one switch in a row of 8.
    _buffer[x] |= (1 << y);
}

void HT1632C_Display::clearPixel(uint8_t x, uint8_t y) {
    if (x >= HT1632C_WIDTH || y >= HT1632C_HEIGHT) return;

    // Clear bit `y` in column `x`.
    // The ~(1 << y) creates a mask with all bits set EXCEPT bit y,
    // then AND-ing clears just that one bit.
    // In Python: self.buffer[x] &= ~(1 << y)
    _buffer[x] &= ~(1 << y);
}

bool HT1632C_Display::getPixel(uint8_t x, uint8_t y) {
    if (x >= HT1632C_WIDTH || y >= HT1632C_HEIGHT) return false;
    return (_buffer[x] >> y) & 1;
}

void HT1632C_Display::setColumn(uint8_t col, uint8_t data) {
    if (col >= HT1632C_WIDTH) return;
    _buffer[col] = data;
}


// ════════════════════════════════════════════════════════════════════════
// Display Output
// ════════════════════════════════════════════════════════════════════════

// Push the entire framebuffer to the HT1632C.
//
// We use a "burst write" optimisation: start writing at address 0 and
// just keep clocking data — the HT1632C auto-increments the address
// after each nibble. This is significantly faster than writing each
// address individually (24 transactions → 1 transaction).

void HT1632C_Display::flush() {
    digitalWriteFast(_pinCS, LOW);

    _writeBits(0b101, 3);     // Write mode ID
    _writeBits(0x00, 7);      // Start at address 0

    // Stream all 24 columns. Each column is 8 bits (2 nibbles), and
    // the address auto-increments by nibble, so this fills all 48
    // nibbles of the display RAM in one continuous burst.
    for (uint8_t col = 0; col < HT1632C_WIDTH; col++) {
        _writeBits(_buffer[col], 8);
    }

    digitalWriteFast(_pinCS, HIGH);
}


// Set display brightness (PWM duty cycle).
// Level 0 = 1/16 duty, Level 15 = 16/16 duty.
void HT1632C_Display::setBrightness(uint8_t level) {
    if (level > 15) level = 15;
    _sendCommand(HT1632C_CMD_PWM_BASE | level);
}


// ════════════════════════════════════════════════════════════════════════
// Convenience Drawing Functions
// ════════════════════════════════════════════════════════════════════════

// Draw a horizontal bar graph across the 24 columns.
// Fills columns from left to right proportional to value/maxValue.
// All 8 rows in each filled column are lit — gives a solid bar.
//
// This maps nicely to displaying motor speed, pressure, etc.
// For example:
//   display.drawBar(motorSpeed, 255);       // Show speed as bar
//   display.drawBar(pressure - avg, limit); // Show pressure delta

void HT1632C_Display::drawBar(int value, int maxValue) {
    // map() works just like Arduino's built-in map function, or in
    // Python: int(value / maxValue * (WIDTH - 1))
    int cols = map(constrain(value, 0, maxValue), 0, maxValue, 0, HT1632C_WIDTH);

    for (uint8_t x = 0; x < HT1632C_WIDTH; x++) {
        _buffer[x] = (x < cols) ? 0xFF : 0x00;
    }
}


// Draw a single character from the 5×7 font at horizontal position x.
// Returns the character width (5) plus 1 pixel gap = 6, so you can
// chain calls to render strings:
//   uint8_t x = 0;
//   x += display.drawChar(x, 'H');
//   x += display.drawChar(x, 'I');

uint8_t HT1632C_Display::drawChar(uint8_t x, char c) {
    // Clamp to supported range
    if (c < FONT_FIRST_CHAR || c > FONT_LAST_CHAR) c = ' ';

    uint8_t index = c - FONT_FIRST_CHAR;

    for (uint8_t col = 0; col < 5; col++) {
        uint8_t colX = x + col;
        if (colX >= HT1632C_WIDTH) break;  // Don't write past the edge

        // pgm_read_byte reads from PROGMEM (flash memory) on AVR.
        // On Teensy 4.0/ARM this just reads normally, but using the
        // macro keeps the code portable to Arduino Uno etc.
        _buffer[colX] = pgm_read_byte(&FONT_5X7[index][col]);
    }

    // Return width consumed (5 pixels + 1 gap)
    return 6;
}


// Draw a null-terminated string starting at position x.
// Characters that would extend past column 23 are clipped.

void HT1632C_Display::drawString(uint8_t x, const char* str) {
    while (*str && x < HT1632C_WIDTH) {
        x += drawChar(x, *str);
        str++;
    }
}


// Direct buffer access for advanced use
uint8_t* HT1632C_Display::getBuffer() {
    return _buffer;
}

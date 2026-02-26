// HT1632C_Display.cpp
// Implementation of the HT1632C driver for DFRobot FireBeetle 24×8 LED Matrix
//
// ═══════════════════════════════════════════════════════════════════════
// WIRING (Teensy 4.0 → HT1632C display board)
// ═══════════════════════════════════════════════════════════════════════
//   Teensy Pin 6  ──→  CS   (directly, or via the DIP-switch-selected pad)
//   Teensy Pin 7  ──→  WR   (Write Clock)
//   Teensy Pin 8  ──→  DATA (Data In)
//   Teensy 3.3V   ──→  VCC
//   Teensy GND    ──→  GND
//
// ═══════════════════════════════════════════════════════════════════════
// HOW THE RAM LAYOUT WAS FIGURED OUT (from hardware testing)
// ═══════════════════════════════════════════════════════════════════════
//
// The HT1632C addresses RAM in nibbles (4-bit chunks), NOT bytes.
// In 24×16 COM mode (command 0x24), each column takes 4 nibbles:
//
//   Nibble 0,1 (byte 0) = COM0–COM7  ← the 8 real LEDs
//   Nibble 2,3 (byte 1) = COM8–COM15 ← nothing connected, just padding
//
// When we burst-write 24 bytes naively (one per column), the chip
// consumes them as pairs:
//   Byte 0 → nibbles 0,1 → Column 0, COM0-7 (LEDs light!)
//   Byte 1 → nibbles 2,3 → Column 0, COM8-15 (blank — no LEDs!)
//   Byte 2 → nibbles 4,5 → Column 1, COM0-7 (LEDs light!)
//   Byte 3 → nibbles 6,7 → Column 1, COM8-15 (blank!)
//
// This explains exactly what was observed: only even-indexed bytes
// produced visible output, and 24 bytes only covered 12 columns.
//
// The fix: send 48 bytes — LED data + zero padding for each column.
// Column order is also reversed (RAM addr 0 = rightmost physical).

#include "HT1632C_Display.h"

// ── Basic 5×7 Font ─────────────────────────────────────────────────────
// Each character is 5 bytes wide, each byte is one column with bit 0
// at the top. ASCII 32 (space) through 90 (Z).
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

#define FONT_FIRST_CHAR 32
#define FONT_LAST_CHAR  90


// ════════════════════════════════════════════════════════════════════════
// Constructor
// ════════════════════════════════════════════════════════════════════════

HT1632C_Display::HT1632C_Display(uint8_t pinCS, uint8_t pinWR, uint8_t pinDATA)
    : _pinCS(pinCS), _pinWR(pinWR), _pinDATA(pinDATA)
{
    memset(_buffer, 0, HT1632C_WIDTH);
}


// ════════════════════════════════════════════════════════════════════════
// Low-level bit-banging
// ════════════════════════════════════════════════════════════════════════

void HT1632C_Display::_writeBits(uint16_t data, uint8_t numBits) {
    for (uint8_t i = numBits; i > 0; i--) {
        uint8_t bit = (data >> (i - 1)) & 1;
        digitalWriteFast(_pinDATA, bit);
        digitalWriteFast(_pinWR, LOW);
        digitalWriteFast(_pinWR, HIGH);
    }
}

void HT1632C_Display::_sendCommand(uint8_t cmd) {
    digitalWriteFast(_pinCS, LOW);
    _writeBits(0b100, 3);     // Command ID
    _writeBits(cmd, 8);       // 8-bit command
    _writeBits(0, 1);         // Don't care bit
    digitalWriteFast(_pinCS, HIGH);
}


// ════════════════════════════════════════════════════════════════════════
// Initialisation & Shutdown
// ════════════════════════════════════════════════════════════════════════

void HT1632C_Display::begin() {
    pinMode(_pinCS, OUTPUT);
    pinMode(_pinWR, OUTPUT);
    pinMode(_pinDATA, OUTPUT);

    digitalWriteFast(_pinCS, HIGH);
    digitalWriteFast(_pinWR, HIGH);

    _sendCommand(HT1632C_CMD_SYS_EN);         // 1. Wake oscillator
    _sendCommand(HT1632C_CMD_NMOS_24x16);     // 2. 24 ROW × 16 COM mode
    _sendCommand(HT1632C_CMD_INT_RC);         // 3. Internal RC clock
    _sendCommand(HT1632C_CMD_LED_ON);         // 4. LED driver on
    _sendCommand(HT1632C_CMD_BLINK_OFF);      // 5. No blinking
    setBrightness(8);                          // 6. Mid brightness

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
// Our buffer is 24 bytes — one per logical column, bit 0 = top row.
// The mapping to hardware RAM is handled entirely by flush().

void HT1632C_Display::clear() {
    memset(_buffer, 0x00, HT1632C_WIDTH);
}

void HT1632C_Display::fill() {
    memset(_buffer, 0xFF, HT1632C_WIDTH);
}

void HT1632C_Display::setPixel(uint8_t x, uint8_t y) {
    if (x >= HT1632C_WIDTH || y >= HT1632C_HEIGHT) return;
    _buffer[x] |= (1 << y);
}

void HT1632C_Display::clearPixel(uint8_t x, uint8_t y) {
    if (x >= HT1632C_WIDTH || y >= HT1632C_HEIGHT) return;
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
// Display Output — THE CRITICAL FIX
// ════════════════════════════════════════════════════════════════════════
//
// Push the entire framebuffer to the HT1632C.
//
// Two corrections based on hardware testing:
//
// 1. COM16 PADDING: Each physical column needs 2 bytes of RAM.
//    The first byte is COM0-7 (the actual LEDs), the second byte
//    is COM8-15 (no LEDs, sent as 0x00). Without this padding,
//    our 24 bytes only filled 12 physical columns.
//
// 2. COLUMN REVERSAL: RAM address 0 maps to the rightmost physical
//    column. We send columns in reverse so that buffer[0] (leftmost
//    in our drawing coordinate system) ends up at the left of the
//    physical display.
//
// In Python terms, the old (broken) code was:
//   for col in buffer:
//       send(col)                        # 24 bytes → 12 columns!
//
// The fix:
//   for col in reversed(buffer):
//       send(col)   # LED data byte
//       send(0x00)  # padding byte       # 48 bytes → 24 columns!

void HT1632C_Display::flush() {
    digitalWriteFast(_pinCS, LOW);

    _writeBits(0b101, 3);     // Write mode ID
    _writeBits(0x00, 7);      // Start at RAM address 0

    // Send 24 columns × 2 bytes each = 48 bytes total.
    // Reverse order: buffer[23] goes to RAM addr 0 (rightmost),
    // buffer[0] goes to RAM addr 46 (leftmost).
    //
    // Using int8_t (signed) because we count down past zero.
    // With uint8_t, decrementing 0 would wrap to 255 and loop forever.
    // It's a classic C/C++ gotcha — Python's range(23, -1, -1) handles
    // this automatically, but in C++ we have to think about the type.
    for (int8_t col = HT1632C_WIDTH - 1; col >= 0; col--) {
        _writeBits(_buffer[col], 8);  // COM0–7: the 8 actual LEDs
        _writeBits(0x00, 8);          // COM8–15: padding (no LEDs here)
    }

    digitalWriteFast(_pinCS, HIGH);
}


void HT1632C_Display::setBrightness(uint8_t level) {
    if (level > 15) level = 15;
    _sendCommand(HT1632C_CMD_PWM_BASE | level);
}


// ════════════════════════════════════════════════════════════════════════
// Convenience Drawing Functions
// ════════════════════════════════════════════════════════════════════════

void HT1632C_Display::drawBar(int value, int maxValue) {
    int cols = map(constrain(value, 0, maxValue), 0, maxValue, 0, HT1632C_WIDTH);
    for (uint8_t x = 0; x < HT1632C_WIDTH; x++) {
        _buffer[x] = (x < cols) ? 0xFF : 0x00;
    }
}

uint8_t HT1632C_Display::drawChar(uint8_t x, char c) {
    if (c < FONT_FIRST_CHAR || c > FONT_LAST_CHAR) c = ' ';
    uint8_t index = c - FONT_FIRST_CHAR;

    for (uint8_t col = 0; col < 5; col++) {
        uint8_t colX = x + col;
        if (colX >= HT1632C_WIDTH) break;
        _buffer[colX] = pgm_read_byte(&FONT_5X7[index][col]);
    }
    return 6;
}

void HT1632C_Display::drawString(uint8_t x, const char* str) {
    while (*str && x < HT1632C_WIDTH) {
        x += drawChar(x, *str);
        str++;
    }
}

uint8_t* HT1632C_Display::getBuffer() {
    return _buffer;
}
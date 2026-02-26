// HT1632C_Display.h
// Driver for DFRobot FireBeetle 24x8 Yellow LED Matrix (HT1632C controller)
//
// KEY DISCOVERY FROM HARDWARE TESTING:
// Despite having only 8 rows of LEDs, this board runs the HT1632C in
// NMOS 24 ROW × 16 COM mode (command 0x24). Each physical column of
// 8 LEDs occupies 4 nibbles (2 bytes) of display RAM:
//   Byte 0: COM0-COM7  → the 8 actual LEDs
//   Byte 1: COM8-COM15 → no LEDs connected, must send as zero padding
//
// This means the full 24-column display needs 48 bytes of RAM, not 24.
// Additionally, RAM address 0 maps to the rightmost physical column,
// so the driver reverses column order in flush().

#ifndef HT1632C_DISPLAY_H
#define HT1632C_DISPLAY_H

#include <Arduino.h>

// ── HT1632C Command Constants ──────────────────────────────────────────
#define HT1632C_CMD_SYS_DIS  0x00
#define HT1632C_CMD_SYS_EN   0x01
#define HT1632C_CMD_LED_OFF  0x02
#define HT1632C_CMD_LED_ON   0x03
#define HT1632C_CMD_BLINK_OFF 0x08
#define HT1632C_CMD_BLINK_ON  0x09
#define HT1632C_CMD_INT_RC   0x18

// 0x24 = NMOS, 24 ROW × 16 COM — what this DFRobot board requires.
// Each column = 4 nibbles = 2 bytes of RAM (even though only 8 LEDs).
#define HT1632C_CMD_NMOS_24x16 0x24

// Brightness: 0xA0 | (level & 0x0F), 0=dimmest, 15=brightest
#define HT1632C_CMD_PWM_BASE 0xA0

// ── Display Dimensions ─────────────────────────────────────────────────
#define HT1632C_WIDTH  24
#define HT1632C_HEIGHT  8

// ── Pin defaults (chosen to avoid conflicts with other nextgasm I/O) ──
#define HT1632C_DEFAULT_CS   6
#define HT1632C_DEFAULT_WR   7
#define HT1632C_DEFAULT_DATA 8


class HT1632C_Display {

public:
    HT1632C_Display(
        uint8_t pinCS   = HT1632C_DEFAULT_CS,
        uint8_t pinWR   = HT1632C_DEFAULT_WR,
        uint8_t pinDATA = HT1632C_DEFAULT_DATA
    );

    void begin();
    void shutdown();

    void clear();
    void fill();
    void setPixel(uint8_t x, uint8_t y);
    void clearPixel(uint8_t x, uint8_t y);
    bool getPixel(uint8_t x, uint8_t y);
    void setColumn(uint8_t col, uint8_t data);

    void flush();
    void setBrightness(uint8_t level);

    void drawBar(int value, int maxValue);
    uint8_t drawChar(uint8_t x, char c);
    void drawString(uint8_t x, const char* str);

    // --- Scrolling text ---
    // Call this every tick from the main loop. It manages its own 
    // timing and scroll offset internally — you just pass the text 
    // and the scroll interval (ms between each 1-pixel shift).
    //
    // If the text fits on screen without scrolling, it just draws 
    // it statically (no wasted motion).
    //
    // If you pass different text than last time, the scroll resets
    // automatically — so mode changes look instant.
    //
    // Returns true if it actually redrew (useful if you want to 
    // avoid redundant flush() calls elsewhere).
    bool scrollText(const char* text, unsigned long scrollIntervalMs = 50);

    uint8_t* getBuffer();

private:
    uint8_t _pinCS;
    uint8_t _pinWR;
    uint8_t _pinDATA;

    uint8_t _buffer[HT1632C_WIDTH];

    // --- Scroll state ---
    // These persist between calls to scrollText(), tracking where 
    // we are in the animation. In Python terms, they're like 
    // instance variables on a class — self._scrollOffset etc.
    int _scrollOffset;                  // Current pixel offset (starts at WIDTH, decreases)
    unsigned long _lastScrollTime;      // millis() of last pixel shift
    const char* _lastText;              // Detect when text changes to reset scroll
    int _textPixelWidth;                // Cached total width of current text in pixels

    // --- Private drawing helpers ---
    // Like drawChar/drawString but using signed int for x position,
    // allowing characters to be partially off the left edge.
    // The originals stay untouched with uint8_t for backwards compat.
    void _drawCharSigned(int x, char c);
    void _drawStringSigned(int x, const char* str);

    void _writeBits(uint16_t data, uint8_t numBits);
    void _sendCommand(uint8_t cmd);
};

#endif // HT1632C_DISPLAY_H
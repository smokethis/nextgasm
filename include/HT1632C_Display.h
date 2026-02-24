// HT1632C_Display.h
// Driver for DFRobot FireBeetle 24x8 Yellow LED Matrix (HT1632C controller)
//
// BACKGROUND — WHY THIS FILE EXISTS:
// The DFRobot FireBeetle LED matrix is normally sold with an ESP32 carrier
// board that handles communication with the HT1632C chip. Since we only have
// the bare display panel, we're driving the HT1632C directly from the Teensy.
//
// HOW THE HT1632C WORKS:
// Unlike I2C or SPI, the HT1632C uses a proprietary 3-wire serial protocol.
// We "bit-bang" it — meaning we manually toggle GPIO pins high and low in the
// right sequence, rather than relying on a hardware peripheral. Think of it
// like Morse code: we pulse data one bit at a time, clocked by toggling a
// write pin.
//
// The three wires are:
//   CS   (Chip Select) — Pull LOW to start talking, HIGH when done.
//                         Like tapping someone on the shoulder before speaking.
//   WR   (Write Clock) — We toggle this to tell the chip "read the data pin NOW."
//                         Data is latched on the RISING edge (LOW → HIGH).
//   DATA (Data)        — The actual 1s and 0s we're sending.
//
// Every transaction starts with a 3-bit "ID" that tells the chip what kind
// of message is coming:
//   100 = "This is a COMMAND"   (e.g. turn on, set brightness)
//   101 = "This is WRITE DATA"  (e.g. here are the pixels to display)
//   110 = "This is a READ"      (we don't use this)
//
// MEMORY LAYOUT:
// The HT1632C has 96 nibbles (4-bit chunks) of display RAM. For a 24×8
// display, each column of 8 LEDs maps to 2 nibbles (= 1 byte). So 24
// columns × 1 byte = 24 bytes, which is our framebuffer.

#ifndef HT1632C_DISPLAY_H
#define HT1632C_DISPLAY_H

#include <Arduino.h>

// ── HT1632C Command Constants ──────────────────────────────────────────
// These are the 8-bit command codes sent after the 100 command ID prefix.
// The chip interprets these to configure its operating mode.
//
// Think of these like function calls to the chip:
//   "Hey chip (CS low), this is a command (100), please turn on (SYS_EN)."

#define HT1632C_CMD_SYS_DIS  0x00  // Turn off system oscillator (low power)
#define HT1632C_CMD_SYS_EN   0x01  // Turn on system oscillator (required first!)
#define HT1632C_CMD_LED_OFF  0x02  // Turn off LED duty cycle generator
#define HT1632C_CMD_LED_ON   0x03  // Turn on LED duty cycle generator
#define HT1632C_CMD_BLINK_OFF 0x08 // Disable blinking
#define HT1632C_CMD_BLINK_ON  0x09 // Enable blinking (0.5s on, 0.5s off)
#define HT1632C_CMD_INT_RC   0x18  // Use internal RC oscillator as clock source
#define HT1632C_CMD_N_MOS_COM8 0x24 // NMOS open-drain output, 8 COM lines (24×8 mode)
#define HT1632C_CMD_P_MOS_COM8 0x28 // PMOS open-drain output, 8 COM lines

// Brightness control: 0xA0 | (level & 0x0F)
// Level 0 = dimmest (1/16 duty), Level 15 = brightest (16/16 duty)
#define HT1632C_CMD_PWM_BASE 0xA0

// ── Display Dimensions ─────────────────────────────────────────────────
#define HT1632C_WIDTH  24   // Columns
#define HT1632C_HEIGHT  8   // Rows (one byte per column — convenient!)

// ── Pin defaults (override in constructor) ──────────────────────────────
// These are sensible defaults for Teensy 4.0, chosen to avoid conflict
// with pins already used by the main protogasm/nextgasm code:
//   Pin 9  = Motor PWM (MOTPIN)
//   Pin 10 = NeoPixel data (LED_PIN)
//   Pin 3,2 = Encoder quadrature
//   Pin 5  = Encoder switch
//   Pin A0 = Pressure sensor
#define HT1632C_DEFAULT_CS   6
#define HT1632C_DEFAULT_WR   7
#define HT1632C_DEFAULT_DATA 8


// ── The Display Class ──────────────────────────────────────────────────
// In C++, a "class" is similar to a Python class. The main differences:
//   - Members declared under "public:" are like regular Python methods
//   - Members under "private:" are like _underscore_prefixed conventions,
//     but actually enforced by the compiler
//   - The constructor (same name as the class) is like __init__
//   - There's no "self" — the object is implicit (like "this" in JS)

class HT1632C_Display {

public:
    // ── Constructor ────────────────────────────────────────────────────
    // Python equivalent:
    //   def __init__(self, cs=6, wr=7, data=8):
    //       self.cs = cs  ...etc
    HT1632C_Display(
        uint8_t pinCS   = HT1632C_DEFAULT_CS,
        uint8_t pinWR   = HT1632C_DEFAULT_WR,
        uint8_t pinDATA = HT1632C_DEFAULT_DATA
    );

    // ── Lifecycle ──────────────────────────────────────────────────────
    void begin();           // Call once in setup(). Initialises the chip.
    void shutdown();        // Low-power off. Call before cycled sleep etc.

    // ── Framebuffer operations ─────────────────────────────────────────
    // We draw into a local buffer first, then flush it all to the chip.
    // This avoids flicker — same idea as double-buffering in games.
    //
    // Python equivalent of the framebuffer:
    //   self.buffer = [0x00] * 24   # one byte per column

    void clear();                           // Zero the buffer (all LEDs off)
    void fill();                            // Set the buffer to all-on
    void setPixel(uint8_t x, uint8_t y);    // Turn on one LED
    void clearPixel(uint8_t x, uint8_t y);  // Turn off one LED
    bool getPixel(uint8_t x, uint8_t y);    // Read one LED's state from buffer

    // Write a raw byte (8 vertical pixels) to a column.
    // Handy for custom patterns or fast full-column writes.
    void setColumn(uint8_t col, uint8_t data);

    // ── Display output ─────────────────────────────────────────────────
    void flush();           // Push the entire framebuffer to the HT1632C
    void setBrightness(uint8_t level);  // 0 (dimmest) to 15 (brightest)

    // ── Convenience drawing ────────────────────────────────────────────
    // Render a bar graph — perfect for showing motor speed or pressure.
    // `value` is 0–max, mapped across the 24 columns.
    void drawBar(int value, int maxValue);

    // Render a simple text character at position x (basic 5×7 font).
    // Returns the width drawn, so you can chain: x += drawChar(x, 'H');
    uint8_t drawChar(uint8_t x, char c);

    // Draw a null-terminated string starting at x.
    void drawString(uint8_t x, const char* str);

    // ── Direct access to the buffer (for advanced use) ─────────────────
    // In Python terms: self.buffer[col]
    uint8_t* getBuffer();

private:
    // ── Pin assignments (set once in constructor) ──────────────────────
    uint8_t _pinCS;
    uint8_t _pinWR;
    uint8_t _pinDATA;

    // ── The framebuffer ────────────────────────────────────────────────
    // 24 bytes — one per column, each bit is one row (bit 0 = top row)
    uint8_t _buffer[HT1632C_WIDTH];

    // ── Low-level protocol methods ─────────────────────────────────────
    // These handle the raw bit-banging. You shouldn't need to call these
    // directly, but they're here if you want to send custom commands.
    void _writeBits(uint16_t data, uint8_t numBits);  // Clock out N bits
    void _sendCommand(uint8_t cmd);                     // Send a command word
    void _writeDataAt(uint8_t addr, uint8_t data);      // Write to a RAM address
};

#endif // HT1632C_DISPLAY_H

// colour_lcd.cpp — ST7789V2 colour LCD implementation
//
// Uses Adafruit's ST7789 driver, which sits on top of Adafruit_GFX 
// (a generic graphics library for small displays). The relationship 
// is similar to how your OLED uses U8g2:
//
//   U8g2      → handles SH1106 hardware + drawing primitives
//   Adafruit_GFX → handles drawing primitives (lines, rects, text)
//   Adafruit_ST7789 → handles ST7789 hardware specifics
//
// One key difference from U8g2: Adafruit_ST7789 does NOT use a 
// framebuffer by default. When you call fillScreen(), it sends the 
// pixel data directly to the display over SPI — there's no 
// "clearBuffer() → draw → sendBuffer()" cycle. This means:
//
//   Pro:  Less RAM used (240×280×2 = 134KB would be huge)
//   Con:  You can see the screen being drawn if you do complex stuff
//
// For our test (solid colour fills), this is perfect — fillScreen() 
// blasts the entire display in one fast SPI burst.

#include "colour_lcd.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

// ── Pin assignments ────────────────────────────────────────────────────
// These match what we wired up. CS/DC/RST are on free GPIO pins;
// MOSI (pin 11) and SCK (pin 13) are hardware SPI0 and don't need 
// to be specified — the library finds them automatically.
//
// Why these specific pins?
//   Pin 4:  CS  — free GPIO, keeps SPI control pins grouped
//   Pin 22: DC  — free GPIO on the left side of the Teensy
//   Pin 23: RST — adjacent to DC for tidy wiring
//
// The BL (backlight) pin is tied to 3.3V on the breadboard, so 
// the backlight is always on. No software control needed for testing.

constexpr uint8_t LCD_CS  = 4;
constexpr uint8_t LCD_DC  = 22;
constexpr uint8_t LCD_RST = 23;

// ── Display object ─────────────────────────────────────────────────────
// The constructor takes CS, DC, RST. By using this 3-argument form,
// the library knows to use hardware SPI (fast!) rather than 
// bit-banged software SPI (slow).
//
// In Python terms, this is like:
//   display = ST7789(spi_bus=SPI(0), cs=4, dc=22, rst=23)
//
// The 'static' keyword here means this object is only visible within 
// this file — other modules can't accidentally poke at the display 
// hardware directly. They go through our public functions instead.
static Adafruit_ST7789 tft = Adafruit_ST7789(LCD_CS, LCD_DC, LCD_RST);

// ── Test state ─────────────────────────────────────────────────────────
// Which colour are we showing, and when did we last flip?
// 'static' file-scope variables — like Python module-level privates.
static uint8_t colourIndex = 0;
static unsigned long lastFlipTime = 0;
constexpr unsigned long FLIP_INTERVAL_MS = 1000;  // 1 second per colour

// The colours we cycle through, in ST7789's native 16-bit RGB565 format.
// RGB565 packs a colour into 2 bytes: 5 bits red, 6 bits green, 5 bits blue.
// Green gets the extra bit because human eyes are most sensitive to green.
//
// Adafruit_GFX provides named constants for common colours:
//   ST77XX_RED   = 0xF800  (11111 000000 00000 in RGB565)
//   ST77XX_GREEN = 0x07E0  (00000 111111 00000)
//   ST77XX_BLUE  = 0x001F  (00000 000000 11111)
static const uint16_t testColours[] = {
    ST77XX_RED,
    ST77XX_GREEN,
    ST77XX_BLUE
};
constexpr uint8_t NUM_COLOURS = sizeof(testColours) / sizeof(testColours[0]);

// ════════════════════════════════════════════════════════════════════════
// Initialisation
// ════════════════════════════════════════════════════════════════════════

void colour_lcd_init()
{
    // init() takes the width and height of your specific display.
    // The Waveshare 1.69" is 240×280 pixels.
    //
    // Internally this does:
    //   1. Configures SPI (clock speed, mode)
    //   2. Pulses the RST pin to hardware-reset the controller
    //   3. Sends a sequence of initialisation commands over SPI
    //      (sleep out, colour format, memory access control, etc.)
    //   4. Turns the display on
    //
    // The SPI clock defaults to 24MHz on Teensy, which is plenty 
    // fast. A full 240×280 screen fill at 24MHz takes about 5ms.
    tft.init(240, 280);

    // Set rotation if needed. 0-3 for 0°/90°/180°/270°.
    // 0 = connector at bottom. Adjust once you see which way 
    // your display is oriented on the breadboard.
    tft.setRotation(0);

    // Start with a black screen so we know init worked even if 
    // the test tick hasn't fired yet.
    tft.fillScreen(ST77XX_BLACK);
}

// ════════════════════════════════════════════════════════════════════════
// Test tick — call from main loop
// ════════════════════════════════════════════════════════════════════════
//
// Flips to the next colour every FLIP_INTERVAL_MS milliseconds.
// Uses the same "check elapsed time" pattern as your OLED throttle 
// and the main loop's 60Hz gate.

void colour_lcd_test_tick()
{
    unsigned long now = millis();
    if (now - lastFlipTime < FLIP_INTERVAL_MS) return;
    lastFlipTime = now;

    // Fill the entire screen with the current test colour.
    // fillScreen() sends 240×280 = 67,200 pixels over SPI.
    // At 16 bits per pixel and 24MHz SPI clock, that's:
    //   67200 × 16 bits = 1,075,200 bits ÷ 24,000,000 = ~45ms
    //
    // That's longer than one 60Hz tick (16ms), so you might see 
    // a brief pause during the fill. This is fine for testing —
    // in production we'd use partial updates or DMA transfers.
    tft.fillScreen(testColours[colourIndex]);

    // Advance to next colour, wrapping around.
    // The modulo operator (%) wraps 3 back to 0, like Python's %.
    colourIndex = (colourIndex + 1) % NUM_COLOURS;
}
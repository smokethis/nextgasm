// oleddisplay.cpp — OLED display implementation
//
// Uses the U8g2 library, which is the most capable graphics library 
// for monochrome OLEDs in the Arduino ecosystem. It supports tons of 
// controllers (SH1106, SSD1306, etc.) and gives you fonts, drawing 
// primitives, and a page-buffer or full-buffer rendering mode.
//
// We're using the "F" (full buffer) variant, which means the entire 
// 128x64 display is buffered in RAM and sent to the hardware in one 
// go when you call sendBuffer(). This uses ~1KB of RAM but avoids 
// the complexity of page-based rendering. The Teensy 4.0 has 1MB 
// of RAM so this is nothing.

#include "oleddisplay.h"
#include "config.h"
#include <U8g2lib.h>
#include <Wire.h>

// The constructor name encodes the hardware config:
//   U8G2 = library name
//   SH1106 = controller chip
//   128X64 = resolution
//   NONAME = generic (non-branded) display
//   F = full framebuffer mode (vs "1" or "2" for page modes)
//   HW_I2C = hardware I2C (faster than software/bit-banged I2C)
//
// U8X8_PIN_NONE means we don't have a reset pin connected.
// Most I2C SH1106 boards tie reset high internally.
static U8G2_SH1106_128X64_NONAME_F_HW_I2C oleddisplay(U8G2_R0, U8X8_PIN_NONE);

// ── Shared display throttle ────────────────────────────────────────────
// All three display functions (display_update, display_menu, 
// display_message) share this throttle. Since only ONE of them runs 
// at any given time (the main loop picks based on AppState), they 
// won't interfere with each other.
//
// Without throttling, a 60Hz main loop would hammer the I2C bus with 
// display updates. Each full-frame I2C transfer takes ~10-30ms at 
// 400kHz, which would eat into our 16ms tick budget. Throttling to 
// 10Hz keeps updates snappy without starving other I/O.
constexpr unsigned long DISPLAY_UPDATE_INTERVAL_MS = 100;  // 10Hz
static unsigned long lastDisplayUpdate = 0;

// Returns true if enough time has passed for a display refresh.
// Updates the timestamp internally so callers can just do:
//   if (!display_throttle_check()) return;
static bool display_throttle_check()
{
    unsigned long now = millis();
    if (now - lastDisplayUpdate < DISPLAY_UPDATE_INTERVAL_MS) return false;
    lastDisplayUpdate = now;
    return true;
}

// Convert the numeric mode constant to a human-readable string.
static const char* mode_to_string(uint8_t mode)
{
    switch (mode) {
        case MANUAL:       return "Manual";
        case AUTO:         return "Auto";
        case OPT_SPEED:    return "Set Speed";
        case OPT_RAMPSPD:  return "Set Ramp";
        case OPT_BEEP:     return "Settings";
        case OPT_PRES:     return "Pressure";
        case OPT_USER_MODE: return "User Mode";
        case STANDBY:      return "Standby";
        default:           return "Unknown";
    }
}

// ════════════════════════════════════════════════════════════════════════
// Initialisation
// ════════════════════════════════════════════════════════════════════════

void display_init()
{
    oleddisplay.begin();
    oleddisplay.setFont(u8g2_font_6x10_tr);
    oleddisplay.clearBuffer();
    oleddisplay.drawStr(20, 32, "Nextgasm");
    oleddisplay.sendBuffer();
}

// ════════════════════════════════════════════════════════════════════════
// Operational Display (used in APP_RUNNING)
// ════════════════════════════════════════════════════════════════════════

void display_update(uint8_t mode, float motorSpeed, int pressure, int averagePressure, NavDirection navDir)
{
    if (!display_throttle_check()) return;

    // --- Build the frame ---
    oleddisplay.clearBuffer();

    // Mode name — large, top of screen
    oleddisplay.setFont(u8g2_font_7x14B_tr);
    oleddisplay.drawStr(0, 12, mode_to_string(mode));

    // Nav direction indicator — top right corner
    if (navDir != NAV_NONE) {
        const char* dirName = nav_direction_name(navDir);
        int textWidth = oleddisplay.getStrWidth(dirName);
        oleddisplay.drawStr(128 - textWidth, 12, dirName);
    }

    // Divider line under the mode name
    oleddisplay.drawHLine(0, 16, 128);

    // Status values — smaller font, below the divider
    oleddisplay.setFont(u8g2_font_6x10_tr);

    // Motor speed as a percentage (0-100%) and a bar graph
    int speedPct = (int)(motorSpeed / MOT_MAX * 100);
    char buf[22];

    snprintf(buf, sizeof(buf), "Motor: %3d%%", speedPct);
    oleddisplay.drawStr(0, 30, buf);

    // Visual bar for motor speed — 50px wide, next to the text
    int barWidth = map(speedPct, 0, 100, 0, 50);
    oleddisplay.drawFrame(74, 22, 52, 10);    // Outline
    oleddisplay.drawBox(75, 23, barWidth, 8);  // Filled portion

    // Pressure delta (what the edging algorithm actually uses)
    int delta = pressure - averagePressure;
    snprintf(buf, sizeof(buf), "Pressure: %4d", delta);
    oleddisplay.drawStr(0, 44, buf);

    // Raw pressure for debugging / trimpot adjustment
    snprintf(buf, sizeof(buf), "Raw: %4d  Avg: %4d", pressure, averagePressure);
    oleddisplay.drawStr(0, 58, buf);

    // --- Send to hardware ---
    oleddisplay.sendBuffer();
}

// ════════════════════════════════════════════════════════════════════════
// Menu Display (used in APP_MENU)
// ════════════════════════════════════════════════════════════════════════
//
// Layout for a 128x64 OLED with 3 menu items:
//
//   ┌────────────────────────────┐
//   │     N E X T G A S M        │  ← title, bold font, centred
//   │────────────────────────────│  ← divider line at y=18
//   │                            │
//   │    ▸ Start                 │  ← selected item (▸ = filled triangle)
//   │      Settings              │
//   │      Demo                  │
//   │                            │
//   └────────────────────────────┘
//
// The cursor is a small filled triangle (▸) drawn to the left of the 
// highlighted item. It's more visually distinct than a ">" character 
// and doesn't depend on the font having that glyph at the right size.
//
// Vertical spacing is calculated to centre the item list in the 
// remaining space below the divider. With 3 items at 14px line height, 
// that's 42px of content in 44px of space — nicely balanced.

void display_menu(const char* title, const char* items[], uint8_t itemCount, uint8_t cursorPos)
{
    if (!display_throttle_check()) return;

    oleddisplay.clearBuffer();

    // ── Title ──────────────────────────────────────────────────────────
    oleddisplay.setFont(u8g2_font_7x14B_tr);
    int titleWidth = oleddisplay.getStrWidth(title);
    int titleX = (128 - titleWidth) / 2;  // Centre horizontally
    oleddisplay.drawStr(titleX, 13, title);

    // Divider
    oleddisplay.drawHLine(0, 18, 128);

    // ── Menu items ─────────────────────────────────────────────────────
    // Each item gets 14px of vertical space (matches the bold font height).
    // We start at y=32 which gives a comfortable gap below the divider.
    //
    // Y coordinates in U8g2 refer to the font BASELINE (bottom of 
    // letters, not top). So y=32 means the bottom of the first line 
    // of text sits at pixel row 32. This is like CSS with 
    // vertical-align: baseline, not top.
    
    oleddisplay.setFont(u8g2_font_7x14_tr);  // Regular weight for items

    constexpr uint8_t ITEM_START_Y = 34;   // Baseline of first item
    constexpr uint8_t ITEM_SPACING = 15;   // Pixels between baselines
    constexpr uint8_t TEXT_LEFT = 20;       // Left margin for item text
    constexpr uint8_t CURSOR_LEFT = 8;     // Left margin for cursor triangle

    for (uint8_t i = 0; i < itemCount; i++) {
        uint8_t y = ITEM_START_Y + (i * ITEM_SPACING);

        // Draw the item label
        oleddisplay.drawStr(TEXT_LEFT, y, items[i]);

        // Draw cursor triangle next to the highlighted item.
        // Using drawTriangle() to make a small filled right-pointing 
        // arrow: ▸. Three vertices form a rightward-pointing triangle.
        //
        // The triangle is 5px wide and 7px tall, vertically centred 
        // on the text line. Since y is the baseline and the font is 
        // ~10px tall with ascenders, we offset upward by 8px for the 
        // top vertex and 1px for the bottom.
        if (i == cursorPos) {
            // Filled triangle: (x0,y0), (x1,y1), (x2,y2)
            //   Top vertex:    (CURSOR_LEFT, y - 9)
            //   Bottom vertex: (CURSOR_LEFT, y - 2)
            //   Point vertex:  (CURSOR_LEFT + 5, y - 5)  ← the rightward tip
            oleddisplay.drawTriangle(
                CURSOR_LEFT,     y - 9,   // Top-left
                CURSOR_LEFT,     y - 2,   // Bottom-left
                CURSOR_LEFT + 5, y - 5    // Right point (tip of arrow)
            );
        }
    }

    oleddisplay.sendBuffer();
}

// ════════════════════════════════════════════════════════════════════════
// Message Display (used for placeholder screens)
// ════════════════════════════════════════════════════════════════════════
//
// Simple two-line centred display for screens that don't have full 
// UI yet (Settings, Demo). Shows a title and a message, plus a hint 
// about how to get back to the menu.
//
// Layout:
//   ┌────────────────────────────┐
//   │                            │
//   │        SETTINGS            │  ← title, bold, centred
//   │      Coming soon...        │  ← message, regular, centred
//   │                            │
//   │          \x18 Back              │  ← nav hint
//   └────────────────────────────┘

void display_message(const char* title, const char* message)
{
    if (!display_throttle_check()) return;

    oleddisplay.clearBuffer();

    // Title — bold, centred vertically and horizontally
    oleddisplay.setFont(u8g2_font_7x14B_tr);
    int titleWidth = oleddisplay.getStrWidth(title);
    oleddisplay.drawStr((128 - titleWidth) / 2, 28, title);

    // Message — regular weight, centred below title
    oleddisplay.setFont(u8g2_font_6x10_tr);
    int msgWidth = oleddisplay.getStrWidth(message);
    oleddisplay.drawStr((128 - msgWidth) / 2, 44, message);

    // Navigation hint — plain text since the arrow glyphs in U8g2's 
    // small fonts are unreliable (they're control characters in some 
    // font builds and get silently skipped).
    oleddisplay.setFont(u8g2_font_5x7_tr);
    oleddisplay.drawStr(36, 63, "UP = Back to menu");

    oleddisplay.sendBuffer();
}

// 4×4 Bayer threshold matrix, stored in program memory.
// 'static constexpr' means it's computed at compile time and lives 
// in flash, not RAM. Same as a Python module-level tuple of tuples 
// that never changes.
static constexpr uint8_t BAYER4[4][4] = {
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 }
};

// How many pixels below the surface before the fill becomes 
// fully solid. Larger = more gradual fade-in = more visible 
// translucency. 20px gives a nice band of dithering that's 
// clearly visible on a 64px tall display.
constexpr int DITHER_DEPTH = 15;

// Display water effect in demo mode
void display_demo_water(float gsr)
{
    // 'static float phase' persists between calls — this is our 
    // time variable. Incrementing by a fixed amount each call gives 
    // smooth animation. Like a Python class attribute that accumulates.
    static float phase = 0.0f;
    phase += 0.08f;  // Scroll speed (tune to taste at 10Hz)

    if (!display_throttle_check()) return;

    oleddisplay.clearBuffer();

    // For each of the 128 columns, calculate where the water 
    // surface sits, then fill every pixel below it.
    for (int x = 0; x < 128; x++)
    {
        // Base water level — GSR lifts the water up the screen.
        // Remember: Y=0 is TOP of the OLED, Y=63 is bottom.
        // So a lower baseY number = higher water on screen.
        float baseY = 52.0f - (gsr * 35.0f);  // Range: ~47 (rest) to ~22 (max)

        // Layer three sine waves with increasing frequency.
        // Amplitude scales with GSR so calm = gentle, aroused = choppy.
        //
        // sin() returns -1 to +1, so multiplying by amplitude gives 
        // the wave height in pixels. Same as Python's math.sin().
        float wave1 = sinf(x * 0.05f + phase * 0.8f)  * (2.0f + gsr * 6.0f);
        float wave2 = sinf(x * 0.12f + phase * 1.7f)  * (gsr * 5.0f);
        float wave3 = sinf(x * 0.25f + phase * 3.1f)  * (fmaxf(0, gsr - 0.4f) * 5.0f);

        // Surface position for this column
        int surfaceY = (int)(baseY + wave1 + wave2 + wave3);
        surfaceY = constrain(surfaceY, 0, 63);

        // Fill from surface down to the bottom of the screen.
        // drawVLine is the fastest way to fill a column in U8g2.
        if (surfaceY < 64) {
            for (int x = 0; x < 128; x++)
                {
                float baseY = 52.0f - (gsr * 35.0f);

                float wave1 = sinf(x * 0.05f + phase * 0.8f) * (2.0f + gsr * 6.0f);
                float wave2 = sinf(x * 0.12f + phase * 1.7f) * (gsr * 5.0f);
                float wave3 = sinf(x * 0.25f + phase * 3.1f) * (fmaxf(0, gsr - 0.4f) * 5.0f);

                int surfaceY = constrain((int)(baseY + wave1 + wave2 + wave3), 0, 63);

                // Fill from surface to bottom, with dithering near the surface.
                for (int y = surfaceY; y < 64; y++)
                {
                    // How far below the surface is this pixel?
                    int depth = y - surfaceY;

                    if (depth >= DITHER_DEPTH)
                    {
                        // Past the dither zone — fill the rest solid in one call
                        // and break out of the per-pixel loop. This is the kind of
                        // early-exit optimisation that matters in tight loops.
                        // In Python: you'd slice the remaining range and bulk-fill.
                        oleddisplay.drawVLine(x, y, 64 - y);
                        break;  // Done with this column
                    }

                    // Map depth to a 0–15 brightness value.
                    // At depth 0 (the surface): brightness ≈ 0 → almost no pixels
                    // At depth >= DITHER_DEPTH:  brightness = 15 → solid fill
                    //
                    // The +1 means even the very surface row gets *some* pixels,
                    // giving a thin bright edge that reads as the water's surface.
                    int brightness = (depth + 1) * 15 / DITHER_DEPTH;

                    // Compare against the Bayer threshold at this pixel's 
                    // position in the repeating 4×4 tile.
                    //
                    // '& 3' is a fast way to do '% 4' — it works because 4 is 
                    // a power of 2. The compiler would optimise the modulo anyway, 
                    // but it's a common embedded idiom you'll see everywhere.
                    // In Python: x % 4 and y % 4
                    if (brightness > BAYER4[y & 3][x & 3])
                    {
                        oleddisplay.drawPixel(x, y);
                    }
                }
            }
        }
    }

    oleddisplay.sendBuffer();
}
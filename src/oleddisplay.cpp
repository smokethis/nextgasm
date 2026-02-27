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

    // ── Navigation hint at bottom ──────────────────────────────────────
    // Subtle reminder of controls. Small font, grey-ish (on a mono 
    // display "grey" just means we draw it — could be dimmed with 
    // setDrawColor in some modes, but keeping it simple for now).
    // Navigation hint — using a font that includes arrow glyphs.
    // The "_tf" suffix means "transparent, full range" (chars 0-255)
    // vs "_tr" which only covers printable ASCII (32-127).
    // 0x18 = ↑ and 0x19 = ↓ in the CP437 character set that U8g2 uses.
    oleddisplay.setFont(u8g2_font_5x7_tf);
    oleddisplay.drawStr(28, 63, "\x18\x19 Navigate  OK Select");

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

    // Navigation hint
    oleddisplay.setFont(u8g2_font_5x7_tf);
    oleddisplay.drawStr(44, 63, "\x18 Back to menu");

    oleddisplay.sendBuffer();
}
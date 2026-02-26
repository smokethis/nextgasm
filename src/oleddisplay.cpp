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
static U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// Display updates are slow (~10-30ms for a full I2C transfer at 
// 400kHz). We don't want to block the 60Hz control loop, so we 
// throttle display refreshes to a sensible rate.
constexpr unsigned long DISPLAY_UPDATE_INTERVAL_MS = 100;  // 10Hz
static unsigned long lastDisplayUpdate = 0;

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
        default:           return "Unknown";
    }
}

void display_init()
{
    display.begin();
    display.setFont(u8g2_font_6x10_tr);
    display.clearBuffer();
    display.drawStr(20, 32, "Nextgasm");
    display.sendBuffer();
}

void display_update(uint8_t mode, float motorSpeed, int pressure, int averagePressure, NavDirection navDir)
{
    // Throttle updates so we don't bog down the main loop.
    unsigned long now = millis();
    if (now - lastDisplayUpdate < DISPLAY_UPDATE_INTERVAL_MS) return;
    lastDisplayUpdate = now;

    // --- Build the frame ---
    display.clearBuffer();

    // Mode name — large, top of screen
    display.setFont(u8g2_font_7x14B_tr);
    display.drawStr(0, 12, mode_to_string(mode));

    // Nav direction indicator — top right corner
    // Shows which direction on the 5-way switch is currently pressed.
    // This gives immediate visual feedback for testing the switch.
    if (navDir != NAV_NONE) {
        // Draw direction name right-aligned in the header area
        const char* dirName = nav_direction_name(navDir);
        // getStrWidth() returns pixel width of a string in the current font.
        // We use it to right-align: start position = screen width - text width.
        // In Python terms: x = 128 - len(text) * char_width
        int textWidth = display.getStrWidth(dirName);
        display.drawStr(128 - textWidth, 12, dirName);
    }

    // Divider line under the mode name
    display.drawHLine(0, 16, 128);

    // Status values — smaller font, below the divider
    display.setFont(u8g2_font_6x10_tr);

    // Motor speed as a percentage (0-100%) and a bar graph
    int speedPct = (int)(motorSpeed / MOT_MAX * 100);
    
    char buf[22];

    snprintf(buf, sizeof(buf), "Motor: %3d%%", speedPct);
    display.drawStr(0, 30, buf);

    // Visual bar for motor speed — 60px wide max, next to the text
    int barWidth = map(speedPct, 0, 100, 0, 50);
    display.drawFrame(74, 22, 52, 10);    // Outline
    display.drawBox(75, 23, barWidth, 8); // Filled portion

    // Pressure delta (what the edging algorithm actually uses)
    int delta = pressure - averagePressure;
    snprintf(buf, sizeof(buf), "Pressure: %4d", delta);
    display.drawStr(0, 44, buf);

    // Raw pressure for debugging / trimpot adjustment
    snprintf(buf, sizeof(buf), "Raw: %4d  Avg: %4d", pressure, averagePressure);
    display.drawStr(0, 58, buf);

    // --- Send to hardware ---
    display.sendBuffer();
}
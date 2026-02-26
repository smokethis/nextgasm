// main.cpp — Verification test for fixed HT1632C driver
//
// TEST SEQUENCE:
// 1. Sweep a single column left-to-right across all 24 columns (400ms each)
//    OLED shows which column number is active.
//    EXPECTED: Column walks smoothly from left to right, one at a time,
//    hitting all 24 positions with straight vertical lines.
//
// 2. Fill the entire display (all 24×8 LEDs on) for 2 seconds.
//    EXPECTED: Every single LED lit.
//
// 3. Display "HELLO" text using the built-in 5×7 font.
//    EXPECTED: Readable text, oriented correctly (not mirrored/reversed).
//
// 4. Bar graph sweep from 0% to 100%.
//    EXPECTED: Smooth fill from left to right.

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "HT1632C_Display.h"

// OLED display for status messages
static U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

// LED matrix with default pins (CS=6, WR=7, DATA=8)
HT1632C_Display ledMatrix;

void oled_message(const char* line1, const char* line2 = nullptr) {
    oled.clearBuffer();
    oled.setFont(u8g2_font_7x14B_tr);
    oled.drawStr(0, 14, line1);
    if (line2) {
        oled.setFont(u8g2_font_6x10_tr);
        oled.drawStr(0, 32, line2);
    }
    oled.sendBuffer();
}

void setup() {
    Serial.begin(115200);
    
    // Init OLED
    oled.begin();
    oled_message("Matrix Test", "Initialising...");
    delay(1000);

    // Init LED matrix
    ledMatrix.begin();
    
    // ════════════════════════════════════════════════════════════════
    // TEST 1: Single column sweep, left to right, all 24 columns
    // ════════════════════════════════════════════════════════════════
    oled_message("TEST 1", "Column sweep L->R");
    delay(1000);

    for (int col = 0; col < 24; col++) {
        ledMatrix.clear();
        ledMatrix.setColumn(col, 0xFF);  // All 8 LEDs in this column
        ledMatrix.flush();

        // Show column number on OLED
        char buf[32];
        snprintf(buf, sizeof(buf), "Column: %d / 23", col);
        oled.clearBuffer();
        oled.setFont(u8g2_font_7x14B_tr);
        oled.drawStr(0, 14, "TEST 1: Sweep");
        oled.setFont(u8g2_font_6x10_tr);
        oled.drawStr(0, 32, buf);
        
        // Visual position indicator bar on OLED
        int oledX = map(col, 0, 23, 0, 127);
        oled.drawFrame(0, 44, 128, 12);
        oled.drawBox(0, 45, oledX + 2, 10);
        oled.sendBuffer();

        Serial.print("Column sweep: ");
        Serial.println(col);
        
        delay(400);
    }

    // ════════════════════════════════════════════════════════════════
    // TEST 2: Fill — every LED on
    // ════════════════════════════════════════════════════════════════
    oled_message("TEST 2", "All LEDs ON");
    ledMatrix.fill();
    ledMatrix.flush();
    Serial.println("Fill test: all on");
    delay(2000);

    // ════════════════════════════════════════════════════════════════
    // TEST 3: Text rendering — "HELLO"
    // ════════════════════════════════════════════════════════════════
    oled_message("TEST 3", "Text: HELLO");
    ledMatrix.clear();
    ledMatrix.drawString(0, "HELLO");
    ledMatrix.flush();
    Serial.println("Text test: HELLO");
    delay(3000);

    // ════════════════════════════════════════════════════════════════
    // TEST 4: Bar graph sweep — validates drawBar()
    // ════════════════════════════════════════════════════════════════
    oled_message("TEST 4", "Bar graph sweep");
    for (int v = 0; v <= 100; v += 2) {
        ledMatrix.clear();
        ledMatrix.drawBar(v, 100);
        ledMatrix.flush();
        delay(50);
    }
    delay(1000);

    // Done
    ledMatrix.clear();
    ledMatrix.flush();
    oled_message("TESTS COMPLETE", "Check serial log");
    Serial.println("All tests complete.");
}

void loop() {
    // Nothing — tests run once in setup()
}
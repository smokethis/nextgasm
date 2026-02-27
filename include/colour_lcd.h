// colour_lcd.h — Waveshare 1.69" ST7789V2 colour LCD interface
//
// Driver based on Waveshare's official Arduino demo code, adapted
// for Teensy 4.0 with raw SPI. Confirmed working 2026-02-27 after
// discovering a broken CLK cable on the display module.
//
// Display: 240×280 pixels, RGB565, IPS panel
// Controller: ST7789V2
// Interface: SPI_MODE3 with separate DC (data/command) pin
//
// Hardware notes:
//   - Requires SPI_MODE3 (CPOL=1, CPHA=1)
//   - Init commands need CS toggled per-byte (acts as latch)
//   - Bulk pixel data can be streamed with CS held low
//   - Full power/gamma init sequence required (not minimal ST7789)

#pragma once

#include <Arduino.h>

// ── Pin assignments ────────────────────────────────────────────────────
// CS is software-controlled (not hardware SPI CS on pin 10, which is
// used by NeoPixels). DC and RST on adjacent pins for tidy wiring.
constexpr uint8_t LCD_PIN_CS  = 4;   // Software chip select
constexpr uint8_t LCD_PIN_DC  = 22;  // Data/Command select
constexpr uint8_t LCD_PIN_RST = 23;  // Hardware reset
// BL (backlight) tied directly to 3.3V — no PWM control for now.

// ── Display dimensions ─────────────────────────────────────────────────
constexpr uint16_t LCD_WIDTH  = 240;
constexpr uint16_t LCD_HEIGHT = 280;

// ── Helper macro for compile-time RGB565 conversion ────────────────────
// RGB565 packs 16 bits of colour into 2 bytes:
//   Bits 15-11: Red   (5 bits → 32 levels)
//   Bits 10-5:  Green (6 bits → 64 levels — human eyes are more 
//                       sensitive to green, so it gets the extra bit)
//   Bits 4-0:   Blue  (5 bits → 32 levels)
//
// The >> 3 and >> 2 throw away the lower bits of 8-bit colour values
// to fit them into 5 or 6 bits. It's lossy — (0x07 >> 3) = 0, so 
// very dark values collapse to black. That's fine for fire where 
// the interesting stuff happens in the bright end.
//
// In Python terms: int(r / 8) << 11 | int(g / 4) << 5 | int(b / 8)
#define RGB565(r, g, b) \
    (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

// ── Public interface ───────────────────────────────────────────────────

// Initialize SPI, reset the display, run the full Waveshare init
// sequence, and clear to black. Call once from setup().
void lcd_init();

// Fill the entire screen with a single RGB565 colour.
void lcd_fill(uint16_t colour);

// Diagnostic: cycle through solid colours every ~1 second.
// Call from loop() — manages its own timing internally.
void lcd_test_tick();

// ── Bulk pixel drawing API ─────────────────────────────────────────────
// These three functions let you push arbitrary pixel data to any 
// rectangular region of the screen. They're the building blocks for 
// things like the fire effect, sprite rendering, or any per-pixel work.
//
// Usage pattern (same idea as Python's context manager):
//
//   lcd_begin_draw(0, 0, 239, 279);   # with open(file) as f:
//   for each pixel:
//       lcd_push_pixel(colour);        #     f.write(data)
//   lcd_end_draw();                    # (auto-close)
//
// IMPORTANT: Pixels fill left-to-right, top-to-bottom within the 
// window you set. The ST7789 controller auto-increments its internal
// address pointer, so you just blast pixels sequentially and it fills
// the rectangle like reading a book — left to right, then next line.
// No need to set coordinates per-pixel.
//
// Between begin_draw and end_draw, the SPI bus is held (CS low, 
// DC high). Don't call any other LCD functions in between — it would 
// corrupt the data stream. In Python terms, it's like holding a file 
// lock: only one writer at a time.

void lcd_begin_draw(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void lcd_push_pixel(uint16_t colour);
void lcd_end_draw();
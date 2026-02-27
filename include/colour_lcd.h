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

// ── Public interface ───────────────────────────────────────────────────

// Initialize SPI, reset the display, run the full Waveshare init
// sequence, and clear to black. Call once from setup().
void lcd_init();

// Fill the entire screen with a single RGB565 colour.
void lcd_fill(uint16_t colour);

// Diagnostic: cycle through solid colours every ~1 second.
// Call from loop() — manages its own timing internally.
void lcd_test_tick();
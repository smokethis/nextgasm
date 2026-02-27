// colour_lcd.h — ST7789V2 colour LCD interface
//
// Drives a Waveshare 1.69" 240x280 colour LCD over SPI.
// For now this is just a test module to verify the wiring works —
// it cycles through solid red, green, blue fills.
//
// The ST7789V2 is an SPI device, which means it shares the SPI bus 
// with any other SPI peripherals you add later (e.g. an SD card).
// Each SPI device gets its own CS (Chip Select) pin — pulling CS LOW 
// tells that specific device "listen up, this data is for you."
// When CS is HIGH, the device ignores everything on the bus.
//
// In Python terms, SPI is like a shared radio channel where CS is 
// each device's name — you call their name before speaking to them.

#pragma once

#include <Arduino.h>

// Initialise the display hardware. Call once from setup().
void colour_lcd_init();

// Call this from the main loop. It manages its own timing internally
// and flips to the next colour when enough time has elapsed.
// Safe to call every tick — it only redraws when it's time.
void colour_lcd_test_tick();
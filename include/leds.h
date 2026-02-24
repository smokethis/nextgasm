// leds.h — LED drawing functions interface

#pragma once

#include <Arduino.h>
#include "FastLED.h"   // Need CRGB type for our function signatures
#include "config.h"    // Need NUM_LEDS

// Make the LED array accessible to other files that include this header.
// 'extern' here means "this array is defined in leds.cpp, but you can 
// reference it." Same pattern as Python's module-level variables.
extern CRGB leds[];

void draw_cursor(int pos, CRGB C1);
void draw_cursor_3(int pos, CRGB C1, CRGB C2, CRGB C3);
void draw_bars_3(int pos, CRGB C1, CRGB C2, CRGB C3);
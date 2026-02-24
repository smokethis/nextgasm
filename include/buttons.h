// buttons.h — Rotary encoder and button press detection

#pragma once

#include <Arduino.h>

// Initialize the encoder button pin. Called from setup().
void button_init();

// Poll the encoder button each tick. Returns one of:
//   BTN_NONE, BTN_SHORT, BTN_LONG, BTN_V_LONG
// 
// This is a simple state machine that tracks key-down time.
// Detection happens on key-up (release), so the press duration 
// is measured from press to release — similar to how a Python 
// GUI framework's on_release callback works.
uint8_t check_button();

// Read the encoder knob, clamped to [minVal, maxVal].
// Divides raw encoder pulses by 4 to match physical detent clicks.
// (Most rotary encoders produce 4 electrical pulses per click.)
int encLimitRead(int minVal, int maxVal);
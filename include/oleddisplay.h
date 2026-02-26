// oleddisplay.h — OLED display interface
//
// Drives a 128x64 SH1106 OLED over I2C to show current device 
// status at a glance. Think of this like a simple dashboard.

#pragma once

#include <Arduino.h>
#include "nav_switch.h"  // For NavDirection type

// Initialize the display hardware. Called once from setup().
void display_init();

// Refresh the display with current state values.
// Call this from the main loop, but not every tick — OLED updates 
// are slow compared to the 60Hz loop (see oleddisplay.cpp for details).
void display_update(uint8_t mode, float motorSpeed, int pressure, int averagePressure, NavDirection navDir);
// oleddisplay.h — OLED display interface
//
// Drives a 128x64 SH1106 OLED over I2C to show current device 
// status at a glance. Think of this like a simple dashboard.
//
// This module "owns" the OLED hardware — all rendering goes through 
// here, whether it's the operational status display, the main menu, 
// or a placeholder message screen. Other modules (like menu.cpp) 
// tell this module WHAT to draw, but never touch the display directly.

#pragma once

#include <Arduino.h>
#include "nav_switch.h"  // For NavDirection type

// Initialize the display hardware. Called once from setup().
void display_init();

// ── Operational display ────────────────────────────────────────────────
// Refresh the display with current state values.
// Call this from the main loop when in APP_RUNNING state.
// Throttled internally — safe to call every tick.
void display_update(uint8_t mode, float motorSpeed, int pressure, int averagePressure, NavDirection navDir);

// ── Menu display ───────────────────────────────────────────────────────
// Draw a menu screen with a title, a list of items, and a cursor 
// indicating which item is currently highlighted.
//
// Parameters:
//   title     — header text displayed at the top (e.g. "NEXTGASM")
//   items     — array of C strings, one per menu item
//   itemCount — how many items in the array
//   cursorPos — which item is highlighted (0-indexed)
//
// Throttled internally — safe to call every tick.
void display_menu(const char* title, const char* items[], uint8_t itemCount, uint8_t cursorPos);

// ── Message display ────────────────────────────────────────────────────
// Draw a simple two-line centred message. Useful for placeholder 
// screens (Settings, Demo) before their full UI is built out.
//
// Throttled internally — safe to call every tick.
void display_message(const char* title, const char* message);
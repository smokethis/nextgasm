// menu.h — Top-level menu system
//
// This sits ABOVE the operational state machine (state.h / modes.h).
// Think of it like a launcher or home screen — the existing mode-cycling
// (Manual, Auto, Speed settings, etc.) is one "app" that runs when you
// select "Start" from this menu.
//
// Navigation:
//   NAV_UP / NAV_DOWN  — move the cursor
//   NAV_CENTER         — select the highlighted item
//   NAV_UP from any running app state returns here (handled in main.cpp)
//
// In Python terms, the relationship is something like:
//
//   class App:
//       def __init__(self):
//           self.screen = MainMenu()         # ← this module
//           self.operational = StateMachine() # ← existing state.h/modes.h
//
//       def run(self):
//           if self.screen == "menu":
//               choice = self.screen.handle_input()
//               if choice == "start":
//                   self.screen = "running"
//           elif self.screen == "running":
//               self.operational.tick()

#pragma once

#include <Arduino.h>
#include "nav_switch.h"

// ── Application-level states ───────────────────────────────────────────
// These are the "big picture" states — which screen the device is 
// currently showing. They're a layer above the operational modes 
// (MANUAL, AUTO, OPT_SPEED, etc.) which only exist within APP_RUNNING.
//
// Using an enum with an explicit underlying type (uint8_t) keeps it 
// small and prevents accidental mixing with the operational state 
// constants in config.h.
enum AppState : uint8_t {
    APP_MENU,        // Main menu displayed on OLED
    APP_RUNNING,     // Device is operational (existing state machine active)
    APP_SETTINGS,    // Settings submenu (placeholder for now)
    APP_DEMO         // Demo / attract mode (placeholder for now)
};

// ── Public interface ───────────────────────────────────────────────────

// Set up the menu system. Call once from setup().
void menu_init();

// Process a nav switch direction and return the resulting app state.
//
// If the user just moves the cursor, this returns APP_MENU (stay here).
// If they press center on an item, it returns that item's target state
// (e.g. APP_RUNNING for "Start").
//
// Edge detection is handled internally — holding a direction doesn't 
// repeat. You need to release and press again to move further.
AppState menu_update(NavDirection dir);

// Draw the current menu state to the OLED display.
// Call this from the main loop when in APP_MENU state.
// Rendering is throttled internally (won't hog I2C bandwidth).
void menu_render();

// Reset the cursor to the top item. Call this when returning to the 
// menu from another state, so the user always sees "Start" highlighted.
void menu_reset_cursor();
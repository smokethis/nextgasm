// fire_effect.h — Doom Fire effect for the ST7789V2 colour LCD
//
// Implements the classic "Doom PSX fire" algorithm — a simple cellular
// automaton that produces surprisingly convincing fire with very little
// code. The algorithm was famously used in the PlayStation port of Doom
// and later documented by Fabien Sanglard.
//
// HOW IT WORKS (the short version):
//
// Imagine a grid of numbers, where each number represents "heat" 
// (0 = cold/black, 36 = max heat/white). The bottom row is always 
// set to maximum heat — that's the "fuel" feeding the fire.
//
// Each tick, every cell looks at the cell BELOW it, copies that heat 
// value with a small random reduction (cooling), and optionally shifts 
// sideways (wind). Heat propagates upward and cools as it goes, 
// creating the characteristic flame shape.
//
// A colour palette maps heat values to fire colours:
//   0 = black → dark red → red → orange → yellow → white = 36
//
// In Python pseudocode:
//
//   for y in range(height - 1):    # everything except bottom row
//       for x in range(width):
//           src_heat = grid[y + 1][x]            # look below
//           cooling = random.randint(0, 2)        # random decay
//           wind = random.randint(-1, 1)          # horizontal drift
//           dest_x = clamp(x + wind, 0, width-1)
//           grid[y][dest_x] = max(0, src_heat - cooling)
//
// That's it. ~5 lines of logic produce realistic fire. The magic is 
// in the palette and the randomness creating organic-looking turbulence.
//
// PERFORMANCE NOTES:
//
// The simulation runs at a reduced resolution (FIRE_WIDTH × FIRE_HEIGHT)
// and is scaled up 2× in each dimension when pushed to the LCD. This 
// keeps the simulation cheap while filling the full 240×280 display.
//
// The SPI transfer is the bottleneck (~45ms at 24MHz for a full frame),
// so the fire tick is throttled to ~20 FPS. The Teensy's 600MHz CPU
// handles the simulation itself in under 1ms.

#pragma once

#include <Arduino.h>

// ── Simulation dimensions ──────────────────────────────────────────────
// Half the LCD resolution in each dimension. Each fire pixel becomes
// a 2×2 block on screen, giving a nice chunky retro look that also
// happens to be very fire-appropriate — real flames don't have sharp
// per-pixel detail anyway.
constexpr uint16_t FIRE_WIDTH  = 120;   // LCD_WIDTH / 2
constexpr uint16_t FIRE_HEIGHT = 140;   // LCD_HEIGHT / 2

// ── Public interface ───────────────────────────────────────────────────

// Set up the fire buffer and seed the bottom row.
// Call once before the first fire_tick().
void fire_init();

// Run one simulation step and push the result to the LCD.
// Throttled internally — safe to call every main loop tick.
// Manages its own timing so it won't hog the SPI bus on every tick.
void fire_tick();
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
//   for y in range(height - 1):
//       for x in range(width):
//           src_heat = grid[y + 1][x]
//           cooling = random.randint(0, 2)
//           wind = random.randint(-1, 1)
//           dest_x = clamp(x + wind, 0, width-1)
//           grid[y][dest_x] = max(0, src_heat - cooling)
//
// ═══════════════════════════════════════════════════════════════════════
// SCALING: 4× CHUNKY MODE
// ═══════════════════════════════════════════════════════════════════════
//
// The simulation runs at 60×70 (quarter-resolution) and each fire
// pixel becomes a 4×4 block on the 240×280 LCD. This gives:
//   60 × 4 = 240 horizontal  ✓
//   70 × 4 = 280 vertical    ✓
//
// The 4× scaling creates bold, blocky flames — more like a campfire
// or torch than a delicate candle. Each flame cell is large enough
// to see clearly, giving the fire a physical, tangible quality.
//
// ═══════════════════════════════════════════════════════════════════════
// DMA DOUBLE-BUFFERING
// ═══════════════════════════════════════════════════════════════════════
//
// The rendering uses two full-frame pixel buffers in a ping-pong
// arrangement:
//
//   Buffer A ← CPU renders the next frame here
//   Buffer B → DMA streams this to the LCD via SPI
//
//   When DMA finishes:
//     swap A and B
//     kick off new DMA transfer (now sending what was just rendered)
//     CPU starts rendering the NEXT frame into the now-free buffer
//
// This means the CPU never waits for the display. It's like a
// restaurant kitchen with two serving plates: the waiter takes one
// to the table while the chef starts plating the next dish. Neither
// ever has to wait for the other.
//
// The two pixel buffers use DMAMEM to live in the Teensy's secondary
// RAM (RAM2/OCRAM), keeping the fast tightly-coupled DTCM free for
// the stack and other time-critical data.

#pragma once

#include <Arduino.h>

// ── Simulation dimensions ──────────────────────────────────────────────
// Quarter LCD resolution in each dimension. Each fire pixel becomes
// a 4×4 block on screen, giving bold, chunky flames.
constexpr uint16_t FIRE_WIDTH  = 60;    // LCD_WIDTH  / 4
constexpr uint16_t FIRE_HEIGHT = 70;    // LCD_HEIGHT / 4

// ── Public interface ───────────────────────────────────────────────────

// Set up the fire buffer, pixel buffers, and seed the bottom row.
// Call once before the first fire_tick().
void fire_init();

// Run one simulation step, render to a pixel buffer, and kick off
// a DMA transfer if the previous one has finished.
// Non-blocking — safe to call every main loop tick.
// If DMA is still busy sending the last frame, this returns
// immediately without doing anything (frame skip).
void fire_tick();
// matrix_graph.h — Scrolling arousal history graph for HT1632C LED matrix
//
// Draws a left-scrolling bar chart on the 24×8 LED matrix where each
// column represents an arousal snapshot (pressure delta) at a point 
// in time. New data enters from the right edge; old data scrolls off
// the left.
//
// Because the HT1632C is a 1-bit display (each LED is simply on or 
// off — no per-pixel brightness), we fake "dimming" with spatial 
// dithering: older columns have fewer pixels lit per row, creating 
// the visual impression of fading. The peak pixel of each column is 
// always fully lit, making it stand out as a bright dot above the 
// dimmer body of the bar.
//
// In Python terms, the data flow is:
//
//   history = deque(maxlen=24)     # one value per column
//   every 500ms:
//       history.append(current_arousal)
//   every frame:
//       for col, value in enumerate(history):
//           age = 23 - col
//           draw_column(col, value, dim_factor=age)

#pragma once

#include <Arduino.h>
#include "HT1632C_Display.h"

// Clear the history buffer. Call once from setup().
void matrix_graph_init();

// Call every main loop tick (~60Hz). Manages its own scroll timing 
// internally — safe to call every frame without flooding the display.
//
// arousalDelta:  pressure - averagePressure (the signal the edging 
//                algorithm watches). Can be negative; clamped to 0.
// maxDelta:      pressureLimit — scales the bar height so a "just 
//                about to trigger" reading fills the full 8 rows.
// display:       reference to the HT1632C display to render on.
void matrix_graph_tick(int arousalDelta, int maxDelta, HT1632C_Display& display);
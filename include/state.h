// state.h — State machine dispatcher and transition logic

#pragma once

#include <Arduino.h>

// Run the current state's logic (dispatches to the right mode function).
void run_state_machine(uint8_t state);

// Determine the next state based on the current state and button press.
// Handles transitions between modes and the power-off sequence.
// Returns the new state.
// uint8_t set_state(uint8_t state);
uint8_t get_next_state(uint8_t state);
uint8_t get_previous_state(uint8_t state);
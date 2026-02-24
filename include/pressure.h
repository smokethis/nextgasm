// pressure.h — Pressure reading and running average interface

#pragma once

#include <Arduino.h>

// Call once per main loop tick. Handles sampling the ADC and 
// updating the running average at the correct sub-frequency.
// sampleTick is the main loop's tick counter.
void update_pressure(int sampleTick);

// Read the raw pressure sensor (4x oversampled).
// Separated out so it can also be used in the debug display mode.
int read_pressure_raw();

void pressure_init();
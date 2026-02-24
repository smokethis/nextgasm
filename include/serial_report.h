// serial_report.h — USB serial data reporting

#pragma once

#include <Arduino.h>

// Output current state over serial in CSV-like format.
// Useful for external analysis tools or plotting.
void report_serial();

void debug_print(const char* label, int value);
void debug_print(const char* label, float value);
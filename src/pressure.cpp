// pressure.cpp — Pressure reading and running average

#include "pressure.h"
#include "config.h"
#include "globals.h"
#include "RunningAverage.h"

// The RunningAverage object lives here since this module "owns" 
// pressure averaging. No other file needs direct access to it.
// In Python terms, this is a module-private variable — like 
// prefixing with underscore: _ra_pressure
static RunningAverage raPressure(RA_FREQUENCY * RA_HIST_SECONDS);

// Called from setup() to initialize the running average buffer
void pressure_init()
{
    raPressure.clear();
}

int read_pressure_raw()
{
    int p = 0;
    for (uint8_t i = OVERSAMPLE; i; --i) {
        p += analogRead(BUTTPIN);
        if (i) delay(1);
    }
    return p / OVERSAMPLE;  // Average instead of sum
}

void update_pressure(int sampleTick)
{
    // Read fresh pressure value into the global
    pressure = read_pressure_raw();

    // Update running average at a slower rate than the main loop.
    // At 60Hz main loop and RA_TICK_PERIOD of 10, this runs at 6Hz.
    if (sampleTick % RA_TICK_PERIOD == 0) {
        raPressure.addValue(pressure);
        averagePressure = raPressure.getAverage();
    }
}
// sim_session.h — Simulated session data for demo mode
//
// Generates fake-but-convincing physiological data for driving all 
// the displays during demo/attract mode. Everything updates together 
// from a single tick function, keeping the simulation coherent across 
// displays.
//
// The simulation models an edging session:
//   - Arousal ramps up gradually with small random fluctuations
//   - When it hits a threshold, it "edges" — sharp drop, then restart
//   - Heart rate loosely tracks arousal (resting ~65, elevated ~95)
//   - Beat detection pulses true for one tick at the right BPM interval
//
// In Python terms, the module is like:
//
//   class SimSession:
//       arousal: float = 0
//       bpm: int = 65
//       beat_detected: bool = False
//
//       def tick(self):
//           self.arousal += ramp_speed + noise()
//           if self.arousal > threshold:
//               self.arousal = 0   # edge!
//           self.bpm = map(self.arousal, 0, max, 65, 95)
//           self.beat_detected = (tick_count % beats_interval == 0)
//
// Usage:
//   Call sim_tick() once per main loop tick (60Hz).
//   Read the sim_* values from wherever you need them (displays, LEDs, etc).

#pragma once

#include <Arduino.h>

// ── Simulated values ───────────────────────────────────────────────────
// These are updated every time sim_tick() is called. Read them freely 
// from any display module — they're stable between ticks.

extern int   sim_arousal;       // Pressure delta (0 to ~MAX_PRESSURE_LIMIT)
extern int   sim_bpm;           // Heart rate in beats per minute (55–100)
extern bool  sim_beat;          // True for exactly one tick when a "beat" occurs
extern float sim_motor_speed;   // Simulated motor output (0–255), tracks arousal cycle

// GSR output: normalised 0.0–1.0, combining tonic baseline and 
// phasic (event-triggered) components. Unlike arousal, GSR has 
// significant inertia — it rises slowly and falls even slower,
// accumulating over repeated edge cycles.
//
// Timescale comparison:
//   arousal:  seconds      (ramps in ~24s, drops instantly)
//   BPM:     sub-second   (tracks arousal within one beat)
//   GSR:     tens of secs  (tonic drifts over minutes, 
//                           phasic spikes decay over ~10s)
extern float sim_gsr;           // Combined output (0.0 – 1.0)
extern float sim_gsr_phasic;    // Just the spike component (for displays
                                // that want to show "event reactivity"
                                // separately from baseline)

// ── Simulated pressure system ──────────────────────────────────────────
// Instead of directly generating a "delta" value, we simulate what
// the pressure sensor actually reads, then derive the delta through
// the same RunningAverage logic the real device uses.
//
// This means sim_arousal (the delta) naturally exhibits all the 
// real system's behaviours:
//   - Running average lag (delta builds as RA can't keep up)
//   - Post-edge negative delta (RA still elevated after relaxation)
//   - Gradual RA catch-up during sustained contractions
//
// In Python terms, we used to generate:      delta = sawtooth()
// Now we generate:  pressure = baseline + contractions + noise
//                   average  = running_average(pressure)
//                   delta    = pressure - average         ← emergent!

extern int   sim_pressure;        // Raw simulated pressure (≈ADC reading)
extern int   sim_avg_pressure;    // Running average of sim_pressure
// sim_arousal is now derived:    sim_pressure - sim_avg_pressure

// ── Control ────────────────────────────────────────────────────────────

// Reset all simulation state. Call when entering demo mode so each 
// demo session starts fresh from a "resting" baseline.
void sim_reset();

// Advance the simulation by one tick. Call once per 60Hz loop 
// iteration when in demo mode.
void sim_tick();

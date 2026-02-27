// sim_session.h — Simulated session signal for demo mode
//
// Generates a fake but realistic-looking arousal delta signal that
// mimics the actual edging cycle: gradual ramp → peak → sharp drop
// → cooldown → repeat. Useful for testing displays and animations
// without needing the pressure sensor connected.
//
// The output is an integer in the same range as (pressure - averagePressure),
// so it can be fed directly into matrix_graph_tick() or any other
// visualisation that expects an arousal delta.
//
// In Python terms, this is like:
//
//   class ArousalSimulator:
//       def __init__(self):
//           self.level = 0.0
//           self.state = "ramping"
//
//       def tick(self) -> int:
//           if self.state == "ramping":
//               self.level += random_increment()
//               if self.level > threshold:
//                   self.state = "cooldown"
//           elif self.state == "cooldown":
//               self.level = 0
//               ...
//           return int(self.level)

#pragma once

#include <Arduino.h>

// Reset the simulator state. Call once from setup() or when entering demo.
void sim_arousal_init();

// Advance the simulation by one tick and return the current arousal delta.
// Call once per main loop tick (~60Hz). The signal evolves smoothly 
// between ticks — no need to call at a slower rate.
//
// maxDelta:  the ceiling value (like pressureLimit). The simulated 
//            signal will peak near this value before dropping.
//            Pass the same value to matrix_graph_tick() for proper scaling.
int sim_arousal_tick(int maxDelta);
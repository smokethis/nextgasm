// sim_arousal.cpp — Simulated arousal signal for demo mode
//
// ═══════════════════════════════════════════════════════════════════════
// WHY A STATE MACHINE AND NOT JUST A SINE WAVE?
// ═══════════════════════════════════════════════════════════════════════
//
// A sine wave looks obviously artificial — perfectly smooth and 
// symmetrical. Real arousal signals have very distinct phases:
//
//   1. RAMP:     Slow, wobbly climb. Not steady — there are moments 
//                where it plateaus or dips slightly before continuing.
//                Think of it like climbing a hill on a windy day.
//
//   2. PLATEAU:  Near the peak, the signal bounces around for a few 
//                seconds. This is the "approaching the edge" zone where
//                the real device would be about to cut the motor.
//
//   3. DROP:     Sharp, fast fall — like a cliff, not a slope. This 
//                mirrors the motor cutting off and arousal rapidly 
//                decreasing.
//
//   4. COOLDOWN: Stays near zero for a while before the next ramp 
//                begins. Duration varies randomly to keep it interesting.
//
// The state machine produces asymmetric, organic-looking waveforms 
// that exercise the graph display much more realistically than any 
// simple mathematical function would.
//
// ═══════════════════════════════════════════════════════════════════════
// THE NOISE APPROACH
// ═══════════════════════════════════════════════════════════════════════
//
// Rather than using Perlin noise (which would be overkill and heavy),
// we use "smoothed random walk" — the level changes by a small random 
// amount each tick, biased in the direction the current state wants to 
// go. The small step size naturally creates smooth curves without any 
// filtering. It's like a drunk person walking home — they wobble, but 
// the general trend is toward the destination.
//
// In Python terms:
//   step = random.uniform(-wobble, wobble) + bias
//   level = clamp(level + step, 0, max)

#include "sim_session.h"

// ── Simulation states ──────────────────────────────────────────────────
// Using an enum here rather than #defines (like the original protogasm 
// code) because:
//   1. The compiler enforces that you only use valid states
//   2. Debuggers show "SIM_RAMP" instead of "0" in watch windows
//   3. They're scoped — won't collide with MANUAL/AUTO/etc in config.h
//
// In Python you'd use an Enum class for the same reasons.
enum SimState : uint8_t {
    SIM_RAMP,       // Gradually increasing, with wobble
    SIM_PLATEAU,    // Bouncing near the peak
    SIM_DROP,       // Sharp fall
    SIM_COOLDOWN    // Resting near zero before next cycle
};

// ── Internal state ─────────────────────────────────────────────────────
// 'static' file-scope variables — only visible in this .cpp file.
// In Python terms: module-private (like _underscore convention).

static SimState simState = SIM_RAMP;
static float level = 0.0;           // Current simulated arousal (0.0 to ~maxDelta)
static unsigned long stateEnteredAt = 0;   // millis() when current state began
static unsigned long stateDuration = 0;    // How long to stay in current state (ms)

// ── Tuning constants ───────────────────────────────────────────────────
// These control the "feel" of the simulation. Tweak to taste.

// RAMP: how fast the level climbs per tick
// At 60Hz, 0.3/tick = ~18/sec. With maxDelta=600, a full ramp takes 
// ~33 seconds, but the wobble adds +/- a few seconds of variance.
constexpr float RAMP_BIAS = 0.8;
constexpr float RAMP_WOBBLE = 0.5;   // Random jitter per tick

// PLATEAU: bounces around near peak for this long
constexpr unsigned long PLATEAU_MIN_MS = 2000;
constexpr unsigned long PLATEAU_MAX_MS = 5000;
constexpr float PLATEAU_WOBBLE = 1.5; // Bigger wobble = more drama at the peak

// DROP: how fast it falls per tick (much faster than ramp = asymmetric)
constexpr float DROP_RATE = 8.0;

// COOLDOWN: rests near zero for this long
constexpr unsigned long COOLDOWN_MIN_MS = 2000;
constexpr unsigned long COOLDOWN_MAX_MS = 3000;
constexpr float COOLDOWN_WOBBLE = 0.15; // Tiny wobble even at rest

// What fraction of maxDelta triggers the transition to PLATEAU.
// 0.75 means we start plateauing at 75% of the way up.
constexpr float PEAK_THRESHOLD = 0.85;


// ── Helpers ────────────────────────────────────────────────────────────

// Random float in range [lo, hi]. Arduino's random() returns a long 
// in [0, max), so we scale it into a float range.
//
// This is equivalent to Python's random.uniform(lo, hi).
//
// The 1000-step granularity is plenty for our purposes — we're 
// generating wobble, not cryptographic randomness.
static float random_float(float lo, float hi)
{
    return lo + (float)random(1000) / 1000.0 * (hi - lo);
}

// Enter a new state and optionally set a random duration for it.
static void enter_state(SimState newState, unsigned long minMs = 0, unsigned long maxMs = 0)
{
    simState = newState;
    stateEnteredAt = millis();
    if (maxMs > 0) {
        // random(min, max) returns a long in [min, max)
        stateDuration = random(minMs, maxMs);
    } else {
        stateDuration = 0;  // Duration-less states (RAMP, DROP) exit by level, not time
    }
}


// ════════════════════════════════════════════════════════════════════════
// Public interface
// ════════════════════════════════════════════════════════════════════════

void sim_arousal_init()
{
    level = 0.0;
    // Seed the random number generator from an unconnected analog pin.
    // analogRead() on a floating pin returns electrical noise, which
    // makes a decent-enough random seed for our purposes. Without this,
    // random() produces the same sequence every boot — the demo would 
    // play the exact same "performance" each time.
    //
    // In Python terms: random.seed(os.urandom(...)) — but cruder.
    randomSeed(analogRead(A9));
    enter_state(SIM_RAMP);
}


int sim_arousal_tick(int maxDelta)
{
    float peak = (float)maxDelta * PEAK_THRESHOLD;
    unsigned long elapsed = millis() - stateEnteredAt;

    switch (simState)
    {
        // ── RAMP: biased random walk upward ────────────────────────
        // Each tick, add a small positive bias plus random wobble.
        // The bias ensures we trend upward; the wobble makes it 
        // organic. Occasionally the wobble wins and the level dips 
        // slightly, which looks natural.
        case SIM_RAMP:
        {
            float step = RAMP_BIAS + random_float(-RAMP_WOBBLE, RAMP_WOBBLE);
            level += step;

            // Clamp: don't go below zero during a dip
            if (level < 0) level = 0;

            // Transition: once we're near the peak, start plateauing
            if (level >= peak) {
                enter_state(SIM_PLATEAU, PLATEAU_MIN_MS, PLATEAU_MAX_MS);
            }
            break;
        }

        // ── PLATEAU: wobble near the peak ──────────────────────────
        // Level bounces around the peak zone for a random duration.
        // This is the tense moment before the "edge" — the graph 
        // should show the bars flickering near max height.
        case SIM_PLATEAU:
        {
            level += random_float(-PLATEAU_WOBBLE, PLATEAU_WOBBLE);

            // Keep it in the peak zone (don't wander too far)
            float ceiling = (float)maxDelta * 0.95;
            float floor = (float)maxDelta * 0.55;
            if (level > ceiling) level = ceiling;
            if (level < floor)   level = floor;

            // Transition: time's up, simulate the edge trigger
            if (elapsed >= stateDuration) {
                // Spike to near-max right before dropping — dramatic!
                level = (float)maxDelta * random_float(0.85, 0.95);
                enter_state(SIM_DROP);
            }
            break;
        }

        // ── DROP: rapid fall ───────────────────────────────────────
        // Decreases much faster than it ramped up, creating the 
        // characteristic asymmetric sawtooth shape. In the real 
        // device, this is the motor cutting off and arousal quickly 
        // subsiding.
        case SIM_DROP:
        {
            level -= DROP_RATE;

            // Transition: once we're near zero, enter cooldown
            if (level <= 0) {
                level = 0;
                enter_state(SIM_COOLDOWN, COOLDOWN_MIN_MS, COOLDOWN_MAX_MS);
            }
            break;
        }

        // ── COOLDOWN: rest near zero ───────────────────────────────
        // Tiny wobble to show the display is still alive, but 
        // essentially flat. In the real device, this is the motor-off 
        // period before ramping begins again.
        case SIM_COOLDOWN:
        {
            level += random_float(-COOLDOWN_WOBBLE, COOLDOWN_WOBBLE);
            if (level < 0) level = 0;

            // Transition: cooldown elapsed, start the next ramp
            if (elapsed >= stateDuration) {
                enter_state(SIM_RAMP);
            }
            break;
        }
    }

    return (int)level;
}
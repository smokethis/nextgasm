// sim_session.cpp — Simulated session data for demo mode
//
// Models a simplified edging session with these interacting systems:
//
// ┌─────────────────────────────────────────────────────────────┐
// │  AROUSAL CYCLE                                              │
// │                                                             │
// │  arousal  ╱╲      ╱╲        ╱╲                              │
// │          ╱  │    ╱  │      ╱  │     ← sawtooth with noise   │
// │         ╱   │   ╱   │     ╱   │                             │
// │  ──────╱    │──╱    │────╱    │──── time                    │
// │             ↓       ↓        ↓                              │
// │           edge    edge     edge     ← sharp drop            │
// │                                                             │
// │  motor   ╱╲      ╱╲        ╱╲      ← follows arousal       │
// │         ╱  ╲    ╱  ╲      ╱  ╲       but smoother ramp     │
// │  ──────╱    ╲──╱    ╲────╱    ╲────                         │
// │                                                             │
// │  BPM    65→90  65→88  65→92        ← tracks arousal level   │
// │                                                             │
// │  beats  ♡ ♡ ♡ ♡♡♡♡ ♡ ♡ ♡♡♡♡       ← interval from BPM     │
// │                                                             │
// │  GSR    ╱‾‾‾↑‾‾╲___╱‾‾‾↑‾‾‾╲____    ← ratcheting baseline    │
// │              edge       edge          with spikes at edges   │
// └─────────────────────────────────────────────────────────────┘
//
// The arousal value drives everything else. Heart rate is a simple 
// linear mapping from arousal level, and beat detection is derived 
// from the current BPM. This means all the displays stay coherent — 
// when arousal is high, the heart beats faster, the motor is near 
// max, and everything drops together when an "edge" is detected.
//
// RANDOMNESS:
// We use random() for jitter, which is fine for demo visuals.
// The seed comes from an unconnected analog pin read in sim_reset(),
// so each demo session looks different. In Python terms, this is 
// like calling random.seed(os.urandom(4)) — not cryptographic, 
// but plenty for visual variety.

#include "sim_session.h"
#include "config.h"

// ── Public state (defined here, declared extern in header) ─────────
int   sim_arousal     = 0;
int   sim_bpm         = 65;
bool  sim_beat        = false;
float sim_motor_speed = 0;

// ── Internal simulation state ──────────────────────────────────────
// 'static' = file-private. These persist between tick() calls but 
// aren't accessible from other modules.

static float arousalFloat    = 0.0;  // Smooth float for gradual ramping
static float motorFloat      = 0.0;  // Smooth motor ramp (independent of arousal drop)
static int   edgeThreshold   = 0;    // Arousal level that triggers an "edge"
static int   cooldownTicks   = 0;    // Ticks remaining in post-edge cooldown
static bool edgeJustFired = false; // Did we just reach an edge?
static int   ticksSinceLastBeat = 0; // For timing the heartbeat pulses

// ── Tuning constants ───────────────────────────────────────────────
// These control how the demo "feels". Tweak them if the pacing seems
// off when watching it on the actual hardware.

// Arousal ramp speed: how many units of arousal per tick.
// At 0.35/tick and threshold ~500, a full ramp takes ~24 seconds.
// That's close to the real 30-second ramp with some acceleration.
constexpr float AROUSAL_RAMP_BASE    = 0.35;

// Random jitter added to each tick's ramp (±this value).
// Makes the ramp look organic rather than perfectly linear.
constexpr float AROUSAL_NOISE_RANGE  = 0.15;

// After an edge, arousal drops to this fraction of its peak.
// Not quite zero — there's always some residual tension.
constexpr float POST_EDGE_FLOOR      = 0.05;

// Cooldown duration range (in ticks at 60Hz).
// Random between these values — simulates the variable off-time.
constexpr int COOLDOWN_MIN_TICKS     = 120;  // 2 seconds
constexpr int COOLDOWN_MAX_TICKS     = 360;  // 6 seconds

// Edge threshold range — varies per cycle for visual interest.
constexpr int THRESHOLD_MIN          = 400;
constexpr int THRESHOLD_MAX          = 580;

// Heart rate range — maps linearly from arousal level.
constexpr int BPM_RESTING            = 62;
constexpr int BPM_ELEVATED           = 97;

// Motor follows arousal but with its own ramp characteristics.
constexpr float MOTOR_RAMP_RATE      = 0.18;  // Slower than arousal ramp
constexpr float MOTOR_BACKOFF_RATE   = 0.6;   // Fast drop when ceiling falls

// ── Public GSR state ───────────────────────────────────────────────
float sim_gsr         = 0.0;
float sim_gsr_phasic  = 0.0;

// ── Internal GSR state ─────────────────────────────────────────────
static float gsrTonic   = 0.0;   // Slow-moving baseline
static float gsrPhasic  = 0.0;   // Fast-attack, slow-decay spike

// ── GSR tuning constants ───────────────────────────────────────────
//
// These are chosen to approximate real electrodermal timing.
// Real GSR research uses terms like "skin conductance level" (SCL)
// for tonic and "skin conductance response" (SCR) for phasic.

// How fast the tonic baseline tracks arousal.
// 0.001 at 60Hz ≈ 17-second effective window — very sluggish,
// like heating a cast-iron pan.
constexpr float GSR_TONIC_ALPHA     = 0.001;

// Tonic output range: even at rest there's some baseline
// conductance, and it never quite reaches 1.0 from tonic alone.
constexpr float GSR_TONIC_FLOOR     = 0.15;
constexpr float GSR_TONIC_CEILING   = 0.70;

// How much a single edge event kicks the phasic component.
// Think of this as the "startle response" magnitude.
constexpr float GSR_PHASIC_KICK     = 0.25;

// Phasic decay rate per tick. 0.993 at 60Hz gives a half-life
// of about 100 ticks ≈ 1.7 seconds. The tail lingers for ~10
// seconds before becoming negligible.
//
// Math: half-life = ln(0.5) / ln(decay) = -0.693 / ln(0.993)
//     = -0.693 / -0.00702 ≈ 99 ticks
//
// In Python: import math; math.log(0.5) / math.log(0.993) → ~98.7
constexpr float GSR_PHASIC_DECAY    = 0.993;

// Tiny noise amplitude for organic texture
constexpr float GSR_NOISE_RANGE     = 0.005;


// ── Helper: pick a new random edge threshold ───────────────────────
// Each cycle edges at a slightly different level, just like real 
// sessions where sensitivity shifts over time.
static void pick_new_threshold()
{
    edgeThreshold = random(THRESHOLD_MIN, THRESHOLD_MAX + 1);
}


// ════════════════════════════════════════════════════════════════════
// Reset
// ════════════════════════════════════════════════════════════════════

void sim_reset()
{
    // Seed the PRNG from an unconnected analog pin for variety.
    // analogRead on a floating pin picks up electrical noise, giving 
    // us a different-ish seed each time. Not great entropy, but fine 
    // for demo visuals.
    //
    // In Python: random.seed(int.from_bytes(os.urandom(2), 'big'))
    randomSeed(analogRead(A9));

    arousalFloat       = 0.0;
    motorFloat         = 0.0;
    sim_arousal        = 0;
    sim_bpm            = BPM_RESTING;
    sim_beat           = false;
    sim_motor_speed    = 0;
    cooldownTicks      = 0;
    ticksSinceLastBeat = 0;
    gsrTonic           = GSR_TONIC_FLOOR;  // Start at resting baseline, not zero
    gsrPhasic          = 0.0;
    sim_gsr            = GSR_TONIC_FLOOR;
    sim_gsr_phasic     = 0.0;

    pick_new_threshold();
}


// ════════════════════════════════════════════════════════════════════
// Tick — advance simulation by one 60Hz frame
// ════════════════════════════════════════════════════════════════════

void sim_tick()
{
    // ── 1. AROUSAL RAMP ────────────────────────────────────────────
    //
    // During cooldown, arousal stays near the floor (with tiny drift).
    // Otherwise, it ramps up with noise until hitting the threshold.

    if (cooldownTicks > 0)
    {
        // Post-edge cooldown: arousal drifts slowly near the floor.
        // The tiny random walk keeps the display alive rather than 
        // showing a static number.
        cooldownTicks--;
        arousalFloat += (random(-10, 11) / 100.0);  // ±0.1 drift
        arousalFloat = max(arousalFloat, 0.0f);

        // Motor stays off during cooldown
        motorFloat -= MOTOR_BACKOFF_RATE;
        if (motorFloat < 0) motorFloat = 0;
    }
    else
    {
        // Active ramping phase
        float noise = (random(-100, 101) / 100.0) * AROUSAL_NOISE_RANGE;
        arousalFloat += AROUSAL_RAMP_BASE + noise;

        // Motor ramps up toward a ceiling proportional to arousal
        float motorCeiling = (arousalFloat / edgeThreshold) * MOT_MAX;
        motorCeiling = constrain(motorCeiling, 0, (float)MOT_MAX);

        if (motorFloat < motorCeiling)
            motorFloat += MOTOR_RAMP_RATE;
        else if (motorFloat > motorCeiling)
            motorFloat -= MOTOR_BACKOFF_RATE;

        // ── EDGE DETECTION ─────────────────────────────────────────
        if (arousalFloat >= edgeThreshold)
        {
            // Edge hit! Sharp drop, enter cooldown.
            arousalFloat = edgeThreshold * POST_EDGE_FLOOR;
            motorFloat   = 0;
            cooldownTicks = random(COOLDOWN_MIN_TICKS, COOLDOWN_MAX_TICKS + 1);
            edgeJustFired = true;
            pick_new_threshold();  // Next cycle edges at a different level
        }
    }

    // Clamp and publish the integer version
    sim_arousal     = constrain((int)arousalFloat, 0, MAX_PRESSURE_LIMIT);
    sim_motor_speed = constrain(motorFloat, 0, (float)MOT_MAX);

    // ── 2. HEART RATE ──────────────────────────────────────────────
    //
    // Linear mapping: more aroused → faster heartbeat.
    // Plus a small random jitter so it doesn't look robotic.
    //
    // In Python terms:
    //   base = lerp(BPM_RESTING, BPM_ELEVATED, arousal / threshold)
    //   bpm = base + random.randint(-1, 1)

    float arousalFraction = constrain(arousalFloat / THRESHOLD_MAX, 0.0f, 1.0f);
    int baseBpm = BPM_RESTING + (int)(arousalFraction * (BPM_ELEVATED - BPM_RESTING));
    sim_bpm = constrain(baseBpm + random(-1, 2), BPM_RESTING - 3, BPM_ELEVATED + 3);

    // ── 3. BEAT DETECTION ──────────────────────────────────────────
    //
    // Calculate how many ticks between beats at the current BPM, 
    // then pulse sim_beat for exactly one tick when that interval 
    // elapses.
    //
    // At 70 BPM:  60/70 = 0.857 seconds × 60Hz = ~51 ticks/beat
    // At 95 BPM:  60/95 = 0.632 seconds × 60Hz = ~38 ticks/beat
    //
    // We use integer division which gives us slight tempo variation 
    // for free — the rounding error means beats aren't perfectly 
    // metronomic, which actually looks more natural.

    ticksSinceLastBeat++;
    int ticksPerBeat = (60 * FREQUENCY) / sim_bpm;  // e.g. 3600 / 72 = 50

    if (ticksSinceLastBeat >= ticksPerBeat)
    {
        sim_beat = true;
        ticksSinceLastBeat = 0;
    }
    else
    {
        sim_beat = false;
    }

    // ── 4. GSR (GALVANIC SKIN RESPONSE) ────────────────────────────
    //
    // Two-layer model: slow tonic baseline + fast phasic spikes.
    //
    // Unlike arousal and BPM which drop at the edge, GSR *spikes*
    // — the sympathetic nervous system fires harder at the moment
    // of the edge (it's a stress/surprise event). Then it decays
    // very slowly. After several cycles, the tonic level has 
    // ratcheted up even though arousal resets each time.
    //
    // This gives you a signal that encodes "session depth" — 
    // something no other simulated channel captures.

    // Tonic: sluggishly track a target derived from arousal level.
    float tonicTarget = GSR_TONIC_FLOOR 
                      + arousalFraction * (GSR_TONIC_CEILING - GSR_TONIC_FLOOR);
    gsrTonic += GSR_TONIC_ALPHA * (tonicTarget - gsrTonic);

    // Phasic: spike on edge, then decay exponentially.
    // 'edgeJustFired' is set to true in the edge detection block above,
    // then consumed here and reset.
    if (edgeJustFired) {
        gsrPhasic += GSR_PHASIC_KICK;
        edgeJustFired = false;  // Consume the event
    }
    gsrPhasic *= GSR_PHASIC_DECAY;

    // Combine with noise and clamp
    float gsrNoise = (random(-100, 101) / 100.0f) * GSR_NOISE_RANGE;
    float gsrRaw = gsrTonic + gsrPhasic + gsrNoise;

    // Publish
    sim_gsr        = constrain(gsrRaw, 0.0f, 1.0f);
    sim_gsr_phasic = constrain(gsrPhasic, 0.0f, 1.0f);
}
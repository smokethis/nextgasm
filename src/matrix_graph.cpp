// matrix_graph.cpp — Scrolling arousal history graph implementation
//
// ═══════════════════════════════════════════════════════════════════════
// HOW THE DIMMING WORKS ON A 1-BIT DISPLAY
// ═══════════════════════════════════════════════════════════════════════
//
// The HT1632C can only turn each LED fully on or fully off — there's 
// a global brightness (PWM duty cycle), but no per-pixel control. So
// how do we make older columns look dimmer?
//
// The answer is SPATIAL DITHERING: we selectively turn off some pixels
// in a pattern. Fewer lit pixels = the column looks dimmer to the eye.
// It's the same principle as newspaper halftone printing — vary the 
// density of dots to create the illusion of shading.
//
// We define "dim masks" — 8-bit patterns that determine which rows 
// are allowed to be lit. A mask of 0xFF allows all rows; 0xAA 
// (10101010) allows every other row; 0x88 (10001000) allows only 
// every 4th row. By AND-ing the bar data with the mask, we thin out
// the bar to create the dimming effect.
//
// To avoid the dithering looking like obvious horizontal stripes,
// we alternate the pattern on even/odd columns. So column 10 might 
// use 0xAA (rows 1,3,5,7) while column 11 uses 0x55 (rows 0,2,4,6).
// This creates a checkerboard texture that reads as smooth shading 
// rather than stripy aliasing.
//
// In Python terms:
//   mask = 0xAA if col % 2 == 0 else 0x55
//   dimmed_bar = bar_bits & mask
//
// ═══════════════════════════════════════════════════════════════════════
// COLUMN BYTE LAYOUT
// ═══════════════════════════════════════════════════════════════════════
//
// Each column in the HT1632C framebuffer is one byte:
//   bit 0 = row 0 (TOP of display)
//   bit 7 = row 7 (BOTTOM of display)
//
// Our bars grow upward from the bottom, so a bar of height 3 
// lights rows 5, 6, 7 → bits 5, 6, 7 → binary 11100000 = 0xE0.
//
// The peak pixel is the topmost lit row of the bar — the bit with 
// the lowest index in our lit range. We always keep this bit on
// regardless of the dim mask, so the peak trace remains crisp even 
// as the body fades.
//
// Visual example — height 5 bar at different ages:
//
//   Age 0 (newest)   Age 10 (mid)    Age 20 (oldest)
//   ················  ················  ················
//   ················  ················  ················
//   ················  ················  ················
//   ████ peak        ████ peak        ████ peak
//   ████             ····             ····
//   ████             ████             ····
//   ████             ····             ····
//   ████             ████             ████
//
// The peak pixel stays bright across all ages, while the body 
// below it thins out progressively. Visually, this creates a 
// clear "arousal trace" line at the peaks with energy/intensity 
// fading beneath it.

#include "matrix_graph.h"
#include "HT1632C_Display.h"

// ── Configuration ──────────────────────────────────────────────────────

// How often to shift the graph left and sample a new value.
// 500ms = 2 samples/sec, giving 12 seconds of visible history 
// across the 24 columns. Adjust to taste.
constexpr unsigned long SHIFT_INTERVAL_MS = 90;

// Display dimensions (pulled from the driver's constants)
constexpr uint8_t COLS = HT1632C_WIDTH;   // 24
constexpr uint8_t ROWS = HT1632C_HEIGHT;  // 8

// ── Dim mask age thresholds ────────────────────────────────────────────
// The 24 columns are divided into 4 zones of 6 columns each.
// Each zone gets progressively sparser dithering.
constexpr uint8_t DIM_ZONE_SIZE = COLS / 4;  // 6 columns per zone

// ── Internal state ─────────────────────────────────────────────────────
// history[] stores one bar height (0–ROWS) per column. Index 0 is 
// the leftmost (oldest) column, index 23 is rightmost (newest).
//
// In Python terms: it's a list of 24 ints that we treat like a deque,
// shifting left and appending on the right.

static uint8_t history[COLS];
static unsigned long lastShiftTime = 0;


// ════════════════════════════════════════════════════════════════════════
// Internal helpers
// ════════════════════════════════════════════════════════════════════════

// Map a raw arousal delta to a bar height (0 to ROWS).
static uint8_t delta_to_height(int delta, int maxDelta)
{
    if (delta <= 0 || maxDelta <= 0) return 0;

    float normalised = (float)delta / (float)maxDelta;
    if (normalised > 1.0f) normalised = 1.0f;

    // Square root curve — expands low values, compresses highs.
    // The display fills gradually as arousal builds, rather than 
    // being invisible for most of the ramp then exploding at the end.
    //
    // Comparison at pressureLimit = 250:
    //
    //   delta    squared    sqrt     linear
    //   ─────    ───────    ────     ──────
    //     50        0        3        2     ← sqrt shows early activity
    //    100        1        5        3
    //    150        3        6        5
    //    200        5        7        6
    //    250        8        8        8
    //
    // sqrtf() is a single hardware instruction on the Teensy 4.0's 
    // FPU — essentially free. In Python: math.sqrt(normalised)
    float curved = sqrtf(normalised);

    int h = (int)(curved * ROWS + 0.5f);
    if (h > ROWS) h = ROWS;
    return (uint8_t)h;
}


// Get the spatial dithering mask for a column based on its age.
//
// Age 0 = newest (rightmost column), age 23 = oldest (leftmost).
// Each mask is 8 bits — one per row. A 1 means "allowed to be lit",
// a 0 means "forced off." Even/odd columns use complementary patterns
// to create a checkerboard rather than horizontal stripes.
//
// The progression:
//   Zone 0 (age  0–5):  0xFF       = all rows on    (100% density)
//   Zone 1 (age  6–11): 0xEE/0xDD  = 6 of 8 rows    (~75% density)
//   Zone 2 (age 12–17): 0xAA/0x55  = 4 of 8 rows    ( 50% density)
//   Zone 3 (age 18–23): 0x88/0x22  = 2 of 8 rows    ( 25% density)
//
// Why these specific hex values? They're bit patterns chosen for 
// even visual spacing:
//   0xAA = 10101010  (every other bit)
//   0x55 = 01010101  (complement — shifted by 1)
//   0xEE = 11101110  (3 on, 1 off, repeating)
//   0xDD = 11011101  (complement of 0xEE)
//   0x88 = 10001000  (1 on, 3 off)
//   0x22 = 00100010  (complement of 0x88)

static uint8_t get_dim_mask(uint8_t age, uint8_t col)
{
    bool even = (col % 2 == 0);

    if (age < 10)  return 0xFF;                      // Full — newest half
    if (age < 14)  return even ? 0xEE : 0xDD;        // 75%
    if (age < 18)  return even ? 0xAA : 0x55;        // 50%
    return even ? 0x88 : 0x22;                        // 25% — oldest 4 columns
}


// Build the final byte for one column of the display.
//
// Three layers are combined:
//   1. BAR:  a solid block of bits from bottom up to `height`
//   2. PEAK: the single topmost bit of the bar — always kept solid
//   3. DIM:  the density mask applied to the body (bar minus peak)
//
// Result = (body & dimMask) | peakBit
//
// This ensures the peak pixel is always visible even when the body 
// has been heavily dithered, creating a clear "trace line" across 
// the display at the arousal level.

static uint8_t build_column(uint8_t height, uint8_t age, uint8_t col)
{
    if (height == 0) return 0x00;

    // ── Bar: fill bottom `height` rows ─────────────────────────────
    // We want bits (8-height) through 7 set. The formula:
    //   ~((1 << (8 - height)) - 1)
    //
    // Walk through it for height=3:
    //   (1 << 5)  = 0b00100000 = 32
    //   32 - 1    = 0b00011111 = 31     (mask of rows ABOVE the bar)
    //   ~31       = 0b11100000 = 0xE0   (rows 5,6,7 = bottom 3)  ✓
    //
    // For height=8:
    //   (1 << 0)  = 1
    //   1 - 1     = 0
    //   ~0        = 0xFF                (all rows)  ✓
    //
    // For height=1:
    //   (1 << 7)  = 128
    //   128 - 1   = 127 = 0x7F
    //   ~0x7F     = 0x80                (just bottom row)  ✓
    uint8_t bar = ~((1 << (8 - height)) - 1);

    // ── Peak: topmost lit pixel ────────────────────────────────────
    // This is the bit at position (8 - height).
    //   height=3 → bit 5 → 0x20
    //   height=8 → bit 0 → 0x01  (very top of display)
    //   height=1 → bit 7 → 0x80  (peak IS the only pixel)
    uint8_t peakBit = 1 << (8 - height);

    // ── Combine: dimmed body + solid peak ──────────────────────────
    uint8_t body = bar & ~peakBit;  // Bar minus the peak pixel
    uint8_t dimMask = get_dim_mask(age, col);
    return (body & dimMask) | peakBit;
}


// ════════════════════════════════════════════════════════════════════════
// Public interface
// ════════════════════════════════════════════════════════════════════════

void matrix_graph_init()
{
    memset(history, 0, sizeof(history));
    lastShiftTime = millis();
    // Can't reset the static from here directly, but we could 
    // add a flag — or just let it settle naturally in ~400ms.
    // For a clean reset, make smoothedDelta file-scope static instead.
}

void matrix_graph_tick(int arousalDelta, int maxDelta, HT1632C_Display& display)
{
    unsigned long now = millis();

    // ── Smooth the input with an EMA ───────────────────────────────
    // The raw delta is noisy tick-to-tick (muscle contractions are 
    // jittery, plus the sim adds random noise). Since we only sample 
    // into the history buffer every SHIFT_INTERVAL_MS (~90ms), the 
    // unsmoothed value at sample time is essentially random — it 
    // might catch a spike or a dip, giving a jagged display.
    //
    // The EMA continuously tracks the delta across ALL ticks, so 
    // when we do sample, we get the recent trend rather than an 
    // arbitrary instant. Same technique as the BPM smoothing on 
    // the alphanumeric display.
    //
    // Alpha of 0.08 at 60Hz ≈ 2/(24+1) ≈ 24-sample window ≈ 400ms.
    // That's long enough to iron out tick-to-tick jitter but short 
    // enough to track genuine changes in the delta over seconds.
    //
    // The alpha value (0.08) is the main tuning knob. If the display still looks a bit twitchy, drop it to 0.05 for a ~600ms window. If it feels too sluggish and doesn't react quickly enough to rising arousal, push it up to 0.12. The sweet spot is where you can see the bars building smoothly during a ramp but still see the drop after an edge within a second or two.
    //
    // 'static' so it persists between calls, like a class attribute.
    static float smoothedDelta = 0.0f;
    constexpr float GRAPH_ALPHA = 0.08f;

    // Clamp negative deltas to zero before smoothing — we don't 
    // want post-edge negative deltas dragging the average down 
    // and creating a sluggish recovery on the display.
    float clampedDelta = (arousalDelta > 0) ? (float)arousalDelta : 0.0f;
    smoothedDelta = GRAPH_ALPHA * clampedDelta + (1.0f - GRAPH_ALPHA) * smoothedDelta;

    // ── Shift & sample at the configured interval ──────────────────
    if (now - lastShiftTime >= SHIFT_INTERVAL_MS)
    {
        lastShiftTime = now;
        memmove(&history[0], &history[1], COLS - 1);

        // Sample the SMOOTHED value, not the raw one
        history[COLS - 1] = delta_to_height((int)(smoothedDelta + 0.5f), maxDelta);
    }

    // ── Render all columns ─────────────────────────────────────────
    // We rebuild the entire framebuffer every frame. This is fast — 
    // 24 iterations of simple bitwise math, trivial for the Teensy's 
    // 600MHz ARM core. The flush() at the end pushes all 48 bytes 
    // to the display over bit-banged serial.
    display.clear();
    for (uint8_t col = 0; col < COLS; col++)
    {
        // Age: rightmost column (23) is newest (age 0),
        // leftmost column (0) is oldest (age 23).
        uint8_t age = (COLS - 1) - col;

        display.setColumn(col, build_column(history[col], age, col));
    }
    display.flush();
}
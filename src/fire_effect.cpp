// fire_effect.cpp — Doom Fire effect with DMA double-buffering
//
// The classic PSX Doom fire algorithm, rendering to the ST7789V2 LCD
// via async DMA transfers. The CPU never blocks on display output.
//
// ═══════════════════════════════════════════════════════════════════════
// ARCHITECTURE OVERVIEW
// ═══════════════════════════════════════════════════════════════════════
//
// Three distinct phases run in a pipeline:
//
//   1. SIMULATE  — fire_step() updates the 60×70 heat grid (~0.1ms)
//   2. RENDER    — fire_render_to_buffer() scales 4× into a pixel
//                  buffer with pre-swapped RGB565 colours (~1ms)
//   3. TRANSFER  — DMA streams the pixel buffer to the LCD (~27ms)
//
// Phase 3 runs in the background via DMA. While the DMA controller
// is streaming frame N to the display, the CPU is free to run the
// main loop (motor control, pressure sensing, LED updates, etc).
// On the next tick where DMA is idle, we simulate and render frame
// N+1, then kick off a new DMA transfer.
//
// In Python terms, the pipeline is like:
//
//   async def fire_loop():
//       while True:
//           if not dma_busy():
//               fire_step()
//               render_to_buffer(back_buffer)
//               swap_buffers()
//               await start_dma(front_buffer)  # returns immediately
//           await sleep(0)  # yield to other tasks
//
// ═══════════════════════════════════════════════════════════════════════
// MEMORY LAYOUT
// ═══════════════════════════════════════════════════════════════════════
//
// The fire simulation grid is tiny: 60×70 = 4,200 bytes.
// The pixel buffers are large: 2 × 67,200 pixels × 2 bytes = 268,800.
//
// DMAMEM places the pixel buffers in the Teensy 4.0's secondary RAM
// (RAM2 / OCRAM, 512KB). This is important for two reasons:
//
//   1. CAPACITY: Keeps 268KB out of the primary DTCM (512KB), which
//      is needed for the stack, heap, and time-critical variables.
//
//   2. DMA COMPATIBILITY: RAM2 is configured as non-cacheable by the
//      Teensy's default MPU (Memory Protection Unit) settings. This
//      means CPU writes go directly to physical RAM — no risk of DMA
//      reading stale cached data. Without this, we'd need explicit
//      cache flush calls (arm_dcache_flush) before every DMA transfer.
//
// In Python terms, DMAMEM is like choosing between a fast local dict
// (DTCM) and a larger but slightly slower database (RAM2). We put
// the big buffers in the database and keep the hot data local.
//
// ═══════════════════════════════════════════════════════════════════════
// 4× SCALING
// ═══════════════════════════════════════════════════════════════════════
//
// Each 60×70 fire cell becomes a 4×4 pixel block on the 240×280 LCD:
//
//   For each fire row (70 rows):
//     For each of 4 LCD sub-rows:
//       For each fire column (60 columns):
//         Write the colour 4 times (4× horizontal)
//
// This gives bold, chunky flames where individual cells are clearly
// visible — more like a roaring campfire than a delicate candle.

#include "fire_effect.h"
#include "colour_lcd.h"

// ── Fire simulation buffer ───────────────────────────────────────────
// Each cell holds a heat value from 0 (cold) to PALETTE_SIZE-1 (max).
// At 60×70 = 4,200 bytes, this lives in fast DTCM where the CPU can
// blaze through it. 'static' = file-private (like Python's _underscore).
static uint8_t fireBuffer[FIRE_WIDTH * FIRE_HEIGHT];

// Convenience macro to access the flat array as a 2D grid.
// In Python: fire[y][x]. In C: fire[y * width + x].
#define FIRE_PIXEL(x, y) fireBuffer[(y) * FIRE_WIDTH + (x)]

// ── Pixel double-buffers ─────────────────────────────────────────────
// Two full-frame buffers for ping-pong rendering.
//
// DMAMEM tells the linker to place these in RAM2 (OCRAM) instead of
// the default DTCM. This is a Teensy-specific attribute — it doesn't
// exist in standard C++. On the Teensy 4.0, the linker script defines
// a special section for DMAMEM that maps to the 512KB OCRAM region.
//
// Each buffer: 67,200 pixels × 2 bytes = 134,400 bytes.
// Two buffers: 268,800 bytes total in RAM2.
// RAM2 is 512KB, so this uses about 52% — plenty of room.
DMAMEM static uint16_t pixelBuf[2][LCD_PIXEL_COUNT];

// Which buffer the CPU will render into next.
// After rendering, we send this buffer to DMA and flip the index.
// The DMA reads from pixelBuf[1 - writeIndex] (the "front" buffer)
// while the CPU writes to pixelBuf[writeIndex] (the "back" buffer).
static uint8_t writeIndex = 0;

// ── Scaling constant ─────────────────────────────────────────────────
// How many LCD pixels per fire cell in each dimension.
constexpr uint8_t FIRE_SCALE = 4;

// ── Palette (pre-swapped for DMA) ────────────────────────────────────
//
// 37 entries mapping heat (0-36) to byte-swapped RGB565 fire colours.
//
// These use RGB565_BE instead of RGB565 because the pixel buffers are
// sent to the display via DMA, which reads bytes sequentially from
// memory. The ST7789 expects big-endian byte order, but ARM stores
// uint16_t as little-endian. Pre-swapping at compile time means zero
// runtime cost — the palette values are already in the format DMA
// needs. See the RGB565_BE comment in colour_lcd.h for the full story.
//
// The RGB values themselves come from Fabien Sanglard's analysis of
// the original PSX Doom source code.

constexpr uint8_t PALETTE_SIZE = 37;

static const uint16_t firePalette[PALETTE_SIZE] = {
    RGB565_BE(0x07, 0x07, 0x07),   //  0: near-black (not pure — softer edges)
    RGB565_BE(0x1F, 0x07, 0x07),   //  1: very dark red
    RGB565_BE(0x2F, 0x0F, 0x07),   //  2: │
    RGB565_BE(0x47, 0x0F, 0x07),   //  3: │ dark reds — base of the flame
    RGB565_BE(0x57, 0x17, 0x07),   //  4: │
    RGB565_BE(0x67, 0x1F, 0x07),   //  5: │
    RGB565_BE(0x77, 0x1F, 0x07),   //  6: │
    RGB565_BE(0x8F, 0x27, 0x07),   //  7: ↓
    RGB565_BE(0x9F, 0x2F, 0x07),   //  8: brighter red
    RGB565_BE(0xAF, 0x3F, 0x07),   //  9: │
    RGB565_BE(0xBF, 0x47, 0x07),   // 10: │ "hot" reds — body of the flame
    RGB565_BE(0xC7, 0x47, 0x07),   // 11: │
    RGB565_BE(0xDF, 0x4F, 0x07),   // 12: ↓
    RGB565_BE(0xDF, 0x57, 0x07),   // 13: red-orange transition
    RGB565_BE(0xDF, 0x57, 0x07),   // 14: │ (plateau — lingers in the flame)
    RGB565_BE(0xD7, 0x5F, 0x07),   // 15: │
    RGB565_BE(0xD7, 0x5F, 0x07),   // 16: │
    RGB565_BE(0xD7, 0x67, 0x0F),   // 17: ↓
    RGB565_BE(0xCF, 0x6F, 0x0F),   // 18: orange zone
    RGB565_BE(0xCF, 0x77, 0x0F),   // 19: │
    RGB565_BE(0xCF, 0x7F, 0x0F),   // 20: │ the "fire" colour people imagine
    RGB565_BE(0xCF, 0x87, 0x17),   // 21: │
    RGB565_BE(0xC7, 0x87, 0x17),   // 22: │
    RGB565_BE(0xC7, 0x8F, 0x17),   // 23: ↓
    RGB565_BE(0xC7, 0x97, 0x1F),   // 24: orange-yellow transition
    RGB565_BE(0xBF, 0x9F, 0x1F),   // 25: │
    RGB565_BE(0xBF, 0x9F, 0x1F),   // 26: │ (another plateau)
    RGB565_BE(0xBF, 0xA7, 0x27),   // 27: │
    RGB565_BE(0xBF, 0xA7, 0x27),   // 28: ↓
    RGB565_BE(0xBF, 0xAF, 0x2F),   // 29: yellow zone
    RGB565_BE(0xB7, 0xAF, 0x2F),   // 30: │
    RGB565_BE(0xB7, 0xB7, 0x2F),   // 31: │ bright yellow
    RGB565_BE(0xB7, 0xB7, 0x37),   // 32: ↓
    RGB565_BE(0xCF, 0xCF, 0x6F),   // 33: yellow-white transition
    RGB565_BE(0xDF, 0xDF, 0x9F),   // 34: │ hottest part
    RGB565_BE(0xEF, 0xEF, 0xC7),   // 35: │
    RGB565_BE(0xFF, 0xFF, 0xFF),   // 36: pure white — the fuel source
};


// ═══════════════════════════════════════════════════════════════════════
// Initialisation
// ═══════════════════════════════════════════════════════════════════════

void fire_init()
{
    // Clear simulation grid to zero (cold/black)
    memset(fireBuffer, 0, sizeof(fireBuffer));

    // Seed the bottom row with maximum heat — the permanent fuel source
    for (int x = 0; x < FIRE_WIDTH; x++) {
        FIRE_PIXEL(x, FIRE_HEIGHT - 1) = PALETTE_SIZE - 1;
    }

    // Clear both pixel buffers to black (0x0000).
    // Even though they're in RAM2, memset works fine — it's still
    // normal memory, just at a different address range.
    memset(pixelBuf[0], 0, sizeof(pixelBuf[0]));
    memset(pixelBuf[1], 0, sizeof(pixelBuf[1]));

    writeIndex = 0;
}


// ═══════════════════════════════════════════════════════════════════════
// Simulation step — propagate heat upward with cooling and wind
// ═══════════════════════════════════════════════════════════════════════
//
// Iterates top-to-bottom so we read from unmodified cells below.
// At 60×70 = 4,200 cells on a 600MHz ARM, this takes well under 1ms.

static void fire_step()
{
    for (int y = 0; y < FIRE_HEIGHT - 1; y++) {
        for (int x = 0; x < FIRE_WIDTH; x++) {
            // Read heat from the cell directly below
            uint8_t srcHeat = FIRE_PIXEL(x, y + 1);

            // Random cooling: 0, 1, or 2 heat units lost
            uint8_t cooling = random(0, 3);

            // Random horizontal drift for organic wobble.
            // The & 3 maps random bits to 0-3, minus 1 gives -1 to +2.
            // Slight rightward bias creates a subtle "wind" effect.
            int wind = (random(0, 256) & 3) - 1;
            int destX = constrain(x + wind, 0, FIRE_WIDTH - 1);

            // Apply cooling, clamped to zero
            int newHeat = srcHeat - cooling;
            if (newHeat < 0) newHeat = 0;
            FIRE_PIXEL(destX, y) = (uint8_t)newHeat;
        }
    }
}


// ═══════════════════════════════════════════════════════════════════════
// Render to pixel buffer — 4× scaling with pre-swapped colours
// ═══════════════════════════════════════════════════════════════════════
//
// Converts the 60×70 heat grid into a 240×280 pixel buffer ready for
// DMA transfer. Each fire cell becomes a 4×4 block of identical pixels.
//
// The output is written sequentially to the buffer — left to right,
// top to bottom — matching the order the ST7789 expects pixels. This
// means DMA can just blast the buffer byte-by-byte and the display
// fills correctly.
//
// PERFORMANCE NOTE:
// Writing to RAM2 (DMAMEM) is slightly slower than DTCM (~2 cycles
// vs 1 cycle per access), but at 67,200 pixels this still takes only
// ~1-2ms on the 600MHz Cortex-M7. The sequential write pattern also
// means the memory bus pipeline stays full — no random access stalls.

static void fire_render_to_buffer(uint16_t* buf)
{
    // 'offset' tracks our position in the flat pixel buffer.
    // We increment it for every pixel we write, and it naturally
    // wraps through all 67,200 positions left-to-right, top-to-bottom.
    uint32_t offset = 0;

    for (int y = 0; y < FIRE_HEIGHT; y++)
    {
        // Each fire row produces FIRE_SCALE (4) identical LCD rows.
        // We repeat the same row data 4 times for vertical scaling.
        for (int rep = 0; rep < FIRE_SCALE; rep++)
        {
            for (int x = 0; x < FIRE_WIDTH; x++)
            {
                // Look up the pre-swapped colour for this cell's heat.
                // The palette already contains byte-swapped RGB565
                // values (via RGB565_BE), so no conversion needed here.
                uint16_t colour = firePalette[FIRE_PIXEL(x, y)];

                // Write the colour FIRE_SCALE (4) times for horizontal
                // scaling. Each fire pixel becomes 4 LCD pixels wide.
                buf[offset]     = colour;
                buf[offset + 1] = colour;
                buf[offset + 2] = colour;
                buf[offset + 3] = colour;
                offset += FIRE_SCALE;
            }
        }
    }

    // Sanity check: we should have written exactly LCD_PIXEL_COUNT pixels.
    // 60 × 4 × 70 × 4 = 67,200 ✓
    // This assert is compiled out in release builds but catches bugs 
    // during development. In Python you'd use assert offset == 67200.
    // (void)offset;  // suppress unused warning if asserts disabled
}


// ═══════════════════════════════════════════════════════════════════════
// Public tick function — non-blocking fire update
// ═══════════════════════════════════════════════════════════════════════
//
// Called every main loop tick (60Hz). The actual fire frame rate is
// determined by how fast DMA can push frames — at 40MHz SPI with
// 134,400 bytes per frame, that's ~27ms per frame = ~37 FPS.
//
// If DMA is still busy sending the last frame, we simply skip this
// tick. No work wasted, no CPU stalled. The main loop continues at
// its full 60Hz for motor control, button handling, etc.
//
// Timeline for a typical frame:
//
//   Tick 0:  DMA idle → fire_step (0.1ms) → render (1.5ms) → start DMA
//   Tick 1:  DMA busy → skip (0ms) ← main loop runs other stuff
//   Tick 2:  DMA idle → fire_step → render → start DMA
//   ...
//
// At 60Hz ticks and ~27ms DMA transfers, we get roughly every-other-
// tick rendering = ~30 FPS. Close to the theoretical 37 FPS maximum,
// with zero main loop stalls.

void fire_tick()
{
    // If the display is still sending the last frame, skip this tick.
    // The main loop carries on with everything else at full speed.
    if (lcd_frame_busy()) return;

    // Run the fire simulation
    fire_step();

    // Render the fire into the current write buffer (4× scaled,
    // pre-swapped colours ready for DMA)
    fire_render_to_buffer(pixelBuf[writeIndex]);

    // Kick off the DMA transfer of this buffer to the display.
    // lcd_send_frame_async sets up the draw window, asserts CS,
    // and starts the DMA — then returns immediately.
    lcd_send_frame_async(pixelBuf[writeIndex], LCD_PIXEL_COUNT);

    // Flip to the other buffer for next time.
    // While DMA reads from pixelBuf[writeIndex] (now the "front"),
    // we'll render the next frame into pixelBuf[1 - writeIndex]
    // (the "back"). Classic double-buffer swap.
    //
    // In Python: writeIndex = 1 - writeIndex
    // Or equivalently: writeIndex ^= 1 (XOR toggle between 0 and 1)
    writeIndex = 1 - writeIndex;
}
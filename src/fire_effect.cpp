// fire_effect.cpp — Doom Fire effect implementation
//
// The classic PSX Doom fire algorithm, rendering to the ST7789V2 LCD.
//
// ═══════════════════════════════════════════════════════════════════════
// MEMORY LAYOUT
// ═══════════════════════════════════════════════════════════════════════
//
// The fire buffer is a flat 1D array used as a 2D grid. We access it
// with FIRE_PIXEL(x, y) which does the row-major index math.
//
// In Python you'd just use fire[y][x] with a list of lists. In C++ a
// flat array is faster because all the data sits in one contiguous 
// block of memory — the CPU cache loves this. A 2D array (array of 
// pointers to arrays) scatters data around memory, causing cache misses.
//
// At 120×140 = 16,800 bytes, this fits comfortably in the Teensy 4.0's
// 1MB of RAM. For comparison, the OLED display buffer is ~1KB.
//
// ═══════════════════════════════════════════════════════════════════════
// PALETTE
// ═══════════════════════════════════════════════════════════════════════
//
// The 37-entry palette is the soul of the effect. It maps heat values
// (0-36) to RGB565 colours following the classic Doom fire gradient:
//
//   Black → Dark Red → Red → Orange → Yellow → White
//
// These specific RGB values come from Fabien Sanglard's analysis of 
// the original PSX Doom source code. They're converted to RGB565 at 
// compile time using the RGB565() macro, so there's zero runtime cost.
//
// ═══════════════════════════════════════════════════════════════════════
// SCALING
// ═══════════════════════════════════════════════════════════════════════
//
// The simulation runs at 120×140 but the LCD is 240×280. During the 
// LCD push, each fire pixel is written as a 2×2 block:
//
//   For each row in the fire buffer:
//     Write the row twice (2× vertical scaling)
//     Within each row, write each pixel twice (2× horizontal scaling)
//
// This gives us 120×2 = 240 horizontal pixels and 140×2 = 280 vertical
// pixels — exactly filling the screen. The 2× scaling also gives the 
// fire a nice chunky, retro aesthetic.

#include "fire_effect.h"
#include "colour_lcd.h"

// ── Fire buffer ────────────────────────────────────────────────────────
// Each cell holds a heat value from 0 (cold) to PALETTE_SIZE-1 (max).
// 'static' means this array is only visible within this file — like a 
// module-private variable in Python (prefixed with underscore).
static uint8_t fireBuffer[FIRE_WIDTH * FIRE_HEIGHT];

// Convenience macro to access the buffer as a 2D grid.
// In Python: fire[y][x]. In C with a flat array, we calculate the 
// offset manually: row * width + column.
#define FIRE_PIXEL(x, y) fireBuffer[(y) * FIRE_WIDTH + (x)]

// ── Palette ────────────────────────────────────────────────────────────
// 37 entries mapping heat (0-36) to RGB565 fire colours.
//
// The RGB565() macro converts 8-bit RGB to 16-bit packed format at 
// compile time. The actual hex values are from the original Doom PSX
// fire palette — each one was chosen to create a smooth gradient from 
// black through the warm spectrum to white.
//
// Index 0 is NOT pure black — it's (0x07, 0x07, 0x07), a very dark 
// grey. This was deliberate in the original: pure black pixels create 
// hard edges that look unnatural. The slight grey gives the fire's 
// boundary a softer, smokier appearance.

constexpr uint8_t PALETTE_SIZE = 37;

static const uint16_t firePalette[PALETTE_SIZE] = {
    RGB565(0x07, 0x07, 0x07),   //  0: near-black (not pure black — softer edges)
    RGB565(0x1F, 0x07, 0x07),   //  1: very dark red
    RGB565(0x2F, 0x0F, 0x07),   //  2: │
    RGB565(0x47, 0x0F, 0x07),   //  3: │ dark reds — the base of the flame
    RGB565(0x57, 0x17, 0x07),   //  4: │ These are barely visible but add
    RGB565(0x67, 0x1F, 0x07),   //  5: │ depth to the darkest parts
    RGB565(0x77, 0x1F, 0x07),   //  6: │
    RGB565(0x8F, 0x27, 0x07),   //  7: ↓
    RGB565(0x9F, 0x2F, 0x07),   //  8: transitioning to brighter red
    RGB565(0xAF, 0x3F, 0x07),   //  9: │
    RGB565(0xBF, 0x47, 0x07),   // 10: │ "hot" reds — the body of the flame
    RGB565(0xC7, 0x47, 0x07),   // 11: │
    RGB565(0xDF, 0x4F, 0x07),   // 12: ↓
    RGB565(0xDF, 0x57, 0x07),   // 13: red-orange transition
    RGB565(0xDF, 0x57, 0x07),   // 14: │ (duplicate — creates a "plateau"
    RGB565(0xD7, 0x5F, 0x07),   // 15: │  in the gradient, making this colour
    RGB565(0xD7, 0x5F, 0x07),   // 16: │  linger longer in the flame)
    RGB565(0xD7, 0x67, 0x0F),   // 17: ↓
    RGB565(0xCF, 0x6F, 0x0F),   // 18: orange zone
    RGB565(0xCF, 0x77, 0x0F),   // 19: │
    RGB565(0xCF, 0x7F, 0x0F),   // 20: │ The "sweet spot" — this is the colour
    RGB565(0xCF, 0x87, 0x17),   // 21: │ most people think of as "fire"
    RGB565(0xC7, 0x87, 0x17),   // 22: │
    RGB565(0xC7, 0x8F, 0x17),   // 23: ↓
    RGB565(0xC7, 0x97, 0x1F),   // 24: orange-yellow transition
    RGB565(0xBF, 0x9F, 0x1F),   // 25: │
    RGB565(0xBF, 0x9F, 0x1F),   // 26: │ (another plateau)
    RGB565(0xBF, 0xA7, 0x27),   // 27: │
    RGB565(0xBF, 0xA7, 0x27),   // 28: ↓
    RGB565(0xBF, 0xAF, 0x2F),   // 29: yellow zone
    RGB565(0xB7, 0xAF, 0x2F),   // 30: │
    RGB565(0xB7, 0xB7, 0x2F),   // 31: │ Bright yellow — near the fuel source
    RGB565(0xB7, 0xB7, 0x37),   // 32: ↓
    RGB565(0xCF, 0xCF, 0x6F),   // 33: yellow-white transition
    RGB565(0xDF, 0xDF, 0x9F),   // 34: │ The hottest part of the flame
    RGB565(0xEF, 0xEF, 0xC7),   // 35: │ where it's so hot it goes white
    RGB565(0xFF, 0xFF, 0xFF),   // 36: pure white — the fuel source
};


// ── Throttle ───────────────────────────────────────────────────────────
// The SPI transfer for a full frame takes ~45ms at 24MHz, so we 
// limit updates to keep the main loop responsive. 50ms interval 
// gives us ~20 FPS — more than enough for fire to look smooth.
// (Real fire flickers at maybe 10-15 Hz anyway.)
constexpr unsigned long FIRE_UPDATE_MS = 50;
static unsigned long lastFireUpdate = 0;


// ═══════════════════════════════════════════════════════════════════════
// Initialisation
// ═══════════════════════════════════════════════════════════════════════

void fire_init()
{
    // Clear the entire buffer to zero (cold)
    // memset is the C way of saying "fill this block of memory with 
    // a value." Like Python's [0] * length for a list.
    memset(fireBuffer, 0, sizeof(fireBuffer));

    // Seed the bottom row with maximum heat — this is the "fuel" that 
    // feeds the fire. Without this, the fire would cool to black and 
    // die out. It's like keeping a row of gas burners lit at the bottom.
    for (int x = 0; x < FIRE_WIDTH; x++) {
        FIRE_PIXEL(x, FIRE_HEIGHT - 1) = PALETTE_SIZE - 1;
    }
}


// ═══════════════════════════════════════════════════════════════════════
// Simulation step
// ═══════════════════════════════════════════════════════════════════════
//
// This is the heart of the Doom Fire algorithm. It's beautifully 
// simple — just propagate heat upward with random cooling and drift.
//
// The iteration goes from TOP to BOTTOM (row 0 to row height-2).
// Each cell reads from the cell below it (row + 1). We skip the 
// bottom row because that's our fixed heat source.
//
// Why top-to-bottom? Because we're reading from below and writing 
// to the current row. If we went bottom-to-top, we'd overwrite 
// source data before reading it. In Python terms, it's like the 
// difference between:
//
//   # Safe (top to bottom — reads unmodified data from below):
//   for y in range(0, height-1):
//       grid[y] = transform(grid[y+1])
//
//   # Broken (bottom to top — reads already-modified data):
//   for y in range(height-2, -1, -1):
//       grid[y] = transform(grid[y+1])  # y+1 was already changed!

static void fire_step()
{
    for (int y = 0; y < FIRE_HEIGHT - 1; y++) {
        for (int x = 0; x < FIRE_WIDTH; x++) {
            // Read heat from the cell directly below
            uint8_t srcHeat = FIRE_PIXEL(x, y + 1);

            // Random cooling: subtract 0, 1, or 2 from the heat value.
            // This is what makes the flame taper off as it rises — heat 
            // gradually leaks away. More aggressive cooling (0-3 range) 
            // would make shorter, more violent flames. Less cooling (0-1) 
            // would make tall, lazy flames.
            //
            // random(0, 3) returns 0, 1, or 2. The upper bound is 
            // EXCLUSIVE in Arduino's random() — same as Python's 
            // random.randint would be randint(0, 2) for the same range.
            uint8_t cooling = random(0, 3);

            // Random horizontal drift: shift the destination left, right,
            // or not at all. This creates the organic sideways wobble 
            // that makes the fire look alive. Without it, you'd get 
            // perfectly vertical columns of cooling colour — more like 
            // a gradient test pattern than fire.
            //
            // The & 3 trick: random(0, 3) would work, but bitwise AND 
            // with 3 is slightly faster on ARM. It maps random values 
            // to 0, 1, 2, or 3, then we subtract 1 to get -1, 0, 1, 2.
            // The slight rightward bias (one more positive than negative)
            // gives a subtle "wind" effect — the fire leans slightly 
            // right, which looks more natural than perfect symmetry.
            int wind = (random(0, 256) & 3) - 1;   // -1, 0, 1, or 2
            int destX = constrain(x + wind, 0, FIRE_WIDTH - 1);

            // Write the cooled heat to the destination cell.
            // max(0, ...) prevents underflow — heat can't go below 0.
            int newHeat = srcHeat - cooling;
            if (newHeat < 0) newHeat = 0;
            FIRE_PIXEL(destX, y) = (uint8_t)newHeat;
        }
    }
}


// ═══════════════════════════════════════════════════════════════════════
// LCD rendering — push fire buffer to the display with 2× scaling
// ═══════════════════════════════════════════════════════════════════════
//
// This is where the simulation meets the hardware. We iterate through 
// the fire buffer and push each pixel as a 2×2 block to fill the 
// 240×280 LCD from our 120×140 simulation.
//
// The key insight: we DON'T need to set coordinates per pixel. After 
// lcd_begin_draw() sets the window to full-screen, the ST7789 
// controller auto-advances through its RAM: left to right, then next 
// row. So we just blast pixels in the right order and they land in 
// the right place.
//
// In Python terms:
//
//   lcd.begin_draw(0, 0, 239, 279)
//   for y in range(FIRE_HEIGHT):
//       for _ in range(2):          # each row drawn twice (2× vertical)
//           for x in range(FIRE_WIDTH):
//               colour = palette[fire[y][x]]
//               lcd.push_pixel(colour)   # left half of 2×2 block
//               lcd.push_pixel(colour)   # right half of 2×2 block
//   lcd.end_draw()

static void fire_render()
{
    // Set window to full screen. After this, we just stream 
    // 240×280 = 67,200 pixels sequentially.
    lcd_begin_draw(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);

    for (int y = 0; y < FIRE_HEIGHT; y++) {
        // Each fire row gets drawn TWICE for 2× vertical scaling.
        // The 'rep' loop just replays the same row data.
        for (int rep = 0; rep < 2; rep++) {
            for (int x = 0; x < FIRE_WIDTH; x++) {
                // Look up the colour for this cell's heat value
                uint16_t colour = firePalette[FIRE_PIXEL(x, y)];

                // Write the pixel TWICE for 2× horizontal scaling
                lcd_push_pixel(colour);
                lcd_push_pixel(colour);
            }
        }
    }

    lcd_end_draw();
}


// ═══════════════════════════════════════════════════════════════════════
// Public tick function — call from main loop
// ═══════════════════════════════════════════════════════════════════════

void fire_tick()
{
    unsigned long now = millis();
    if (now - lastFireUpdate < FIRE_UPDATE_MS) return;
    lastFireUpdate = now;

    fire_step();    // Simulate one tick of fire propagation
    fire_render();  // Push the result to the LCD
}
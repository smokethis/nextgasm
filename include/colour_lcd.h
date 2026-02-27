// colour_lcd.h — Waveshare 1.69" ST7789V2 colour LCD interface
//
// Driver based on Waveshare's official Arduino demo code, adapted
// for Teensy 4.0 with raw SPI. Confirmed working 2026-02-27 after
// discovering a broken CLK cable on the display module.
//
// Display: 240×280 pixels, RGB565, IPS panel
// Controller: ST7789V2
// Interface: SPI_MODE3 with separate DC (data/command) pin
//
// Hardware notes:
//   - Requires SPI_MODE3 (CPOL=1, CPHA=1)
//   - Init commands need CS toggled per-byte (acts as latch)
//   - Bulk pixel data can be streamed with CS held low
//   - Full power/gamma init sequence required (not minimal ST7789)
//
// ═══════════════════════════════════════════════════════════════════════
// DMA ASYNC TRANSFERS
// ═══════════════════════════════════════════════════════════════════════
//
// The lcd_send_frame_async() function uses DMA (Direct Memory Access)
// to push a full frame of pixel data to the display WITHOUT blocking
// the CPU. This is essential for keeping the 60Hz main loop responsive
// while driving a 240×280 display.
//
// DMA is like hiring a courier to deliver a package. Instead of you
// personally walking each byte to the SPI port (blocking SPI.transfer),
// you hand the DMA controller a pointer to the data and say "deliver
// all 134,400 bytes to the SPI port, ping me when you're done."
// Meanwhile your CPU is free to read sensors, update LEDs, run the
// motor control loop, etc.
//
// In Python terms, it's the difference between:
//   response = requests.get(url)             # blocks until done
// and:
//   task = asyncio.create_task(fetch(url))   # returns immediately
//   # ... do other work ...
//   await task                                # check when needed
//
// IMPORTANT: The pixel buffer passed to lcd_send_frame_async() must
// remain valid and UNMODIFIED for the entire duration of the DMA
// transfer (~27ms at 40MHz). DMA reads bytes from it progressively
// over that time — it's not a snapshot. This is why the fire effect
// uses double buffering: DMA reads from one buffer while the CPU
// writes the next frame into the other.

#pragma once

#include <Arduino.h>

// ── Pin assignments ────────────────────────────────────────────────────
constexpr uint8_t LCD_PIN_CS  = 4;   // Software chip select
constexpr uint8_t LCD_PIN_DC  = 22;  // Data/Command select
constexpr uint8_t LCD_PIN_RST = 23;  // Hardware reset

// ── Display dimensions ─────────────────────────────────────────────────
constexpr uint16_t LCD_WIDTH  = 240;
constexpr uint16_t LCD_HEIGHT = 280;
constexpr uint32_t LCD_PIXEL_COUNT = (uint32_t)LCD_WIDTH * LCD_HEIGHT;  // 67,200

// ── RGB565 colour conversion ───────────────────────────────────────────
//
// RGB565 packs 16 bits of colour into 2 bytes:
//   Bits 15-11: Red   (5 bits → 32 levels)
//   Bits 10-5:  Green (6 bits → 64 levels — eyes more sensitive)
//   Bits 4-0:   Blue  (5 bits → 32 levels)
//
// In Python terms: int(r / 8) << 11 | int(g / 4) << 5 | int(b / 8)

#define RGB565(r, g, b) \
    (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

// ── Byte-swapped RGB565 for DMA transfers ──────────────────────────────
//
// This is a subtlety that trips everyone up with SPI + DMA on ARM.
//
// The ST7789 expects pixel bytes in big-endian order: high byte first,
// then low byte. For pure red (0xF800), it wants byte 0xF8 then 0x00.
//
// But the Teensy (ARM Cortex-M7) is little-endian. When you store the
// uint16_t value 0xF800 in RAM, it's laid out as:
//   address N:   0x00  (low byte first — little end first!)
//   address N+1: 0xF8  (high byte second)
//
// Synchronous SPI.transfer() handles this because we explicitly send
// the high byte then low byte in our code. But DMA just reads bytes
// sequentially from memory addresses — it doesn't know about endianness.
// So it sends 0x00 then 0xF8 — exactly backwards!
//
// The fix: pre-swap the bytes BEFORE storing to the buffer.
// __builtin_bswap16 is a compiler intrinsic that swaps the two bytes
// of a uint16_t. On ARM it compiles to a single REV16 instruction —
// essentially free (one clock cycle).
//
// After swapping: 0xF800 → 0x00F8 (as a uint16_t in memory)
// Stored in RAM:  byte[0] = 0xF8, byte[1] = 0x00
// DMA sends:      0xF8 first, then 0x00 → display sees 0xF800 → Red ✓
//
// In Python terms, it's like struct.pack('>H', value) for big-endian
// vs struct.pack('<H', value) for little-endian.
//
// Use RGB565_BE for any pixel data going into a DMA buffer.
// Use plain RGB565 for synchronous transfers (lcd_fill etc).

#define RGB565_BE(r, g, b)  __builtin_bswap16(RGB565(r, g, b))

// ── Public interface ───────────────────────────────────────────────────

void lcd_init();
void lcd_fill(uint16_t colour);
void lcd_test_tick();

// ── Synchronous bulk drawing API ───────────────────────────────────────
// These block until all pixels are sent. Fine for small regions or
// one-off draws, but NOT for full-frame updates in the main loop.

void lcd_begin_draw(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void lcd_push_pixel(uint16_t colour);
void lcd_end_draw();

// ── Async DMA frame transfer API ───────────────────────────────────────
//
// Usage pattern (called from the fire module or similar):
//
//   if (!lcd_frame_busy()) {
//       // ...render pixels into buffer using RGB565_BE colours...
//       lcd_send_frame_async(buffer, LCD_PIXEL_COUNT);
//   }
//
// RULES:
// 1. Only call lcd_send_frame_async when lcd_frame_busy() returns false
// 2. The buffer must contain pre-swapped RGB565 pixels (use RGB565_BE)
// 3. The buffer must stay untouched until lcd_frame_busy() returns false
// 4. Don't mix sync and async LCD calls — check lcd_frame_busy() first

// Start an async full-screen DMA transfer.
// Returns true if transfer started, false if busy.
bool lcd_send_frame_async(const uint16_t* pixelData, uint32_t pixelCount);

// Check if a DMA transfer is still in progress.
bool lcd_frame_busy();
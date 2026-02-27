// colour_lcd.cpp — Waveshare 1.69" ST7789V2 colour LCD driver
//
// Based on Waveshare's official Arduino demo, adapted for Teensy 4.0.
// Uses their exact init register sequence — every value was confirmed
// working on real hardware (after fixing a broken CLK cable, 2026-02-27).
//
// ═══════════════════════════════════════════════════════════════════════
// SPEED STRATEGY — THREE MODES
// ═══════════════════════════════════════════════════════════════════════
//
// The display needs different treatment depending on what we're sending:
//
// 1. COMMANDS (init sequence, window setup):
//    CS toggled per-byte using digitalWriteFast. The display uses the
//    CS rising edge as a "latch" — without it, bytes aren't committed.
//    Only ~50 command bytes during init, so speed doesn't matter.
//
// 2. SYNCHRONOUS PIXEL DATA (lcd_fill, lcd_push_pixel):
//    CS held LOW, data streamed via SPI.transfer(). CPU blocks until
//    each byte is clocked out. Fine for small fills or init.
//
// 3. DMA PIXEL DATA (lcd_send_frame_async):
//    CS held LOW, entire pixel buffer sent via DMA. CPU returns
//    immediately and is free for other work. The DMA controller reads
//    bytes from the buffer in the background, feeding them to the SPI
//    peripheral at wire speed. A callback fires when done.
//
//    In Python terms, modes 1+2 are like:
//      for byte in data: spi.write(byte)   # blocks
//    Mode 3 is like:
//      asyncio.create_task(spi.write_all(data))  # returns immediately
//
// ═══════════════════════════════════════════════════════════════════════
// SPI CLOCK SPEED — 40MHz
// ═══════════════════════════════════════════════════════════════════════
//
// The ST7789V2 datasheet allows writes up to ~60MHz. We're running at
// 40MHz which gives us ~27ms per full frame (134,400 bytes), or about
// 37 FPS theoretical maximum. This is aggressive enough to get good
// performance while leaving some margin for signal integrity through
// hand-soldered breadboard wiring.
//
// If you see visual glitches (wrong colours, shifted image), drop to
// 30000000 or 24000000.

#include "colour_lcd.h"
#include <SPI.h>
#include <EventResponder.h>

// ═══════════════════════════════════════════════════════════════════════
// Configuration
// ═══════════════════════════════════════════════════════════════════════

constexpr uint32_t LCD_SPI_SPEED = 30000000;  // 30MHz — maximum using dodgy breadboard wiring, 40MHz should be possible on a dedicated board.

// ═══════════════════════════════════════════════════════════════════════
// DMA state
// ═══════════════════════════════════════════════════════════════════════
//
// EventResponder is Teensy's mechanism for DMA completion callbacks.
// When the SPI DMA transfer finishes, the hardware triggers an
// interrupt, and the EventResponder calls our function. Think of it
// like a Promise in JavaScript — you attach a .then() handler and
// the runtime calls it when the async operation completes.
//
// 'volatile' on dmaBusy is critical. Without it, the compiler might
// optimise the check in lcd_frame_busy() into a single read that
// gets cached in a CPU register — it would never see the ISR's update.
// 'volatile' says "always read this from actual memory, it can change
// behind your back." In Python this isn't an issue because the GIL
// prevents true concurrent memory access, but in C++ with interrupts
// the CPU and ISR really do run concurrently on the same core.

static EventResponder spiEvent;
static volatile bool dmaBusy = false;

// DMA completion callback — called from interrupt context when the
// SPI DMA transfer finishes. Releases the chip select and clears
// the busy flag.
//
// IMPORTANT: This runs in ISR (Interrupt Service Routine) context,
// which means:
//   - Keep it SHORT — no Serial.print, no delay()
//   - Only touch volatile variables
//   - Don't allocate memory
// It's like a signal handler in Python — minimal work only.
static void onDmaComplete(EventResponderRef event)
{
    digitalWriteFast(LCD_PIN_CS, HIGH);  // Release chip select
    dmaBusy = false;                     // Signal "ready for next frame"
}


// ═══════════════════════════════════════════════════════════════════════
// Low-level: COMMAND mode (CS toggled per-byte for latching)
// ═══════════════════════════════════════════════════════════════════════

static void lcd_write_command(uint8_t cmd)
{
    digitalWriteFast(LCD_PIN_CS, LOW);
    digitalWriteFast(LCD_PIN_DC, LOW);   // DC LOW = this is a command
    SPI.transfer(cmd);
    digitalWriteFast(LCD_PIN_CS, HIGH);  // Latch!
}

static void lcd_write_data(uint8_t data)
{
    digitalWriteFast(LCD_PIN_CS, LOW);
    digitalWriteFast(LCD_PIN_DC, HIGH);  // DC HIGH = this is data
    SPI.transfer(data);
    digitalWriteFast(LCD_PIN_CS, HIGH);  // Latch!
}


// ═══════════════════════════════════════════════════════════════════════
// Low-level: BULK mode (CS held low for streaming)
// ═══════════════════════════════════════════════════════════════════════

static void lcd_bulk_start()
{
    digitalWriteFast(LCD_PIN_CS, LOW);
    digitalWriteFast(LCD_PIN_DC, HIGH);
}

static inline void lcd_bulk_pixel(uint16_t colour)
{
    SPI.transfer((colour >> 8) & 0xFF);
    SPI.transfer(colour & 0xFF);
}

static void lcd_bulk_end()
{
    digitalWriteFast(LCD_PIN_CS, HIGH);
}


// ═══════════════════════════════════════════════════════════════════════
// Hardware reset
// ═══════════════════════════════════════════════════════════════════════

static void lcd_hardware_reset()
{
    digitalWriteFast(LCD_PIN_CS, LOW);
    delay(20);
    digitalWriteFast(LCD_PIN_RST, LOW);
    delay(20);
    digitalWriteFast(LCD_PIN_RST, HIGH);
    delay(20);
}


// ═══════════════════════════════════════════════════════════════════════
// Set draw window
// ═══════════════════════════════════════════════════════════════════════

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    lcd_write_command(0x2A);
    lcd_write_data(x0 >> 8);
    lcd_write_data(x0 & 0xFF);
    lcd_write_data(x1 >> 8);
    lcd_write_data(x1 & 0xFF);

    lcd_write_command(0x2B);
    lcd_write_data((y0 + 20) >> 8);
    lcd_write_data((y0 + 20) & 0xFF);
    lcd_write_data((y1 + 20) >> 8);
    lcd_write_data((y1 + 20) & 0xFF);

    lcd_write_command(0x2C);
}


// ═══════════════════════════════════════════════════════════════════════
// Initialisation
// ═══════════════════════════════════════════════════════════════════════

void lcd_init()
{
    // ── GPIO setup ─────────────────────────────────────────────────────
    pinMode(LCD_PIN_CS,  OUTPUT);
    pinMode(LCD_PIN_DC,  OUTPUT);
    pinMode(LCD_PIN_RST, OUTPUT);
    digitalWriteFast(LCD_PIN_CS, HIGH);
    digitalWriteFast(LCD_PIN_RST, HIGH);

    // ── SPI setup ──────────────────────────────────────────────────────
    SPI.begin();
    SPI.beginTransaction(SPISettings(LCD_SPI_SPEED, MSBFIRST, SPI_MODE3));

    // ── DMA completion handler ─────────────────────────────────────────
    // attachImmediate() means the callback fires directly from the DMA
    // interrupt — no queuing delay. The alternative attachInterrupt()
    // queues it for the next yield(), which would add latency.
    spiEvent.attachImmediate(onDmaComplete);

    // ── Hardware reset ─────────────────────────────────────────────────
    lcd_hardware_reset();

    // ── Waveshare init sequence ────────────────────────────────────────
    // (every register value verbatim from Waveshare's demo code)

    lcd_write_command(0x36);  // MADCTL
    lcd_write_data(0x00);

    lcd_write_command(0x3A);  // COLMOD
    lcd_write_data(0x05);     // RGB565

    lcd_write_command(0xB2);  // PORCTRL
    lcd_write_data(0x0B);
    lcd_write_data(0x0B);
    lcd_write_data(0x00);
    lcd_write_data(0x33);
    lcd_write_data(0x35);

    lcd_write_command(0xB7);  // GCTRL
    lcd_write_data(0x11);

    lcd_write_command(0xBB);  // VCOMS
    lcd_write_data(0x35);

    lcd_write_command(0xC0);  // LCMCTRL
    lcd_write_data(0x2C);

    lcd_write_command(0xC2);  // VDVVRHEN
    lcd_write_data(0x01);

    lcd_write_command(0xC3);  // VRHS
    lcd_write_data(0x0D);

    lcd_write_command(0xC4);  // VDVS
    lcd_write_data(0x20);

    lcd_write_command(0xC6);  // FRCTRL2
    lcd_write_data(0x13);

    lcd_write_command(0xD0);  // PWCTRL1
    lcd_write_data(0xA4);
    lcd_write_data(0xA1);

    lcd_write_command(0xD6);  // Undocumented
    lcd_write_data(0xA1);

    lcd_write_command(0xE0);  // PVGAMCTRL
    lcd_write_data(0xF0); lcd_write_data(0x06); lcd_write_data(0x0B);
    lcd_write_data(0x0A); lcd_write_data(0x09); lcd_write_data(0x26);
    lcd_write_data(0x29); lcd_write_data(0x33); lcd_write_data(0x41);
    lcd_write_data(0x18); lcd_write_data(0x16); lcd_write_data(0x15);
    lcd_write_data(0x29); lcd_write_data(0x2D);

    lcd_write_command(0xE1);  // NVGAMCTRL
    lcd_write_data(0xF0); lcd_write_data(0x04); lcd_write_data(0x08);
    lcd_write_data(0x08); lcd_write_data(0x07); lcd_write_data(0x03);
    lcd_write_data(0x28); lcd_write_data(0x32); lcd_write_data(0x40);
    lcd_write_data(0x3B); lcd_write_data(0x19); lcd_write_data(0x18);
    lcd_write_data(0x2A); lcd_write_data(0x2E);

    lcd_write_command(0xE4);  // Undocumented
    lcd_write_data(0x25);
    lcd_write_data(0x00);
    lcd_write_data(0x00);

    lcd_write_command(0x21);  // INVON
    lcd_write_command(0x11);  // SLPOUT
    delay(120);
    lcd_write_command(0x29);  // DISPON
    delay(20);

    lcd_fill(0x0000);

    Serial.println("[LCD] Init complete (40MHz SPI, DMA enabled)");
}


// ═══════════════════════════════════════════════════════════════════════
// Synchronous fill
// ═══════════════════════════════════════════════════════════════════════

void lcd_fill(uint16_t colour)
{
    lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
    lcd_bulk_start();
    for (uint32_t i = 0; i < LCD_PIXEL_COUNT; i++) {
        lcd_bulk_pixel(colour);
    }
    lcd_bulk_end();
}


// ═══════════════════════════════════════════════════════════════════════
// Synchronous bulk drawing API
// ═══════════════════════════════════════════════════════════════════════

void lcd_begin_draw(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    lcd_set_window(x0, y0, x1, y1);
    lcd_bulk_start();
}

void lcd_push_pixel(uint16_t colour)
{
    lcd_bulk_pixel(colour);
}

void lcd_end_draw()
{
    lcd_bulk_end();
}


// ═══════════════════════════════════════════════════════════════════════
// Async DMA frame transfer
// ═══════════════════════════════════════════════════════════════════════
//
// This is the key performance feature. Instead of the CPU sitting idle
// for ~27ms while bytes clock out over SPI, we hand the buffer to the
// DMA controller and return immediately.
//
// The Teensy SPI library's async transfer() uses the i.MX RT1062's
// DMA engine internally. We just provide:
//   - TX buffer (our pixel data)
//   - RX buffer (nullptr — we don't care about MISO data)
//   - Byte count
//   - EventResponder (our completion callback)
//
// The DMA controller then autonomously:
//   1. Reads a byte from our buffer in RAM
//   2. Writes it to the SPI transmit FIFO
//   3. Advances to the next byte
//   4. Repeats until count reaches zero
//   5. Fires the interrupt → our callback runs
//
// It's like setting up a mail merge in an email client — you prepare
// all the content, hit "send all", and the system handles delivery
// while you do something else.

bool lcd_send_frame_async(const uint16_t* pixelData, uint32_t pixelCount)
{
    // Don't start a new transfer if one is in progress
    if (dmaBusy) return false;

    // Set up the draw window (synchronous — just a few command bytes,
    // takes microseconds). This tells the ST7789 "the next pixels go
    // into this rectangular region."
    lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);

    // Assert CS and DC for data streaming.
    // These stay held throughout the entire DMA transfer. The
    // completion callback (onDmaComplete) releases CS when done.
    digitalWriteFast(LCD_PIN_CS, LOW);
    digitalWriteFast(LCD_PIN_DC, HIGH);

    // Mark as busy BEFORE starting DMA. If we did this after, there's
    // a tiny window where the DMA could complete before we set the
    // flag, and another call could start a second transfer. Classic
    // race condition — like two threads checking "is_processing" at
    // the exact same moment.
    dmaBusy = true;

    // Start the DMA transfer.
    //
    // The cast to (void*) is needed because SPI.transfer() takes void*
    // but our buffer is uint16_t*. The DMA doesn't care about types —
    // it just moves bytes. The pixel data is already byte-swapped
    // (RGB565_BE) so the bytes are in the right order for the display.
    //
    // pixelCount * 2 because each pixel is 2 bytes (uint16_t = 16 bits).
    // The DMA counts in bytes, not pixels.
    SPI.transfer((void*)pixelData, nullptr, pixelCount * 2, spiEvent);

    return true;
}

bool lcd_frame_busy()
{
    return dmaBusy;
}


// ═══════════════════════════════════════════════════════════════════════
// Test tick — cycle through solid colours
// ═══════════════════════════════════════════════════════════════════════

void lcd_test_tick()
{
    // Don't run sync fills while DMA is active
    if (dmaBusy) return;

    static unsigned long lastChange = 0;
    static uint8_t colourIndex = 0;

    if (millis() - lastChange < 1000) return;
    lastChange = millis();

    const uint16_t colours[] = { 0xF800, 0x07E0, 0x001F, 0xFFFF };
    const char* names[] = { "RED", "GREEN", "BLUE", "WHITE" };

    lcd_fill(colours[colourIndex]);
    Serial.print("[LCD] Fill: ");
    Serial.println(names[colourIndex]);

    colourIndex = (colourIndex + 1) % 4;
}
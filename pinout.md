# Nextgasm — Teensy 4.0 Pinout Reference

Quick reference for breadboard wiring. Keep this up to date as new
components are added.

Last updated: 2026-02-26

## Power Rails

| Rail | Source | Supplies | Notes |
|------|--------|----------|-------|
| 3.3V | Teensy 3.3V pin → breadboard power rail | OLED, 5-way nav switch, LED matrix | ~250mA available from Teensy regulator |
| GND | Teensy GND → breadboard ground rail | All components | |
| 5V | USB-C breakout VBUS | Teensy VIN, NeoPixel ring (when added) | NeoPixels need 5V; Teensy regulates down to 3.3V |

## Teensy 4.0 Pin Assignments

### Currently Wired

| Teensy Pin | Function | Component | Wire Colour | Config |
|-----------|----------|-----------|-------------|--------|
| 2 | Encoder A | Rotary encoder | | Quadrature input |
| 3 | Encoder B | Rotary encoder | | Quadrature input |
| 5 | Encoder button | Rotary encoder | | INPUT, internal pull-up |
| 6 | CS | HT1632C LED matrix | | OUTPUT, active low |
| 7 | WR | HT1632C LED matrix | | OUTPUT |
| 8 | DATA | HT1632C LED matrix | | OUTPUT |
| 15 | Up | 5-way nav switch | | INPUT_PULLUP |
| 16 | Down | 5-way nav switch | | INPUT_PULLUP |
| 17 | Left | 5-way nav switch | | INPUT_PULLUP |
| 18 | SDA | OLED display (I2C) | | Hardware I2C — fixed, not configurable |
| 19 | SCL | OLED display (I2C) | | Hardware I2C — fixed, not configurable |
| 20 | Right | 5-way nav switch | | INPUT_PULLUP |
| 21 | Center/Press | 5-way nav switch | | INPUT_PULLUP |

### Reserved (not yet wired)

| Teensy Pin | Function | Component | Notes |
|-----------|----------|-----------|-------|
| 9 | Motor PWM | Vibrator motor | analogWriteFrequency set to 31kHz |
| 10 | NeoPixel data | NeoPixel ring (24 LED) | WS2812B, needs 5V power separately |
| A0 (14) | Analog input | Pressure sensor | 12-bit ADC, 4x oversampled |

### Free Pins

| Teensy Pin | Notes |
|-----------|-------|
| 0, 1 | Serial1 RX/TX — keep free if you ever want a second serial port |
| 4 | General purpose |
| 11 | SPI MOSI — useful if adding SPI devices (e.g. colour LCD) |
| 12 | SPI MISO |
| 13 | SPI SCK (also onboard LED) |
| 22 | General purpose |
| 23 | General purpose |
| A1–A9 (15–23) | Some overlap with nav switch pins above; remaining analog-capable pins are available |

## Component Wiring Details

### OLED Display (1.3" SH1106 128x64, I2C)

| Display Pin | Connects To |
|------------|-------------|
| VCC | 3.3V rail |
| GND | GND rail |
| SDA | Teensy pin 18 |
| SCL | Teensy pin 19 |

I2C address: 0x3C (7-bit) / 0x78 (8-bit). Set by resistor on board.

### 5-Way Navigation Switch

My switch marks pin 1 underneath and has a pinout like [This Adafruit component](https://cdn-shop.adafruit.com/datasheets/SKQUCAA010-ALPS.pdf), but check your specific switch's datasheet for pinout. Assuming pin 1 is top right when mounted in a breadboard, the pinout is:

| Switch Pin | Connects To | Breadboard Pin |
|-----------|-------------|-------------|
| COM | GND rail | Bottom left |
| Up | Teensy pin 15 | Top middle |
| Down | Teensy pin 16 | Bottom middle |
| Left | Teensy pin 17 | Top left |
| Right | Teensy pin 20 | Top right |
| Center | Teensy pin 21 | Bottom right |

No external resistors needed — uses internal pull-ups.

### HT1632C LED Matrix (DFRobot FireBeetle 24x8)

| Matrix Pin | Connects To |
|-----------|-------------|
| VCC | 3.3V rail |
| GND | GND rail |
| CS | Teensy pin 6 |
| WR | Teensy pin 7 |
| DATA | Teensy pin 8 |

### Rotary Encoder

| Encoder Pin | Connects To |
|------------|-------------|
| A | Teensy pin 3 |
| B | Teensy pin 2 |
| SW (button) | Teensy pin 5 |
| + | 3.3V rail |
| GND | GND rail |

## Notes

- The "Wire Colour" column is for you to fill in as you wire things
  up — helps enormously when debugging a rat's nest of jumpers.
- Pins 18/19 are hardware I2C and cannot be reassigned. If you need
  a second I2C bus later, Wire1 is on pins 16/17 — but those are
  currently used by the nav switch, so you'd need to shuffle pins.
- All 5-way nav switch pins are on the same side of the Teensy board
  (right edge) for tidy wiring.
# Poudre Trail — CYD

A narrative travel game for the **ESP32-2432S028R** ("Cheap Yellow Display").  
Set in the Cache la Poudre valley, Colorado, 1861.

## Features

- **Split reputation system** — separate scores for Janis, Chief Friday, and Mason track relationships independently; each NPC's rep label shows in the status bar (`Neutral` → `Open` → `Trusting`/`Trusted`)
- **Moral contradiction** — if you reach `repMason ≥ 5` AND `repFriday ≥ 5` (serving both new order and old), the victory footer records the contradiction
- **Five route stops** — Laporte → Council Tree → Virginia Dale → Mason's Claim → Fort Collins (Virginia Dale added as a full stop with its own event and trading post)
- **Janis event at start** — the game opens in EVENT mode at Laporte; Antoine Janis greets every run
- **Expanded Chief Friday event** — three choices (listen / offer tobacco / press on) with distinct split-rep outcomes
- **Save / load** — persists food, ammo, medicine, trade goods, horse condition, morale, health, all three rep scores, flags, and route position to `/saves/slot1.sav` on the SD card
- **BMP art** — 24-bit BMP images read directly from SD (`/art/*.bmp`), displayed at 92×68 in the card panel; degrades gracefully when SD is absent
- **Fallback world** — all events and locations are hard-coded in the engine; no SD card is needed to play
- **CSV-driven content** — load custom events, locations, and trades from SD (`/data/`)

## Hardware

| Component | Value |
|-----------|-------|
| Board | ESP32-2432S028R (CYD) |
| Display | ILI9341 320×240 |
| Touch | XPT2046 capacitive |
| Storage | MicroSD via SPI |
| Backlight | GPIO 21 |
| TFT CS | GPIO 15 |
| TFT DC | GPIO 2 |
| TFT RST | GPIO 4 |
| TFT MOSI | GPIO 13 |
| TFT MISO | GPIO 12 |
| TFT SCK | GPIO 14 |
| Touch CS | GPIO 33 |
| Touch IRQ | GPIO 36 |
| SD CS | GPIO 5 |

## Build

### PlatformIO (recommended)

```bash
# From repo root
pio run                  # compile
pio run -t upload        # flash to device
pio device monitor       # serial output at 115200
```

Requires PlatformIO Core ≥ 6.x. The `platformio.ini` at the repo root configures the `esp32-2432S028R` environment with TFT_eSPI build flags — no `User_Setup.h` edits needed.

### Host tests (g++)

```bash
cd test
make run
```

Compiles and runs the engine test suite on the host (no Arduino toolchain needed). All 24 checks should pass.

## SD card layout

Copy these directories to the root of a FAT32-formatted SD card:

```
/art/           ← BMP images from data/art/
/data/
  locations.csv
  events.csv
  trades.csv
  events/
    janis_store.txt
    chief_friday.txt
    virginia_dale.txt
    mason_reveal.txt
    flood_1864.txt
/saves/         ← created automatically on first save
```

Without an SD card the engine runs the built-in fallback world.

## Changelog

### merge/reputation-e2e

- **Split reputation**: `resources.reputation` replaced by `repJanis`, `repFriday`, `repMason`; status bar shows all three labels
- **Moral contradiction**: threshold is `repMason ≥ 5 AND repFriday ≥ 5`; victory footer branches on `flags_["moral_contradiction"]`
- **travelTo() mode guard**: no-op when mode is `VICTORY` or `GAME_OVER`; `clampResources()` triggers `GAME_OVER` before the VICTORY check in `travelTo()`
- **Janis fires at start**: `startGame()` sets `mode_ = EVENT` and queues the Laporte event immediately
- **Virginia Dale**: added as the third route stop between Council Tree and Mason's Claim; has its own event and two trades
- **Chief Friday expanded**: three choices (listen / offer tobacco / press on) with distinct rep deltas
- **Food drain rebalance**: `-12` per travel (up from `-8` in art_save; `-6` horse condition, `-1` morale unchanged)
- **Save format**: now persists `ammo`, `medicine`, `repJanis`, `repFriday`, `repMason`, `day`, `flag_moral_contradiction`; old saves load with sane defaults
- **Display ported**: `Arduino_GFX_Library` → `TFT_eSPI` (same pin wiring); `pushPixels()` replaces `draw16bitRGBBitmap()`

## Hardware-only checklist (cannot be verified in CI)

- [ ] Touch calibration: verify `TOUCH_MIN/MAX_X/Y` constants in `src/main.cpp` against your specific panel
- [ ] BMP art renders without color inversion — if colors are wrong, add `tft.setSwapBytes(true)` before `drawBmpFromSd()` call
- [ ] SD card mounts and `SD content loaded` appears in boot splash
- [ ] Save writes to `/saves/slot1.sav` and survives power cycle
- [ ] Touch debounce (220 ms) feels responsive — adjust `lastTouchMs` threshold as needed
- [ ] Backlight (GPIO 21) lights immediately on power-up

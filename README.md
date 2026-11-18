# Poudre Trail CYD

A CYD/ESP32 historical narrative game inspired by Oregon Trail, relocated to Colorado's Poudre Valley. Features touch UI, SD-loaded dialogue and art, branching encounters, split reputation systems, save/load, and story-driven travel through La Porte, Council Tree, Virginia Dale, and Fort Collins.

## Project layout

- `platformio.ini` - PlatformIO project config
- `firmware/PoudreTrailCYD/include/` - engine and UI helper headers
- `firmware/PoudreTrailCYD/src/` - merged reference engine and CYD UI sources
- `data/dialogue/` - SD-loaded dialogue overrides
- `docs/` - integration notes

## Hardware bring-up

`src/main.cpp` is currently a bring-up firmware that checks the display, touch
and SD card before the game engine goes in. Pin map and panel settings
(`include/cyd_display.h`) come from waywardtech/CYD-Diagnostic.

1. Copy `assets/art/` to the root of a FAT32 microSD card as `/art`.
   The card should then have `/art/test/frame1.bmp` to `frame4.bmp`.
2. Flash the firmware:

   ```bash
   pio run -t upload
   ```

   If the colours look inverted or the picture is shifted, use the ST7789 build:

   ```bash
   pio run -e esp32-2432S028R-st7789 -t upload
   ```

3. Watch the serial port at 115200 baud (`pio device monitor`), then reset the board.

What to check:

- **[TFT]**: the boot sequence shows full-screen RED, GREEN, BLUE, WHITE and BLACK, each
  labelled with its own name. In the layout, the swatch row should match its
  labels, and the magenta 1 px frame should be visible on all four edges.
- **[SD]**: `mount OK`, followed by one `art` line per BMP. The art panel should cycle
  frame1 to frame4 (the sun moves left to right), and the top band should read
  red, green, blue from left to right. If it reads blue, green, red, the BMP channels are swapped.
- **[TOUCH]**: tap the title bar, each button, the footer and the four corners.
  Each tap logs the raw and mapped coordinates and the target it hit, and a yellow
  cross marks where the firmware thinks you tapped. Tapping the title bar clears the crosses.
  If taps land offset or mirrored, put the raw corner values into
  `CYD_TOUCH_*` in `include/cyd_display.h`.

## Status

This repo contains a merged reference implementation and project structure for the mature CYD branch. It is intended as the working public repo for continued development.

## Next steps

- Replace the BMP stub renderer with the full decoder
- Tune touch calibration on hardware
- Expand world data from fallback-coded content to full SD/CSV content

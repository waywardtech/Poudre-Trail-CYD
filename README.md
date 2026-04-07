# Poudre Trail CYD

A CYD/ESP32 historical narrative game inspired by Oregon Trail, relocated to Colorado's Poudre Valley. Features touch UI, SD-loaded dialogue and art, branching encounters, split reputation systems, save/load, and story-driven travel through La Porte, Council Tree, Virginia Dale, and Fort Collins.

## Project layout

- `platformio.ini` - PlatformIO project config
- `firmware/PoudreTrailCYD/include/` - engine and UI helper headers
- `firmware/PoudreTrailCYD/src/` - merged reference engine and CYD UI sources
- `data/dialogue/` - SD-loaded dialogue overrides
- `docs/` - integration notes

## Status

This repo contains a merged reference implementation and project structure for the mature CYD branch. It is intended as the working public repo for continued development.

## Next steps

- Replace the BMP stub renderer with the full decoder
- Tune touch calibration on hardware
- Expand world data from fallback-coded content to full SD/CSV content

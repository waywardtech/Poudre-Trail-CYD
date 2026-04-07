Fast BMP renderer target:
- 24-bit uncompressed BMP only
- no scaling
- designed for CYD portrait boxes
- runtime reads from SD paths like /data/art/janis.bmp

Recommended portrait size:
- 96x72 or similar

Next code step:
- replace placeholder draw box in firmware/PoudreTrailCYD/src/main.cpp with the real decoder

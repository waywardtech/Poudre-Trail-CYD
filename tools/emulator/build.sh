#!/usr/bin/env bash
# Build the host emulator. Needs the PlatformIO libraries (run `pio run` once, or
# `pio pkg install`) for the real AnimatedGIF / PNGdec / TFT_eSPI font sources.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
LIBS="$(ls -d "$ROOT"/.pio/libdeps/*/ | head -1)"
[ -d "$LIBS/AnimatedGIF" ] || { echo "run 'pio run' first to fetch libraries" >&2; exit 1; }
OUT="${1:-$HERE/build}"
mkdir -p "$OUT"
PNG="$LIBS/PNGdec/src"
CFLAGS="-O1 -w -D__LINUX__ -I$HERE -I$ROOT/include -I$LIBS/AnimatedGIF/src -I$PNG -I$LIBS/TFT_eSPI/Fonts"
for c in adler32 crc32 infback inffast inflate inftrees zutil; do gcc $CFLAGS -c "$PNG/$c.c" -o "$OUT/$c.o"; done
g++ -std=gnu++17 $CFLAGS -c "$LIBS/AnimatedGIF/src/AnimatedGIF.cpp" -o "$OUT/AnimatedGIF.o"
g++ -std=gnu++17 $CFLAGS -c "$PNG/PNGdec.cpp" -o "$OUT/PNGdec.o"
g++ -std=gnu++17 $CFLAGS -c "$ROOT/src/poudre_trail_engine.cpp" -o "$OUT/engine.o"
g++ -std=gnu++17 $CFLAGS "$HERE/harness.cpp" "$OUT"/*.o -o "$OUT/emulator"
echo "built $OUT/emulator"

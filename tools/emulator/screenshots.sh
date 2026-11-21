#!/usr/bin/env bash
# Render README screenshots from the real firmware UI code into docs/screenshots/.
# Output is 2x nearest-neighbour of the 320x240 framebuffer. Needs Pillow.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
"$HERE/build.sh" "$HERE/build"
TMP="$(mktemp -d)"
"$HERE/build/emulator" "$TMP/ppm" "$ROOT/data" "$TMP/saves"
mkdir -p "$ROOT/docs/screenshots"
PY=python3; /usr/bin/python3 -c "import PIL.Image" 2>/dev/null && PY=/usr/bin/python3
$PY - "$TMP/ppm" "$ROOT/docs/screenshots" <<'PYEOF'
import sys, glob, os
from PIL import Image
src, dst = sys.argv[1:3]
for p in sorted(glob.glob(src + "/*.ppm")):
    im = Image.open(p).convert("RGB")
    im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(
        os.path.join(dst, os.path.basename(p)[:-4] + ".png"), optimize=True)
print("wrote", len(glob.glob(dst + "/*.png")), "screenshots to", dst)
PYEOF
rm -rf "$TMP"

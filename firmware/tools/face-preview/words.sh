#!/bin/bash
# Checks the Time in Words clockface in every language at every minute (see words.cpp) and draws
# a sheet with some times in each language.
#
#   ./words.sh [outdir]       default: ./out
#
# Needs a C++ compiler, Python with Pillow, and the Time in Words firmware built once
# (pio run -e cw-cf-0x02) so the Adafruit GFX library has been downloaded.
set -euo pipefail
cd "$(dirname "$0")"

GFX="../../.pio/libdeps/cw-cf-0x02/Adafruit GFX Library"
F=../../clockfaces/cw-cf-0x02
OUT=$(mkdir -p "${1:-out}" && cd "${1:-out}" && pwd)
[ -d "$GFX" ] || { echo "Build the Time in Words firmware once first: pio run -e cw-cf-0x02" >&2; exit 1; }
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

ENGINE=(../../lib/cw-gfx-engine/EventBus.cpp ../../lib/cw-gfx-engine/Locator.cpp "$GFX/Adafruit_GFX.cpp")
c++ -std=c++14 -O2 -fno-rtti -DARDUINO=100 -DARDUINO_ESP32_DEV -w -I ../../lib/cw-gfx-engine -I shim -I "$GFX" \
	-I ../../lib/cw-commons -I $F words.cpp $F/Clockface.cpp $F/WordClock.cpp $F/WordLayout.cpp "${ENGINE[@]}" -o "$WORK/words"
status=0
"$WORK/words" "$WORK" || status=$?

python3 - "$WORK" "$OUT" <<'PYEOF'
import glob, os, sys
from PIL import Image, ImageDraw
work, out = sys.argv[1:3]
S = 5

def panel(ppm):
    img = Image.open(ppm).convert("RGB")
    big = Image.new("RGB", (64 * S, 64 * S), (8, 8, 10))
    d = ImageDraw.Draw(big)
    for y in range(64):
        for x in range(64):
            c = img.getpixel((x, y))
            c = c if any(c) else (22, 22, 24)  # an LED that's off
            d.ellipse((x * S, y * S, x * S + S - 2, y * S + S - 2), fill=c)
    return big

files = sorted(glob.glob(work + "/*.ppm"))
langs = sorted({os.path.basename(f)[:2] for f in files})
times = sorted({os.path.basename(f)[3:7] for f in files})
pad = 6
sheet = Image.new("RGB", (len(times) * (64 * S + pad) + pad, len(langs) * (64 * S + pad) + pad), (0, 0, 0))
for f in files:
    name = os.path.basename(f)
    r, c = langs.index(name[:2]), times.index(name[3:7])
    sheet.paste(panel(f), (pad + c * (64 * S + pad), pad + r * (64 * S + pad)))
sheet.save(out + "/words.png")
for lang in langs:  # one sheet per language too
    row = sheet.crop((0, langs.index(lang) * (64 * S + pad), sheet.width, (langs.index(lang) + 1) * (64 * S + pad) + pad))
    row.save(out + "/words_%s.png" % lang)
PYEOF
echo "Wrote pictures to $OUT"
exit $status

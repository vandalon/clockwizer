#!/bin/bash
# Makes the clockface pictures for the web flasher (docs/images/faces) from the real firmware code:
# Tetris, Football and Formula 1 are drawn by their own Clockface.cpp on a fake panel, Luigi is the
# Mario picture with the red turned green.
#
#   ./thumbs.sh [outdir]       default: ../../../docs/images/faces
#
# Needs a C++ compiler and Python with Pillow, and the Football firmware built once
# (pio run -e cw-cf-0x0B) so the Adafruit GFX library has been downloaded.
set -euo pipefail
cd "$(dirname "$0")"

GFX="../../.pio/libdeps/cw-cf-0x0B/Adafruit GFX Library"
FB=../../lib/cw-football
F1=../../lib/cw-f1
OUT=$(mkdir -p "${1:-../../../docs/images/faces}" && cd "${1:-../../../docs/images/faces}" && pwd)
[ -d "$GFX" ] || { echo "Build the Football firmware once first: pio run -e cw-cf-0x0B" >&2; exit 1; }
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
CXX=(c++ -std=c++14 -O2 -DARDUINO=100 -w -I shim -I "$GFX")

"${CXX[@]}" -I $FB -I ../../clockfaces/cw-cf-0x08 thumb_tetris.cpp ../../clockfaces/cw-cf-0x08/Clockface.cpp \
	../../clockfaces/cw-cf-0x08/TetrisMatrixDraw.cpp $FB/GoalAnimation.cpp $FB/TeamColors.cpp "$GFX/Adafruit_GFX.cpp" -o "$WORK/tetris"
"${CXX[@]}" -I $FB -I ../../clockfaces/cw-cf-0x0B thumb_football.cpp ../../clockfaces/cw-cf-0x0B/Clockface.cpp \
	$FB/GoalAnimation.cpp $FB/TeamColors.cpp "$GFX/Adafruit_GFX.cpp" -o "$WORK/football"
"${CXX[@]}" -I $F1 -I ../../clockfaces/cw-cf-0x0C thumb_f1.cpp ../../clockfaces/cw-cf-0x0C/Clockface.cpp "$GFX/Adafruit_GFX.cpp" -o "$WORK/f1"

"$WORK/tetris" "$WORK"
"$WORK/football" "$WORK"
"$WORK/f1" "$WORK" 3 f1      # the race scenario of the face's own test screens

python3 - "$WORK" "$OUT" ../../../docs/images/faces/cw-cf-0x01.jpg <<'PYEOF'
import colorsys, sys
from PIL import Image, ImageDraw
work, out, mario = sys.argv[1:4]
S = 8

def panel(name):
    img = Image.open(work + "/" + name + ".ppm").convert("RGB")
    big = Image.new("RGB", (64 * S, 64 * S), (8, 8, 10))
    d = ImageDraw.Draw(big)
    for y in range(64):
        for x in range(64):
            c = img.getpixel((x, y))
            c = c if any(c) else (22, 22, 24)  # an LED that's off
            d.ellipse((x * S + 1, y * S + 1, x * S + S - 2, y * S + S - 2), fill=c)
    return big.resize((400, 400), Image.LANCZOS)

for name, face in (("tetris", "0x08"), ("football", "0x0B"), ("f1", "0x0C")):
    panel(name).save("%s/cw-cf-%s.jpg" % (out, face), quality=90)

# Luigi: Mario's red (cap and shirt) turned green, only around the sprite
im = Image.open(mario).convert("RGB")
px = im.load()
for y in range(240, 352):
    for x in range(135, 235):
        h, s, v = colorsys.rgb_to_hsv(*(c / 255 for c in px[x, y]))
        if (h > 0.95 or h < 0.06) and s > 0.4 and v > 0.3:
            px[x, y] = tuple(int(c * 255) for c in colorsys.hsv_to_rgb(0.34, s, v))
im.save(out + "/cw-cf-0x09.jpg", quality=88)
PYEOF
echo "Wrote pictures to $OUT"

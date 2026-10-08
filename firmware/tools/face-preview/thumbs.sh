#!/bin/bash
# Makes the clockface pictures for the web flasher (docs/images/faces) from the real firmware code:
# Tetris, Football, Formula 1, Mario, Luigi, Pacman and Pokemon are drawn by their own Clockface.cpp
# on a fake panel, at a moment that shows the face at its best (Mario and Luigi: just after the hit
# on the minute block, with the coin or the 1-UP and the plant out).
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

# The faces built on the small game engine (lib/cw-gfx-engine)
ENGINE=(../../lib/cw-gfx-engine/Sprite.cpp ../../lib/cw-gfx-engine/EventBus.cpp ../../lib/cw-gfx-engine/Locator.cpp "$GFX/Adafruit_GFX.cpp")
ENG=(c++ -std=c++14 -O2 -fno-rtti -DARDUINO=100 -DARDUINO_ESP32_DEV -w -I ../../lib/cw-gfx-engine -I shim -I "$GFX" -I ../../lib/cw-commons)
M=../../clockfaces/cw-cf-0x01
for v in mario luigi; do
	"${ENG[@]}" $([ $v = luigi ] && echo -DCW_LUIGI) -I $M thumb_mario.cpp $M/Clockface.cpp $M/gfx/block.cpp $M/gfx/mario.cpp $M/gfx/plant.cpp "${ENGINE[@]}" -o "$WORK/$v"
	mkdir "$WORK/$v-frames" && (cd "$WORK/$v-frames" && "$WORK/$v" .)
done
P=../../clockfaces/cw-cf-0x05
"${ENG[@]}" -DFACE_HEADER='"../../clockfaces/cw-cf-0x05/Clockface.h"' -I $P thumb_engine.cpp $P/Clockface.cpp $P/pacman.cpp $P/ghost.cpp "${ENGINE[@]}" -o "$WORK/pacman"
K=../../clockfaces/cw-cf-0x06
"${ENG[@]}" -DFACE_HEADER='"../../clockfaces/cw-cf-0x06/Clockface.h"' -I $K thumb_engine.cpp $K/Clockface.cpp "${ENGINE[@]}" -o "$WORK/pokemon"

"$WORK/tetris" "$WORK"
"$WORK/football" "$WORK"
"$WORK/f1" "$WORK" 3 f1      # the race scenario of the face's own test screens
mkdir "$WORK/pacman-out" "$WORK/pokemon-out"
"$WORK/pacman" "$WORK/pacman-out" 10     # seconds the face runs before the picture is taken
"$WORK/pokemon" "$WORK/pokemon-out" 5

python3 - "$WORK" "$OUT" <<'PYEOF'
import sys
from PIL import Image, ImageDraw
work, out = sys.argv[1:3]
S = 8

def panel(ppm):
    img = Image.open(ppm).convert("RGB")
    big = Image.new("RGB", (64 * S, 64 * S), (8, 8, 10))
    d = ImageDraw.Draw(big)
    for y in range(64):
        for x in range(64):
            c = img.getpixel((x, y))
            c = c if any(c) else (22, 22, 24)  # an LED that's off
            d.ellipse((x * S + 1, y * S + 1, x * S + S - 2, y * S + S - 2), fill=c)
    return big.resize((400, 400), Image.LANCZOS)

# face id: picture. Mario and Luigi are numbered frames (20 ms apart from the start of the jump):
# the coin / the 1-UP is out and the plant is up
PICTURES = {
    "0x08": "tetris.ppm",
    "0x0B": "football.ppm",
    "0x0C": "f1.ppm",
    "0x01": "mario-frames/mario_027.ppm",
    "0x09": "luigi-frames/mario_024.ppm",
    "0x05": "pacman-out/face.ppm",
    "0x06": "pokemon-out/face.ppm",
}
for face, ppm in PICTURES.items():
    panel(work + "/" + ppm).save("%s/cw-cf-%s.jpg" % (out, face), quality=90)
PYEOF
echo "Wrote pictures to $OUT"

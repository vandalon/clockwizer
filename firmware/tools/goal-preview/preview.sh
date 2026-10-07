#!/bin/bash
# Renders the goal animation of the football lib (lib/cw-football) to an
# animated GIF that looks like the 64x32 LED panel, so it can be tweaked
# without flashing an ESP.
#
#   ./preview.sh                     Feyenoord scores against Ajax
#   ./preview.sh GRE GER 0 1 0  295D FFFF 0  FFFF 4208 0  x.gif
#       home away homeScore awayScore homeScored(0/1)
#       homeShirt homeShorts homeSecond  awayShirt awayShorts awaySecond  output
#       (colours as RGB565 hex, second colour 0 = none)
#
# Needs a C++ compiler and ffmpeg, and the Tetris env built once
# (pio run -e cw-cf-0x08) so the Adafruit GFX library has been downloaded.
set -euo pipefail
cd "$(dirname "$0")"

GFX="../../.pio/libdeps/cw-cf-0x08/Adafruit GFX Library"
FACE=../../lib/cw-football
if [ ! -d "$GFX" ]; then
	echo "Build the Tetris firmware once first: pio run -e cw-cf-0x08" >&2
	exit 1
fi

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

c++ -std=c++14 -O2 -DARDUINO=100 -w -I shim -I "$GFX" -I "$FACE" \
	preview.cpp "$FACE/GoalAnimation.cpp" "$GFX/Adafruit_GFX.cpp" -o "$OUT/preview"
"$OUT/preview" "$OUT" "${1:-FEY}" "${2:-AJA}" "${3:-2}" "${4:-1}" "${5:-1}" \
	"${6:-E964}" "${7:-4208}" "${8:-FFFF}" "${9:-D8C4}" "${10:-FFFF}" "${11:-FFFF}"

GIF="${12:-goal.gif}"
(cd "$OUT" && ffmpeg -loglevel error -y -f concat -i frames.txt \
	-vf "split[a][b];[a]palettegen=reserve_transparent=0[p];[b][p]paletteuse=dither=none" \
	-fps_mode vfr -loop 0 out.gif)
cp "$OUT/out.gif" "$GIF"
echo "Wrote $GIF"

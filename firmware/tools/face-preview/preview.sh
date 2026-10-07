#!/bin/bash
# Renders the screens of the Football clockface (all simulator scenarios and the goal celebration)
# from the real firmware code, as PNG pictures that look like the 64x64 LED panel.
#
#   ./preview.sh [outdir]       default: ./out
#
# Needs a C++ compiler, Python with Pillow, and the Football firmware built once
# (pio run -e cw-cf-0x0B) so the Adafruit GFX library has been downloaded.
set -euo pipefail
cd "$(dirname "$0")"

GFX="../../.pio/libdeps/cw-cf-0x0B/Adafruit GFX Library"
FOOTBALL=../../lib/cw-football
OUT=$(cd "$(dirname "${1:-out}")" 2>/dev/null && pwd)/$(basename "${1:-out}")
[ -d "$GFX" ] || { echo "Build the Football firmware once first: pio run -e cw-cf-0x0B" >&2; exit 1; }
mkdir -p "$OUT"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

c++ -std=c++14 -O2 -DARDUINO=100 -w -I shim -I "$GFX" -I "$FOOTBALL" -I ../../clockfaces/cw-cf-0x0B \
	preview.cpp ../../clockfaces/cw-cf-0x0B/Clockface.cpp "$FOOTBALL/GoalAnimation.cpp" "$FOOTBALL/TeamColors.cpp" \
	"$GFX/Adafruit_GFX.cpp" -o "$WORK/preview"
"$WORK/preview" "$WORK"
python3 render.py "$WORK" "$OUT"
echo "Wrote pictures to $OUT"

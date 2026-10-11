#!/bin/bash
# Builds and runs the host tests for the ESPN parsers. ArduinoJson only serves as the oracle here (the firmware
# no longer needs it), so point ARDUINOJSON at any copy of its src folder.
#   ./run.sh [directory with saved feeds]     (default: ../espn-samples)
set -euo pipefail
cd "$(dirname "$0")"
AJ=${ARDUINOJSON:-$(ls -d ../../.pio/libdeps/*/ArduinoJson/src 2>/dev/null | head -1)}
[ -n "$AJ" ] || { echo "ArduinoJson not found: set ARDUINOJSON to its src folder" >&2; exit 1; }
OUT=${TMPDIR:-/tmp}/espn_parse_test
g++ -std=c++17 -O1 -Wall -Wextra -I"$AJ" -I../../lib/cw-commons -o "$OUT" espn_parse_test.cpp ../../lib/cw-football/EspnParse.cpp ../../lib/cw-commons/JsonScan.cpp
"$OUT" "${1:-../espn-samples}"

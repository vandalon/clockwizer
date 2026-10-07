#!/bin/bash
# Build clockface firmware and upload it to the update server.
#
#   ./update-fw.sh              build + upload all clockfaces
#   ./update-fw.sh 05 08        build + upload only Pacman and Tetris
#   ./update-fw.sh -i 05 08     ...and tell the panels to install their face
#   ./update-fw.sh -g           build + publish all clockfaces to GitHub Pages instead
#   ./update-fw.sh -g 0B        ...or only the Football one
#
# Panels fetch <firmware location>/cw-cf-0xNN.bin (see updateFirmware() in
# src/main.cpp); the location is a setting on the clock, by default the GitHub Pages site.
set -euo pipefail
cd "$(dirname "$0")"

SERVER=user@192.168.1.10
SERVER_DIR=/var/www/html/ledMatrix

# Which face each panel runs, used with -i
PANELS=(
	"panel1.local 5"
	"panel2.local 8"
)

# Use the PlatformIO install from the VS Code extension: Homebrew's pio lacks
# the Python modules the ESP32 platform needs.
PIO=~/.platformio/penv/bin/pio
[ -x "$PIO" ] || PIO=pio

# GitHub publishing: the files go to the gh-pages branch, which Pages serves, so
# the binaries never end up in the history of the code branches
PAGES_BRANCH=gh-pages

INSTALL=0
GITHUB=0
while [[ "${1:-}" == -* ]]; do
	case "$1" in
		-i) INSTALL=1 ;;
		-g) GITHUB=1 ;;
		*) echo "Unknown option $1" >&2; exit 1 ;;
	esac
	shift
done

if [ $GITHUB -eq 1 ]; then
	# What goes to GitHub is public: only ever build it from main, without the private extras
	branch=$(git rev-parse --abbrev-ref HEAD)
	if [ "$branch" != main ] || [ -e lib/cw-commons/local_config.h ] || [ -e lib/cw-commons/private.h ]; then
		echo "Publishing to GitHub only works from the main branch (this is '$branch')" >&2
		exit 1
	fi
fi

FACES=("$@")
[ ${#FACES[@]} -eq 0 ] && FACES=(01 02 03 04 05 06 08 09 0B 0C)

ENV_ARGS=()
for f in "${FACES[@]}"; do
	ENV_ARGS+=(-e "cw-cf-0x$f")
done

"$PIO" run "${ENV_ARGS[@]}"

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
for f in "${FACES[@]}"; do
	cp ".pio/build/cw-cf-0x$f/firmware.bin" "$STAGE/cw-cf-0x$f.bin"
	# Panels compare this with their own firmware md5 to spot new builds
	md5sum "$STAGE/cw-cf-0x$f.bin" | cut -d' ' -f1 > "$STAGE/cw-cf-0x$f.md5"
done
if [ $GITHUB -eq 1 ]; then
	REMOTE=$(git remote get-url origin)
	PAGES=$(mktemp -d)
	trap 'rm -rf "$STAGE" "$PAGES"' EXIT
	if git ls-remote --exit-code --heads origin "$PAGES_BRANCH" > /dev/null 2>&1; then
		git clone -q --depth 1 -b "$PAGES_BRANCH" "$REMOTE" "$PAGES"
	else
		git init -q "$PAGES"
		git -C "$PAGES" remote add origin "$REMOTE"
	fi
	cp "$STAGE"/* "$PAGES/"
	touch "$PAGES/.nojekyll"

	# The web flasher: the page, the list of faces and a manifest per face. A flash from scratch
	# needs the bootloader, partition table and boot_app0 as well, which all builds share.
	cp -R ../docs/. "$PAGES/"
	mkdir -p "$PAGES/flash"
	cp ".pio/build/cw-cf-0x${FACES[0]}/bootloader.bin" ".pio/build/cw-cf-0x${FACES[0]}/partitions.bin" "$PAGES/flash/"
	cp ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin "$PAGES/flash/"
	VERSION=$(sed -n 's/.*CW_FW_VERSION="\\"\([^\\]*\)\\"".*/\1/p' platformio.ini | head -1)
	python3 - "$PAGES" "$VERSION" <<'PYEOF'
import json, os, sys
pages, version = sys.argv[1:3]
for face in json.load(open(os.path.join(pages, "faces.json"))):
    name = "cw-cf-" + face["id"]
    if not os.path.exists(os.path.join(pages, name + ".bin")):
        continue
    manifest = {
        "name": "Clockwizer " + face["name"],
        "version": version,
        "builds": [{
            "chipFamily": "ESP32",
            "parts": [
                {"path": "flash/bootloader.bin", "offset": 0x1000},
                {"path": "flash/partitions.bin", "offset": 0x8000},
                {"path": "flash/boot_app0.bin", "offset": 0xE000},
                {"path": name + ".bin", "offset": 0x10000},
            ],
        }],
    }
    json.dump(manifest, open(os.path.join(pages, "manifest-" + name + ".json"), "w"), indent=2)
PYEOF
	cd "$PAGES"
	git add -A
	if git diff --cached --quiet && git rev-parse -q --verify HEAD > /dev/null; then
		echo "GitHub already has these builds"
	else
		# A single commit each time: Pages only needs the latest files, and the history would only grow
		git checkout -q --orphan publish
		git commit -q -m "Firmware $(date +%Y-%m-%d\ %H:%M)"
		git push -q --force origin "publish:$PAGES_BRANCH"
		echo "Published ${#FACES[@]} build(s) to the $PAGES_BRANCH branch"
	fi
	exit 0
fi

# On the update server itself just copy; from anywhere else use scp
if [ -d "$SERVER_DIR" ] && [ -w "$SERVER_DIR" ]; then
	cp "$STAGE"/* "$SERVER_DIR/"
else
	scp "$STAGE"/* "$SERVER:$SERVER_DIR/"
fi

if [ $INSTALL -eq 1 ]; then
	for p in "${PANELS[@]}"; do
		read -r host face <<< "$p"
		if [[ " ${FACES[*]} " == *" 0$face "* ]]; then
			echo "Telling $host to install cw-cf-0x0$face"
			echo "$face" | nc -w5 "$host" 23 || echo "  $host did not respond"
		fi
	done
fi

#!/bin/bash
# Build clockface firmware and upload it to the update server.
#
#   ./update-fw.sh              build + upload all clockfaces
#   ./update-fw.sh 05 08        build + upload only Pacman and Tetris
#   ./update-fw.sh -i 05 08     ...and tell every clock on the network that runs one of them to install it
#   ./update-fw.sh -H 192.168.1.50 05   ...the same for one clock by address (repeat -H for more; also
#                               works when mDNS doesn't reach the clock)
#   ./update-fw.sh -n -i 05     ...only list the clocks that would be told, don't touch them
#   ./update-fw.sh -g           build + publish all clockfaces to GitHub Pages instead
#   ./update-fw.sh -g 0B        ...or only the Football one
#   ./update-fw.sh -h           this help
#
# Faces are named by the last two characters of their id (05 for 0x05); none named means all
# of docs/faces.json. -i needs avahi-browse (avahi-utils) to find clocks; -H does not.
#
# Panels fetch <firmware location>/cw-cf-0xNN.bin (see updateFirmware() in
# src/main.cpp); the location is a setting on the clock, by default the GitHub Pages site.
set -euo pipefail
cd "$(dirname "$0")"

# The comment block at the top of this file is the help text
usage() {
	sed -n '2,/^set /{/^set /d;s/^# \{0,1\}//;p;}' "$0"
}

SERVER=user@192.168.1.10
SERVER_DIR=/var/www/html/ledMatrix

# Use the PlatformIO install from the VS Code extension: Homebrew's pio lacks
# the Python modules the ESP32 platform needs.
PIO=~/.platformio/penv/bin/pio
[ -x "$PIO" ] || PIO=pio

# GitHub publishing: the files go to the gh-pages branch, which Pages serves, so
# the binaries never end up in the history of the code branches
PAGES_BRANCH=gh-pages

INSTALL=0
DISCOVER=0
DRY=0
HOSTS=()
GITHUB=0
while [[ "${1:-}" == -* ]]; do
	case "$1" in
		-i) INSTALL=1; DISCOVER=1 ;;
		-H) [ $# -ge 2 ] || { echo "-H needs an address" >&2; exit 1; }
			HOSTS+=("$2"); INSTALL=1; shift ;;
		-n) DRY=1 ;;
		-h|--help) usage; exit 0 ;;
		-g) GITHUB=1 ;;
		*) echo "Unknown option $1 (try -h)" >&2; exit 1 ;;
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
# No faces named: all of them, in the order of the list of faces (docs/faces.json: 0x0B becomes 0B)
if [ ${#FACES[@]} -eq 0 ]; then
	FACES=($(python3 -c 'import json; print(" ".join(f["id"][2:] for f in json.load(open("../docs/faces.json"))))'))
fi

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
	# Clocks announce themselves as _clockwise._tcp; the address is column 8 of avahi's parsable output
	if [ $DISCOVER -eq 1 ]; then
		if command -v avahi-browse > /dev/null; then
			while read -r addr; do
				[ -n "$addr" ] && HOSTS+=("$addr")
			done < <(avahi-browse -rtp _clockwise._tcp | awk -F';' '$1 == "=" && $3 == "IPv4" { print $8 }')
		else
			echo "avahi-browse not found (apt install avahi-utils): name the clocks with -H instead" >&2
		fi
	fi
	[ ${#HOSTS[@]} -gt 0 ] || echo "No clocks to tell"

	for host in $(printf '%s\n' ${HOSTS[@]+"${HOSTS[@]}"} | sort -u); do
		# A clock names its face in a response header of /get ("X-CW_FW_ID: 0x05")
		id=$(curl -fsS -m 5 -D - -o /dev/null "http://$host/get" 2> /dev/null | tr -d '\r' | sed -n 's/^X-CW_FW_ID: 0x//p' || true)
		if [ -z "$id" ]; then
			echo "  $host did not answer"
		elif [[ " ${FACES[*]} " != *" $id "* ]]; then
			echo "  $host runs 0x$id, not built now: skipped"
		elif [ $DRY -eq 1 ]; then
			echo "  $host runs 0x$id: would be told to install it"
		else
			echo "Telling $host to install cw-cf-0x$id"
			curl -fsS -m 5 -X POST "http://$host/face?id=0x$id" || echo "  $host did not respond"
		fi
	done
fi

#!/usr/bin/env bash
# Sets up what is needed to build the firmware on a clean Linux or macOS machine.
# It checks that git, Python 3.9+ with venv and pip, and enough free disk space are there, then
# installs PlatformIO 6.2+ into its own virtualenv and links `pio` into ~/.local/bin.
# It installs no system packages itself (no sudo): if something is missing it says what, which
# version, and the command for your distro to install it. Safe to run again.
#
#   ./setup.sh
set -euo pipefail
cd "$(dirname "$0")"

MIN_PY="3.9"
MIN_PIO="6.2.0"
MIN_FREE_GB=8
VENV=${CW_VENV:-$HOME/.local/share/clockwizer/pio-venv}
BIN=$HOME/.local/bin
CORE_DIR=${PLATFORMIO_CORE_DIR:-$HOME/.platformio}

fail() { echo "ERROR: $*" >&2; exit 1; }

# Group that owns serial ports (Arch and derivatives use uucp), and the install command to suggest
SERIAL_GROUP=dialout
if [ "$(uname)" = Darwin ]; then
	PKG_CMD="brew install git python"
else
	os_id=$(. /etc/os-release 2> /dev/null && echo "${ID:-} ${ID_LIKE:-}")
	case " $os_id " in
		*" arch "* | *" manjaro "*)
			PKG_CMD="sudo pacman -S --needed git python python-pip"
			SERIAL_GROUP=uucp ;;
		*" fedora "* | *" rhel "* | *" centos "*)
			PKG_CMD="sudo dnf install -y git python3 python3-pip" ;;
		*" suse "* | *" opensuse"*)
			PKG_CMD="sudo zypper install -y git python3 python3-pip" ;;
		*" alpine "*)
			PKG_CMD="sudo apk add bash git python3 py3-pip" ;;
		*" debian "* | *" ubuntu "*)
			PKG_CMD="sudo apt update && sudo apt install -y git python3 python3-venv python3-pip" ;;
		*)
			PKG_CMD="" ;;
	esac
fi
# What to append to a "missing" message
hint() {
	if [ -n "$PKG_CMD" ]; then echo "Install it with: $PKG_CMD"; else echo "Install it with your package manager."; fi
}

command -v git > /dev/null || fail "git is missing. $(hint)"

# Newest python3 on PATH that is new enough
PY=
found=
for c in python3.13 python3.12 python3.11 python3.10 python3.9 python3; do
	command -v "$c" > /dev/null || continue
	found=$c
	"$c" -c "import sys; sys.exit(sys.version_info < tuple(map(int, '$MIN_PY'.split('.'))))" 2> /dev/null || continue
	PY=$c
	break
done
[ -n "$found" ] || fail "python3 is missing (Python $MIN_PY or newer is needed). $(hint)"
[ -n "$PY" ] || fail "Python $MIN_PY or newer is needed, but the newest found is $($found --version 2>&1). $(hint)"
"$PY" -c "import venv" 2> /dev/null || fail "$PY is missing the venv module (a separate package on some distros, e.g. python3-venv). $(hint)"
"$PY" -c "import ensurepip" 2> /dev/null || fail "$PY is missing pip/ensurepip (a separate package on some distros, e.g. python3-pip). $(hint)"
echo "Python:   $($PY --version) ($(command -v "$PY"))"

# The first build unpacks the framework (about 3.5 GB) next to a temporary copy of it
mkdir -p "$(dirname "$CORE_DIR")"
free_gb=$(df -Pk "$(dirname "$CORE_DIR")" | awk 'NR==2 {print int($4/1024/1024)}')
[ "$free_gb" -ge "$MIN_FREE_GB" ] || fail "Only ${free_gb} GB free where PlatformIO keeps its files ($CORE_DIR); the first build needs about $MIN_FREE_GB GB"
echo "Disk:     ${free_gb} GB free for $CORE_DIR"

# PlatformIO in its own virtualenv: apt's Python refuses a plain pip install, and this
# keeps it away from any other PlatformIO (the VS Code one, ~/.platformio)
if [ ! -x "$VENV/bin/pio" ]; then
	echo "Creating $VENV"
	"$PY" -m venv "$VENV"
fi
"$VENV/bin/python" -m pip install --quiet --upgrade pip "platformio>=$MIN_PIO"
echo "PlatformIO: $("$VENV/bin/pio" --version)"

mkdir -p "$BIN"
ln -sf "$VENV/bin/pio" "$BIN/pio"
case ":$PATH:" in
	*":$BIN:"*) ;;
	*) echo "NOTE: $BIN is not on your PATH. Add this to your shell profile:  export PATH=\"$BIN:\$PATH\"" ;;
esac

# 6.2+ needs a pioarduino platform that pins SCons 4.11.1 (55.03.312 or newer)
if grep -qE '^platform *=.*/55\.03\.31[01]/' platformio.ini; then
	echo "WARNING: platformio.ini pins pioarduino 55.03.311 or older: with PlatformIO $MIN_PIO+ every build fails" >&2
	echo "         with \"No module named 'SCons.Tool.FortranCommon'\". Use 55.03.312-1 or newer." >&2
fi

if [ "$(uname)" = Linux ] && ! id -nG | grep -qw "$SERIAL_GROUP"; then
	echo "NOTE: to flash over USB add yourself to the $SERIAL_GROUP group: sudo usermod -aG $SERIAL_GROUP \$USER (then log out and in)"
fi

echo
echo "Done. Build a clockface (the first build downloads several GB and takes a few minutes):"
echo "  pio run -e cw-cf-0x0C"

#!/usr/bin/env python3
# Puts together the folder GitHub Pages serves, the same layout firmware/update-fw.sh -g publishes:
#   cw-cf-0xNN.bin and .md5   what the clocks download (they compare the md5 with their own, the default
#                             firmware location is https://vandalon.github.io/clockwizer/)
#   manifest-cw-cf-0xNN.json, flash/*.bin, index.html, faces.json, images/, vendor/   the web flasher
#   .nojekyll
#
#   python3 tools/make_pages.py <builds> <pages>
#
# <builds> holds one folder per face (cw-cf-0xNN) with firmware.bin, bootloader.bin, partitions.bin and
# boot_app0.bin; <pages> is the folder to fill (an existing checkout of the gh-pages branch works: files are replaced).
import hashlib
import json
import os
import re
import shutil
import sys

here = os.path.dirname(os.path.abspath(__file__))
firmware = os.path.dirname(here)
docs = os.path.join(os.path.dirname(firmware), "docs")

builds, pages = sys.argv[1:3]
faces = json.load(open(os.path.join(docs, "faces.json")))
version = re.search(r'CW_FW_VERSION="\\"([^\\]*)\\"', open(os.path.join(firmware, "platformio.ini")).read()).group(1)

shutil.copytree(docs, pages, dirs_exist_ok=True)
open(os.path.join(pages, ".nojekyll"), "w").close()

built = []
for face in faces:
    name = "cw-cf-" + face["id"]
    firmware_bin = os.path.join(builds, name, "firmware.bin")
    if not os.path.exists(firmware_bin):
        continue
    shutil.copy(firmware_bin, os.path.join(pages, name + ".bin"))
    open(os.path.join(pages, name + ".md5"), "w").write(hashlib.md5(open(firmware_bin, "rb").read()).hexdigest() + "\n")
    built.append((face, name))
if not built:
    sys.exit("no builds found in " + builds)

# A flash from scratch needs the bootloader, partition table and boot_app0 as well, which all builds share
first = os.path.join(builds, built[0][1])
os.makedirs(os.path.join(pages, "flash"), exist_ok=True)
for part in ("bootloader.bin", "partitions.bin", "boot_app0.bin"):
    shutil.copy(os.path.join(first, part), os.path.join(pages, "flash", part))

for face, name in built:
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
print("%d builds, version %s" % (len(built), version))

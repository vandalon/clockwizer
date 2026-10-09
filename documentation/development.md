# Development

## Repository layout

```
.
├── firmware/                 the PlatformIO project
│   ├── src/main.cpp          start-up, WiFi, the main loop, updates, telnet commands
│   ├── clockfaces/cw-cf-0xNN one folder per clockface
│   ├── lib/
│   │   ├── cw-commons/       settings, web server, WiFi, status screens, fonts, the settings page
│   │   ├── cw-gfx-engine/    small event bus and locator used by the faces
│   │   ├── cw-football/      football data, goal animation, team colours (Football, Tetris)
│   │   ├── cw-f1/            Formula 1 data (Formula 1, Tetris)
│   │   └── improv-wifi/      WiFi setup over USB (Improv serial protocol)
│   ├── tools/                preview tools, the page packer, the football catalogue generator
│   ├── test/                 unit tests
│   ├── platformio.ini        one environment per clockface
│   └── update-fw.sh          build and publish
├── docs/                     the web flasher (published to GitHub Pages)
├── documentation/            these pages
├── base_firmware/            a copy of the partition table (reference)
└── 3d-files/                 printable frame parts
```

Settings (`CWPreferences.h`) are stored in the ESP32's flash. The web server (`CWWebServer.h`) is a plain `WiFiServer` with a hand-written request router.

## Branches

- **`main`** is the public version. Do clockface and firmware work here.
- A private branch can sit on top of `main` and add things that must not be public (personal pictures, a home server address). It is rebased by merging `main` into it, and it is never pushed. The files it adds are `firmware/lib/cw-commons/private.h` and `local_config.h`; both are picked up automatically when present and the publish script refuses to publish a build that contains them.

## Building and flashing

Install [PlatformIO](https://platformio.org) (the VS Code extension or the command line). The toolchain and libraries are downloaded on the first build; everything is pinned, so a fresh checkout builds the same firmware. A first build takes several minutes on a small computer such as a Raspberry Pi.

```bash
cd firmware
pio run -e cw-cf-0x05                  # build Pacman
pio run -e cw-cf-0x05 -t upload        # build and flash over USB
pio device monitor                     # serial log
```

`pio run` without `-e` builds every face. The result is `.pio/build/<env>/firmware.bin`, which is also what the update server hosts.

### Partition table

The firmware uses the `min_spiffs` layout (two OTA slots of about 1.9 MB). The partition table can only be changed with a full flash over USB, not over WiFi. Changing it means every clock needs a one-time USB flash, so avoid it. `base_firmware/partitions.bin` is a compiled copy of the table.

### Libraries from GitHub

`platformio.ini` pins a few libraries to a commit on GitHub (WiFiManager, TelnetStream, the HUB75 driver). If one of those repositories ever disappears, a fresh build can't fetch it; keep a fork if that worries you.

## The settings page

The page is `firmware/lib/cw-commons/settings.html`: plain HTML, CSS and JavaScript in one file, no build tooling. A PlatformIO pre-build step (`tools/gzip_page.py`) compresses it into `SettingsWebPage.h`, which the web server sends. **Edit the `.html`, never the header.** The generated header is not checked in (it is in `.gitignore`); every build rewrites it when the page has changed.

Adding a setting takes five places:
1. `CWPreferences.h`: a `PREF_` name, a field, and the lines in `save()` and `load()`
2. `CWWebServer.h`: a branch in the `/set` handler (validate the value) and a line in the `/get` header list
3. `settings.html`: the control, reading in `load()`, the value in `current()`, the compare and the `set(...)` call in `save()`
4. the [web UI page](web-ui.md) and the [API table](control-api.md)

## Adding a clockface

1. Copy a small face (for example `clockfaces/cw-cf-0x04`) to `clockfaces/cw-cf-0xNN`. A face is a class `Clockface` with `setup(CWDateTime *)` and `update()` (see `IClockface.h`) that draws on the shared `Adafruit_GFX` display. Keep a `library.json` like the other faces.
2. Add an environment to `platformio.ini`:
   ```ini
   [env:cw-cf-0xNN]
   extends = clockwise
   lib_deps = ${clockwise.lib_deps}
   	symlink://clockfaces/cw-cf-0xNN
   build_flags = ${clockwise.build_flags}
   	-D CW_FW_NAME="\"My face\""
   	-D CW_FW_ID="\"0xNN\""
   ```
   Add `-D DOUBLE_BUFFER_ON` for smooth animation, `-D CLOCKFACE_UPDATE_MS=50` to be called more often than the default.
3. Make the clock know it: add one entry to `docs/faces.json` (see below). The settings page, the `/face` check, the telnet keys and the default list of `update-fw.sh` all follow from it. Put a photo of the panel in `docs/images/faces/` as the `thumb`, and run `python3 firmware/tools/make_face_minis.py` (needs Pillow and numpy) to give it its little picture on the settings page.
4. Document it in [clockfaces.md](clockfaces.md) and the table in the README.

## Tests

```bash
cd firmware
pio test -e native          # runs on your computer
```

There is only a placeholder test so far (`test/test_native`). Most checking is done on a real panel.

## Preview tools

`firmware/tools/` has a few ways to see a screen without flashing a clock; they compile parts of the real firmware against a tiny Arduino shim on your computer:

- `goal-preview/preview.sh` renders the football goal celebration to an animated GIF (needs a C++ compiler and `ffmpeg`, and the Tetris environment built once so the Adafruit GFX library is present).
- `face-preview/thumbs.sh` renders Football, Formula 1, Tetris, Mario, Luigi, Pacman and Pokemon on a fake panel from the real `Clockface.cpp` files and writes the pictures the web flasher shows (`docs/images/faces`). The moment it shows (a live match, a race, the coin popping out of Mario's block) is chosen in the small `thumb_*.cpp` programs and in the `PICTURES` table at the end of the script. Needs a C++ compiler and Pillow, and the Football environment built once. Castlevania, World Map and Time in Words still use the older photos from upstream.
- `face-preview/preview.sh` is an older tool for the Football screens that depended on a simulator which has since been removed; use `thumbs.sh` as the starting point for a new preview instead.
- `football-catalog/generate.py` regenerates `FootballCatalog.h`, the list of competitions and teams offered on the settings page, from ESPN.

## The web flasher

`docs/` is the source of the flasher page that `update-fw.sh -g` copies to the `gh-pages` branch:

- `index.html` is the page; it reads `faces.json` for the list of faces and uses [ESP Web Tools](https://github.com/esphome/esp-web-tools) from `vendor/esp-web-tools` (copied unchanged, Apache-2.0, no CDN).
- `faces.json` is the one list of faces, for the page, the build and the clock itself: `id`, `name`, `panel` ("64x64", or "64x64 and 64x32" for a face that is drawn on both), `key` (the telnet key that installs it: one character, not one of `R C X L U P T h S N G Y D W`), optional `thumb` and the `icon` that `tools/make_face_minis.py` makes from it. Add a face here to put it on the page and on the clock.
- At build time `tools/gen_faces.py` turns it into `lib/cw-commons/Faces.h`, which the clock uses for the face picker (`GET /faces`), the `/face` check and the telnet keys. The build stops when an id or key is wrong or used twice. `update-fw.sh` builds the faces of the list when you don't name any.
- For every published face the script writes `manifest-cw-cf-0xNN.json`. The manifest lists four files to write to the ESP32's flash: the bootloader at `0x1000`, the partition table at `0x8000`, `boot_app0` at `0xE000` and the app at `0x10000`. The first three are shared and come from the last build (`flash/` on the site).
- Browsers only allow WebSerial on HTTPS, which GitHub Pages provides. For local testing, serve `docs/` with `python3 -m http.server` and open it on `localhost` (also allowed); copy a `manifest-*.json`, the `flash/` folder and the `.bin` files into it first.

## Publishing a release

1. Merge your work into `main` and commit.
2. `cd firmware && ./update-fw.sh -g` (see [updating-firmware.md](updating-firmware.md)). Clocks on the default firmware location pick the new build up within the hour.
3. Push `main`.

The version number in the firmware (`CW_FW_VERSION` in `platformio.ini`) is shown on the settings page and in the flasher; clocks decide whether to update by the md5 of the build, not by the version.

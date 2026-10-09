# Updating firmware

Clocks update themselves over WiFi. They **pull**: a clock asks a web server whether a newer build exists and downloads it. Nothing is pushed to a clock.

## How a clock updates

1. Once a day (and when you press *Check for update*, or send `C` over telnet) the clock downloads a tiny file `cw-cf-0xNN.md5` from its **firmware location** (`NN` is the clockface it runs). The file holds the md5 of the newest build of that clockface.
2. It compares that with the md5 of the firmware it is running. Same: nothing to do, the web page says "up to date".
3. Different: the clock downloads `cw-cf-0xNN.bin`, shows `UPDATING...` with a progress bar, installs it and restarts.

For each file the clock first tries a folder with its own MAC address (`<location>/AA:BB:CC:DD:EE:FF/cw-cf-0xNN.md5`) and, if that doesn't exist, the shared one (`<location>/cw-cf-0xNN.md5`). That lets you give one particular clock a different build, for testing, while the rest follow the shared build.

The same download is used when you pick another clockface on the settings page or send a face number over telnet: the clock fetches `cw-cf-0xNN.bin` for the face you chose.

### Quiet hours

The daily check doesn't run between the *Pause automatic updates at night* hours (default 22:00 to 08:00). A check that came due in the quiet hours runs when they end. Checking by hand, and picking a clockface, always work.

### What if an update goes wrong

- **Download fails:** the clock keeps running the old firmware and retries at the next check. After three failed downloads of the same build it stops trying that build until a different one appears. (Telnet `X` forces another try.)
- **Out of memory:** a check that runs out of memory for the secure connection restarts the clock once and checks again right after the restart, when memory is free. The settings page follows it through the restart.
- **Crash loop:** a build that crashes (panic or watchdog) three times without staying up for 10 minutes in between is rolled back to the previous firmware, and the clock then leaves that build alone until a different one appears.
- **The new firmware doesn't work:** a freshly installed build is "pending". It has to reach the update server (fetch the md5) within five minutes of getting on WiFi, otherwise the clock restarts into the previous firmware. The build that was rolled back isn't tried again until a newer one is published (telnet `X` forces it).
- **Telnet `R` (restart) or `P` (switch panel height) while a build is pending** counts as a deliberate restart and confirms the new build.

A complete failure to boot still needs a USB flash, for instance with the [web flasher](https://vandalon.github.io/clockwizer/).

## The firmware location

The location is a web folder, written with the trailing slash. It is a setting on each clock (*Updates → Firmware location* on the settings page); empty means the default.

| Where | Default location |
|-------|------------------|
| Public builds | `https://vandalon.github.io/clockwizer/` |

HTTP and HTTPS both work, redirects are followed. HTTPS downloads **are not certificate-checked** (the clock doesn't carry root certificates for this), so the md5 protects against a corrupt download, not against someone controlling your network or the server. Host your own copy on a server you trust if that matters to you.

### Hosting your own builds

Any web server works. Put these files in a folder:

```
cw-cf-0x05.bin    the firmware (build output .pio/build/cw-cf-0x05/firmware.bin)
cw-cf-0x05.md5    its md5 as 32 hex characters
```

and type the folder's URL in the settings page. `update-fw.sh` does exactly this, see below.

A build can also set a different default location at compile time: define `CW_DEFAULT_FW_URL` in a header called `local_config.h` in `firmware/lib/cw-commons/`. It is included automatically when present and is not part of the public repository (the author's private branch uses it for a home server).

## Publishing with `update-fw.sh`

`firmware/update-fw.sh` builds clockfaces with PlatformIO and publishes them.

```
./update-fw.sh                # build all faces, copy them to your own update server
./update-fw.sh 05 08          # only Pacman and Tetris
./update-fw.sh -i 05 08       # ...and tell every clock on the network that runs one of them to install it
./update-fw.sh -H 192.168.1.50 05   # ...or one clock by address (repeat -H for more)
./update-fw.sh -n -i 05       # only list the clocks that would be told
./update-fw.sh -g             # build all faces and publish them to GitHub Pages
./update-fw.sh -g 0B          # only Football
```

For your own server, edit the top of the script: `SERVER` (login for `scp`), `SERVER_DIR` (the folder on that server; if it exists locally and is writable the files are simply copied) .

`-i` finds the clocks with mDNS (they announce `_clockwise._tcp`; it needs `avahi-browse`, from the `avahi-utils` package), asks each one which face it runs (`GET /get`, the `X-CW_FW_ID` header) and tells it to install that face with `POST /face` if you just built it. Use `-H <address>` for a clock mDNS can't reach; `-H` can be given more than once and also works without `-i`.

### Publishing to GitHub Pages (`-g`)

The public builds live on the `gh-pages` branch of the repository, which GitHub Pages serves at `https://<user>.github.io/<repo>/`. `-g`:

1. refuses to run unless you are on `main` and there is no `local_config.h` or `private.h`, so that a private build can never end up on a public site;
2. builds the faces you asked for;
3. clones the `gh-pages` branch, adds the new `.bin` and `.md5` files, the [web flasher](development.md#the-web-flasher) (page, face list, libraries) and one flasher manifest per face;
4. commits that as a **single commit** and force-pushes it to `gh-pages`. Pages only needs the newest files, and this keeps the repository from growing with every build.

One-time setup in the GitHub repository: *Settings → Pages → Deploy from a branch → `gh-pages` / root*, and in your clock settings leave the firmware location on the default (or put your Pages URL in).

Pages caches files for a few minutes, so a fresh publish can take a little while to reach the clocks.

## Moving an existing clock to this firmware

Older Clockwise installations have a different partition table. This firmware uses the `min_spiffs` layout (two update slots of about 1.9 MB). The partition table cannot be changed over WiFi, so the first install has to be a full flash over USB. Use the [web flasher](https://vandalon.github.io/clockwizer/), which writes the whole flash. After that, updates and clockface changes work over WiFi.

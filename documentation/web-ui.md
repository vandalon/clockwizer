# The web settings page

Every clock serves its own settings page. Open `http://clockwizer.local`, the clock's IP address, or scan the QR code the clock shows at startup. The page works on a phone and a computer, and can be added to a phone's home screen.

**Changes are saved by themselves**, about half a second after you stop touching a control. A short "Saved" message confirms it. Only two settings need a restart, the timezone and the 24-hour switch; a bar appears at the bottom with a *Restart now* button when you change them.

The page only lists what applies to the clock in front of you: a section for another clockface's options stays hidden.

At the top, the page shows the name of the clock, the clockface, the firmware version and the WiFi network it is on.

## Clockface

A drop-down with the clockfaces. Choosing one installs it: the clock downloads that face from its [firmware location](updating-firmware.md), shows `UPDATING...` with a progress bar, installs it and restarts. This takes about a minute and needs the internet (or your update server). A 64x32 panel only lists the faces that fit it.

If the download fails the clock keeps running what it had and shows `UPDATE FAILED`.

## Face-specific sections

These sections appear only while the matching clockface is installed. See [clockfaces.md](clockfaces.md) for what the faces do.

### Formula 1 (Tetris on a 64x32 panel)

*Show Formula 1*: while an F1 session is on, its top three is one of the pages in the ticker row.

### Pacman colours (Pacman)

Colours for the first ghost, the second ghost and the dots, each from a list of pastel and basic colours. *Default* keeps the colours the clock picks itself. *Border* sets the colour of the maze walls, and also of the clock at night (see *Night mode* below). Ghosts and dots change immediately.

### Football (Football, and Tetris on 64x32)

- **Rolling ball** (Football): on the main screen the ball is passed along the grass at the bottom, or it rests there.
- **Clock digits change by** (Football): flipping cards, fading, rolling up, dissolving, drifting up, or fading with a shimmer (a soft highlight that sweeps over the digits all the time).
- **Next live match every**: with several matches on at once, the clock shows one at a time. This is how long each stays (4 to 30 seconds). Lists of results slide on after the same time.
- **Show a finished match for**: how long a result stays on screen (30 minutes to 24 hours, or until midnight).
- **Competitions to follow**: pick leagues and cups. They are grouped as *Europe* (Champions League, Europa League, Conference League), *National teams* (World Cup, European Championship, Nations League and other international tournaments), and then by country, with each country's leagues and cups in its own group. Up to 10 at a time.
- **Favourite teams**: type a club or a country to search and tap it to add it; up to 8. Clubs and national teams both work. A favourite's live matches stay at the top of the live feed even when other matches are on, and they are followed even if their competition isn't ticked above.

The match data come from ESPN's public scoreboard feed. The clock polls it in the background; if the internet is down it shows the clock only.

## Other clocks on your network

Shown when the clock has found other Clockwizer clocks. They announce themselves over mDNS (`_clockwise._tcp`) and are sorted by name. Each entry shows the clock's name, its clockface and IP address; tap it to open that clock's settings page. A clock is only dropped from the list after it has been missing for about a day.

## Display

- **Brightness**: the fixed brightness, 2 to 100 %. The clock never goes fully dark.
- **Auto brightness**: lets a light sensor (LDR) set the brightness. Needs an LDR on the ESP32 (see [Getting started](getting-started.md#5-optional-a-light-sensor)). When switched on you get:
  - *Display brightness*: two handles, the dimmest level (dark room) and the brightest level (bright room).
  - *Room light*: a live bar with the light level the sensor sees right now, and two sliders, *Dark* and *Bright*, for the sensor readings you want to count as "dark room" and "bright room". Look at the live value in your dark and bright situations and set the sliders around them.

Auto brightness also drives night mode (below).

## Time

- **24-hour clock**: off shows `8:00PM` instead of `20:00`. Needs a restart.
- **Timezone**: pick yours from the list, or press *Use my phone's timezone*. Needs a restart. The clock gets the time from an NTP server; the default is `time.cloudflare.com`, see [advanced settings](control-api.md#advanced-settings).

## Updates

- **Pause automatic updates at night**: the clock checks for a new version once a day. With this on it stays quiet between the two hours you choose (default 22:00 until 08:00), so a restart doesn't wake anyone. Checking by hand always works. If the clock has no time yet it can't tell the hour and doesn't pause.
- **Firmware location**: the web folder the clock downloads firmware from. The default is the build published with this project. Clear the field to go back to the default. See [updating-firmware.md](updating-firmware.md).

## Clock

- **Name**: what you call this clock (24 characters). It is shown in the page title and in the lists of other clocks.
- **QR code at startup**: show or hide the code at boot.
- **Who can open this page**: *only devices on this network* (default), *any private network* (10.x, 172.16-31.x, 192.168.x) or *anyone who can reach the clock*. The page has no password, so leave it on the default unless the clock sits behind something you trust. Devices on the clock's own network, including its setup network, are always allowed.
- **Check for update**: looks for a new build right now. The status shows what happened (checking, up to date, installing).
- **WiFi setup**: restarts the clock into the `Clockwizer-Wifi` setup network so you can choose another network. Your current network stays saved until you pick a new one.
- **Reboot**: restarts the clock.
- **Reset to factory settings**: erases the settings and the WiFi network after a confirmation. The settings tied to your particular panel (colour order, rotation, light-sensor pin and panel height) are kept.

## Night mode

When auto brightness sees a dark room (below the dark level), the clock switches to a plain, dim clock in the accent colour instead of the clockface. The accent colour is the *Border* colour on the Pacman section (red by default).

## Things the page doesn't have

The wiring-related options (colour order, rotation, panel height, sensor pin), the NTP server and a manual POSIX timezone string have no control on the page. Set them as described in [control-api.md](control-api.md#advanced-settings).

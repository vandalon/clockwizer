# Troubleshooting

## The clock doesn't connect to WiFi

- Only **2.4 GHz** networks work. A router that merges 2.4 and 5 GHz under one name usually works, but if setup fails, create a separate 2.4 GHz network name.
- Check the password. WiFi setup stays open for five minutes and then the clock gives up until the next restart.
- Start the setup again: *Clock → WiFi setup* on the settings page (if you can still reach it), or power the clock off and on with no network available. You can also re-run the WiFi step of the [web flasher](https://vandalon.github.io/clockwizer/) over USB.
- The display shows a QR code (for the `CW-SETUP` network) when it waits for you in setup mode. Join that network from your phone and a setup page opens. The display runs in a faster mode meanwhile so the code is easier to scan, and the clock restarts into the normal mode once it has joined your network.

## I can't open the settings page

- Try the IP address instead of `clockwizer.local`. Windows without mDNS support and some routers don't resolve `.local` names. Your router's list of devices shows the IP, and the clock prints it on its serial log at start-up (`pio device monitor`).
- Your phone or computer must be on the same network as the clock. By default the page refuses devices from other networks (*Who can open this page*). A VPN or a guest network counts as another network.
- Another clock's page lists this clock under *Other clocks on your network*, with its IP, if it is on the same network.

## The time is wrong or missing

- The clock sets the time over NTP, so it needs internet access. Without a synced clock it shows nothing useful until it gets one.
- Check the timezone in *Time*; changing it needs a restart (the bar at the bottom offers it).
- Your network may block NTP. Set another server with the `ntpServer` setting, see [control-api.md](control-api.md#advanced-settings).
- 24-hour versus AM/PM is a separate switch, also needing a restart.

## The colours are wrong

Some panels wire red, green and blue in another order. Set `swapBlueGreen=1`, see [control-api.md](control-api.md#advanced-settings), and restart.

## The image is rotated or upside down

Set `displayRotation` to 1, 2 or 3 (quarter turns) and restart.

## Only half of the screen is used, or the picture is squashed

The clock must know the number of rows of the panel (64 or 32). The Tetris face supports both; all other faces need 64. Send `P` over telnet to flip the height, or set `displayHeight`.

## The display is too bright or too dark

- *Display → Brightness* sets the brightness; with *Auto brightness* on, the light sensor decides.
- With auto brightness on, look at *Light in the room now* on the settings page in the dark and in the light, and put the *Dark* and *Bright* sliders around those readings.
- If the sensor doesn't move at all, check the wiring and `ldrPin` (default 35). Send `L` over telnet to log the sensor values.
- A dark room can also switch the clock into a plain dim clock (night mode); that's below the *Dark* level.

## Football or Formula 1 shows no matches

- The data come from ESPN and Jolpica over the internet. The clock only shows the clock when it can't reach them.
- Check *Competitions to follow* and *Favourite teams* on the settings page; with nothing selected there is nothing to show.
- Only matches ESPN lists for today are shown.
- *Show a finished match for* may be set low; results disappear after that time.

## Updates

- **"Check for update" says no server:** the clock couldn't read `cw-cf-0xNN.md5` from its firmware location. Open `<firmware location>/cw-cf-0xNN.md5` in a browser (replace `NN` with your face's id, as shown by the face list). It must show 32 hex characters. Check the location in *Updates → Firmware location*.
- **It says the update was skipped:** this exact build failed before (it was rolled back, or the download failed three times). Publish a new build, or send `X` over telnet to try again.
- **Nothing happens at night:** the daily check pauses in the quiet hours.
- **The clock keeps rolling back:** a new firmware has to reach the update server within five minutes of getting on WiFi. If your server is slow or blocked after a restart, that fails. Check the telnet log for the `[Update]` lines.
- **GitHub Pages serves an old file:** Pages caches for a few minutes. Wait and check again.

More about the mechanism in [updating-firmware.md](updating-firmware.md).

## The clock restarts by itself

Send `U` over telnet right after: it prints why the last reset happened (power on, software, crash, watchdog or brownout) and the free memory. A brownout almost always means the panel and ESP32 share a weak power supply or a thin cable. Use a 3 A or stronger supply and short, thick power leads.

## Factory reset

*Clock → Reset to factory settings* on the settings page, or `POST /cmd?c=reset`. It erases the settings and the WiFi network but keeps the panel wiring options (colour order, rotation, sensor pin, panel height). A full reflash over USB with the [web flasher](https://vandalon.github.io/clockwizer/) fixes a clock whose firmware is broken.

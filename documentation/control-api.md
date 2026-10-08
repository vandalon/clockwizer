# Controlling a clock from a script or a terminal

Besides the settings page, a clock has a small HTTP interface (the page itself uses it) and a telnet console. Both are meant for a trusted home network: neither has a password. Who may use the HTTP interface follows the *Who can open this page* setting; telnet is not limited by it.

Replace `clockwizer.local` with the clock's IP address if your computer can't resolve it.

## HTTP interface (port 80)

| Request | What it does |
|---------|--------------|
| `GET /` | The settings page (gzipped HTML) |
| `GET /get` | The current settings, as response headers named `X-<setting>`, plus `X-updateStatus`, `X-CW_FW_VERSION`, `X-CW_FW_NAME`, `X-CLOCKFACE_NAME` and `X-CW_FW_ID` |
| `POST /set?<setting>=<value>` | Changes one setting and saves it |
| `POST /face?id=0x05` | Installs a clockface: `0x0B 0x0C 0x08 0x01 0x09 0x05 0x06 0x04 0x03 0x02` |
| `POST /cmd?c=update` | Checks for a firmware update now |
| `POST /cmd?c=reboot` | Restarts the clock |
| `POST /cmd?c=wifi` | Restarts into the `CW-SETUP` setup |
| `POST /cmd?c=reset` | Factory reset (erases settings and WiFi) |
| `POST /restart` | Restarts the clock |
| `GET /read?pin=35` | Reads an analog pin, used by the light-sensor display on the page |
| `GET /peers` | Other clocks found on the network, as JSON |
| `GET /football/leagues`, `GET /football/teams` | The football catalogue as JSON (Football and Tetris builds) |

Anything from a device that isn't allowed gets `403 Forbidden`.

Examples:

```bash
# all settings
curl -sI http://clockwizer.local/get | grep -i '^x-'

# 24-hour clock off, then restart to apply it
curl -X POST "http://clockwizer.local/set?use24hFormat=0"
curl -X POST "http://clockwizer.local/cmd?c=reboot"

# switch to Pacman
curl -X POST "http://clockwizer.local/face?id=0x05"
```

### Settings

Values for `/set`. Text values with special characters must be URL-encoded. Settings marked * are also on the [settings page](web-ui.md).

| Key | Value |
|-----|-------|
| `displayBright` * | brightness 0-255 (the page shows it as a percentage) |
| `displayBrMin` * | dimmest level for auto brightness, 3-255 |
| `autoBright` * | `MMMM,XXXX`: sensor readings (4 digits each, 1-4095) for "dark" and "bright", `0000,0000` switches auto brightness off |
| `use24hFormat` * | `1` or `0` (restart needed) |
| `timeZone` * | an IANA name such as `Europe/Amsterdam` (restart needed) |
| `clockName` * | the clock's name, URL-encoded |
| `updQuietFrom`, `updQuietUntil` * | hours 0-23; the same value twice means no quiet hours |
| `fwUrl` * | the firmware location (URL-encoded, `http(s)://...`); empty or the default means the default |
| `webAccess` * | `0` anyone, `1` own network (default), `2` any private network |
| `color`, `ghost1Color`, `ghost2Color`, `dotColor` * | index into the colour lists (see the page) |
| `showF1` *, `ballRoll` *, `timeStyle` * (0-5) | Football and Tetris options |
| `matchSecs` * (3-60), `resultMins` * (0-1440, 0 = until midnight) | Football options |
| `fbLeagues` *, `fbTeams` * | comma separated codes, e.g. `ned.1,eng.1` and `449:NED:n` |

### Advanced settings

These belong to the particular panel and have no control on the page.

| Key | Value |
|-----|-------|
| `swapBlueGreen` | `1` if the colours come out wrong (some panels wire red/blue/green in another order) |
| `displayRotation` | `0` to `3`: quarter turns, for panels mounted with the cables on another side |
| `displayHeight` | `32` or `64`: the rows of the panel. Restart needed |
| `ldrPin` | the ESP32 pin of the light sensor, default 35 |
| `ntpServer` | NTP server, default `time.cloudflare.com` |
| `manualPosix` | a POSIX timezone string. Setting `timeZone` clears it again |
| `wifiSsid`, `wifiPwd` | the WiFi network and password |

Settings that change the panel (`swapBlueGreen`, `displayRotation`, `displayHeight`) take effect after a restart.

## Telnet commands

Connect with `nc clockwizer.local 23` (or `telnet clockwizer.local 23`). The clock prints its log there, and a single key is a command; `h` lists them. No Enter is needed with `nc` in raw mode, otherwise Enter after the letter works too.

| Key | What it does |
|-----|--------------|
| `h` | Show the help |
| `R` | Restart the device |
| `C` | Check for a firmware update now |
| `X` | The same, but also retry builds that failed or were rolled back before |
| `L` | Switch the light sensor log on or off |
| `U` | Print uptime, the reason of the last reset (1 power on, 3 software, 4 crash, 5-7 watchdog, 9 brownout) and the free memory |
| `P` | Switch the panel between 64 and 32 rows and restart |
| `1` `2` `3` `4` `5` `6` `8` `9` | Install Mario, Time in Words, World Map, Castlevania, Pacman, Pokemon, Tetris, Luigi |
| `B` | Install Football |
| `F` | Install Formula 1 |

On Football and Formula 1 builds there are also test commands, see [clockfaces.md](clockfaces.md#trying-football-and-formula-1-without-a-match): `G`, `Y`, `D`, `W` on Football and `S`, `N` on Formula 1.

The `1`-`F` keys install the build for that face from the clock's [firmware location](updating-firmware.md). `update-fw.sh -i` uses them to tell a clock to install the face you just published.

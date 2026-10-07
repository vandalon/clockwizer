# Clockfaces

Each clockface is its own firmware build, named `cw-cf-0xNN`. You switch between them on the [settings page](web-ui.md#clockface) (the clock downloads and installs the face), or flash one from the [web flasher](https://vandalon.github.io/clockwizer/).

| Id | Name | Panel | Needs internet for more than the time |
|----|------|-------|----------------------------------------|
| `0x01` | Mario | 64x64 | no |
| `0x02` | Time in Words | 64x64 | no |
| `0x03` | World Map | 64x64 | no |
| `0x04` | Castlevania | 64x64 | no |
| `0x05` | Pacman | 64x64 | no |
| `0x06` | Pokemon | 64x64 | no |
| `0x08` | Tetris | 64x64 and 64x32 | on 64x32: football and F1 ticker |
| `0x09` | Luigi | 64x64 | no |
| `0x0B` | Football | 64x64 | yes, ESPN |
| `0x0C` | Formula 1 | 64x64 | yes, ESPN and Jolpica |

All faces get the time over NTP, so they all need the internet once to show the right time (and the connection again after a power cut).

## The classics

These come from upstream Clockwise. The thumbnails are in the clockface folders under `firmware/clockfaces/`.

- **Mario (`0x01`)**: the time in blocks that Mario bumps, with the Super Mario world around it.
- **Time in Words (`0x02`)**: the time and date written out in words. The date text exists in English and Portuguese (`DateI18nEN.h`, `DateI18nPT.h`).
- **World Map (`0x03`)**: a map of the world with the day and night side, and the time. The map follows UTC and moves a pixel every 12 minutes.
- **Castlevania (`0x04`)**: the clock tower, with a second hand.
- **Pacman (`0x05`)**: Pacman and the ghosts run through a maze that shows the time. The wall, ghost and dot colours can be set on the [settings page](web-ui.md#pacman-colours-pacman).
- **Pokemon (`0x06`)**: a Pokedex-style screen with the time.
- **Luigi (`0x09`)**: the Mario clockface in Luigi's colours (it is built from the same folder as Mario).

## Tetris (`0x08`)

The time is built from falling Tetris blocks, then the date. It is the one face that also runs on a 64x32 panel.

- **64x64:** the clock with the animation.
- **64x32:** the clock on top and a ticker row at the bottom. The ticker shows football scores (live matches, results from today) and, if you switch it on, the top three of a live Formula 1 race. The row shows the date when there is nothing to show. A goal on a followed match plays a full-screen celebration. The football settings and the *Show Formula 1* switch are on the [settings page](web-ui.md#football-football-and-tetris-on-64x32).

Switch a panel between 64 and 32 rows with the telnet command `P` or the `displayHeight` setting, see [control-api.md](control-api.md).

## Football (`0x0B`)

Football as two clocks, depending on what is on.

**While something is on, the live view:**
- a green bar with the competition, the game time left and the time on the right
- both teams as shirts, either side of the score
- a timeline of the playing time, with the home team's goals (balls) and cards (stripes) above it and the away team's below
- under it, the matches that finished, or the other live matches

With several live matches the main view takes them one by one (see *Next live match every*). With one match and nothing else the time moves to the bottom, with seconds. A card or a substitution plays a full-screen animation, and a goal a full celebration with the team's kit colours.

Without a live match the same view shows results: a favourite's, otherwise the latest on top (FT), with the other results and today's coming matches below.

**With no matches at all, the main view:** flip-clock tiles, hours over minutes, a ball rolling (or resting) on a stadium band at the bottom, and the next kick-offs on the right. If there is nothing to show at all it is `HH:MM` on flip tiles over the stadium band.

Which competitions and teams are followed, and how long results stay, is set on the settings page. The defaults are the Dutch Eredivisie and KNVB Cup and the Netherlands national team; change them on the settings page. The catalogue of competitions and teams is generated from ESPN's data by `firmware/tools/football-catalog/generate.py`.

## Formula 1 (`0x0C`)

Three screens:

1. **A session is live:** a header with the session and its lap or time left, then the running order (position, team colour, driver code) as far down as the panel goes. A safety car turns the header yellow, a red flag white.
2. **A race weekend, nothing live:** the clock, the top three of the session that just finished, and the start of the next one.
3. **Otherwise:** the clock, the top three of the drivers' championship and the next Grand Prix, with a race car in the leader's team colour driving past along the bottom.

The data come from ESPN (the running order, the calendar) and from the Jolpica F1 API (the championship, successor of the Ergast API). ESPN gives the order but no gaps or lap times, so the screens don't show them.

## Trying Football and Formula 1 without a match

Football and Formula 1 builds have test commands you send over telnet (see [control-api.md](control-api.md#telnet-commands)):

- **Football:** `G` plays a goal celebration, `Y` shows a yellow card, `D` a red card and `W` a substitution.
- **Formula 1:** `S` (or `N`) steps through made-up screens (a live session, a safety car and so on). After the last one the real data come back.

You can also render screens on a computer without a clock, see [development.md](development.md#preview-tools).

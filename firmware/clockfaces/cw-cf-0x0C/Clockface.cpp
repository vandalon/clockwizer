#include "Clockface.h"
#include "F1Ticker.h"
#include <Locator.h>
#include <ezTime.h>
#include <stdio.h>

// Formula 1 as three screens:
//  - A session is live: a header with the session and its lap or time left, then the running order
//    (position, team colour, driver) as far down as the panel goes. A safety car turns the header yellow,
//    a red flag white.
//  - A race weekend, nothing live: the clock, the top three of the session that just finished and the start
//    of the next one.
//  - Otherwise: the clock, the top three of the drivers' championship and the coming Grand Prix, with a racecar
//    in the leader's team colour driving past along the bottom.

static F1Ticker f1Ticker;
static F1Ticker::Snapshot snap;
static uint32_t snapVersion = 0;
static bool snapKnown = false;
static char lastKey[96];

static const int W = 64;

static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); }

static const uint16_t WHITE = rgb(255, 255, 255);
static const uint16_t CLOCK = rgb(238, 246, 240);
static const uint16_t MINUTES = rgb(190, 235, 150);
static const uint16_t GREY = rgb(150, 150, 160);
static const uint16_t DIMMED = rgb(110, 116, 126);
static const uint16_t GOLD = rgb(255, 214, 0);
static const uint16_t F1_RED = rgb(225, 6, 0);
static const uint16_t SESSION_GREY = rgb(60, 70, 90);
static const uint16_t LINE = rgb(40, 44, 52);
static const uint16_t BLACK = 0;

// 3x5 font, one row per byte, bit 2 = left pixel
static const uint8_t *glyph3x5(char c) {
  static const struct { char c; uint8_t rows[5]; } FONT[] = {
    {'0', {7,5,5,5,7}}, {'1', {2,6,2,2,7}}, {'2', {7,1,7,4,7}}, {'3', {7,1,7,1,7}}, {'4', {5,5,7,1,1}},
    {'5', {7,4,7,1,7}}, {'6', {7,4,7,5,7}}, {'7', {7,1,1,1,1}}, {'8', {7,5,7,5,7}}, {'9', {7,5,7,1,7}},
    {'A', {2,5,7,5,5}}, {'B', {6,5,6,5,6}}, {'C', {3,4,4,4,3}}, {'D', {6,5,5,5,6}}, {'E', {7,4,6,4,7}},
    {'F', {7,4,6,4,4}}, {'G', {3,4,5,5,3}}, {'H', {5,5,7,5,5}}, {'I', {7,2,2,2,7}}, {'J', {1,1,1,5,2}},
    {'K', {5,5,6,5,5}}, {'L', {4,4,4,4,7}}, {'M', {5,7,7,5,5}}, {'N', {6,5,5,5,5}}, {'O', {7,5,5,5,7}},
    {'P', {6,5,6,4,4}}, {'Q', {2,5,5,7,3}}, {'R', {6,5,6,5,5}}, {'S', {3,4,2,1,6}}, {'T', {7,2,2,2,2}},
    {'U', {5,5,5,5,7}}, {'V', {5,5,5,5,2}}, {'W', {5,5,5,7,5}}, {'X', {5,5,2,5,5}}, {'Y', {5,5,2,2,2}},
    {'Z', {7,1,2,4,7}}, {'-', {0,0,7,0,0}}, {'\'', {2,2,0,0,0}}, {'+', {0,2,7,2,0}}, {':', {0,2,0,2,0}},
    {'/', {1,1,2,4,4}}, {'.', {0,0,0,0,2}},
  };
  for (const auto &g : FONT)
    if (g.c == c) return g.rows;
  return nullptr;  // space and anything unknown
}

// 5x7 font for the time, bit 4 = left pixel
static const uint8_t DIGITS[10][7] = {
  {14,17,17,17,17,17,14}, {4,12,4,4,4,4,14},  {14,17,1,2,4,8,31},    {30,1,1,14,1,1,30},  {2,6,10,18,31,2,2},
  {31,16,30,1,1,17,14},   {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8},    {14,17,17,14,17,17,14}, {14,17,17,15,1,2,12},
};

static const char *const DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

static int textWidth(const char *s, int scale = 1) { return *s ? (strlen(s) * 4 - 1) * scale : 0; }

static void drawText(MatrixPanel_I2S_DMA *d, const char *s, int x, int y, uint16_t color) {
  for (; *s; s++, x += 4) {
    const uint8_t *rows = glyph3x5(*s);
    if (!rows) continue;
    for (int j = 0; j < 5; j++)
      for (int k = 0; k < 3; k++)
        if (rows[j] >> (2 - k) & 1) d->drawPixel(x + k, y + j, color);
  }
}

static void drawTextRight(MatrixPanel_I2S_DMA *d, const char *s, int right, int y, uint16_t color) {
  drawText(d, s, right - textWidth(s), y, color);
}

// A two digit number in the 5x7 font, each pixel 2x2, centred on cx
static void drawBigNumber(MatrixPanel_I2S_DMA *d, int value, int cx, int y, uint16_t color) {
  int digits[2] = {value / 10, value % 10};
  for (int n = 0; n < 2; n++) {
    int x = cx - 11 + n * 12;
    for (int j = 0; j < 7; j++)
      for (int k = 0; k < 5; k++)
        if (DIGITS[digits[n]][j] >> (4 - k) & 1) d->fillRect(x + k * 2, y + j * 2, 2, 2, color);
  }
}

static void drawHeader(MatrixPanel_I2S_DMA *d, const char *left, const char *right, uint16_t fill, uint16_t ink) {
  d->fillRect(0, 0, W, 8, fill);
  drawText(d, left, 2, 1, ink);
  drawTextRight(d, right, W - 2, 1, ink);
}

static void drawClock(MatrixPanel_I2S_DMA *d, int hour, int minute, int y) {
  drawBigNumber(d, hour, 17, y, CLOCK);
  drawBigNumber(d, minute, 47, y, MINUTES);
  d->drawPixel(32, y + 4, WHITE);
  d->drawPixel(32, y + 9, WHITE);
}

// A racecar facing right, 16x5: b body, d wings and nose, h helmet, W wheel, k hub
static const char *const CAR[5] = {
  "d.....hh........",
  "dbbbbbbbbb......",
  "dWWWbbbbbWWWbbb.",
  "bWkWbbbbbWkWbbbd",
  ".WWW.....WWW.ddd",
};
static const int CAR_W = 16, CAR_Y = 58, ROAD_Y = 63;
static const unsigned long CAR_PASS_MS = 4000;  // one pass: driving past for 2 seconds, then the road is empty
static const unsigned long CAR_DRIVE_MS = 2000;
static const int CAR_OFF = 100;                 // where the car is when it isn't on the panel

// Where the car is now, from its left edge (it starts and ends off the panel)
static int carX(unsigned long now) {
  unsigned long t = now % CAR_PASS_MS;
  return t < CAR_DRIVE_MS ? -(CAR_W + 2) + (int)(t * (W + 2 * (CAR_W + 2)) / CAR_DRIVE_MS) : CAR_OFF;
}

// A colour at level/8 of its brightness
static uint16_t dimmed(uint16_t c, int level) {
  return (((c >> 11) * level / 8) << 11) | ((((c >> 5) & 63) * level / 8) << 5) | ((c & 31) * level / 8);
}

// The car with motion blur: two fading copies of it trail behind
static void drawCar(MatrixPanel_I2S_DMA *d, int x, uint16_t body) {
  d->drawFastHLine(0, ROAD_Y, W, rgb(36, 40, 50));
  if (x == CAR_OFF) return;
  static const struct { int behind, level; } COPIES[] = {{6, 2}, {3, 4}, {0, 8}};  // furthest first, so the car is on top
  for (const auto &copy : COPIES)
    for (int j = 0; j < 5; j++)
      for (int i = 0; i < CAR_W; i++) {
        char c = CAR[j][i];
        uint16_t color = c == 'b' ? body : c == 'd' ? rgb(120, 120, 130) : c == 'h' ? WHITE : c == 'W' ? rgb(205, 205, 215)
                         : c == 'k' ? rgb(20, 20, 24) : 0;
        int px = x - copy.behind + i;
        if (c != '.' && px >= 0 && px < W) d->drawPixel(px, CAR_Y + j, dimmed(color, copy.level));
      }
}

static const uint16_t FASTEST = rgb(190, 80, 255);  // purple, as on the timing screens

// One position: place, team colour bar, driver code and, when known, the time or gap on the right.
// The first place's time is green for a race (LEAD) and purple for a best lap. Nothing for an empty place.
static void drawPlace(MatrixPanel_I2S_DMA *d, int place, const F1Ticker::Row &row, int y, bool race = false) {
  if (!row.code[0]) return;
  char number[3];
  snprintf(number, sizeof(number), "%d", place);
  drawText(d, number, place < 10 ? 1 : 0, y, place == 1 ? GOLD : GREY);
  d->fillRect(7, y, 2, 5, row.color);
  drawText(d, row.code, 11, y, WHITE);
  drawTextRight(d, row.time, W - 2, y, place > 1 ? GREY : race ? MINUTES : FASTEST);
}

// Local time parts of a UTC time
struct LocalTime { int weekday, hour, minute; };
static LocalTime localParts(CWDateTime *dateTime, time_t utc) {
  time_t local = dateTime->utcToLocal(utc);
  return {(int)((local / SECS_PER_DAY + 4) % 7), (int)(local % SECS_PER_DAY / 3600), (int)(local % 3600 / 60)};
}

static void drawLive(MatrixPanel_I2S_DMA *d, const F1Ticker::Session &s) {
  char right[12] = "";
  if (s.tag == 'R' || s.tag == 'S') {
    if (s.period > 0 && s.totalLaps > 0) snprintf(right, sizeof(right), "L%d/%d", s.period, s.totalLaps);
    else if (s.period > 0) snprintf(right, sizeof(right), "L%d", s.period);
  } else {
    strlcpy(right, s.clock, sizeof(right));
  }
  if (s.flag == 'y') drawHeader(d, s.name, right, GOLD, BLACK);
  else if (s.flag == 'r') drawHeader(d, "RED FLAG", right, WHITE, F1_RED);
  else drawHeader(d, s.name, right, s.tag == 'P' ? SESSION_GREY : F1_RED, WHITE);
  for (int i = 0; i < F1Ticker::ROWS; i++) drawPlace(d, i + 1, s.rows[i], 10 + i * 6, s.tag == 'R' || s.tag == 'S');
}

// The car is in the colour of the team of the leader of the championship, F1 red until that is known
static uint16_t carColor() { return snap.standingsCount ? snap.standings[0].color : F1_RED; }

static void drawBetween(MatrixPanel_I2S_DMA *d, CWDateTime *dateTime, int hour, int minute, int car) {
  drawHeader(d, snap.last.name, "RESULT", SESSION_GREY, WHITE);
  drawClock(d, hour, minute, 11);
  d->drawFastHLine(2, 28, 60, LINE);
  for (int i = 0; i < 3; i++) drawPlace(d, i + 1, snap.last.rows[i], 31 + i * 6, snap.last.tag == 'R' || snap.last.tag == 'S');
  d->drawFastHLine(2, 49, 60, LINE);
  if (snap.next.valid) {
    LocalTime t = localParts(dateTime, snap.next.start);
    char when[12];
    snprintf(when, sizeof(when), "%s %02d%02d", DAYS[t.weekday], t.hour, t.minute);
    drawText(d, snap.next.name, 2, 51, WHITE);
    drawTextRight(d, when, W - 2, 51, MINUTES);
  }
  drawCar(d, car, carColor());
}

// When the weekend starts: TODAY, a weekday for the coming six days ("FRI"), a date after that ("24/10")
static void whenText(CWDateTime *dateTime, time_t utc, char *out, size_t size) {
  time_t local = dateTime->utcToLocal(utc);
  long days = local / SECS_PER_DAY - dateTime->localNow() / SECS_PER_DAY;
  if (days <= 0) strlcpy(out, "TODAY", size);
  else if (days <= 6) strlcpy(out, DAYS[(local / SECS_PER_DAY + 4) % 7], size);
  else snprintf(out, size, "%02d/%02d", ezt::day(local), ezt::month(local));
}

// No header: the clock on top, then the championship and the coming race
static void drawIdle(MatrixPanel_I2S_DMA *d, CWDateTime *dateTime, int hour, int minute, int car) {
  drawClock(d, hour, minute, 4);
  d->drawFastHLine(2, 21, 60, LINE);
  for (int i = 0; i < snap.standingsCount; i++) {
    int y = 25 + i * 7;
    F1Ticker::Row row;
    strlcpy(row.code, snap.standings[i].code, sizeof(row.code));
    row.color = snap.standings[i].color;
    drawPlace(d, i + 1, row, y);
    char points[8];
    snprintf(points, sizeof(points), "%d", snap.standings[i].points);
    drawTextRight(d, points, W - 2, y, i == 0 ? MINUTES : GREY);
  }
  d->drawFastHLine(2, 47, 60, LINE);
  time_t nowUtc = ezt::now();
  for (int i = 0; i < snap.upcomingCount; i++) {
    if (snap.upcoming[i].start <= nowUtc) continue;
    char when[8];
    whenText(dateTime, snap.upcoming[i].start, when, sizeof(when));
    char city[11];  // what fits beside the date
    strlcpy(city, snap.upcoming[i].city, sizeof(city));
    drawText(d, city, 2, 50, WHITE);
    drawTextRight(d, when, W - 2, 50, MINUTES);
    break;
  }
  drawCar(d, car, carColor());
}

// Made-up screens for the telnet simulator, so every look can be checked outside a race weekend
static int simScenario = 0;
static const char *const SIM_NAMES[] = {"off", "idle", "between sessions", "race", "safety car", "red flag", "qualifying",
                                        "practice"};
static const int SIM_COUNT = 8;
static const char *const SIM_GRID[] = {"ANT", "RUS", "HAM", "LEC", "NOR", "PIA", "VER", "ALO", "GAS"};

static void simFill(F1Ticker::Session &s, const char *name, char tag, bool live) {
  s.valid = true;
  s.live = live;
  s.tag = tag;
  strlcpy(s.name, name, sizeof(s.name));
  s.count = F1Ticker::ROWS;
  static const char *const GAPS[] = {"+.088", "+.201", "+.317", "+.455", "+.620", "+.742", "+.911", "+1.030"};
  for (int i = 0; i < F1Ticker::ROWS; i++) {
    strlcpy(s.rows[i].code, SIM_GRID[i], sizeof(s.rows[i].code));
    s.rows[i].color = F1Ticker::driverColor(SIM_GRID[i]);
    if (i == 0) strlcpy(s.rows[0].time, tag == 'R' || tag == 'S' ? "LEAD" : "1:29.412", sizeof(s.rows[0].time));
    else strlcpy(s.rows[i].time, GAPS[i - 1], sizeof(s.rows[i].time));
  }
}

static void simSnapshot(F1Ticker::Snapshot &s) {
  s = F1Ticker::Snapshot();
  const int points[] = {372, 351, 289};
  for (int i = 0; i < 3; i++) {
    strlcpy(s.standings[i].code, SIM_GRID[i], sizeof(s.standings[i].code));
    s.standings[i].color = F1Ticker::driverColor(SIM_GRID[i]);
    s.standings[i].points = points[i];
  }
  s.standingsCount = 3;
  strlcpy(s.upcoming[0].city, "MEXICO CITY", sizeof(s.upcoming[0].city));
  s.upcoming[0].start = ezt::now() + 17 * SECS_PER_DAY;
  s.upcomingCount = 1;
  switch (simScenario) {
    case 2:
      s.weekend = true;
      simFill(s.last, "QUALI", 'Q', false);
      simFill(s.next, "RACE", 'R', false);
      s.next.start = ezt::now() + 20 * 3600;
      break;
    case 3:
    case 4:
    case 5:
      s.weekend = true;
      simFill(s.live, "RACE", 'R', true);
      s.live.period = 32;
      s.live.flag = simScenario == 4 ? 'y' : simScenario == 5 ? 'r' : 0;
      break;
    case 6:
      s.weekend = true;
      simFill(s.live, "QUALI", 'Q', true);
      strlcpy(s.live.clock, "4:12", sizeof(s.live.clock));
      break;
    case 7:
      s.weekend = true;
      simFill(s.live, "FP2", 'P', true);
      strlcpy(s.live.clock, "23:10", sizeof(s.live.clock));
      break;
  }
}

Clockface::Clockface(MatrixPanel_I2S_DMA* display) {
  _display = display;
  Locator::provide(display);  // the startup logo and status screens draw through the Locator
}

void Clockface::setup(CWDateTime *dateTime) {
  _dateTime = dateTime;
  f1Ticker.begin(dateTime);
}

const char *Clockface::simulateNext() {
  simScenario = (simScenario + 1) % SIM_COUNT;
  lastKey[0] = 0;
  return SIM_NAMES[simScenario];
}

const char *Clockface::simulate() { return simulateNext(); }

void Clockface::update() {
  int hour = _dateTime->getHour(), minute = _dateTime->getMinute();
  uint32_t version = f1Ticker.version();
  if (simScenario) {
    simSnapshot(snap);
    snapVersion = version - 1;  // the real data comes back when the simulator stops
    version = 0x80000000UL + simScenario;
  } else if (!snapKnown || version != snapVersion) {
    f1Ticker.snapshot(snap);
    snapVersion = version;
    snapKnown = true;
  }

  enum { LIVE, BETWEEN, IDLE } view = snap.live.valid ? LIVE : snap.weekend && snap.last.valid ? BETWEEN : IDLE;
  int car = view == LIVE ? CAR_OFF : carX(millis());

  // Only draw when something changed
  char key[96];
  snprintf(key, sizeof(key), "%lu|%d|%d:%d|%d", (unsigned long)version, (int)view, hour, minute, car);
  if (strcmp(key, lastKey) == 0) return;
  strcpy(lastKey, key);

  _display->fillScreen(0);
  switch (view) {
    case LIVE: drawLive(_display, snap.live); break;
    case BETWEEN: drawBetween(_display, _dateTime, hour, minute, car); break;
    case IDLE: drawIdle(_display, _dateTime, hour, minute, car); break;
  }
  _display->flipDMABuffer();
}

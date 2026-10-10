#include "Clockface.h"
#include "F1Ticker.h"
#include "FlipDigits.h"
#include <Locator.h>
#include <ezTime.h>
#include <stdio.h>
#include <string.h>
#include <CWPreferences.h>

// Formula 1 as three screens:
//  - A session is live: a header with the session and its lap or time left, then the running order
//    (position, team colour, driver) of the first places. A driver who gains or loses a place is marked for a
//    few seconds. A safety car turns the header yellow, a red flag red.
//  - A race weekend, nothing live: the clock and the result of the session that just finished, the winner fixed
//    and the other places scrolling, and the start of the next session along the bottom.
//  - Otherwise: the clock, the top three of the drivers' championship and the coming Grand Prix along the bottom.
// On a panel of 32 rows there is room for the clock and one line, so the standings, the results and the next
// session take turns in that line, a few seconds each.

static F1Ticker f1Ticker;
static F1Ticker::Snapshot snap;
static uint32_t snapVersion = 0;
static bool snapKnown = false;
static char lastKey[96];
static char lastRight[12];
static unsigned long snapSecond = 0;

static const int W = 64;
// How long a page of the results (or, on 32 rows, a line of the rotating information) stays: the football setting
static unsigned long pageMs() { return max(3, (int)ClockwiseParams::getInstance()->matchSecs) * 1000UL; }
// How long the whole classification stays after a session, in seconds; 0 = until midnight (the football setting)
static uint32_t resultWindowSecs() { return ClockwiseParams::getInstance()->resultMins * 60UL; }
static bool shortPanel() { return ClockwiseParams::getInstance()->displayHeight == 32; }

static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); }

static const uint16_t WHITE = rgb(255, 255, 255);
static const uint16_t CLOCK = rgb(238, 246, 240);
static const uint16_t MINUTES = rgb(190, 235, 150);
static const uint16_t GREY = rgb(150, 150, 160);
static const uint16_t SESSION_NAME = rgb(78, 82, 92);
static const uint16_t DIMMED = rgb(110, 116, 126);
static const uint16_t GOLD = rgb(255, 214, 0);
static const uint16_t F1_RED = rgb(225, 6, 0);
static const uint16_t HEADER_NAVY = rgb(14, 22, 52);  // the header bar: dark, with a small coloured tab on its left edge
static const uint16_t TAB_QUIET = rgb(90, 110, 150);  // the tab when it is not a race or qualifying
static const uint16_t RED_FLAG_BAR = rgb(150, 8, 6);
static const uint16_t LINE = rgb(40, 44, 52);
static const uint16_t RULE = rgb(80, 85, 96);  // the two lines of the tall idle screen: lighter, to stand out from the squares
static const uint16_t BLACK = 0;
static const uint16_t FLAG_TILE = rgb(38, 38, 40);  // the squares of the chequered flag, dim
static const uint16_t TRACK_LINE = rgb(48, 78, 150);

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

// 3x6 font for the header bar: one row more than the 3x5 one, for letters like S, Q and the 2 that were hard to tell apart
static const uint8_t *glyph3x6(char c) {
  static const struct { char c; uint8_t rows[6]; } FONT[] = {
    {'0', {7,5,5,5,5,7}}, {'1', {2,6,2,2,2,7}}, {'2', {7,1,3,6,4,7}}, {'3', {7,1,3,1,1,7}}, {'4', {5,5,5,7,1,1}},
    {'5', {7,4,6,1,1,6}}, {'6', {3,4,7,5,5,7}}, {'7', {7,1,1,2,2,2}}, {'8', {7,5,7,5,5,7}}, {'9', {7,5,5,7,1,6}},
    {'A', {2,5,5,7,5,5}}, {'B', {6,5,6,5,5,6}}, {'C', {3,4,4,4,4,3}}, {'D', {6,5,5,5,5,6}}, {'E', {7,4,6,4,4,7}},
    {'F', {7,4,6,4,4,4}}, {'G', {3,4,4,5,5,3}}, {'H', {5,5,7,5,5,5}}, {'I', {7,2,2,2,2,7}}, {'J', {1,1,1,1,5,2}},
    {'K', {5,5,6,5,5,5}}, {'L', {4,4,4,4,4,7}}, {'M', {5,7,7,5,5,5}}, {'N', {6,5,5,5,5,5}}, {'O', {2,5,5,5,5,2}},
    {'P', {6,5,5,6,4,4}}, {'Q', {2,5,5,5,7,3}}, {'R', {6,5,5,6,5,5}}, {'S', {3,4,2,1,1,6}}, {'T', {7,2,2,2,2,2}},
    {'U', {5,5,5,5,5,7}}, {'V', {5,5,5,5,5,2}}, {'W', {5,5,5,7,7,5}}, {'X', {5,5,2,2,5,5}}, {'Y', {5,5,2,2,2,2}},
    {'Z', {7,1,2,2,4,7}}, {'-', {0,0,7,0,0,0}}, {':', {0,2,0,0,2,0}}, {'/', {1,1,2,2,4,4}}, {'.', {0,0,0,0,0,2}},
  };
  for (const auto &g : FONT)
    if (g.c == c) return g.rows;
  return nullptr;
}

// 5x7 font for the time, bit 4 = left pixel
static const uint8_t DIGITS[10][7] = {
  {14,17,17,17,17,17,14}, {4,12,4,4,4,4,14},  {14,17,1,2,4,8,31},    {30,1,1,14,1,1,30},  {2,6,10,18,31,2,2},
  {31,16,30,1,1,17,14},   {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8},    {14,17,17,14,17,17,14}, {14,17,17,15,1,2,12},
};

static const char *const DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *const DAYS2[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};  // the short days of the Unicode locale data

static int textWidth(const char *s, int scale = 1) { return *s ? (strlen(s) * 4 - 1) * scale : 0; }

// Text is only drawn between these rows: a line sliding in or out of a strip is cut at the strip's edges
static int clipTop = 0, clipBottom = 64, clipLeft = 0, clipRight = 64;

// The text font: single pixel strokes, 7 rows high and 5 wide (the dot and colon 1), with the lower case letters of the days
static const struct { char c; const char *rows[7]; } TALL_FONT[] = {
  {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}}, {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
  {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}}, {'D', {"###..", "#..#.", "#...#", "#...#", "#...#", "#..#.", "###.."}},
  {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}}, {'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
  {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###."}}, {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
  {'I', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"}}, {'J', {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
  {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}}, {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
  {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}}, {'N', {"#...#", "##..#", "##..#", "#.#.#", "#..##", "#..##", "#...#"}},
  {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}}, {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
  {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}}, {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
  {'S', {".###.", "#...#", "#....", ".###.", "....#", "#...#", ".###."}}, {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
  {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}}, {'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
  {'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"}}, {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
  {'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}}, {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
  {'0', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}}, {'1', {"..#..", ".##..", "#.#..", "..#..", "..#..", "..#..", "#####"}},
  {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}}, {'3', {".###.", "#...#", "....#", "..##.", "....#", "#...#", ".###."}},
  {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}}, {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
  {'6', {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."}}, {'7', {"#####", "....#", "...#.", "..#..", "..#..", "..#..", "..#.."}},
  {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}}, {'9', {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."}},
  {'a', {".....", ".....", ".###.", "....#", ".####", "#...#", ".####"}}, {'e', {".....", ".....", ".###.", "#...#", "#####", "#....", ".####"}},
  {'h', {"#....", "#....", "####.", "#...#", "#...#", "#...#", "#...#"}}, {'o', {".....", ".....", ".###.", "#...#", "#...#", "#...#", ".###."}},
  {'r', {".....", ".....", "#.##.", "##..#", "#....", "#....", "#...."}}, {'u', {".....", ".....", "#...#", "#...#", "#...#", "#...#", ".####"}},
  {':', {".", "#", ".", ".", ".", "#", "."}}, {'.', {".", ".", ".", ".", ".", ".", "#"}},
  {'/', {"....#", "...#.", "...#.", "..#..", ".#...", ".#...", "#...."}}, {'-', {".....", ".....", ".....", "#####", ".....", ".....", "....."}},
  {'+', {".....", ".....", "..#..", ".###.", "..#..", ".....", "....."}},
};
static const int TALL_SPACE = 3;
static const char *const *tallGlyph(char c) {
  for (const auto &g : TALL_FONT)
    if (g.c == c) return g.rows;
  return nullptr;
}
static int tallWidth(const char *s) {
  int w = 0;
  for (; *s; s++) {
    const char *const *rows = tallGlyph(*s);
    w += rows ? (int)strlen(rows[0]) + 1 : TALL_SPACE;
  }
  return w ? w - 1 : 0;
}

// tall: the bottom bar's 4x7 letters instead of the 3x5 ones
static void drawText(MatrixPanel_I2S_DMA *d, const char *s, int x, int y, uint16_t color, bool tall = false) {
  if (tall) {
    for (; *s; s++) {
      const char *const *rows = tallGlyph(*s);
      if (!rows) { x += TALL_SPACE; continue; }
      int width = (int)strlen(rows[0]);
      for (int j = 0; j < 7; j++) {
        if (y + j < clipTop || y + j >= clipBottom) continue;
        for (int k = 0; k < width; k++)
          if (rows[j][k] == '#' && x + k >= clipLeft && x + k < clipRight) d->drawPixel(x + k, y + j, color);
      }
      x += width + 1;
    }
    return;
  }
  for (; *s; s++, x += 4) {
    const uint8_t *rows = glyph3x5(*s);
    if (!rows) continue;
    for (int j = 0; j < 5; j++) {
      if (y + j < clipTop || y + j >= clipBottom) continue;
      uint8_t bits = rows[j];
      for (int k = 0; k < 3; k++)
        if (bits >> (2 - k) & 1 && x + k >= clipLeft && x + k < clipRight) d->drawPixel(x + k, y + j, color);
    }
  }
}

// The header's text, 6 rows high (y is the top row), same width as the small font
static void drawHeaderText(MatrixPanel_I2S_DMA *d, const char *s, int x, int y, uint16_t color) {
  for (; *s; s++, x += 4) {
    const uint8_t *rows = glyph3x6(*s);
    if (!rows) continue;
    for (int j = 0; j < 6; j++)
      for (int k = 0; k < 3; k++)
        if (rows[j] >> (2 - k) & 1) d->drawPixel(x + k, y + j, color);
  }
}

static void drawTextRight(MatrixPanel_I2S_DMA *d, const char *s, int right, int y, uint16_t color, bool tall = false) {
  drawText(d, s, right - (tall ? tallWidth(s) : textWidth(s)), y, color, tall);
}

// A time or gap ("1:29.412", "+0.123"): the font's dot and colon sit in the middle of a cell, which leaves two empty
// columns either side; here they take one column and a gap of one, so one column less on each side
static int timeAdvance(char c) { return c == '.' || c == ':' ? 2 : 4; }
static int timeWidth(const char *s) {
  int w = 0;
  for (; *s; s++) w += timeAdvance(*s);
  return w ? w - 1 : 0;
}
static void drawTimeRight(MatrixPanel_I2S_DMA *d, const char *s, int right, int y, uint16_t color) {
  int x = right - timeWidth(s);
  for (; *s; x += timeAdvance(*s), s++) {
    char one[2] = {*s, 0};
    drawText(d, one, timeAdvance(*s) == 2 ? x - 1 : x, y, color);  // the dot is in the middle column of its cell
  }
}

// The colon of the current time breathes: a fade between a third and full brightness, in 32 steps (a frame is 50 ms)
static const unsigned long PULSE_MS = 2000;
static int pulseLevel() { return (int)roundf((0.5f - 0.5f * cosf((millis() % PULSE_MS) * 6.2831853f / PULSE_MS)) * 31); }
static uint16_t pulsed(uint16_t color) {
  int f = 85 + pulseLevel() * 170 / 31;  // out of 255
  int r = (color >> 11 & 31) * f / 255, g = (color >> 5 & 63) * f / 255, b = (color & 31) * f / 255;
  return r << 11 | g << 5 | b;
}

// The time in the banner, 3x6 digits with the colon tucked in: one pixel either side, where the font's own colon
// leaves two. Flush against the right edge.
static int headerTimeWidth(int hour) { return (hour > 9 ? 7 : 3) + 3 + 7; }
static void drawHeaderTime(MatrixPanel_I2S_DMA *d, int hour, int minute, int right, int y, uint16_t color) {
  char h[3], m[3];
  snprintf(h, sizeof(h), "%d", hour);
  snprintf(m, sizeof(m), "%02d", minute);
  int x = right - headerTimeWidth(hour);
  int hw = (int)strlen(h) * 4 - 1;
  drawHeaderText(d, h, x, y, color);
  d->drawPixel(x + hw + 1, y + 1, pulsed(color));
  d->drawPixel(x + hw + 1, y + 4, pulsed(color));
  drawHeaderText(d, m, x + hw + 3, y, color);
}

// A text that is wider than its window waits, scrolls a pixel at a time until its end is in view, waits again and
// scrolls back. marqueeMax is how far it scrolls (0 when nothing does); the offset is a function of the time, so the
// screen is drawn again whenever it changes
static const unsigned long MARQUEE_PAUSE_MS = 2000, MARQUEE_STEP_MS = 160;
static int marqueeMax = 0;
static int marqueeOffset(int most) {
  unsigned long scroll = most * MARQUEE_STEP_MS;
  unsigned long t = millis() % (2 * (MARQUEE_PAUSE_MS + scroll));
  if (t < MARQUEE_PAUSE_MS) return 0;
  t -= MARQUEE_PAUSE_MS;
  if (t < scroll) return (int)(t / MARQUEE_STEP_MS);
  t -= scroll;
  if (t < MARQUEE_PAUSE_MS) return most;
  t -= MARQUEE_PAUSE_MS;
  return most - (int)(t / MARQUEE_STEP_MS);
}
static void drawMarquee(MatrixPanel_I2S_DMA *d, const char *text, int x, int y, int right, uint16_t color, bool tall = false) {
  int width = tall ? tallWidth(text) : textWidth(text);
  if (width <= right - x) {
    drawText(d, text, x, y, color, tall);
    return;
  }
  marqueeMax = width - (right - x);
  clipLeft = x;
  clipRight = right;
  drawText(d, text, x - marqueeOffset(marqueeMax), y, color, tall);
  clipLeft = 0;
  clipRight = 64;
}

// A two digit number in the 5x7 font, each pixel 2x2, centred on cx. The convex corners are rounded, like the
// football clock: where a pixel has no neighbour on either side of a corner, that corner of its block is left out;
// where the only neighbour is diagonal, the two blocks are joined instead
static void drawBigDigit(MatrixPanel_I2S_DMA *d, int digit, int x, int y, uint16_t color, int sy) {
  const int sx = 2;  // each pixel is 2 wide and sy high
  auto lit = [digit](int k, int j) { return k >= 0 && k < 5 && j >= 0 && j < 7 && (DIGITS[digit][j] >> (4 - k) & 1); };
  for (int j = 0; j < 7; j++)
    for (int k = 0; k < 5; k++) {
      if (!lit(k, j)) continue;
      int bx = x + k * sx, by = y + j * sy;
      d->fillRect(bx, by, sx, sy, color);
      for (int dy = -1; dy <= 1; dy += 2)
        for (int dx = -1; dx <= 1; dx += 2)
          if (!lit(k + dx, j) && !lit(k, j + dy)) {
            int cx = dx < 0 ? bx : bx + sx - 1, cy = dy < 0 ? by : by + sy - 1;
            if (!lit(k + dx, j + dy)) {
              d->drawPixel(cx, cy, 0);
            } else {
              d->drawPixel(cx + dx, cy, color);
              d->drawPixel(cx, cy + dy, color);
            }
          }
    }
}

static void drawBigNumber(MatrixPanel_I2S_DMA *d, int value, int cx, int y, uint16_t color, int sy) {
  drawBigDigit(d, value / 10, cx - 11, y, color, sy);
  drawBigDigit(d, value % 10, cx + 1, y, color, sy);
}

static bool hasDigit(const char *s) {
  for (; *s; s++)
    if (*s >= '0' && *s <= '9') return true;
  return false;
}

// The letters of the banner are in the text font when they fit; a countdown or lap count stays in the small digits.
// The countdown is redrawn alone from x 33, so a text in the text font stays left of that when there is one.
static void drawHeader(MatrixPanel_I2S_DMA *d, const char *left, const char *right, uint16_t fill, uint16_t ink, uint16_t tab = 0) {
  d->fillRect(0, 0, W, 8, fill);
  if (tab) d->fillRect(0, 0, 2, 8, tab);
  int x = tab ? 4 : 2;
  bool digits = hasDigit(right);
  int rightWidth = digits ? textWidth(right) : tallWidth(right);
  bool tall = x + tallWidth(left) + 3 + rightWidth + 2 <= W && (!digits || x + tallWidth(left) <= 33);
  if (tall) {
    drawText(d, left, x, 1, ink, true);
    if (digits) drawHeaderText(d, right, W - 2 - rightWidth, 1, ink);
    else drawText(d, right, W - 2 - rightWidth, 1, ink, true);
  } else {
    drawHeaderText(d, left, x, 1, ink);
    drawHeaderText(d, right, W - 2 - textWidth(right), 1, ink);
  }
}

// ---- The clock on the tall panel: the lap-time digits blown up ----

// Each pixel of a digit is 0..32: how much of it the letter covers. Less than SoftEdge.lo is nothing, more than
// SoftEdge.hi all of the colour, in between a part of it: the soft edge of the curves.
struct GlyphSet { int w, h; const uint8_t *pixels; };
// The clock's digits are the 3x5 lap-time ones blown up to 10 x 17 pixels (about the same shape, strokes and bars 3 thick),
// but for the 1: that is drawn by hand
static uint8_t CLOCK_PIXELS[10 * 17 * 10];
static const GlyphSet CLOCK_GLYPHS = {10, 17, CLOCK_PIXELS};
// The same, 13 x 23, for the main screen outside a race weekend, which has room for it
static uint8_t BIG_PIXELS[10 * 23 * 13];
static const GlyphSet BIG_GLYPHS = {13, 23, BIG_PIXELS};
static bool bigClock = false;  // which of the two sizes is drawn
static const char *const CLOCK_ONE[17] = {
  "....XXX...", "...XXXX...", "..XXXXX...", ".XX.XXX...", ".XX.XXX...", "....XXX...", "....XXX...", "....XXX...", "....XXX...",
  "....XXX...", "....XXX...", "....XXX...", "....XXX...", "....XXX...", ".XXXXXXXXX", ".XXXXXXXXX", ".XXXXXXXXX"};
static void initClockGlyphs() {
  static bool done = false;
  if (done) return;
  done = true;
  static const uint8_t ROW_OF[17] = {0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4};  // the 5 rows of a lap-time digit spread over 17
  static const uint8_t COL_OF[10] = {0, 0, 0, 1, 1, 1, 1, 2, 2, 2};
  for (int digit = 0; digit < 10; digit++) {
    const uint8_t *rows = glyph3x5('0' + digit);
    for (int j = 0; j < 17; j++)
      for (int i = 0; i < 10; i++)
        CLOCK_PIXELS[(digit * 17 + j) * 10 + i] = (digit == 1 ? CLOCK_ONE[j][i] == 'X' : rows[ROW_OF[j]] >> (2 - COL_OF[i]) & 1) ? 32 : 0;
  }
}
static void initBigGlyphs() {
  static const char *const ONE[23] = {
    ".....XXXX....", "....XXXXX....", "...XXXXXX....", "..XXXXXXX....", ".XXX.XXXX....", ".XXX.XXXX....", ".XXX.XXXX....",
    ".....XXXX....", ".....XXXX....", ".....XXXX....", ".....XXXX....", ".....XXXX....", ".....XXXX....", ".....XXXX....",
    ".....XXXX....", ".....XXXX....", ".....XXXX....", ".....XXXX....", ".....XXXX....", ".XXXXXXXXXXXX", ".XXXXXXXXXXXX",
    ".XXXXXXXXXXXX", ".XXXXXXXXXXXX"};
  for (int digit = 0; digit < 10; digit++) {
    const uint8_t *rows = glyph3x5('0' + digit);
    for (int j = 0; j < 23; j++)
      for (int i = 0; i < 13; i++)
        BIG_PIXELS[(digit * 23 + j) * 13 + i] = (digit == 1 ? ONE[j][i] == 'X' : rows[j * 5 / 23] >> (2 - i * 3 / 13) & 1) ? 32 : 0;
  }
}
struct SoftEdge { float lo, hi; };
static const SoftEdge WHITE_SOFT = {0.2f, 0.8f};    // the hours
static const SoftEdge GREEN_SOFT = {0.25f, 0.75f};  // the minutes: the brighter colour looks blurrier

// How the digits change (the timeStyle setting, as on the football clock, without its flip cards):
// 1 fade, 2 roll up, 3 dissolve, 4 drift, 5 fade with a shimmer
static int timeStyle = 2;
static const int STYLE_FADE = 1, STYLE_ROLL = 2, STYLE_DISSOLVE = 3, STYLE_DRIFT = 4, STYLE_SHIMMER = 5;
static const unsigned long CHANGE_MS = 640, CHANGE_STAGGER = 140;  // a digit takes this long; the digits go one after the other
static const int TILE_W = 10, TILE_H = 17;
static int tileW() { return bigClock ? 13 : TILE_W; }
static int tileH() { return bigClock ? 23 : TILE_H; }

static int clockDigits[4] = {0, 0, 0, 0}, oldDigits[4] = {0, 0, 0, 0};
static unsigned long changeAt[4] = {0, 0, 0, 0};
static bool clockStateKnown = false;
static bool tileSettled[4] = {true, true, true, true};  // drawn in its final form: no need to draw it again

// A colour at level/32 of its brightness
static uint16_t scaleColor(uint16_t c, int level) {
  return rgb((c >> 11 << 3) * level / 32, (c >> 5 & 63) * 4 * level / 32, (c & 31) * 8 * level / 32);
}

// A pseudo-random 0..1 for a pixel of a digit, to dissolve in a different order each time
static float scatter(int i, int j, int digit) {
  uint32_t h = i * 73856093u ^ j * 19349663u ^ digit * 83492791u;
  h ^= h >> 13;
  h *= 0x5bd1e995u;
  h ^= h >> 15;
  return (h % 1000) / 1000.0f;
}

// One digit in the middle of the w x h box at x, y: level is its brightness 0..32, dy moves it down (rows outside the
// box are not drawn), dissolve says how far it has dissolved in (+) or out (-) 0..1, 0 = not at all.
static void drawGlyph(MatrixPanel_I2S_DMA *d, int digit, int x, int y, uint16_t color, const SoftEdge &soft, int level, int dy, float dissolve) {
  const GlyphSet &set = bigClock ? BIG_GLYPHS : CLOCK_GLYPHS;
  int gx = x + (tileW() - set.w) / 2, gy = y + (tileH() - set.h) / 2 + dy;
  float lo = soft.lo * 32, scale = 32 / ((soft.hi - soft.lo) * 32);
  float sweep = timeStyle == STYLE_SHIMMER ? fmodf(millis() / 40.0f, 150) - 35 : 0;  // 25 px a second, and a pause
  for (int j = 0; j < set.h; j++) {
    int py = gy + j;
    if (py < y || py >= y + tileH()) continue;
    for (int i = 0; i < set.w; i++) {
      int a = (int)constrain((set.pixels[(digit * set.h + j) * set.w + i] - lo) * scale, 0.0f, 32.0f);
      if (!a) continue;
      float lv = level;
      if (dissolve != 0) {
        float v = constrain((fabsf(dissolve) - scatter(i, j, digit) * 0.6f) / 0.4f, 0.0f, 1.0f);
        lv *= dissolve > 0 ? v : 1 - v;
      }
      if (timeStyle == STYLE_SHIMMER) {  // a bright band slanting across the digits
        float k = fabsf((gx + i) + py * 0.6f - sweep) / 12;
        lv *= 0.8f + (k < 1 ? 0.2f * (1 - k) * (1 - k) : 0);
      }
      d->drawPixel(gx + i, py, scaleColor(color, (int)(a * lv / 32)));
    }
  }
}

// How far the digit k has got in changing: 0..1, 1 when it is over, -1 while it waits for the digits after it
static float changeProgress(int k) {
  long t = (long)(millis() - changeAt[k]);
  return t < 0 ? -1 : t >= (long)CHANGE_MS ? 1 : t / (float)CHANGE_MS;
}

// One digit tile: the digit drawn, changing from oldDigits[k] to clockDigits[k], over whatever is behind it
static void drawTile(MatrixPanel_I2S_DMA *d, int k, int x, int y, uint16_t color, const SoftEdge &soft) {
  float p = changeProgress(k);  // (the screen is cleared and the background drawn before: nothing to clear here)
  if (p < 0) {
    drawGlyph(d, oldDigits[k], x, y, color, soft, 32, 0, 0);
  } else if (p >= 1 || oldDigits[k] == clockDigits[k]) {
    drawGlyph(d, clockDigits[k], x, y, color, soft, 32, 0, 0);
  } else if (timeStyle == STYLE_ROLL) {
    int run = tileH() + 2, off = (int)roundf(p * p * (3 - 2 * p) * run);
    drawGlyph(d, oldDigits[k], x, y, color, soft, 32, -off, 0);
    drawGlyph(d, clockDigits[k], x, y, color, soft, 32, run - off, 0);
  } else if (timeStyle == STYLE_DISSOLVE) {
    drawGlyph(d, oldDigits[k], x, y, color, soft, 32, 0, -max(p, 0.001f));
    drawGlyph(d, clockDigits[k], x, y, color, soft, 32, 0, max(p, 0.001f));
  } else {  // fade, drift and shimmer: the old one out, then the new one in
    int drift = timeStyle == STYLE_DRIFT ? max(2, tileH() / 8) : 0;
    if (p < 0.5f) drawGlyph(d, oldDigits[k], x, y, color, soft, (int)(32 * (1 - 2 * p)), -(int)roundf(drift * 2 * p), 0);
    else drawGlyph(d, clockDigits[k], x, y, color, soft, (int)(32 * (2 * p - 1)), (int)roundf(drift * (2 - 2 * p)), 0);
  }
}

static const int TILE_OFFSET[4] = {0, 11, 28, 39};  // the hours and the minutes: 2 pixels either side of the colon
static const int BIG_OFFSET[4] = {0, 14, 35, 49};
static const int CLOCK_WIDTH = 49, BIG_WIDTH = 62;
// timeStyle 0, the flip cards (as on the football clock): 14 x 23 cards on the main screen, 12 x 17 next to the session name
static const int FLIP_BIG_OFFSET[4] = {0, 15, 33, 48}, FLIP_SMALL_OFFSET[4] = {0, 13, 28, 41};
static const int FLIP_BIG_WIDTH = 62, FLIP_SMALL_WIDTH = 53;
static int clockWidth() { return timeStyle == 0 ? (bigClock ? FLIP_BIG_WIDTH : FLIP_SMALL_WIDTH) : bigClock ? BIG_WIDTH : CLOCK_WIDTH; }
static int clockLeft = (64 - CLOCK_WIDTH) / 2;  // where the clock starts: in the middle, or at the right on the results screen

// The four digits for the time: starts a change on the ones that differ
static void updateClockDigits(int hour, int minute) {
  initClockGlyphs();
  initBigGlyphs();
  int digits[4] = {hour / 10, hour % 10, minute / 10, minute % 10};
  timeStyle = ClockwiseParams::getInstance()->timeStyle;
  if (timeStyle > 5) timeStyle = STYLE_ROLL;
  if (!clockStateKnown) {
    for (int i = 0; i < 4; i++) clockDigits[i] = oldDigits[i] = digits[i];
    clockStateKnown = true;
    return;
  }
  int order = 0;
  for (int k = 3; k >= 0; k--)
    if (digits[k] != clockDigits[k]) {
      oldDigits[k] = clockDigits[k];
      clockDigits[k] = digits[k];
      changeAt[k] = millis() + order++ * CHANGE_STAGGER;
      tileSettled[k] = false;
    }
}

// Is a digit changing (or is it the shimmer, which moves all the time)?
static bool clockMoving() {
  if (timeStyle == STYLE_SHIMMER) return true;
  for (int k = 0; k < 4; k++)
    if (changeProgress(k) < 1) return true;
  return false;
}

// One row of a flip card, drawn at screen row y: the card with its rounded corners, the digit's pixels on it and the
// split as a black row. level dims it for the folding halves.
static const uint16_t CARD = rgb(52, 56, 66);
static uint16_t mixColor(uint16_t c0, uint16_t c1, int k) {
  int r0 = c0 >> 11 << 3, g0 = c0 >> 5 & 63, b0 = c0 & 31, r1 = c1 >> 11 << 3, g1 = c1 >> 5 & 63, b1 = c1 & 31;
  return rgb(r0 + (r1 - r0) * k / 32, (g0 + (g1 - g0) * k / 32) << 2, (b0 + (b1 - b0) * k / 32) << 3);
}
static void drawCardRow(MatrixPanel_I2S_DMA *d, const GlyphSet &set, int digit, int x, int y, int w, int h, int row,
                        uint16_t color, const SoftEdge &soft, int level) {
  if (row == h / 2) {
    d->fillRect(x, y, w, 1, 0);
    return;
  }
  bool edge = row == 0 || row == h - 1;
  d->fillRect(edge ? x + 1 : x, y, edge ? w - 2 : w, 1, scaleColor(CARD, level));
  int gy = row - (h - set.h) / 2, gx = x + (w - set.w) / 2;
  if (gy < 0 || gy >= set.h) return;
  const uint8_t *px = set.pixels + (digit * set.h + gy) * set.w;
  float lo = soft.lo * 32, scale = 32 / ((soft.hi - soft.lo) * 32);
  for (int i = 0; i < set.w; i++) {
    int a = (int)constrain((px[i] - lo) * scale, 0.0f, 32.0f);
    if (a) d->drawPixel(gx + i, y, scaleColor(mixColor(CARD, color, a), level));
  }
}

// One flip card: the old top half folds down onto the split, then the new bottom half folds out below it
static void drawFlipCard(MatrixPanel_I2S_DMA *d, int k, int x, int y, int w, int h, uint16_t color, const SoftEdge &soft) {
  static const GlyphSet WIDE = {12, 22, &FLIP_WIDE[0][0]}, LIVE = {10, 12, &FLIP_LIVE[0][0]};
  const GlyphSet &set = bigClock ? WIDE : LIVE;
  float p = changeProgress(k);
  int digit = p < 0 ? oldDigits[k] : clockDigits[k], old = oldDigits[k];
  int split = h / 2, below = h - 1 - split;
  bool flipping = p >= 0 && p < 1 && old != digit;
  for (int row = 0; row < h; row++)  // the new top half over the old bottom half
    drawCardRow(d, set, flipping && row > split ? old : digit, x, y + row, w, h, row, color, soft, 32);
  if (!flipping) return;
  if (p < 0.5f) {
    int n = (int)roundf(split * (1 - 2 * p));
    for (int r = 0; r < n; r++) drawCardRow(d, set, old, x, y + split - n + r, w, h, r * split / n, color, soft, 18 + 14 * n / split);
  } else {
    int n = (int)roundf(below * (2 * p - 1));
    for (int r = 0; r < n; r++) drawCardRow(d, set, digit, x, y + split + 1 + r, w, h, split + 1 + r * below / n, color, soft, 18 + 14 * n / below);
  }
}

// The whole clock, or only the digits that move
static void drawTallClock(MatrixPanel_I2S_DMA *d, int y, bool onlyMoving) {
  if (timeStyle == 0) {
    int w = bigClock ? 14 : 12, h = bigClock ? 23 : 17;
    const int *offset = bigClock ? FLIP_BIG_OFFSET : FLIP_SMALL_OFFSET;
    for (int k = 0; k < 4; k++) {
      drawFlipCard(d, k, clockLeft + offset[k], y, w, h, k < 2 ? CLOCK : MINUTES, k < 2 ? WHITE_SOFT : GREEN_SOFT);
      if (changeProgress(k) >= 1) tileSettled[k] = true;
    }
    int cx = clockLeft + 2 * w + 1 + (bigClock ? 1 : 0);
    d->fillRect(cx, y + h / 2 - 3, 2, 2, pulsed(WHITE));
    d->fillRect(cx, y + h / 2 + 2, 2, 2, pulsed(WHITE));
    return;
  }
  for (int k = 0; k < 4; k++) {
    if (onlyMoving && timeStyle != STYLE_SHIMMER && tileSettled[k]) continue;
    drawTile(d, k, clockLeft + (bigClock ? BIG_OFFSET : TILE_OFFSET)[k], y, k < 2 ? CLOCK : MINUTES, k < 2 ? WHITE_SOFT : GREEN_SOFT);
    if (changeProgress(k) >= 1) tileSettled[k] = true;
  }
  if (bigClock) {
    d->fillRect(clockLeft + 29, y + 6, 4, 4, pulsed(WHITE));
    d->fillRect(clockLeft + 29, y + 13, 4, 4, pulsed(WHITE));
    return;
  }
  d->fillRect(clockLeft + 23, y + 4, 3, 3, pulsed(WHITE));
  d->fillRect(clockLeft + 23, y + 10, 3, 3, pulsed(WHITE));
}

// The clock: the small panel has the 5x7 digits two rows tall; the tall panel the soft ones above
static void drawClock(MatrixPanel_I2S_DMA *d, int hour, int minute, int y, bool tall = false) {
  if (tall) {
    drawTallClock(d, y, false);
    return;
  }
  drawBigNumber(d, hour, 17, y, CLOCK, 2);
  drawBigNumber(d, minute, 47, y, MINUTES, 2);
  d->drawPixel(32, y + 4, pulsed(WHITE));
  d->drawPixel(32, y + 9, pulsed(WHITE));
}


static const uint16_t FASTEST = rgb(190, 80, 255);  // purple, as on the timing screens

// One position: place, team colour bar, driver code and, when known, the time or gap on the right.
// The first place's time is green for a race (LEAD) and purple for a best lap. Nothing for an empty place.
static void drawPlace(MatrixPanel_I2S_DMA *d, int place, const F1Ticker::Row &row, int y, bool race = false, int moved = 0, bool fav = false) {
  if (!row.code[0]) return;
  if (moved) d->fillRect(0, y - 1, W, 7, moved > 0 ? rgb(0, 80, 20) : rgb(50, 0, 4));  // gained or lost a place
  char number[3];
  snprintf(number, sizeof(number), "%d", place);
  drawText(d, number, place < 10 ? 4 : 0, y, place == 1 ? GOLD : GREY);
  d->fillRect(8, y, 2, 5, row.color);
  drawText(d, row.code, 12, y, fav ? GOLD : row.fastest ? FASTEST : WHITE);  // a favourite driver; the fastest lap in purple
  if (moved) {  // a little arrow between the code and the time
    uint16_t color = moved > 0 ? rgb(60, 230, 90) : rgb(255, 70, 60);
    int top = moved > 0 ? y : y + 4;
    int dy = moved > 0 ? 1 : -1;
    d->drawPixel(26, top, color);
    d->drawFastHLine(25, top + dy, 3, color);
    d->drawFastHLine(24, top + 2 * dy, 5, color);
  }
  drawTimeRight(d, row.time, W, y, place > 1 ? GREY : race ? MINUTES : FASTEST);
}

// Who moved up or down at the last change of the order, and until when it is shown
static const unsigned long MOVE_MS = 4000;
static struct Move { char code[4]; int8_t dir; unsigned long until; } moves[F1Ticker::ROWS];
static char lastOrder[F1Ticker::ROWS][4];
static uint32_t lastOrderSession = 0;

static int movedDir(const char *code) {
  for (const Move &m : moves)
    if (m.dir && (long)(m.until - millis()) > 0 && strcmp(m.code, code) == 0) return m.dir;
  return 0;
}

// Bit per row that has a marker now: when it changes, the list is drawn again
static uint32_t moveMask(const F1Ticker::Session &s) {
  uint32_t mask = 0;
  for (int i = 0; i < F1Ticker::ROWS; i++)
    if (s.rows[i].code[0] && movedDir(s.rows[i].code)) mask |= 1UL << i;
  return mask;
}

// Compares the order with the one before it. A big reshuffle is a new session or another source, not overtaking.
static void trackMoves(const F1Ticker::Session &s) {
  int changed = 0, dirs[F1Ticker::ROWS] = {0};
  bool known = lastOrderSession == s.id && lastOrder[0][0];
  for (int i = 0; i < F1Ticker::ROWS; i++) {
    if (!s.rows[i].code[0] || !known) continue;
    for (int j = 0; j < F1Ticker::ROWS; j++)
      if (strcmp(lastOrder[j], s.rows[i].code) == 0) {
        if (j != i) { dirs[i] = j > i ? 1 : -1; changed++; }
        break;
      }
  }
  if (known && changed > 0) {
    unsigned long now = millis();
    for (int i = 0; i < F1Ticker::ROWS; i++) {
      if (!dirs[i]) continue;
      Move *slot = nullptr;
      for (Move &m : moves)
        if (strcmp(m.code, s.rows[i].code) == 0) { slot = &m; break; }
      if (!slot)
        for (Move &m : moves)
          if (!m.dir || (long)(m.until - now) <= 0) { slot = &m; break; }
      if (!slot) continue;
      strlcpy(slot->code, s.rows[i].code, sizeof(slot->code));
      slot->dir = dirs[i];
      slot->until = now + MOVE_MS;
    }
  }
  for (int i = 0; i < F1Ticker::ROWS; i++) strlcpy(lastOrder[i], s.rows[i].code, sizeof(lastOrder[i]));
  lastOrderSession = s.id;
}

// Local time parts of a UTC time
struct LocalTime { int weekday, hour, minute; };
static LocalTime localParts(CWDateTime *dateTime, time_t utc) {
  time_t local = dateTime->utcToLocal(utc);
  return {(int)((local / SECS_PER_DAY + 4) % 7), (int)(local % SECS_PER_DAY / 3600), (int)(local % 3600 / 60)};
}

// The header's right side: laps in a race, the time left in practice and qualifying. Once that has sat at 0:00
// for 5 minutes, the time of day instead.
static const unsigned long OVER_MS = 5 * 60 * 1000UL;
static unsigned long clockZeroAt = 0;  // since when the session clock reads 0:00
static void liveRight(const F1Ticker::Session &s, char *right, size_t size, int hour, int minute) {
  right[0] = 0;
  if (s.tag == 'R' || s.tag == 'S') {
    if (s.period > 0 && s.totalLaps > 0) snprintf(right, size, "L%d/%d", s.period, s.totalLaps);
    else if (s.period > 0) snprintf(right, size, "L%d", s.period);
  } else if (clockZeroAt && millis() - clockZeroAt > OVER_MS) {
    snprintf(right, size, "%d:%02d", hour, minute);
  } else {
    strlcpy(right, s.clock, size);
  }
}

// The chequered flag behind a list: squares of 8 pixels, seven rows of eight under the 8 pixel header
static void flagFill(MatrixPanel_I2S_DMA *d, int x, int y, int w, int h) {
  d->fillRect(x, y, w, h, 0);
  int top = max(y, 8), bottom = y + h;
  for (int row = (top - 8) / 8; 8 + row * 8 < bottom; row++)
    for (int col = 0; col < 8; col++) {
      if (!((row + col) & 1)) continue;
      int x0 = max(x, col * 8), x1 = min(x + w, col * 8 + 8), y0 = max(top, 8 + row * 8), y1 = min(bottom, 16 + row * 8);
      if (x1 > x0 && y1 > y0) d->fillRect(x0, y0, x1 - x0, y1 - y0, FLAG_TILE);
    }
}

static int simScenario = 0;  // the telnet simulator's screen, 0 when off

// The favourite drivers of the settings (the simulator pretends VER is one when none are set)
static bool isFavourite(const char *code) {
  const String &favs = simScenario && ClockwiseParams::getInstance()->f1Drivers.isEmpty() ? String("VER") : ClockwiseParams::getInstance()->f1Drivers;
  return code[0] && favs.indexOf(code) >= 0;
}

// ---- The dim background of the main screens: the flag of the host country of the Grand Prix, drawn from a few stripes
// and shapes ----
static const int BACKDROP_LEVEL = 6;  // out of 32
static int bdTop = 0, bdBottom = 64;  // only these rows are drawn
static uint16_t dimRgb(int r, int g, int b) { return rgb(r * BACKDROP_LEVEL / 32, g * BACKDROP_LEVEL / 32, b * BACKDROP_LEVEL / 32); }
static void bdPixel(MatrixPanel_I2S_DMA *d, int x, int y, uint16_t c) {
  if (y >= bdTop && y < bdBottom) d->drawPixel(x, y, c);
}
static void bdRect(MatrixPanel_I2S_DMA *d, int x, int y, int w, int h, uint16_t c) {
  int y0 = max(y, bdTop), y1 = min(y + h, bdBottom);
  if (y1 > y0 && w > 0) d->fillRect(x, y0, w, y1 - y0, c);
}
static void bdDisc(MatrixPanel_I2S_DMA *d, int cx, int cy, int r, uint16_t c) {
  for (int y = -r; y <= r; y++)
    for (int x = -r; x <= r; x++)
      if (x * x + y * y <= r * r + r / 2) bdPixel(d, cx + x, cy + y, c);
}

// A filled polygon of points relative to (cx, cy), by scan lines
static void bdPolygon(MatrixPanel_I2S_DMA *d, const int8_t (*pts)[2], int n, int cx, int cy, uint16_t c) {
  int ymin = 99, ymax = -99;
  for (int i = 0; i < n; i++) { ymin = min(ymin, (int)pts[i][1]); ymax = max(ymax, (int)pts[i][1]); }
  for (int y = ymin; y <= ymax; y++) {
    int xs[16], m = 0;
    for (int i = 0; i < n && m < 16; i++) {
      int x0 = pts[i][0], y0 = pts[i][1], x1 = pts[(i + 1) % n][0], y1 = pts[(i + 1) % n][1];
      if ((y0 <= y && y < y1) || (y1 <= y && y < y0)) xs[m++] = x0 + (y - y0) * (x1 - x0) / (y1 - y0);
    }
    for (int i = 1; i < m; i++)
      for (int j = i; j > 0 && xs[j] < xs[j - 1]; j--) { int t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t; }
    for (int i = 0; i + 1 < m; i += 2) bdRect(d, cx + xs[i], cy + y, xs[i + 1] - xs[i] + 1, 1, c);
  }
}

// The maple leaf of the Canadian flag, about 24 x 26
static const int8_t MAPLE[][2] = {{0, -13}, {2, -8}, {5, -9}, {4, -4}, {9, -6}, {8, -2}, {12, -1}, {9, 2}, {10, 5}, {5, 4}, {1, 6},
                                   {1, 12}, {-1, 12}, {-1, 6}, {-5, 4}, {-10, 5}, {-9, 2}, {-12, -1}, {-8, -2}, {-9, -6}, {-4, -4},
                                   {-5, -9}, {-2, -8}};

enum Country { C_NONE, C_AU, C_CN, C_JP, C_US, C_CA, C_MC, C_ES, C_AT, C_GB, C_BE, C_HU, C_NL, C_IT, C_AZ, C_SG, C_MX, C_BR, C_QA, C_AE, C_BH, C_SA, C_DE, C_FR, C_TR, C_RU, C_PT };
// Words in ESPN's circuit name and city, lower case, then the country
static const struct { const char *words; Country country; } COUNTRIES[] = {
  {"albert park|melbourne", C_AU}, {"shanghai", C_CN}, {"suzuka", C_JP}, {"miami|americas|austin|las vegas", C_US},
  {"villeneuve|montreal", C_CA}, {"monaco|monte carlo", C_MC}, {"catalunya|barcelona|madring|madrid", C_ES},
  {"red bull ring|spielberg", C_AT}, {"silverstone", C_GB}, {"francorchamps", C_BE}, {"hungaroring|budapest", C_HU},
  {"zandvoort", C_NL}, {"monza|imola|mugello", C_IT}, {"baku", C_AZ}, {"marina bay|singapore", C_SG},
  {"hermanos|mexico", C_MX}, {"interlagos|paulo", C_BR}, {"lusail|losail", C_QA}, {"yas |abu dhabi", C_AE},
  {"sakhir|bahrain", C_BH}, {"jeddah", C_SA}, {"rburgring|hockenheim", C_DE}, {"ricard", C_FR}, {"istanbul", C_TR},
  {"sochi", C_RU}, {"algarve|portim", C_PT},
};
static Country countryFor(const char *text) {
  char lower[80];
  size_t n = 0;
  for (; text[n] && n < sizeof(lower) - 1; n++) lower[n] = text[n] >= 'A' && text[n] <= 'Z' ? text[n] + 32 : text[n];
  lower[n] = 0;
  for (const auto &c : COUNTRIES) {
    const char *w = c.words;
    while (*w) {
      const char *end = strchr(w, '|');
      size_t len = end ? end - w : strlen(w);
      char word[24];
      memcpy(word, w, len);
      word[len] = 0;
      if (strstr(lower, word)) return c.country;
      w += len + (end ? 1 : 0);
    }
  }
  return C_NONE;
}

// The flag, edge to edge (64 x 40) from y Y; each flag as a few stripes and one or two small shapes
static void drawFlag(MatrixPanel_I2S_DMA *d, Country country, int Y) {
  const int X = 0, W2 = 64, H = 40;
  const uint16_t WH = dimRgb(255, 255, 255), RD = dimRgb(225, 30, 40), BL = dimRgb(30, 70, 200), GR = dimRgb(20, 150, 70),
                 YE = dimRgb(255, 205, 0), BK = dimRgb(0, 0, 0);
  auto h3 = [&](uint16_t a, uint16_t b, uint16_t c) { bdRect(d, X, Y, W2, H / 3, a); bdRect(d, X, Y + H / 3, W2, H / 3, b); bdRect(d, X, Y + 2 * (H / 3), W2, H - 2 * (H / 3), c); };
  auto v3 = [&](uint16_t a, uint16_t b, uint16_t c) { bdRect(d, X, Y, W2 / 3, H, a); bdRect(d, X + W2 / 3, Y, W2 / 3, H, b); bdRect(d, X + 2 * (W2 / 3), Y, W2 - 2 * (W2 / 3), H, c); };
  auto h2 = [&](uint16_t a, uint16_t b) { bdRect(d, X, Y, W2, H / 2, a); bdRect(d, X, Y + H / 2, W2, H - H / 2, b); };
  auto union_jack = [&](int x, int y, int w, int h) {
    bdRect(d, x, y, w, h, BL);
    for (int i = 0; i < w; i++) {  // the diagonals
      int j = i * h / w;
      for (int t = -1; t <= 1; t++) { bdPixel(d, x + i, y + j + t, WH); bdPixel(d, x + i, y + h - 1 - j + t, WH); }
    }
    bdRect(d, x, y + h / 2 - 3, w, 6, WH);
    bdRect(d, x + w / 2 - 3, y, 6, h, WH);
    bdRect(d, x, y + h / 2 - 1, w, 2, RD);
    bdRect(d, x + w / 2 - 1, y, 2, h, RD);
  };
  auto crescent = [&](int cx, int cy, int r, uint16_t on, uint16_t off) { bdDisc(d, cx, cy, r, on); bdDisc(d, cx + r / 2 + 1, cy, r - 1, off); };
  switch (country) {
    case C_AU: bdRect(d, X, Y, W2, H, BL); union_jack(X, Y, W2 / 2, H / 2);
      bdRect(d, X + 12, Y + 24, 3, 3, WH);
      for (int k = 0; k < 4; k++) bdRect(d, X + 42 + (k % 2) * 12, Y + 6 + (k / 2) * 16 + (k == 1 ? 6 : 0), 2, 2, WH);
      bdRect(d, X + 52, Y + 22, 2, 2, WH);
      break;
    case C_CN: bdRect(d, X, Y, W2, H, RD); bdDisc(d, X + 9, Y + 8, 4, YE); break;
    case C_JP: bdRect(d, X, Y, W2, H, WH); bdDisc(d, X + W2 / 2, Y + H / 2, 10, RD); break;
    case C_US:
      for (int k = 0; k < 13; k++) bdRect(d, X, Y + k * H / 13, W2, (k + 1) * H / 13 - k * H / 13, k % 2 ? WH : RD);
      bdRect(d, X, Y, W2 * 2 / 5, H * 7 / 13, BL);
      for (int r = 0; r < 5; r++) for (int c = 0; c < 6; c++) bdRect(d, X + 3 + c * 4, Y + 3 + r * 4, 1, 1, WH);
      break;
    case C_CA: bdRect(d, X + 16, Y, W2 - 32, H, WH); bdRect(d, X, Y, 16, H, RD); bdRect(d, X + W2 - 16, Y, 16, H, RD);
      bdPolygon(d, MAPLE, sizeof(MAPLE) / sizeof(MAPLE[0]), X + W2 / 2, Y + H / 2, RD);
      break;
    case C_MC: h2(RD, WH); break;
    case C_ES: bdRect(d, X, Y, W2, H, RD); bdRect(d, X, Y + H / 4, W2, H / 2, YE); break;
    case C_AT: h3(RD, WH, RD); break;
    case C_GB: union_jack(X, Y, W2, H); break;
    case C_BE: v3(BK, YE, RD); break;
    case C_HU: h3(RD, WH, GR); break;
    case C_NL: h3(RD, WH, BL); break;
    case C_IT: v3(GR, WH, RD); break;
    case C_AZ: h3(dimRgb(0, 150, 200), RD, GR); crescent(X + 27, Y + H / 2, 6, WH, RD); bdRect(d, X + 37, Y + H / 2 - 1, 3, 3, WH); break;
    case C_SG: h2(RD, WH);
      crescent(X + 12, Y + 9, 7, WH, RD);  // the crescent, with the five stars in a circle in its opening
      for (int k = 0; k < 5; k++) {
        float angle = (-90 + k * 72) * 3.14159f / 180;
        int sx = X + 16 + (int)roundf(4.2f * cosf(angle)), sy = Y + 9 + (int)roundf(4.2f * sinf(angle));
        bdPixel(d, sx, sy, WH); bdPixel(d, sx - 1, sy, WH); bdPixel(d, sx + 1, sy, WH); bdPixel(d, sx, sy - 1, WH); bdPixel(d, sx, sy + 1, WH);
      }
      break;
    case C_MX: v3(GR, WH, RD); bdDisc(d, X + W2 / 2, Y + H / 2, 8, dimRgb(150, 100, 40)); break;
    case C_BR: bdRect(d, X, Y, W2, H, GR);
      for (int j = -17; j <= 17; j++) bdRect(d, X + W2 / 2 - (28 - abs(j) * 28 / 18), Y + H / 2 + j, 2 * (28 - abs(j) * 28 / 18), 1, YE);
      bdDisc(d, X + W2 / 2, Y + H / 2, 9, BL);
      break;
    case C_QA: bdRect(d, X, Y, W2, H, dimRgb(140, 20, 60)); bdRect(d, X, Y, 17, H, WH);
      for (int k = 0; k < 10; k++) bdRect(d, X + 17, Y + k * 4, 4 - (k % 2) * 3, 4, WH);
      break;
    case C_AE: bdRect(d, X, Y, 14, H, RD); bdRect(d, X + 14, Y, W2 - 14, 13, GR); bdRect(d, X + 14, Y + 13, W2 - 14, 13, WH); bdRect(d, X + 14, Y + 26, W2 - 14, 13, BK); break;
    case C_BH: bdRect(d, X, Y, W2, H, RD); bdRect(d, X, Y, 17, H, WH);
      for (int k = 0; k < 10; k++) bdRect(d, X + 17, Y + k * 4, 4 - (k % 2) * 3, 4, WH);
      break;
    case C_SA: bdRect(d, X, Y, W2, H, GR); bdRect(d, X + 14, Y + 12, 36, 2, WH); bdRect(d, X + 14, Y + 26, 36, 2, WH); break;
    case C_DE: h3(BK, RD, YE); break;
    case C_FR: v3(BL, WH, RD); break;
    case C_TR: bdRect(d, X, Y, W2, H, RD); crescent(X + 24, Y + H / 2, 8, WH, RD); bdRect(d, X + 36, Y + H / 2 - 1, 3, 3, WH); break;
    case C_RU: h3(WH, BL, RD); break;
    case C_PT: bdRect(d, X, Y, 25, H, GR); bdRect(d, X + 25, Y, W2 - 25, H, RD); bdDisc(d, X + 25, Y + H / 2, 7, YE); break;
    default: break;
  }
}

// The flag of the host country of this weekend's Grand Prix, or else of the next one; none when the circuit is not known
static Country backdropCountry() {
  if (shortPanel() || !ClockwiseParams::getInstance()->f1Flags) return C_NONE;  // an option, off unless chosen
  const char *circuit = snap.weekend && snap.circuit[0] ? snap.circuit : snap.upcomingCount ? snap.upcoming[0].circuit : "";
  return countryFor(circuit);
}

// Where the flag starts: 40 rows from here, so its bottom is the row above the bottom bar (y 56)
static const int FLAG_TOP = 16;

// The chequered flag of the results over the rows y0 to y1 (not included): squares of 8, the same everywhere
static void chequer(MatrixPanel_I2S_DMA *d, int y0, int y1) {
  for (int block = y0 / 8; block * 8 < y1; block++)
    for (int col = 0; col < W / 8; col++) {
      if ((block + col) & 1) continue;
      int from = max(y0, block * 8), to = min(y1, block * 8 + 8);
      if (to > from) d->fillRect(col * 8, from, 8, to - from, FLAG_TILE);
    }
}

// The background, in the rows from top to bottom: the chequered flag behind the clock, then the country's flag (or more chequers)
static void drawBackdrop(MatrixPanel_I2S_DMA *d, int top, int bottom) {
  if (shortPanel()) return;
  bdTop = top;
  bdBottom = bottom;
  chequer(d, max(top, 0), min(bottom, FLAG_TOP));
  Country country = backdropCountry();
  if (country != C_NONE) {
    drawFlag(d, country, FLAG_TOP);
  } else {
    chequer(d, max(top, FLAG_TOP), min(bottom, 56));
  }
}

// The bottom bar: one row of the same squares, from the divider's row down
static void drawBottomChequers(MatrixPanel_I2S_DMA *d) {
  d->fillRect(0, 56, W, 8, 0);
  chequer(d, 56, 64);
}

// Is the flag out? A race: as soon as the leader has finished; practice and qualifying: when the clock reads 0:00
static bool flagOn(const F1Ticker::Session &s) {
  if (shortPanel()) return false;
  if (s.tag == 'R' || s.tag == 'S') return s.finished;
  return strcmp(s.clock, "0:00") == 0;
}

// Rows that change place, and the pages of a result list, glide to where they go instead of jumping
static const unsigned long GLIDE_MS = 350;
static float glideEase(unsigned long since) {
  if (since >= GLIDE_MS) return 1;
  float p = (float)since / GLIDE_MS;
  return p * p * (3 - 2 * p);
}

// The live list: where each driver was (from) and is going (to), by code, and when the move started
struct GlideSlot { char code[4]; int y; };
static GlideSlot glideFrom[F1Ticker::ROWS], glideTo[F1Ticker::ROWS];
static int glideFromN = 0, glideToN = 0;
static unsigned long glideAt = 0;

static int glideEnterY = 0;  // where a driver who was not in the list comes in from: just below it

static int glideStart(const char *code, int fallback) {
  for (int i = 0; i < glideFromN; i++)
    if (strcmp(glideFrom[i].code, code) == 0) return glideFrom[i].y;
  return glideFromN ? glideEnterY : fallback;
}

// Where a driver is drawn now, on the way from glideFrom to glideTo
static int glideNow(const char *code, int target) {
  return glideStart(code, target) + (int)roundf((target - glideStart(code, target)) * glideEase(millis() - glideAt));
}

// A new layout for the live list: the drivers glide from where they are drawn now
static void glideTo_(const GlideSlot *layout, int count) {
  bool same = count == glideToN;
  for (int i = 0; same && i < count; i++) same = layout[i].y == glideTo[i].y && strcmp(layout[i].code, glideTo[i].code) == 0;
  if (same) return;
  GlideSlot shown[F1Ticker::ROWS];
  for (int i = 0; i < glideToN; i++) {
    strcpy(shown[i].code, glideTo[i].code);
    shown[i].y = glideNow(glideTo[i].code, glideTo[i].y);
  }
  memcpy(glideFrom, shown, sizeof(glideFrom));
  glideFromN = glideToN;
  memcpy(glideTo, layout, sizeof(GlideSlot) * count);
  glideToN = count;
  glideAt = millis();
}

// A page of a result list: the rows of the page before slide up and out while the next ones come in
struct PageSlide { int cur = -1, prev = 0; unsigned long at = 0; };
static PageSlide slideList, slideBetween;
static void slideSeen(PageSlide &s, int first) {
  if (s.cur < 0) { s.cur = first; s.at = 0; }
  else if (first != s.cur) { s.prev = s.cur; s.cur = first; s.at = millis(); }
}
static bool slideActive(const PageSlide &s) { return s.at && millis() - s.at < GLIDE_MS + 60; }
static int slideShift(const PageSlide &s, int total) { return slideActive(s) ? (int)roundf(total * glideEase(millis() - s.at)) : 0; }

static bool glideActive() { return glideToN && millis() - glideAt < GLIDE_MS + 60; }

static void liveColors(const F1Ticker::Session &s, uint16_t &fill, uint16_t &ink, uint16_t &tab) {
  tab = 0;
  if (s.flag == 'y') { fill = GOLD; ink = BLACK; }
  else if (s.flag == 'r') { fill = RED_FLAG_BAR; ink = WHITE; }
  else { fill = HEADER_NAVY; ink = WHITE; tab = s.tag == 'P' ? TAB_QUIET : F1_RED; }
}

// Just the time left, over the header's fill: a countdown every second without redrawing the whole screen
static void drawLiveClock(MatrixPanel_I2S_DMA *d, const F1Ticker::Session &s, int hour, int minute) {
  char right[12];
  liveRight(s, right, sizeof(right), hour, minute);
  uint16_t fill, ink, tab;
  liveColors(s, fill, ink, tab);
  d->fillRect(33, 0, W - 33, 8, fill);
  drawHeaderText(d, right, W - 2 - textWidth(right), 1, ink);
}

// How many places have a driver
static int filledRows(const F1Ticker::Session &s) {
  int n = 0;
  for (int i = 0; i < F1Ticker::ROWS; i++)
    if (s.rows[i].code[0]) n = i + 1;
  return n;
}

static bool anyFavourite(const F1Ticker::Session &s) {
  for (int i = 0; i < F1Ticker::ROWS; i++)
    if (isFavourite(s.rows[i].code)) return true;
  return false;
}

// Which places to show on `slots` lines: the top three, every favourite, then the places around the best placed
// favourite (or just on from the top when there is none, or when that favourite is in the top three anyway).
// Returns how many, in order, in `out`.
static int pickPlaces(const F1Ticker::Session &s, int slots, int *out) {
  bool chosen[F1Ticker::ROWS] = {false};
  int n = filledRows(s), count = 0, best = -1;
  auto take = [&](int i) { if (i >= 0 && i < n && !chosen[i] && count < slots) { chosen[i] = true; count++; } };
  for (int i = 0; i < 3; i++) take(i);
  for (int i = 0; i < n; i++)
    if (isFavourite(s.rows[i].code)) {
      if (best < 0) best = i;
      take(i);
    }
  if (best >= 3) {
    for (int dist = 1; count < slots && dist < n; dist++) {
      take(best - dist);
      take(best + dist);
    }
  }
  for (int i = 0; count < slots && i < n; i++) take(i);
  int m = 0;
  for (int i = 0; i < n; i++)
    if (chosen[i]) out[m++] = i;
  return m;
}

static void drawLive(MatrixPanel_I2S_DMA *d, const F1Ticker::Session &s, int hour, int minute) {
  // Marked live, but nothing is happening yet (a delayed start): say so, with the time to look at meanwhile
  if (s.rows[0].code[0] == 0) {
    drawHeader(d, s.name, "WAITING", HEADER_NAVY, WHITE, TAB_QUIET);
    clockLeft = (W - clockWidth()) / 2;
    drawClock(d, hour, minute, shortPanel() ? 14 : 24, !shortPanel());
    return;
  }
  char right[12];
  liveRight(s, right, sizeof(right), hour, minute);
  uint16_t fill, ink, tab;
  liveColors(s, fill, ink, tab);
  drawHeader(d, s.flag == 'r' ? "RED FLAG" : s.name, right, fill, ink, tab);
  bool flag = flagOn(s);
  if (flag) flagFill(d, 0, 8, W, 56);
  bool race = s.tag == 'R' || s.tag == 'S';
  int shown = shortPanel() ? 4 : 9;  // 32 rows: from y 9, the last one ends on the bottom row
  int top = shortPanel() ? 9 : 10;
  int places[F1Ticker::ROWS];
  int count = pickPlaces(s, shown, places);
  GlideSlot layout[F1Ticker::ROWS];
  for (int k = 0; k < count; k++) {
    strcpy(layout[k].code, s.rows[places[k]].code);
    layout[k].y = top + k * 6;
  }
  glideEnterY = top + shown * 6;
  glideTo_(layout, count);
  // What is drawn: the places of the list, and while it glides also the drivers who just dropped out of it (they
  // slide off the bottom). Whoever travels furthest is drawn last, so he passes in front of the others.
  struct Item { int row, y, moved, key; };
  Item items[F1Ticker::ROWS * 2];
  int n = 0;
  bool moving = glideActive();
  for (int k = 0; k < count; k++) {
    int i = places[k], y = glideNow(layout[k].code, layout[k].y), moved = movedDir(s.rows[i].code);
    items[n++] = Item{i, y, moved, abs(layout[k].y - glideStart(layout[k].code, layout[k].y)) * 2 + (moved > 0 ? 1 : 0)};
  }
  if (moving)
    for (int f = 0; f < glideFromN; f++) {
      bool kept = false;
      for (int k = 0; k < count && !kept; k++) kept = strcmp(layout[k].code, glideFrom[f].code) == 0;
      if (kept) continue;
      for (int i = 0; i < F1Ticker::ROWS; i++)
        if (strcmp(s.rows[i].code, glideFrom[f].code) == 0) {
          int y = glideFrom[f].y + (int)roundf((glideEnterY - glideFrom[f].y) * glideEase(millis() - glideAt));
          items[n++] = Item{i, y, movedDir(s.rows[i].code), abs(glideEnterY - glideFrom[f].y) * 2};
          break;
        }
    }
  for (int a = 1; a < n; a++)  // insertion sort, few items
    for (int b = a; b > 0 && items[b].key < items[b - 1].key; b--) std::swap(items[b], items[b - 1]);
  for (int a = 0; a < n; a++) {
    const Item &it = items[a];
    if (moving) {  // opaque, so what it passes is covered
      if (flag) {
        flagFill(d, 0, it.y - 1, W, 7);
      } else {
        d->fillRect(0, it.y - 1, W, 7, 0);
      }
    }
    drawPlace(d, it.row + 1, s.rows[it.row], it.y, race, it.moved, isFavourite(s.rows[it.row].code));
  }
}


static void whenText(CWDateTime *dateTime, time_t utc, char *out, size_t size);

// The championship places to show: the top three and, when a favourite driver is further down, the best placed one
static int standingsShown(int *out, int rows = 3) {
  int n = 0;
  for (int i = 0; i < snap.standingsCount && n < 3; i++) out[n++] = i;
  int favourite = -1;
  for (int i = 3; i < snap.standingsCount && favourite < 0; i++)
    if (isFavourite(snap.standings[i].code)) favourite = i;
  int fill = favourite >= rows ? rows - 1 : rows;  // the places after the top 3 up to the rows, the last one for a favourite further down
  for (int i = 3; i < snap.standingsCount && n < fill; i++) out[n++] = i;
  if (favourite >= rows) out[n++] = favourite;
  return n;
}

// 32 rows: the line under the clock. Idle: the top three of the championship, then the next Grand Prix.
// Between sessions: the top three of the one that just finished, then the next one.
static void drawInfoRow(MatrixPanel_I2S_DMA *d, CWDateTime *dateTime, bool idle, int page, int y) {
  int shown[4];
  int places = idle ? standingsShown(shown) : 3;
  bool next = idle ? snap.upcomingCount > 0 : snap.next.valid;
  int count = places + (next ? 1 : 0);
  if (count == 0) return;
  int item = page % count;
  if (item < places) {
    if (idle) {
      F1Ticker::Row row;
      int k = shown[item];
      strlcpy(row.code, snap.standings[k].code, sizeof(row.code));
      row.color = snap.standings[k].color;
      drawPlace(d, k + 1, row, y, false, 0, isFavourite(row.code));
      char points[8];
      snprintf(points, sizeof(points), "%d", snap.standings[k].points);
      drawTextRight(d, points, W - 2, y, k == 0 ? MINUTES : GREY);
    } else {
      drawPlace(d, item + 1, snap.last.rows[item], y, snap.last.tag == 'R' || snap.last.tag == 'S');
    }
  } else if (idle) {
    time_t nowUtc = ezt::now();
    for (int i = 0; i < snap.upcomingCount; i++) {
      if (snap.upcoming[i].start <= nowUtc) continue;
      char when[8];
      whenText(dateTime, snap.upcoming[i].start, when, sizeof(when));
      drawMarquee(d, snap.upcoming[i].city, 2, y, W - 2 - textWidth(when) - 2, WHITE);  // a gap before the date
      drawTextRight(d, when, W - 2, y, MINUTES);
      break;
    }
  } else {
    LocalTime t = localParts(dateTime, snap.next.start);
    char when[12];
    snprintf(when, sizeof(when), "%s %02d:%02d", DAYS[t.weekday], t.hour, t.minute);
    drawText(d, snap.next.name, 2, y, WHITE);
    drawTimeRight(d, when, W - 2, y, MINUTES);
  }
}

// The bottom row: the next session, with a pixel of margin above and below its text
// All the sessions still to come this weekend, one after the other, sliding up like the pages of the lists
static PageSlide slideNext;

static int comingCount() { return snap.comingCount ? snap.comingCount : snap.next.valid ? 1 : 0; }
static int comingIndex() { int n = comingCount(); return n ? (int)(millis() / pageMs() % n) : 0; }

// A session as the bottom row shows it: P1, SQ, SR, Q, R
static const char *shortSession(const char *name) {
  static char buffer[4];
  if (!strcmp(name, "QUALI")) return "Q";
  if (!strcmp(name, "SPRINT")) return "SR";
  if (!strcmp(name, "RACE")) return "R";
  if (!strncmp(name, "FP", 2)) { strlcpy(buffer, name + 1, sizeof(buffer)); return buffer; }  // FP1 is P1 here
  return name;
}

// The coming session along the bottom: its name at the left, the day and the time at the right (the time only today)
static void drawComing(MatrixPanel_I2S_DMA *d, CWDateTime *dateTime, int index, int y) {
  const char *name = snap.comingCount ? snap.coming[index].name : snap.next.name;
  time_t start = snap.comingCount ? snap.coming[index].start : snap.next.start;
  LocalTime t = localParts(dateTime, start);
  char when[12];
  bool today = dateTime->utcToLocal(start) / SECS_PER_DAY == dateTime->localNow() / SECS_PER_DAY;
  if (today) snprintf(when, sizeof(when), "%02d:%02d", t.hour, t.minute);
  else snprintf(when, sizeof(when), "%s %02d:%02d", DAYS[t.weekday], t.hour, t.minute);
  drawText(d, name, 0, y, WHITE);
  drawTextRight(d, when, W, y, MINUTES);
}

static void drawNextRow(MatrixPanel_I2S_DMA *d, CWDateTime *dateTime) {
  drawBottomChequers(d);
  int n = comingCount();
  if (!n) return;
  int index = comingIndex();
  slideSeen(slideNext, index);
  clipTop = 56;
  if (slideActive(slideNext)) {
    int shift = slideShift(slideNext, 8);
    drawComing(d, dateTime, slideNext.prev % n, 56 - shift);
    drawComing(d, dateTime, index, 56 + 8 - shift);
  } else {
    drawComing(d, dateTime, index, 56);
  }
  clipTop = 0;
}

// The first place shown under the winner on a page; the last page is moved back so it ends on the last place, full
static int firstShown(int n, int page, int slots) {
  if (n <= slots + 1) return 1;
  return min(1 + page * slots, n - slots);
}

// After a session, for the time set in the settings: the whole classification in the live list's look, the winner
// fixed and the other places scrolling a page at a time. The header has the session and the time.
static void drawResultList(MatrixPanel_I2S_DMA *d, const F1Ticker::Session &s, int hour, int minute, int page) {
  bool race = s.tag == 'R' || s.tag == 'S';
  int n = filledRows(s);
  bool favourites = n > 9 && anyFavourite(s);  // the favourites and their surroundings stay, nothing scrolls
  int first = firstShown(n, page, 8);
  if (!favourites) slideSeen(slideList, first);
  flagFill(d, 0, 8, W, 56);  // the results of a finished session: the chequered flag behind them
  // The scrolling places go first, so the header and the winner can be drawn over the part that slides out
  if (!favourites) {
    bool sliding = slideActive(slideList);
    int shift = slideShift(slideList, 48);
    for (int slot = 1; slot < 9; slot++) {
      int i = first + slot - 1, y = 10 + slot * 6;
      if (sliding) {
        int before = slideList.prev + slot - 1;
        if (before < n) drawPlace(d, before + 1, s.rows[before], y - shift, race, 0, isFavourite(s.rows[before].code));
        if (i < n) drawPlace(d, i + 1, s.rows[i], y + 48 - shift, race, 0, isFavourite(s.rows[i].code));
      } else if (i < n) {
        drawPlace(d, i + 1, s.rows[i], y, race, 0, isFavourite(s.rows[i].code));
      }
    }
    if (sliding) {
      d->fillRect(0, 0, W, 8, 0);
      flagFill(d, 0, 8, W, 8);
    }
  }
  const char *name = !strcmp(s.name, "QUALI") ? "Q" : !strcmp(s.name, "SPRINT") ? "SR" : !strcmp(s.name, "RACE") ? "R" : !strncmp(s.name, "FP", 2) ? s.name + 1 : s.name;  // FP1 is P1 here
  drawHeader(d, "", "", HEADER_NAVY, WHITE);
  drawText(d, name, 0, 1, FASTEST, true);  // all the way to the left, in the best-time purple,
  drawHeaderTime(d, hour, minute, W - 2, 1, MINUTES);  // and the time 2 pixels from the right edge and 1 from the top, in the clock's green
  if (favourites) {
    int places[F1Ticker::ROWS];
    int count = pickPlaces(s, 9, places);
    for (int k = 0; k < count; k++) drawPlace(d, places[k] + 1, s.rows[places[k]], 10 + k * 6, race, 0, isFavourite(s.rows[places[k]].code));
    return;
  }
  drawPlace(d, 1, s.rows[0], 10, race, 0, isFavourite(s.rows[0].code));
}

static void drawBetween(MatrixPanel_I2S_DMA *d, CWDateTime *dateTime, int hour, int minute, int page) {
  if (shortPanel()) {
    drawClock(d, hour, minute, 1);
    d->drawFastHLine(2, 16, 60, LINE);
    drawInfoRow(d, dateTime, false, page, 19);
    return;
  }
  // The winner stays; the places after it follow three at a time, so the whole classification gets its turn.
  // Those places go first: the part that slides out over the clock or the next session is wiped before those are drawn
  bool race = snap.last.tag == 'R' || snap.last.tag == 'S';
  int n = filledRows(snap.last);
  int first = firstShown(n, page, 4);
  slideSeen(slideBetween, first);
  drawBackdrop(d, 0, 64);
  bool sliding = slideActive(slideBetween);
  int shift = slideShift(slideBetween, 24);
  for (int slot = 0; slot < 4; slot++) {
    int i = first + slot, y = 29 + slot * 6;
    if (sliding) {
      int before = slideBetween.prev + slot;
      if (before < n) drawPlace(d, before + 1, snap.last.rows[before], y - shift, race, 0, isFavourite(snap.last.rows[before].code));
      if (i < n) drawPlace(d, i + 1, snap.last.rows[i], y + 24 - shift, race, 0, isFavourite(snap.last.rows[i].code));
    } else if (i < n) {
      drawPlace(d, i + 1, snap.last.rows[i], y, race, 0, isFavourite(snap.last.rows[i].code));
    }
  }
  if (sliding) {
    d->fillRect(0, 0, W, 29, 0);
    drawBackdrop(d, 0, 29);
    d->fillRect(0, 54, W, 10, 0);
    drawBackdrop(d, 54, 64);
  }
  d->drawFastHLine(0, 20, W, RULE);  // between the clock and the winner, and under the places
  d->drawFastHLine(0, 54, W, RULE);
  clockLeft = W - clockWidth();  // at the right, the session name on the left
  drawClock(d, hour, minute, 1, true);
  // The session the results are of, left of the clock: a light grey, to read well on the squares
  if (snap.last.name[0]) drawText(d, shortSession(snap.last.name), 0, 11, rgb(205, 210, 220), true);
  drawPlace(d, 1, snap.last.rows[0], 23, race, 0, isFavourite(snap.last.rows[0].code));
  drawNextRow(d, dateTime);
}

// When the weekend starts: TODAY, a weekday for the coming six days ("FRI"), a date after that ("24/10")
static void whenText(CWDateTime *dateTime, time_t utc, char *out, size_t size) {
  time_t local = dateTime->utcToLocal(utc);
  long days = local / SECS_PER_DAY - dateTime->localNow() / SECS_PER_DAY;
  if (days <= 0) strlcpy(out, "TODAY", size);
  else if (days <= 6) strlcpy(out, DAYS[(local / SECS_PER_DAY + 4) % 7], size);
  else snprintf(out, size, "%02d/%02d", ezt::day(local), ezt::month(local));
}

// No header: the clock on top, then the championship, and the coming race along the bottom
static void drawIdle(MatrixPanel_I2S_DMA *d, CWDateTime *dateTime, int hour, int minute, int page) {
  if (shortPanel()) {
    drawClock(d, hour, minute, 1);
    d->drawFastHLine(2, 16, 60, LINE);
    drawInfoRow(d, dateTime, true, page, 19);
    return;
  }
  drawBackdrop(d, 0, 64);
  bigClock = true;
  clockLeft = (W - clockWidth()) / 2;
  drawClock(d, hour, minute, 1, true);
  bigClock = false;
  int shown[4];
  int places = standingsShown(shown, 4);
  d->drawFastHLine(0, 26, W, RULE);
  for (int i = 0; i < places; i++) {
    int k = shown[i];
    int y = 29 + i * 6;  // 2 clear rows to both lines
    F1Ticker::Row row;
    strlcpy(row.code, snap.standings[k].code, sizeof(row.code));
    row.color = snap.standings[k].color;
    drawPlace(d, k + 1, row, y, false, 0, isFavourite(row.code));
    char points[8];
    snprintf(points, sizeof(points), "%d", snap.standings[k].points);
    drawTextRight(d, points, W - 2, y, k == 0 ? MINUTES : GREY);
    if (i == 0 && k == 0) {  // the leading team's car, in the middle of the empty part of the row
      static const char *const CAR[5] = {"XX....XX.....", "XXXXXXXXXXX..", ".XXXXXXXXXXXX", ".WWW...WWW...", ".WWW...WWW..."};
      int left = (12 + 11 + 1 + W - 2 - (int)strlen(points) * 4) / 2 - 6;
      for (int j = 0; j < 5; j++)
        for (int c = 0; c < 13; c++)
          if (CAR[j][c] != '.') d->drawPixel(left + c, y + j, CAR[j][c] == 'X' ? row.color : GREY);
    }
  }
  d->drawFastHLine(0, 54, W, RULE);  // a second line, between the list and the bottom bar
  drawBottomChequers(d);
  time_t nowUtc = ezt::now();
  for (int i = 0; i < snap.upcomingCount; i++) {
    if (snap.upcoming[i].start <= nowUtc) continue;
    char when[8];
    whenText(dateTime, snap.upcoming[i].start, when, sizeof(when));
    drawMarquee(d, snap.upcoming[i].city, 0, 56, W - textWidth(when) - 2, WHITE);  // a gap before the date
    drawTextRight(d, when, W, 56, MINUTES);
    break;
  }
}

// Made-up screens for the telnet simulator, so every look can be checked outside a race weekend
static const char *const SIM_NAMES[] = {"off", "idle", "between sessions", "race", "safety car", "red flag", "qualifying",
                                        "practice", "full results"};
static const int SIM_COUNT = 9;
static const char *const SIM_GRID[] = {"ANT", "RUS", "HAM", "LEC", "NOR", "PIA", "VER", "ALO", "GAS", "HAD", "STR", "COL",
                                       "ALB", "SAI", "LAW", "LIN", "HUL", "BOR", "OCO", "BEA", "PER", "BOT"};

static void simFill(F1Ticker::Session &s, const char *name, char tag, bool live) {
  s.valid = true;
  s.live = live;
  s.tag = tag;
  strlcpy(s.name, name, sizeof(s.name));
  s.count = F1Ticker::ROWS;
  static const char *const GAPS[] = {"+.088", "+.201", "+.317", "+.455", "+.620", "+.742", "+.911", "+1.030", "+1.2", "+1.5", "+1.9",
                                     "+2.3", "+2.8", "+3.1", "+3.6", "+4.0", "+4.4", "+5.1", "+5.9", "+6.5", "+7.4"};
  for (int i = 0; i < F1Ticker::ROWS; i++) {
    // every 7 seconds the order changes: a swap, then a driver dropping nine places, then back, to see the markers
    int grid = i;
    int phase = (millis() / 7000) % 3;
    if (live && phase == 1) grid = i == 3 ? 4 : i == 4 ? 3 : i;
    if (live && phase == 2) grid = i == 11 ? 2 : (i >= 2 && i < 11) ? i + 1 : i;  // the third driver drops to twelfth
    strlcpy(s.rows[i].code, SIM_GRID[grid], sizeof(s.rows[i].code));
    s.rows[i].color = F1Ticker::driverColor(SIM_GRID[grid]);
    s.rows[i].fastest = (tag == 'R' || tag == 'S') && strcmp(SIM_GRID[grid], "VER") == 0;
    if (i == 0) strlcpy(s.rows[0].time, tag == 'R' || tag == 'S' ? "LEAD" : "1:29.412", sizeof(s.rows[0].time));
    else strlcpy(s.rows[i].time, GAPS[i - 1], sizeof(s.rows[i].time));
  }
}

static void simSnapshot(F1Ticker::Snapshot &s) {
  s = F1Ticker::Snapshot();
  const int points[] = {372, 351, 289};
  for (int i = 0; i < 22; i++) {
    strlcpy(s.standings[i].code, SIM_GRID[i], sizeof(s.standings[i].code));
    s.standings[i].color = F1Ticker::driverColor(SIM_GRID[i]);
    s.standings[i].points = i < 3 ? points[i] : 250 - i * 10;
  }
  s.standingsCount = 22;
  strlcpy(s.circuit, "Marina Bay Street Circuit Singapore", sizeof(s.circuit));
  strlcpy(s.upcoming[0].city, "MEXICO CITY", sizeof(s.upcoming[0].city));
  strlcpy(s.upcoming[0].circuit, "Marina Bay Street Circuit", sizeof(s.upcoming[0].circuit));  // the only outline there is yet
  s.upcoming[0].start = ezt::now() + 17 * SECS_PER_DAY;
  s.upcomingCount = 1;
  switch (simScenario) {
    case 2:
      s.weekend = true;
      simFill(s.last, "QUALI", 'Q', false);
      simFill(s.next, "SPRINT", 'S', false);
      s.next.start = ezt::now() + 20 * 3600;
      strlcpy(s.coming[0].name, "SPRINT", sizeof(s.coming[0].name));
      s.coming[0].start = s.next.start;
      strlcpy(s.coming[1].name, "RACE", sizeof(s.coming[1].name));
      s.coming[1].start = s.next.start + 24 * 3600;
      s.comingCount = 2;
      break;
    case 8:
      s.weekend = true;
      simFill(s.last, "SQ", 'Q', false);
      simFill(s.next, "SPRINT", 'S', false);
      s.next.start = ezt::now() + 20 * 3600;
      strlcpy(s.coming[0].name, "SPRINT", sizeof(s.coming[0].name));
      s.coming[0].start = s.next.start;
      strlcpy(s.coming[1].name, "RACE", sizeof(s.coming[1].name));
      s.coming[1].start = s.next.start + 24 * 3600;
      s.comingCount = 2;
      break;
    case 3:
    case 4:
    case 5:
      s.weekend = true;
      simFill(s.live, "RACE", 'R', true);
      s.live.period = 32;
      s.live.flag = simScenario == 4 ? 'y' : simScenario == 5 ? 'r' : 0;
      s.live.finished = (millis() / 7000) % 3 == 2;  // the chequered flag for the last seconds of each round
      break;
    case 6:
      s.weekend = true;
      simFill(s.live, "QUALI", 'Q', true);
      strlcpy(s.live.clock, "4:12", sizeof(s.live.clock));
      break;
    case 7:
      s.weekend = true;
      simFill(s.live, "FP2", 'P', true);
      strlcpy(s.live.clock, (millis() / 7000) % 3 == 2 ? "0:00" : "23:10", sizeof(s.live.clock));
      break;
  }
}

Clockface::Clockface(MatrixPanel_I2S_DMA* display) {
  _display = display;
  Locator::provide(display);  // the startup logo and status screens draw through the Locator
}

void Clockface::setup(CWDateTime *dateTime) {
  _dateTime = dateTime;
  lastKey[0] = 0;  // whatever was drawn over the face (notification, birthday) must be redrawn
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
    version = 0x80000000UL + simScenario + ((millis() / 7000) % 3) * 16;  // the sim order changes every 7 s
  } else if (!snapKnown || version != snapVersion || (snap.live.valid && millis() / 1000 != snapSecond)) {
    f1Ticker.snapshot(snap);  // while live also every second, for the countdown
    snapVersion = version;
    snapKnown = true;
    snapSecond = millis() / 1000;
  }

  // How long ago the last session ended: seen going from live to finished, or else guessed from its start
  static bool wasLive = false;
  static uint32_t endId = 0;
  static time_t endUtc = 0;
  time_t nowUtc = ezt::now();
  if (snap.live.valid) {
    wasLive = true;
  } else if (snap.last.valid && snap.last.id != endId) {
    endId = snap.last.id;
    int minutes = snap.last.tag == 'R' ? 120 : snap.last.tag == 'S' ? 40 : snap.last.name[0] == 'S' ? 45 : 60;
    endUtc = wasLive ? nowUtc : snap.last.start + minutes * 60;
    wasLive = false;
  }
  bool resultsOn = false;
  if (!snap.live.valid && snap.weekend && snap.last.valid && !shortPanel()) {
    uint32_t window = resultWindowSecs();
    resultsOn = window ? (long)(nowUtc - endUtc) < (long)window
                       : _dateTime->utcToLocal(nowUtc) / SECS_PER_DAY == _dateTime->utcToLocal(endUtc) / SECS_PER_DAY;
    if (simScenario == 8) resultsOn = true;
    else if (simScenario == 2) resultsOn = false;
  }
  enum { LIVE, LIST, BETWEEN, IDLE } view = snap.live.valid ? LIVE : resultsOn ? LIST : snap.weekend && snap.last.valid ? BETWEEN : IDLE;
  // The info line of the short panel rotates, and so do the places after the first on the result screens
  int page = view != LIVE && (shortPanel() || view == BETWEEN || view == LIST) ? millis() / pageMs() : 0;
  if (view == BETWEEN && !shortPanel()) {
    int others = filledRows(snap.last) - 1;
    page = others > 4 ? page % ((others + 3) / 4) : 0;  // four places at a time; the key only changes with the page
  }
  if (view == LIST) {
    int n = filledRows(snap.last);
    page = n > 9 ? page % ((n + 6) / 8) : 0;  // eight places at a time under the winner
  }
  if (view == LIVE && strcmp(snap.live.clock, "0:00") == 0) {
    if (!clockZeroAt) clockZeroAt = millis();  // a countdown stuck at 0:00 turns into the time of day after a while
  } else {
    clockZeroAt = 0;
  }
  updateClockDigits(hour, minute);
  // Where the soft clock is: on the idle and result screens, and on the one of a delayed start. Its digits change on
  // their own, so the minute is not part of the key there
  bool tallClock = !shortPanel() && (view == IDLE || view == BETWEEN || (view == LIVE && !snap.live.rows[0].code[0]));
  int clockY = view == LIVE ? 22 : 1;
  static int lastView = -1;
  if ((int)view != lastView) {  // nothing glides from another screen
    lastView = view;
    slideList.cur = slideBetween.cur = slideNext.cur = -1;
    glideToN = glideFromN = 0;
  }
  bool gliding = (view == LIVE && glideActive()) || (view == LIST && slideActive(slideList)) || (view == BETWEEN && (slideActive(slideBetween) || slideActive(slideNext)));
  uint32_t marks = 0;
  if (view == LIVE) {
    trackMoves(snap.live);
    marks = moveMask(snap.live);
  }

  // Only draw when something changed
  char key[96];
  int rowPage = view == BETWEEN ? comingIndex() : 0;  // which of the coming sessions the bottom row shows
  int pulse = view != LIVE ? pulseLevel() : 0;  // the colon of the time breathes: drawn again at each step
  if (view == LIVE || view == LIST || view == BETWEEN) marqueeMax = 0;  // only the idle screen has a scrolling text
  if (marqueeMax) pulse = pulse * 1000 + marqueeOffset(marqueeMax);  // and a scrolling name moves a pixel at a time
  snprintf(key, sizeof(key), "%lu|%d|%d:%d|%d|%lx|%d|%d", (unsigned long)version, (int)view, tallClock ? 0 : hour, tallClock ? 0 : minute, page, (unsigned long)marks, rowPage, pulse);
  char right[12] = "";
  if (view == LIVE) liveRight(snap.live, right, sizeof(right), hour, minute);
  // Two buffers are drawn in turn and the one drawn on is the frame from two flips ago, so a change is never a patch on
  // top of it: the whole screen is drawn again, also for a moving clock digit or the countdown of a live session
  static bool wasMoving = false;
  bool moving = tallClock && clockMoving();
  bool countdown = view == LIVE && strcmp(right, lastRight) != 0 && snap.live.rows[0].code[0];
  if (strcmp(key, lastKey) == 0) {
    if (!countdown && !moving && !gliding && !(tallClock && wasMoving)) return;
  }
  wasMoving = moving;
  strcpy(lastKey, key);
  strcpy(lastRight, right);

  marqueeMax = 0;  // set again by a text that scrolls
  _display->fillScreen(0);
  switch (view) {
    case LIVE: drawLive(_display, snap.live, hour, minute); break;
    case LIST: drawResultList(_display, snap.last, hour, minute, page); break;
    case BETWEEN: drawBetween(_display, _dateTime, hour, minute, page); break;
    case IDLE: drawIdle(_display, _dateTime, hour, minute, page); break;
  }
  _display->flipDMABuffer();
}

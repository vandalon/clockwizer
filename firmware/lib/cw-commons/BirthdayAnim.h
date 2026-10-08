#pragma once

// Private build only: the birthday screen. Slow fireworks that glow, balloons, a cake with
// flickering candles, confetti, the name in a rainbow wave, the age and the time.
// Everything is a function of the time, so nothing is kept between frames. Drawn into a
// small RGB buffer first, because the panel can't be read back to blend with.
// Mockup: clockwise-mockups/birthday-mockup.py (the numbers below are the same).

#include <Arduino.h>
#include <math.h>

namespace BirthdayAnim {

const float SLOW = 0.5f;   // all motion runs at half speed
const float LOOP = 7.0f;   // seconds (scaled) before the fireworks start over
const int W = 64;

// 3x5 font, 15 bits per glyph, rows top to bottom: A-Z, 0-9, '!', ':', ' '
const uint16_t FONT[] = {
  0x2BED, 0x6BAE, 0x3923, 0x6B6E, 0x79A7, 0x79A4, 0x396B, 0x5BED, 0x7497, 0x126A, 0x5BAD, 0x4927, 0x5FED,
  0x6B6D, 0x2B6A, 0x6BA4, 0x2B7B, 0x6BAD, 0x388E, 0x7492, 0x5B6F, 0x5B6A, 0x5BFD, 0x5AAD, 0x5A92, 0x72A7,
  0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF, 0x2482, 0x0410, 0x0000};

// 4x6 font for the age, 24 bits per glyph, rows top to bottom: A R J I S ! 0-9 ' '
const char FONT2_CHARS[] = "ARJIS!0123456789: ";
const uint32_t FONT2[] = {0x69F999, 0xE9EA99, 0x311196, 0xE4444E, 0x78611E, 0x444404, 0x699996, 0x262227, 0x69124F, 0xE1611E, 0x99F111, 0xF8E196, 0x68E996, 0xF12244, 0x696996, 0x697116, 0x040040, 0x000000};

struct Burst { int8_t x, y; float start; };
const Burst BURSTS[] = {{14, 24, 0.0f}, {48, 20, 1.4f}, {30, 27, 2.7f}, {10, 31, 4.1f}, {52, 30, 5.4f}};
struct Balloon { int8_t x; float phase, hue; };
const Balloon BALLOONS[] = {{5, 0.0f, 0.0f}, {59, 0.35f, 0.55f}, {11, 0.7f, 0.2f}, {53, 0.15f, 0.8f}};

struct RGB { float r, g, b; };

// The 64x64 frame, only there while a birthday is on screen (12 KB the faces can use the rest of the time)
const size_t FB_SIZE = W * 64 * 3;
static uint8_t *fb = nullptr;
static int H = 64;

// False when there is no memory for a frame: the clock then just keeps showing the time
static bool begin() {
  if (!fb) fb = (uint8_t *)malloc(FB_SIZE);
  return fb != nullptr;
}

static void release() {
  free(fb);
  fb = nullptr;
}

static RGB hsv(float h, float s, float v) {
  h = h - floorf(h);
  int i = (int)(h * 6);
  float f = h * 6 - i;
  float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
  switch (i % 6) {
    case 0: return {v * 255, t * 255, p * 255};
    case 1: return {q * 255, v * 255, p * 255};
    case 2: return {p * 255, v * 255, t * 255};
    case 3: return {p * 255, q * 255, v * 255};
    case 4: return {t * 255, p * 255, v * 255};
    default: return {v * 255, p * 255, q * 255};
  }
}

static void px(float fx, float fy, RGB c, float a = 1.0f) {
  int x = lroundf(fx), y = lroundf(fy);
  if (x < 0 || x >= W || y < 0 || y >= H || a <= 0) return;
  if (a > 1) a = 1;
  uint8_t *o = &fb[(y * W + x) * 3];
  o[0] += (c.r - o[0]) * a;
  o[1] += (c.g - o[1]) * a;
  o[2] += (c.b - o[2]) * a;
}

static void rect(int x, int y, int w, int h, RGB c) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) px(x + i, y + j, c);
}

static int glyph(char ch) {
  if (ch >= 'A' && ch <= 'Z') return ch - 'A';
  if (ch >= '0' && ch <= '9') return 26 + ch - '0';
  if (ch == '!') return 36;
  if (ch == ':') return 37;
  return 38;
}

// col < 0: rainbow wave over the letters, starting at hue offset 'hue'
static void text(const char *s, int x, int y, RGB col, float hue = -1) {
  for (int k = 0; s[k]; k++) {
    uint16_t g = FONT[glyph(s[k])];
    RGB c = hue < 0 ? col : hsv(hue - k * 0.12f, 0.7f, 1);
    for (int r = 0; r < 5; r++)
      for (int cc = 0; cc < 3; cc++)
        if (g & (1 << (14 - (r * 3 + cc)))) px(x + k * 4 + cc, y + r, c);
  }
}

static void centerText(const char *s, int y, RGB col, float hue = -1) {
  int w = strlen(s) * 4 - 1;
  text(s, 32 - w / 2, y, col, hue);
}

// The age and "!" at 2x the 4x6 font: gold gradient, drop shadow, a shimmer sweeping across
// and twinkling sparkles around it
static void bigText(const char *s, int y, float t, int cx = 32) {
  int n = strlen(s), w = n * 10 - 2, x0 = cx - w / 2;
  for (int pass = 0; pass < 2; pass++) {  // 0: shadow, 1: letters
    float sweep = fmodf(t * 14, w + 40) - 20;
    for (int k = 0; k < n; k++) {
      const char *p = strchr(FONT2_CHARS, s[k]);
      uint32_t g = FONT2[p ? p - FONT2_CHARS : sizeof(FONT2_CHARS) - 2];
      for (int r = 0; r < 6; r++)
        for (int c = 0; c < 4; c++) {
          if (!(g & (1UL << (23 - (r * 4 + c))))) continue;
          for (int dy = 0; dy < 2; dy++)
            for (int dx = 0; dx < 2; dx++) {
              int x = x0 + k * 10 + c * 2 + dx, yy = y + r * 2 + dy;
              if (pass == 0) {
                px(x + 1, yy + 1, {110, 45, 10});
              } else {
                float f = (yy - y) / 11.0f;
                RGB col = {255, 235 - 95 * f, 90 - 60 * f};
                float a = fmaxf(0.0f, 1 - fabsf((x - x0) + (yy - y) * 0.6f - sweep) / 5.0f) * 0.75f;
                px(x, yy, {col.r + (255 - col.r) * a, col.g + (255 - col.g) * a, col.b + (255 - col.b) * a});
              }
            }
        }
    }
  }
  const int8_t sp[6][2] = {{-5, 1}, {w + 3, 9}, {w / 2 - 3, -3}, {3, 14}, {w - 6, -2}, {-2, 7}};
  for (int i = 0; i < 6; i++) {
    float a = fmaxf(0.0f, sinf(t * 2.2f + i * 2.1f));
    a = a * a * a;
    if (a < 0.05f) continue;
    int sx = x0 + sp[i][0], sy = y + sp[i][1];
    RGB col = {255, 250, 200};
    px(sx, sy, col, a);
    const int8_t n4[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (auto &d : n4) px(sx + d[0], sy + d[1], col, a * 0.55f);
    if (a > 0.6f)
      for (auto &d : n4) px(sx + d[0] * 2, sy + d[1] * 2, col, (a - 0.6f) * 0.8f);
  }
}

static void sky(float t) {
  const int dy = H == 64 ? 0 : -14;   // on the 64x32 panel the fireworks sit higher
  for (int i = 0; i < 14; i++) {
    int x = (i * 37) % 64, y = 16 + (i * 53) % 20 + dy;
    if (sinf(t * 3 + i * 1.7f) > 0.5f) px(x, y, {70, 70, 110});
  }
  float tl = fmodf(t, LOOP);
  for (const Burst &b : BURSTS) {
    float dt = tl - b.start;
    if (dt < 0 || dt > 3.2f) continue;
    int by = b.y + dy;
    const float rise = 0.9f;
    if (dt < rise) {  // rocket: soft glowing head, faint tail
      float ry = (H == 64 ? 38 : 24) - ((H == 64 ? 38 : 24) - by) * sinf(dt / rise * 1.5708f);
      px(b.x, ry, {255, 220, 150}, 0.7f);
      px(b.x, ry + 1, {255, 150, 60}, 0.3f);
      const int8_t n[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
      for (auto &d : n) px(b.x + d[0], ry + d[1], {255, 200, 120}, 0.12f);
    } else {
      float e = dt - rise;
      float hue = fmodf(b.start * 0.37f, 1.0f);
      float fi = fminf(1.0f, e / 0.5f);                        // fade in
      float fo = fmaxf(0.0f, 1 - fmaxf(0.0f, e - 0.7f) / 1.9f);  // hold, then slow fade out
      float br = fi * fi * (3 - 2 * fi) * fo * (2 - fo);
      float r = 1 - expf(-e * 1.6f);                           // ease-out expansion
      float gl = br * fmaxf(0.0f, 1 - e / 1.8f);               // soft bloom at the centre
      RGB c0 = hsv(hue, 0.5f, 1);
      for (int x = -3; x <= 3; x++)
        for (int y = -3; y <= 3; y++) {
          float d = hypotf(x, y);
          if (d < 3.5f) px(b.x + x, by + y, c0, gl * 0.35f * (1 - d / 3.5f));
        }
      for (int k = 0; k < 28; k++) {
        float a = k / 28.0f * 6.283f + b.start;
        float sp = 11 + (k % 3) * 4;
        float x = b.x + cosf(a) * sp * r, y = by + sinf(a) * sp * r + 5 * e * e;
        RGB c = hsv(hue + (k % 2) * 0.06f, 0.75f, 1);
        px(x, y, c, br);
        const int8_t n[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        for (auto &d : n) px(x + d[0], y + d[1], c, br * 0.4f);  // halo
      }
    }
  }
}

static void balloons(float t) {
  static const int8_t half[7] = {1, 2, 3, 3, 3, 2, 1};  // half width per row, top to bottom
  for (const Balloon &bl : BALLOONS) {
    float u = fmodf(t / 7.0f + bl.phase, 1.0f);
    float y = 62 - u * 52, x = bl.x + sinf(t * 1.4f + bl.phase * 9) * 2;
    RGB c = hsv(bl.hue + 0.02f, 0.9f, 1);
    for (int dy = -3; dy <= 3; dy++)
      for (int dx = -half[dy + 3] + 1; dx < half[dy + 3]; dx++) px(x + dx, y + dy, c);
    px(x - 1, y - 1, {255, 255, 255}, 0.8f);
    px(x, y + 4, c);
    for (int k = 0; k < 5; k++) px(x + sinf(t * 2 + k) * 0.6f, y + 5 + k, {170, 170, 170}, 0.7f);
  }
}

// The time in the 4x6 font, 1 px per glyph tighter around the colon
static void timeText(const char *s, int y, RGB col, int cx = 32) {
  int w = -1;
  for (const char *c = s; *c; c++) w += *c == ':' ? 3 : 5;
  int x = cx - w / 2;
  for (const char *c = s; *c; c++) {
    const char *p = strchr(FONT2_CHARS, *c);
    uint32_t g = FONT2[p ? p - FONT2_CHARS : sizeof(FONT2_CHARS) - 2];
    for (int r = 0; r < 6; r++)
      for (int cc = 0; cc < 4; cc++)
        if (g & (1UL << (23 - (r * 4 + cc)))) px(x + cc, y + r, col);
    x += *c == ':' ? 3 : 5;
  }
}

static void cake(float t, const char *time) {
  const RGB pink = {255, 140, 190};
  rect(11, 61, 42, 1, {200, 200, 210});
  rect(14, 49, 36, 12, {120, 62, 32});
  rect(14, 49, 36, 2, pink);
  const int drips[8] = {15, 16, 21, 27, 36, 42, 47, 48};
  for (int x : drips) rect(x, 51, 1, 1 + x % 3, pink);
  rect(14, 60, 36, 1, {95, 48, 24});
  px(16, 55, {255, 230, 120});
  px(47, 57, {120, 220, 255});
  px(17, 58, {255, 160, 220});
  px(46, 54, {255, 230, 120});
  timeText(time, 53, pink);
  rect(20, 42, 24, 7, {150, 80, 42});
  rect(20, 42, 24, 2, {255, 255, 255});
  for (int x = 21; x < 43; x += 4) rect(x, 44, 1, 1 + (x / 4) % 2, {255, 255, 255});
  const int cx[3] = {26, 32, 38};
  for (int i = 0; i < 3; i++) {
    rect(cx[i], 38, 1, 4, hsv(i / 3.0f, 0.6f, 1));
    px(cx[i], 39, {255, 255, 255}, 0.6f);
    float fl = sinf(t * 17 + i * 2.1f) + sinf(t * 29 + i);
    int fy = 36 + (fl > 0.8f ? 1 : 0);
    px(cx[i], 37, {255, 200, 40});
    px(cx[i], fy, {255, 140, 20});
    px(cx[i], fy - 1, {255, 235, 120}, 0.8f);
    px(cx[i] - 1, 37, {255, 160, 40}, 0.25f);
    px(cx[i] + 1, 37, {255, 160, 40}, 0.25f);
    px(cx[i], 35, {255, 160, 40}, 0.2f);
  }
}

// The small cake in the bottom left corner of the 64x32 panel
static void cake32(float t) {
  const RGB pink = {255, 140, 190};
  rect(2, 31, 22, 1, {200, 200, 210});
  rect(3, 25, 20, 6, {120, 62, 32});
  rect(3, 25, 20, 1, pink);
  const int drips[5] = {4, 8, 13, 19, 21};
  for (int x : drips) rect(x, 26, 1, 1 + x % 2, pink);
  px(6, 28, {255, 230, 120});
  px(11, 29, {120, 220, 255});
  px(17, 28, {255, 160, 220});
  px(20, 29, {255, 230, 120});
  rect(7, 21, 12, 4, {150, 80, 42});
  rect(7, 21, 12, 1, {255, 255, 255});
  const int cx[3] = {9, 13, 17};
  for (int i = 0; i < 3; i++) {
    rect(cx[i], 18, 1, 3, hsv(i / 3.0f, 0.6f, 1));
    float fl = sinf(t * 17 + i * 2.1f) + sinf(t * 29 + i);
    int fy = 16 + (fl > 0.8f ? 1 : 0);
    px(cx[i], 17, {255, 200, 40});
    px(cx[i], fy, {255, 140, 20});
    px(cx[i], fy - 1, {255, 235, 120}, 0.8f);
    px(cx[i] - 1, 17, {255, 160, 40}, 0.25f);
    px(cx[i] + 1, 17, {255, 160, 40}, 0.25f);
    px(cx[i], 15, {255, 160, 40}, 0.2f);
  }
}

static void confetti(float t) {
  for (int i = 0; i < 34; i++) {
    float x = fmodf(i * 29 + sinf(t * 1.3f + i) * 3 + 64, 64.0f);
    float y = fmodf(t * (7 + i % 5 * 2) + i * 17, 70.0f) - 4;
    RGB c = hsv(i * 0.137f, 1, 1);
    if ((int)(t * 4 + i) % 3) px(x, y, c, 0.9f);
    else rect((int)x, (int)y, 1, 2, c);
  }
}

// One frame: the name, the age (a greeting when age is 0) and the time ("17:11"). Call begin() first.
template <typename Display>
void draw(Display *display, int panelHeight, const char *name, int age, const char *time) {
  H = panelHeight >= 64 ? 64 : 32;
  float t = millis() / 1000.0f * SLOW;
  memset(fb, 0, FB_SIZE);
  sky(t);
  if (H == 64) {
    balloons(t);
    cake(t, time);
  }
  confetti(t);
  if (H == 64) {
    centerText(name, 2, {0, 0, 0}, t * 0.25f);
    if (age > 0) {
      char sub[8];
      snprintf(sub, sizeof(sub), "%d", age);
      bigText(sub, 15, t);
    } else {  // no birth year known
      centerText("HAPPY", 14, {255, 215, 90});
      centerText("BIRTHDAY!", 21, {255, 215, 90});
    }
  } else {  // 64x32: the cake on the left, the age and the time on the right
    cake32(t);
    int nameW = strlen(name) * 4 - 1;  // centred over the age, kept on the panel when it is long
    text(name, constrain(45 - nameW / 2, 0, W - nameW), 1, {0, 0, 0}, t * 0.25f);
    if (age > 0) {
      char sub[8];
      snprintf(sub, sizeof(sub), "%d", age);
      bigText(sub, 9, t, 45);
    } else {
      text("HAPPY", 45 - 9, 10, {255, 215, 90});
      text("BIRTHDAY!", 45 - 17, 16, {255, 215, 90});
    }
    timeText(time, 24, {255, 140, 190}, 45);
  }
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      const uint8_t *o = &fb[(y * W + x) * 3];
      display->drawPixelRGB888(x, y, o[0], o[1], o[2]);
    }
}

}  // namespace BirthdayAnim

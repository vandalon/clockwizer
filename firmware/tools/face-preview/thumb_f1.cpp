// Renders one picture of the Formula 1 clockface (for the web flasher) from made-up matches.
// Compiles the real clockface and goal animation, only the Arduino/ESP parts are faked.
#include <Arduino.h>
#include <stdio.h>
#include <string>
#include <vector>

#include <TelnetStream.h>
#include "../../clockfaces/cw-cf-0x0C/Clockface.h"
#include "F1Ticker.h"

// ---- The fakes ----
static unsigned long virtualMs = 0;
unsigned long millis() { return virtualMs; }
void delay(unsigned long ms) { virtualMs += ms; }
namespace ezt { time_t fakeNow = 0; }
TelnetStreamStub TelnetStream;
void (*onFlip)(MatrixPanel_I2S_DMA *) = nullptr;

void F1Ticker::begin(CWDateTime *) {}
void F1Ticker::snapshot(Snapshot &) {}
uint32_t F1Ticker::version() { return 1; }
// The real table is in F1Ticker.cpp, which needs the network code: a few team colours are enough here
uint16_t F1Ticker::driverColor(const char *code) {
  static const struct { const char *code; uint16_t color; } T[] = {
      {"ANT", 0x2EFA}, {"RUS", 0x2EFA}, {"HAM", 0xE0A4}, {"LEC", 0xE0A4}, {"NOR", 0xFC20}, {"PIA", 0xFC20},
      {"VER", 0x2B5F}, {"ALO", 0x2D6B}, {"GAS", 0xFB7F}, {"TSU", 0x2B5F}, {"ALB", 0x23DF}, {"HUL", 0x5FE6}};
  for (const auto &t : T) if (strcmp(t.code, code) == 0) return t.color;
  return 0xFFFF;
}

// ---- Pictures ----
static std::string outDir;
static void save(MatrixPanel_I2S_DMA &d, const std::string &name) {
  FILE *f = fopen((outDir + "/" + name + ".ppm").c_str(), "wb");
  fprintf(f, "P6\n64 64\n255\n");
  for (int y = 0; y < 64; y++)
    for (int x = 0; x < 64; x++) {
      uint16_t c = d.getPixel(x, y);
      unsigned char rgb[3] = {(unsigned char)((c >> 11) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63),
                              (unsigned char)((c & 31) * 255 / 31)};
      fwrite(rgb, 1, 3, f);
    }
  fclose(f);
}

static MatrixPanel_I2S_DMA display;
static CWDateTime clockTime;
static int goalFrame = 0;

// The clock shows 21:34 at the start, then runs with the virtual time
static void setTime(unsigned long ms) {
  virtualMs = ms;
  long secs = 21 * 3600 + 34 * 60 + ms / 1000;
  clockTime.hour = secs / 3600 % 24;
  clockTime.minute = secs / 60 % 60;
  clockTime.second = secs % 60;
  ezt::fakeNow = 1791149640 + ms / 1000;  // Sun 4 Oct 2026 21:34 UTC
}


static constexpr uint16_t rgb(int r, int g, int b) { return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); }


int main(int argc, char **argv) {
  outDir = argc > 1 ? argv[1] : ".";
  int scenario = argc > 2 ? atoi(argv[2]) : 3;  // 1 idle, 2 between sessions, 3 race, 4 safety car, 5 red flag, 6 qualifying
  Clockface face(&display);
  setTime(0);
  face.setup(&clockTime);
  for (int i = 0; i < scenario; i++) face.simulateNext();
  for (int i = 0; i < 40; i++) {
    setTime(i * 100UL);
    face.update();
  }
  save(display, argc > 3 ? argv[3] : "f1");
  return 0;
}

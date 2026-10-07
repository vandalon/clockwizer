// Renders the Football clockface (clockfaces/cw-cf-0x0B/Clockface.cpp) with its own simulator
// scenarios the way a 64x64 LED panel would show them: PPM pictures, put together by preview.sh.
// Compiles the real clockface and goal animation, only the Arduino/ESP parts are faked.
#include <Arduino.h>
#include <stdio.h>
#include <string>
#include <vector>

#include <TelnetStream.h>
#include "../../clockfaces/cw-cf-0x0B/Clockface.h"
#include "FootballTicker.h"

// ---- The fakes ----
static unsigned long virtualMs = 0;
unsigned long millis() { return virtualMs; }
void delay(unsigned long ms) { virtualMs += ms; }
namespace ezt { time_t fakeNow = 0; }
TelnetStreamStub TelnetStream;
void (*onFlip)(MatrixPanel_I2S_DMA *) = nullptr;

static FootballTicker::Overview theOverview;
void FootballTicker::begin(CWDateTime *, bool, bool, uint32_t) {}
void FootballTicker::overview(Overview &out) { out = theOverview; }
uint32_t FootballTicker::version() { return 1; }
FootballTicker::Status FootballTicker::status() { return FootballTicker::OK; }
bool FootballTicker::nextGoal(Goal &) { return false; }

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

int main(int argc, char **argv) {
  outDir = argc > 1 ? argv[1] : ".";
  Clockface face(&display);
  setTime(0);
  face.setup(&clockTime);
  face.update();
  save(display, "s00_start");

  // Which scenario at which seconds after it started (tenths of a second)
  struct Shot { int scenario; std::vector<int> times; };
  const Shot shots[] = {
      {1, {100, 470, 700, 990, 1050}}, {2, {50}},       {3, {50, 37, 77}}, {4, {50, 82}},  {5, {50}},
      {6, {20, 37}},                  {7, {50}},         {8, {50, 90}},    {9, {50}},      {10, {50}},
      {11, {50}},
  };
  unsigned long base = 0;
  for (const Shot &shot : shots) {
    face.simulateNext();  // the scenario starts at "now"
    for (int t : shot.times) {
      setTime(base + t * 100UL);
      face.update();
      char name[48];
      snprintf(name, sizeof(name), "s%02d_t%04d", shot.scenario, t);
      save(display, name);
    }
    base += 120 * 1000UL;
    setTime(base);
  }
  face.simulate();  // off

  // A goal: the celebration plays inside update()
  setTime(base);
  onFlip = [](MatrixPanel_I2S_DMA *d) {
    char name[48];
    snprintf(name, sizeof(name), "goal_%02d", goalFrame++);
    save(*d, name);
  };
  face.testGoal();
  face.update();
  onFlip = nullptr;
  return 0;
}

// Renders one picture of the Tetris clockface (for the web flasher) from made-up matches.
// Compiles the real clockface and goal animation, only the Arduino/ESP parts are faked.
#include <Arduino.h>
#include <stdio.h>
#include <string>
#include <vector>

#include <TelnetStream.h>
#include "../../clockfaces/cw-cf-0x08/Clockface.h"
#include "FootballTicker.h"

// ---- The fakes ----
static unsigned long virtualMs = 0;
unsigned long millis() { return virtualMs; }
void delay(unsigned long ms) { virtualMs += ms; }
namespace ezt { time_t fakeNow = 0; }
TelnetStreamStub TelnetStream;
void (*onFlip)(MatrixPanel_I2S_DMA *) = nullptr;

void FootballTicker::begin(CWDateTime *, bool, bool, uint32_t) {}
bool FootballTicker::nextGoal(Goal &) { return false; }
bool FootballTicker::draw(Adafruit_GFX *, int16_t) { return false; }

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
  Clockface face(&display);
  setTime(0);
  face.setup(&clockTime);
  for (unsigned long ms = 0; ms < 40000; ms += 20) {  // the blocks fall for a few seconds
    setTime(ms);
    face.update();
  }
  save(display, "tetris");
  return 0;
}

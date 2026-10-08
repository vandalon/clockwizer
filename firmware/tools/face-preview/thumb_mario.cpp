// Renders one picture of the Mario and Luigi clockface (for the web flasher) from made-up matches.
// Compiles the real clockface and goal animation, only the Arduino/ESP parts are faked.
#include <Arduino.h>
#include <stdio.h>
#include <string>
#include <vector>

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "../../clockfaces/cw-cf-0x01/Clockface.h"

// ---- The fakes ----
static unsigned long virtualMs = 0;
unsigned long millis() { return virtualMs; }
void delay(unsigned long ms) { virtualMs += ms; }
namespace ezt { time_t fakeNow = 0; }
void (*onFlip)(MatrixPanel_I2S_DMA *) = nullptr;

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

int main(int argc, char **argv) {
  outDir = argc > 1 ? argv[1] : ".";
  Clockface face(&display);
  clockTime.second = 57;
  virtualMs = 0;
  face.setup(&clockTime);
  int shot = 0;
  for (unsigned long ms = 0; ms < 9000; ms += 20) {
    virtualMs = ms;
    clockTime.second = ms < 2000 ? 57 + ms / 1000 % 3 : 0;  // :00 at 2 s starts the jump
    if (ms >= 2000) clockTime.second = (ms - 2000) / 1000;
    face.update();
    if (ms >= 2000 && ms < 3200) {
      char name[32];
      snprintf(name, sizeof(name), "mario_%03d", shot++);
      save(display, name);
    }
  }
  return 0;
}

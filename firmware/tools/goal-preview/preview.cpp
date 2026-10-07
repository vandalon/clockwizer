// Renders the goal animation the way a 64x32 LED panel would show it, as PPM
// frames plus an ffmpeg concat list. Used by preview.sh.
#include <Adafruit_GFX.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#include "GoalAnimation.h"

static const int SCALE = 10;  // each LED becomes a 10x10 block with a round dot

int main(int argc, char **argv) {
  if (argc < 13) {
    fprintf(stderr,
            "usage: %s outdir home away homeScore awayScore homeScored(0/1) "
            "homeShirt homeShorts homeSecond awayShirt awayShorts awaySecond (RGB565 hex, second 0 = none)\n",
            argv[0]);
    return 1;
  }
  std::string dir = argv[1];
  const int H = getenv("PREVIEW_H") ? atoi(getenv("PREVIEW_H")) : 32;  // 32 or 64 rows
  GFXcanvas16 canvas(64, H);
  FILE *list = fopen((dir + "/frames.txt").c_str(), "w");
  int frame = 0;
  std::string last;

  auto hex = [](const char *s) { return (uint16_t)strtol(s, nullptr, 16); };
  TeamKit homeKit = {hex(argv[7]), hex(argv[8]), hex(argv[9])};
  TeamKit awayKit = {hex(argv[10]), hex(argv[11]), hex(argv[12])};
  playGoalAnimation(&canvas, argv[2], argv[3], atoi(argv[4]), atoi(argv[5]), atoi(argv[6]) != 0, homeKit, awayKit,
                    [&](int ms) {
    char name[32];
    snprintf(name, sizeof(name), "f%04d.ppm", frame++);
    FILE *f = fopen((dir + "/" + name).c_str(), "wb");
    fprintf(f, "P6\n%d %d\n255\n", 64 * SCALE, H * SCALE);
    for (int y = 0; y < H * SCALE; y++) {
      for (int x = 0; x < 64 * SCALE; x++) {
        uint16_t c = canvas.getPixel(x / SCALE, y / SCALE);
        float dx = x % SCALE - (SCALE - 1) / 2.0f, dy = y % SCALE - (SCALE - 1) / 2.0f;
        bool lit = dx * dx + dy * dy <= (SCALE / 2.0f - 1) * (SCALE / 2.0f - 1);
        unsigned char rgb[3] = {12, 12, 12};  // panel background between LEDs
        if (lit) {
          rgb[0] = (c >> 11) * 255 / 31;
          rgb[1] = ((c >> 5) & 63) * 255 / 63;
          rgb[2] = (c & 31) * 255 / 31;
          if (!c) rgb[0] = rgb[1] = rgb[2] = 28;  // an LED that's off
        }
        fwrite(rgb, 1, 3, f);
      }
    }
    fclose(f);
    fprintf(list, "file '%s'\nduration %.3f\n", name, ms / 1000.0);
    last = name;
  });
  fprintf(list, "file '%s'\n", last.c_str());  // concat needs the last frame twice
  fclose(list);
  return 0;
}

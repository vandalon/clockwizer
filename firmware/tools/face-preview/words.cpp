// Checks the Time in Words clockface (cw-cf-0x02) in every language at every minute of the day:
// each line has to fit on the panel, the lines have to fit above the date and the fonts must have
// every letter. Then draws some times in every language with the real clockface, as PPM pictures.
//   ./words <outdir>      exits with 1 when something doesn't fit
#include <Arduino.h>
#include <stdio.h>
#include <string>

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <CWPreferences.h>
#include "../../clockfaces/cw-cf-0x02/Clockface.h"
#include "../../clockfaces/cw-cf-0x02/WordLayout.h"

// ---- The fakes ----
static unsigned long virtualMs = 0;
unsigned long millis() { return virtualMs; }
void delay(unsigned long ms) { virtualMs += ms; }
namespace ezt { time_t fakeNow = 0; }
void (*onFlip)(MatrixPanel_I2S_DMA *) = nullptr;

static void save(MatrixPanel_I2S_DMA &d, const std::string &path) {
  FILE *f = fopen(path.c_str(), "wb");
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

static int countQuestionMarks(const char *s) {
  int n = 0;
  for (; *s; s++) n += *s == '?';
  return n;
}

static MatrixPanel_I2S_DMA display;
static CWDateTime clockTime;

// The times in the pictures: the hard ones of the languages
static const int SHOWN[][2] = {{0, 0}, {7, 0}, {7, 15}, {7, 24}, {7, 25}, {7, 30}, {7, 37}, {7, 45}, {12, 0}, {4, 24}, {16, 34}};

int main(int argc, char **argv) {
  std::string out = argc > 1 ? argv[1] : ".";
  int problems = 0;
  for (uint8_t lang = 0; lang < WORD_LANGUAGE_COUNT; lang++) {
    const WordLanguage &language = WORD_LANGUAGES[lang];
    for (int day = 0; day < 7; day++) {
      char codes[16];
      WordLayout::toFontCodes(language.weekdays[day], codes, sizeof(codes));
      if (countQuestionMarks(codes)) {
        printf("%s: the fonts miss a letter of %s\n", language.name, language.weekdays[day]);
        problems++;
      }
    }
    int lines[WordLayout::MAX_LINES + 1] = {0};
    for (int t = 0; t < 24 * 60; t++) {
      WordPhrase phrase;
      language.say(t / 60, t % 60, phrase);
      WordLayout layout(&display, &hour8pt7b, &minute7pt7b, 64, 48);
      layout.build(phrase);
      lines[layout.count]++;
      bool missing = false;
      for (uint8_t i = 0; i < layout.count; i++) missing |= countQuestionMarks(layout.lines[i].text) > 0;
      if (!layout.fits || missing || phrase.count == 0) {
        printf("%s %02d:%02d %s:", language.name, t / 60, t % 60, missing ? "a letter is missing" : "doesn't fit");
        for (uint8_t i = 0; i < layout.count; i++)
          printf(" [%s%s %d px]", layout.lines[i].big ? "B " : "", layout.lines[i].text,
                 layout.reach(layout.lines[i].text, layout.lines[i].big, layout.lines[i].condensed));
        printf("\n");
        problems++;
      }
    }
    printf("%-10s lines:", language.name);
    for (int n = 1; n <= WordLayout::MAX_LINES; n++)
      if (lines[n]) printf(" %d x %d", lines[n], n);
    printf("\n");

    ClockwiseParams::getInstance()->wordLang = lang;
    for (const auto &shown : SHOWN) {
      clockTime.hour = shown[0];
      clockTime.minute = shown[1];
      clockTime.weekday = (lang + shown[0]) % 7;  // a different day in every picture
      clockTime.day = 9 + shown[0];
      Clockface face(&display);
      face.setup(&clockTime);
      char name[64];
      snprintf(name, sizeof(name), "/%02d_%02d%02d.ppm", lang, shown[0], shown[1]);
      save(display, out + name);
    }
  }
  if (problems) printf("%d problems\n", problems);
  return problems ? 1 : 0;
}

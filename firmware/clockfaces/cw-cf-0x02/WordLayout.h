#pragma once

#include <Adafruit_GFX.h>
#include "WordClock.h"

// Puts a phrase of WordClock on the panel. Small pieces share a line as far as they fit, a big
// piece gets a line of its own. A word too long for a line is broken at a '|' (with a hyphen)
// or after a '-'; a big word that is just too wide is drawn with its letters closer together.
// The lines get the same space between them and are centred in the area they are given.
class WordLayout {
 public:
  static const uint8_t MAX_LINES = 6;
  static const uint8_t MAX_TEXT = 32;
  struct Line {
    char text[MAX_TEXT];  // in font codes, see toFontCodes
    bool big;
    bool condensed;
    int16_t baseline;
  };

  Line lines[MAX_LINES];
  uint8_t count = 0;
  bool fits = true;  // false when a line sticks out at the side or the lines are too many for the height

  WordLayout(Adafruit_GFX *gfx, const GFXfont *bigFont, const GFXfont *smallFont, int16_t width, int16_t height)
      : _gfx(gfx), _bigFont(bigFont), _smallFont(smallFont), _width(width), _height(height) {}

  void build(const WordPhrase &phrase);
  void draw(uint16_t bigColor, uint16_t smallColor);

  // UTF-8 to the codes of this face's fonts, which have the accented letters of WordGlyphs.h after '~'.
  // Letters the fonts don't have become '?'.
  static void toFontCodes(const char *utf8, char *out, size_t size);
  // How far the line reaches to the right when drawn from x = 0
  int16_t reach(const char *codes, bool big, bool condensed);

 private:
  Adafruit_GFX *_gfx;
  const GFXfont *_bigFont, *_smallFont;
  int16_t _width, _height;
  bool _open = false;  // the last line is a small one that can take more

  Line *newLine(const char *codes, bool big);
  void addSmall(const char *codes);
  void addBig(const char *codes);
  void addBroken(const char *codes, bool big);
  void placeLines();
  void drawCondensed(const Line &line, uint16_t color);
};

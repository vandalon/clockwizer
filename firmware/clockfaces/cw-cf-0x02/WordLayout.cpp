#include "WordLayout.h"

#include <string.h>
#include "WordGlyphs.h"

void WordLayout::toFontCodes(const char *utf8, char *out, size_t size) {
  size_t n = 0;
  const uint8_t *s = (const uint8_t *)utf8;
  while (*s && n + 1 < size) {
    uint32_t cp = *s++;
    if (cp >= 0x80) {  // a letter of two or more bytes
      int more = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : 1;
      cp &= 0x3F >> more;
      while (more-- && (*s & 0xC0) == 0x80) cp = (cp << 6) | (*s++ & 0x3F);
      char code = '?';
      for (uint8_t i = 0; i < WORD_GLYPH_COUNT; i++)
        if (WORD_GLYPHS[i] == cp) code = (char)(0x7F + i);
      out[n++] = code;
    } else {
      out[n++] = (char)cp;
    }
  }
  out[n] = 0;
}

// The text without the '|' marks
static void plain(const char *codes, char *out, size_t size) {
  size_t n = 0;
  for (; *codes && n + 1 < size; codes++)
    if (*codes != '|') out[n++] = *codes;
  out[n] = 0;
}

int16_t WordLayout::reach(const char *codes, bool big, bool condensed) {
  const GFXfont *font = big ? _bigFont : _smallFont;
  int16_t x = 0, right = 0;
  for (const uint8_t *c = (const uint8_t *)codes; *c; c++) {
    if (*c < font->first || *c > font->last) continue;
    const GFXglyph *g = font->glyph + (*c - font->first);
    if (g->width) right = max(right, (int16_t)(x + g->xOffset + g->width));
    x += g->xAdvance - (condensed ? 1 : 0);
  }
  return right;
}

WordLayout::Line *WordLayout::newLine(const char *codes, bool big) {
  if (count == MAX_LINES) {
    fits = false;
    return &lines[count - 1];
  }
  Line &line = lines[count++];
  strncpy(line.text, codes, MAX_TEXT - 1);
  line.text[MAX_TEXT - 1] = 0;
  line.big = big;
  line.condensed = false;
  line.baseline = 0;
  return &line;
}

void WordLayout::addSmall(const char *codes) {
  char text[MAX_TEXT];
  plain(codes, text, sizeof(text));
  if (_open) {
    Line &last = lines[count - 1];
    char joined[MAX_TEXT];
    snprintf(joined, sizeof(joined), "%s %s", last.text, text);
    if (reach(joined, false, false) <= _width) {
      strcpy(last.text, joined);
      return;
    }
  }
  if (reach(text, false, false) <= _width) {
    newLine(text, false);
    _open = true;
  } else {
    addBroken(codes, false);
  }
}

void WordLayout::addBig(const char *codes) {
  char text[MAX_TEXT];
  plain(codes, text, sizeof(text));
  _open = false;
  if (reach(text, true, false) <= _width) {
    newLine(text, true);
  } else if (reach(text, true, true) <= _width) {
    newLine(text, true)->condensed = true;
  } else {
    addBroken(codes, true);
    _open = false;
  }
}

// Word by word, and a long word in parts, starting on the open line if there is one
void WordLayout::addBroken(const char *codes, bool big) {
  if (!_open) newLine("", big);
  _open = true;
  const char *p = codes;
  char before = ' ';  // what came before this part: a space, a '|' or nothing (after a '-')
  while (*p) {
    size_t len = 0;
    while (p[len] && p[len] != ' ' && p[len] != '|' && (len == 0 || p[len - 1] != '-')) len++;
    Line &last = lines[count - 1];
    char joined[MAX_TEXT];
    const char *gap = last.text[0] && before == ' ' ? " " : "";
    snprintf(joined, sizeof(joined), "%s%s%.*s", last.text, gap, (int)len, p);
    // A part a break with a hyphen may follow needs room for that hyphen
    char withHyphen[MAX_TEXT];
    snprintf(withHyphen, sizeof(withHyphen), "%s%s", joined, p[len] == '|' ? "-" : "");
    if (!last.text[0] || reach(withHyphen, big, false) <= _width) {
      strcpy(last.text, joined);
    } else {
      if (before == '|' && strlen(last.text) + 1 < MAX_TEXT) strcat(last.text, "-");
      char part[MAX_TEXT];
      snprintf(part, sizeof(part), "%.*s", (int)len, p);
      newLine(part, big);
    }
    p += len;
    before = 0;
    if (*p == ' ' || *p == '|') before = *p++;
  }
}

void WordLayout::build(const WordPhrase &phrase) {
  count = 0;
  fits = true;
  _open = false;
  _gfx->setTextWrap(false);  // or the measuring would wrap too
  for (uint8_t i = 0; i < phrase.count; i++) {
    char codes[MAX_TEXT];
    toFontCodes(phrase.text[i], codes, sizeof(codes));
    if (phrase.big[i]) addBig(codes);
    else addSmall(codes);
  }
  for (uint8_t i = 0; i < count; i++)
    if (reach(lines[i].text, lines[i].big, lines[i].condensed) > _width) fits = false;
  placeLines();
}

// The same blank rows between the lines, as many as the height allows (4 at most), the whole centred
void WordLayout::placeLines() {
  int16_t top[MAX_LINES], bottom[MAX_LINES], ink = 0;
  for (uint8_t i = 0; i < count; i++) {
    int16_t x1, y1;
    uint16_t w, h;
    _gfx->setFont(lines[i].big ? _bigFont : _smallFont);
    _gfx->getTextBounds(lines[i].text, 0, 0, &x1, &y1, &w, &h);
    top[i] = y1;
    bottom[i] = y1 + (int16_t)h - 1;
    ink += h;
  }
  int16_t gap = 4;
  while (gap > 1 && ink + gap * (count - 1) > _height) gap--;
  int16_t total = ink + gap * (count - 1);
  if (total > _height) fits = false;
  int16_t y = (_height - total) / 2;
  for (uint8_t i = 0; i < count; i++) {
    lines[i].baseline = y - top[i];
    y += bottom[i] - top[i] + 1 + gap;
  }
}

void WordLayout::drawCondensed(const Line &line, uint16_t color) {
  int16_t x = 0;
  for (const uint8_t *c = (const uint8_t *)line.text; *c; c++) {
    if (*c < _bigFont->first || *c > _bigFont->last) continue;
    _gfx->drawChar(x, line.baseline, *c, color, color, 1);
    x += _bigFont->glyph[*c - _bigFont->first].xAdvance - 1;
  }
}

void WordLayout::draw(uint16_t bigColor, uint16_t smallColor) {
  _gfx->setTextWrap(false);
  for (uint8_t i = 0; i < count; i++) {
    const Line &line = lines[i];
    _gfx->setFont(line.big ? _bigFont : _smallFont);
    _gfx->setTextColor(line.big ? bigColor : smallColor);
    if (line.condensed) {
      drawCondensed(line, bigColor);
    } else {
      _gfx->setCursor(0, line.baseline);
      _gfx->print(line.text);
    }
  }
}

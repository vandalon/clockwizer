#include "plant.h"

// Faces right, towards Mario. '.' is transparent, K outline, R red, W white,
// G green, H light green
const char* PLANT[PLANT_HEIGHT] = {
  "...KKKKK....",
  "..KRRWRRK...",
  ".KRWRRRRRK..",
  "KRRRRRRRK...",
  "KRRRRRKKW...",
  "KWRRRKWW....",
  "KRRRKW......",
  "KRRRKW......",
  "KRRRRKWW....",
  "KRRRRRKKW...",
  "KRRWRRRRK...",
  ".KRRRWRRRK..",
  "..KRRRRRK...",
  "...KKKKK....",
  "....KGGK....",
  ".KK.KGGK.KK.",
  "KGHGGGGGGHGK",
  ".KKKGGGGKKK."
};

const unsigned short PIPE_GREEN = 0x0560;
const unsigned short PIPE_LIGHT = 0x7FEF;
const unsigned short PIPE_DARK = 0x0300;

void Plant::shorten(int pipeY, int pipeHeight) {
  _pipeY = pipeY;
  _pipeHeight = pipeHeight;
}

Plant::Plant(int x, int pipeY) {
  _x = x;
  _pipeY = pipeY;
}

void Plant::drawPipe() {
  Adafruit_GFX* d = Locator::getDisplay();
  int bodyX = _x + 1;

  d->fillRect(bodyX, _pipeY + 4, PIPE_WIDTH - 2, _pipeHeight - 4, PIPE_GREEN);
  d->drawRect(bodyX, _pipeY + 4, PIPE_WIDTH - 2, _pipeHeight - 4, 0x0000);
  d->drawFastVLine(bodyX + 2, _pipeY + 5, _pipeHeight - 6, PIPE_LIGHT);
  d->drawFastVLine(bodyX + PIPE_WIDTH - 4, _pipeY + 5, _pipeHeight - 6, PIPE_DARK);

  d->fillRect(_x, _pipeY, PIPE_WIDTH, 4, PIPE_GREEN);
  d->drawRect(_x, _pipeY, PIPE_WIDTH, 4, 0x0000);
  d->drawFastVLine(_x + 2, _pipeY + 1, 2, PIPE_LIGHT);
  d->drawFastVLine(_x + PIPE_WIDTH - 3, _pipeY + 1, 2, PIPE_DARK);
}

void Plant::drawPlant() {
  Adafruit_GFX* d = Locator::getDisplay();
  int plantX = _x + (PIPE_WIDTH - PLANT_WIDTH) / 2;
  int plantY = _pipeY - PLANT_HEIGHT;

  d->fillRect(plantX, plantY, PLANT_WIDTH, PLANT_HEIGHT, SKY_COLOR);

  // Sunk rows are hidden behind the pipe
  // (seconds + 1) so the plant is fully hidden at :59
  int sunk = max(_rise, (int8_t)((_seconds + 1) * PLANT_HEIGHT / 60));

  for (int row = 0; row < PLANT_HEIGHT - sunk; row++) {
    for (int col = 0; col < PLANT_WIDTH; col++) {
      char c = PLANT[row][col];
      if (c == '.') continue;

      unsigned short color;
      if (c == 'K') color = 0x0000;
      else if (c == 'R') color = 0xE0A4;
      else if (c == 'W') color = 0xFFFF;
      else if (c == 'H') color = 0x8E4C;
      else color = 0x6548;
      d->drawPixel(plantX + col, plantY + row + sunk, color);
    }
  }
}

void Plant::init() {
  drawPipe();
  drawPlant();
  _dirty = false;
}

void Plant::setSecond(int second) {
  if (second == _seconds) return;

  // Popping back up after the minute rolled over
  if (second == 0) _rise = PLANT_HEIGHT;

  _seconds = second;
  _dirty = true;
}

void Plant::update() {
  if (_rise > 0 && millis() - _lastMillis >= 40) {
    _rise -= 2;
    if (_rise < 0) _rise = 0;
    _dirty = true;
    _lastMillis = millis();
  }

  if (_dirty) {
    drawPlant();
    _dirty = false;
  }
}

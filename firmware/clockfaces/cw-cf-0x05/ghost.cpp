#include "ghost.h"

void Ghost::init(uint16_t color) {
  _color = color;
}

void Ghost::reset(int x, int y, Direction dir) {
  _x = x;
  _y = y;
  _direction = dir;
}

void Ghost::move() {
  if (_direction == Direction::RIGHT) {
    _x += 1;
  } else if (_direction == Direction::LEFT) {
    _x -= 1;
  } else if (_direction == Direction::DOWN) {
    _y += 1;
  } else if (_direction == Direction::UP) {
    _y -= 1;
  }
}

void Ghost::reverse() {
  if (_direction == Direction::RIGHT) {
    _direction = Direction::LEFT;
  } else if (_direction == Direction::LEFT) {
    _direction = Direction::RIGHT;
  } else if (_direction == Direction::UP) {
    _direction = Direction::DOWN;
  } else {
    _direction = Direction::UP;
  }
}

void Ghost::draw(bool frightened, bool flash) {
  uint16_t body = _color;
  uint16_t eye = 0xFFFF;

  if (frightened) {
    body = flash ? 0xFFFF : 0x001F;
    eye = flash ? 0xF800 : 0xFFFF;
  }

  uint16_t pixels[25];
  for (int i = 0; i < 25; i++) {
    pixels[i] = _SHAPE[i] == 2 ? eye : (_SHAPE[i] == 1 ? body : 0x0000);
  }

  Locator::getDisplay()->drawRGBBitmap(_x, _y, pixels, SPRITE_SIZE, SPRITE_SIZE);
}

int Ghost::getX() {
  return _x;
}

int Ghost::getY() {
  return _y;
}

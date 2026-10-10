#pragma once

#include <Arduino.h>
#include <Game.h>
#include <Locator.h>


class Ghost {
  private:
    // 0 = transparent, 1 = body, 2 = eye
    const byte _SHAPE[25] = {
      0, 1, 1, 1, 0,
      1, 1, 1, 1, 1,
      1, 2, 1, 2, 1,
      1, 1, 1, 1, 1,
      1, 0, 1, 0, 1
    };

    int _x = 0;
    int _y = 0;
    uint16_t _color = 0x9FD3;

  public:
    static const int SPRITE_SIZE = 5;
    Direction _direction = Direction::LEFT;

    void init(uint16_t color);
    void reset(int x, int y, Direction dir);
    void move();
    void reverse();
    void draw(bool frightened, bool flash);
    int getX();
    int getY();
    uint16_t color() { return _color; }
};

#pragma once

#include <Arduino.h>
#include <Locator.h>
#include "assets.h"

const uint8_t PLANT_WIDTH = 12;
const uint8_t PLANT_HEIGHT = 18;
const uint8_t PIPE_WIDTH = 12;
const uint8_t PIPE_HEIGHT = 14;

// Piranha plant in a pipe that shows the seconds: it sinks into the pipe
// over the minute, then pops back up at :00.
class Plant {
  private:
    int _x;       // left edge of the pipe
    int _pipeY;   // top edge of the pipe

    uint8_t _seconds = 0;
    int8_t _rise = 0;       // rows still hidden while popping back up
    bool _dirty = true;
    unsigned long _lastMillis = 0;

    void drawPipe();
    void drawPlant();

  public:
    Plant(int x, int pipeY);
    void init();
    void setSecond(int second);
    void update();
};

#pragma once

#include <Arduino.h>
#include "IClockface.h"
#include <Locator.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "TetrisMatrixDraw.h"
#define CLOCKFACE_NAME "cw-cf-0x08"


class Clockface: public IClockface {
  private:
    CWDateTime* _dateTime;
    MatrixPanel_I2S_DMA* _display;
    TetrisMatrixDraw* tetris;

  public:
    Clockface(MatrixPanel_I2S_DMA* display);
    void setup(CWDateTime *dateTime);
    void printCenter();
    void update();
    void handleColonAfterAnimation();
    void animate();
};

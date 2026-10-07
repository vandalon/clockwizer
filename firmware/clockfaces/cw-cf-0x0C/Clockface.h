#pragma once

#include <Arduino.h>
#include "IClockface.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#define CLOCKFACE_NAME "cw-cf-0x0C"


class Clockface: public IClockface {
  private:
    CWDateTime* _dateTime;
    MatrixPanel_I2S_DMA* _display;

  public:
    Clockface(MatrixPanel_I2S_DMA* display);
    void setup(CWDateTime *dateTime);
    void update();
    // Telnet S or N: the next made-up screen (idle, between sessions, race, safety car, red flag, qualifying,
    // practice), then back to the real data
    const char *simulate();
    const char *simulateNext();
    void testGoal() {}
    void testIncident(char) {}
};

#pragma once

#include <Arduino.h>
#include "IClockface.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#define CLOCKFACE_NAME "cw-cf-0x0B"


class Clockface: public IClockface {
  private:
    CWDateTime* _dateTime;
    MatrixPanel_I2S_DMA* _display;

  public:
    Clockface(MatrixPanel_I2S_DMA* display);
    void setup(CWDateTime *dateTime);
    void update();
    // Telnet S and N: the simulator is gone, these only say so
    const char *simulate();
    const char *simulateNext();
    // Telnet G: celebrate a made-up goal
    void testGoal();
    // Telnet Y, D and W: a made-up yellow card, red card ('y', 'r') or substitution ('s')
    void testIncident(char kind);
};

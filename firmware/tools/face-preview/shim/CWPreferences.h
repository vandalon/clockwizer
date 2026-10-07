#pragma once
#include <Arduino.h>
struct ClockwiseParams {
  uint8_t matchSecs = 8;
  uint16_t resultMins = 120;
  bool ballRoll = true;
  uint8_t timeStyle = 2;
  static ClockwiseParams *getInstance() { static ClockwiseParams p; return &p; }
};

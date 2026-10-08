#pragma once
#include <Arduino.h>
struct ClockwiseParams {
  uint8_t matchSecs = 8;
  uint16_t resultMins = 120;
  bool ballRoll = true;
  uint8_t timeStyle = 2;
  uint8_t displayHeight = 64;
  bool showF1 = false;
  static ClockwiseParams *getInstance() { static ClockwiseParams p; return &p; }
};

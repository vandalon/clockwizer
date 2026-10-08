#pragma once
#include <Arduino.h>
struct ClockwiseParams {
  uint8_t matchSecs = 8;
  uint16_t resultMins = 120;
  bool ballRoll = true;
  uint8_t timeStyle = 2;
  uint8_t displayHeight = 64;
  bool showF1 = false;
  uint8_t color = 0;
  uint8_t ghost1Color = 0, ghost2Color = 0, dotColor = 0;  // 0 = the face picks
  uint16_t wallColor() { return 0x0016; }                  // the default blue maze walls
  uint16_t pacmanColor(uint8_t) { return 0; }
  uint16_t nightColor() { return 0x3800; }
  static ClockwiseParams *getInstance() { static ClockwiseParams p; return &p; }
};

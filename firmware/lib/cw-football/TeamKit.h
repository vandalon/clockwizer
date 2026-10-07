#pragma once

#include <stdint.h>

// A team's colours (RGB565): the home shirt and shorts, and the club's second
// colour for two-tone details (team names, sleeves, the GOAL! background).
// second is 0 when the club has none that shows on the LEDs (e.g. black).
struct TeamKit {
  uint16_t shirt, shorts, second;
};

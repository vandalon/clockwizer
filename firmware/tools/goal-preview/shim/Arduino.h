// Just enough of the Arduino API to compile Adafruit GFX on a computer
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#define PROGMEM
typedef bool boolean;
typedef uint8_t byte;

inline void yield() {}

class __FlashStringHelper;

class String {
 public:
  String(const char *s = "") : _s(s) {}
  const char *c_str() const { return _s.c_str(); }
  unsigned int length() const { return _s.length(); }
 private:
  std::string _s;
};

#include "Print.h"

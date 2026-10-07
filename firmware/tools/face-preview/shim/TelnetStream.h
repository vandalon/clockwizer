#pragma once
#include <Arduino.h>
struct TelnetStreamStub {
  void println(const String &) {}
};
extern TelnetStreamStub TelnetStream;

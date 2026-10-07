// Just enough of the Arduino API to compile the Football clockface and Adafruit GFX on a computer
#pragma once

#include <algorithm>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <time.h>

#define PROGMEM
#define PI 3.14159265358979f
typedef bool boolean;
typedef uint8_t byte;
using std::max;
using std::min;
#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))

inline void yield() {}
unsigned long millis();
void delay(unsigned long ms);

class __FlashStringHelper;

class String {
 public:
  String(const char *s = "") : _s(s ? s : "") {}
  String(const std::string &s) : _s(s) {}
  String(char c) : _s(1, c) {}
  String(int v) : _s(std::to_string(v)) {}
  String(unsigned int v) : _s(std::to_string(v)) {}
  String(long v) : _s(std::to_string(v)) {}
  String(unsigned long v) : _s(std::to_string(v)) {}
  String(long long v) : _s(std::to_string(v)) {}
  String(unsigned long long v) : _s(std::to_string(v)) {}
  const char *c_str() const { return _s.c_str(); }
  unsigned int length() const { return _s.length(); }
  bool isEmpty() const { return _s.empty(); }
  char operator[](unsigned i) const { return _s[i]; }
  int indexOf(char c) const { size_t p = _s.find(c); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const char *s) const { size_t p = _s.find(s); return p == std::string::npos ? -1 : (int)p; }
  String substring(unsigned from) const { return from >= _s.size() ? String("") : String(_s.substr(from)); }
  String substring(unsigned from, unsigned to) const {
    if (from >= _s.size() || to <= from) return String("");
    return String(_s.substr(from, to - from));
  }
  bool startsWith(const String &p) const { return _s.compare(0, p._s.size(), p._s) == 0; }
  long toInt() const { return atol(_s.c_str()); }
  String &operator+=(const String &o) { _s += o._s; return *this; }
  String &operator+=(char c) { _s += c; return *this; }
  bool operator==(const String &o) const { return _s == o._s; }
  bool operator==(const char *o) const { return _s == o; }
  bool operator!=(const String &o) const { return _s != o._s; }
  bool operator!=(const char *o) const { return _s != o; }
  bool operator<(const String &o) const { return _s < o._s; }
  const std::string &str() const { return _s; }
 private:
  std::string _s;
};
inline String operator+(const String &a, const String &b) { return String(a.str() + b.str()); }
inline String operator+(const String &a, const char *b) { return String(a.str() + b); }
inline String operator+(const char *a, const String &b) { return String(std::string(a) + b.str()); }
inline String operator+(const String &a, char b) { return String(a.str() + b); }
template <typename T, typename = typename std::enable_if<std::is_arithmetic<T>::value>::type>
String operator+(const String &a, T b) { return String(a.str() + String(b).str()); }

#include "Print.h"

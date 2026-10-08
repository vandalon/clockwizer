#pragma once

#include <Arduino.h>

// The birthdays the clock celebrates, kept as one text setting: "MMDD:NAME:YYYY" entries separated by
// commas. The year is optional ("0511:ELIOT:" shows no age), a space in a name is written as "_".
namespace Birthdays {

const int MAX = 10;
const int NAME_LEN = 12;

struct Entry {
  uint8_t month, day;
  char name[NAME_LEN + 1];  // capitals, digits and spaces: what the clock's small font can draw
  int year;                 // 0 = not given
};

// The entries of the setting; a broken or impossible one is skipped. Returns how many were read.
inline int parse(const String &text, Entry *out, int max = MAX) {
  static const uint8_t DAYS[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int count = 0;
  int from = 0;
  while (from <= (int)text.length() && count < max) {
    int to = text.indexOf(',', from);
    if (to < 0) to = text.length();
    String item = text.substring(from, to);
    from = to + 1;
    int c1 = item.indexOf(':'), c2 = item.indexOf(':', c1 + 1);
    if (c1 != 4 || c2 < 0) continue;
    int date = item.substring(0, 4).toInt();
    int month = date / 100, day = date % 100;
    if (month < 1 || month > 12 || day < 1 || day > DAYS[month - 1]) continue;
    Entry e = {(uint8_t)month, (uint8_t)day, "", item.substring(c2 + 1).toInt()};
    if (e.year != 0 && (e.year < 1900 || e.year > 2100)) e.year = 0;
    int n = 0;
    for (int i = c1 + 1; i < c2 && n < NAME_LEN; i++) {
      char ch = toupper(item[i]);
      if (ch == '_') ch = ' ';
      if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ') e.name[n++] = ch;
    }
    while (n > 0 && e.name[n - 1] == ' ') n--;  // no trailing spaces
    e.name[n] = 0;
    if (n == 0) continue;
    out[count++] = e;
  }
  return count;
}

// What gets stored: the same setting, with the broken entries dropped and the names in capitals
inline String clean(const String &text) {
  Entry list[MAX];
  int n = parse(text, list);
  String out;
  for (int i = 0; i < n; i++) {
    char head[8];
    snprintf(head, sizeof(head), "%02d%02d:", list[i].month, list[i].day);
    if (i) out += ',';
    out += head;
    for (const char *p = list[i].name; *p; p++) out += *p == ' ' ? '_' : *p;
    out += ':';
    if (list[i].year) out += list[i].year;
  }
  return out;
}

}  // namespace Birthdays

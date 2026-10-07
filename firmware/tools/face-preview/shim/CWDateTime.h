// A clock the preview sets by hand
#pragma once
#include <Arduino.h>
#include <ezTime.h>

class CWDateTime {
 public:
  int hour = 21, minute = 34, second = 0, day = 4, month = 10, weekday = 0;
  int getHour() { return hour; }
  int getMinute() { return minute; }
  int getSecond() { return second; }
  int getDay() { return day; }
  int getMonth() { return month; }
  int getWeekday() { return weekday; }
  time_t localNow() { return ezt::now(); }
  time_t utcToLocal(time_t utc) { return utc; }
};

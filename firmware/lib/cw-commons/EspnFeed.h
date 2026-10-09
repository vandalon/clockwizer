#pragma once

#include <Arduino.h>
#include <ezTime.h>

// What the football and the F1 tickers share for reading ESPN's feeds.

// Feeds the HTTP stream to ArduinoJson without busy-waiting. Stream's own
// timed read spins while waiting for data, which starves the idle task on
// core 0 and trips the task watchdog during a slow download.
struct YieldingReader {
  Stream &stream;
  unsigned long timeoutMs;
  size_t count = 0;

  YieldingReader(Stream &s, unsigned long timeout) : stream(s), timeoutMs(timeout) {}

  int read() {
    if (++count % 1024 == 0) vTaskDelay(1);
    unsigned long start = millis();
    do {
      int c = stream.read();
      if (c >= 0) return c;
      vTaskDelay(1);
    } while (millis() - start < timeoutMs);
    return -1;
  }

  size_t readBytes(char *buffer, size_t length) {
    size_t n = 0;
    for (; n < length; n++) {
      int c = read();
      if (c < 0) break;
      buffer[n] = c;
    }
    return n;
  }
};

// ESPN's "2026-10-09T08:30Z" as a UTC time_t, 0 when it can't be read
static inline time_t parseEspnDate(const char *date) {
  int y, mo, d, h, mi;
  if (sscanf(date, "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi) != 5) return 0;
  return ezt::makeTime(h, mi, 0, d, mo, y);
}

#include "F1LiveState.h"

#include <stdlib.h>
#include <string.h>

// What a message holds that is used. In a feed update the topic's name is dropped (a filter keeps no strings
// in an array of objects), but each topic's data looks different: Lines, CurrentLap and Status.
static const char FILTER[] =
    "{\"result\":{"
      "\"DriverList\":{\"*\":{\"Tla\":true,\"TeamColour\":true}},"
      "\"TimingData\":{\"Lines\":{\"*\":LINE}},"
      "\"LapCount\":{\"CurrentLap\":true,\"TotalLaps\":true},"
      "\"TrackStatus\":{\"Status\":true}},"
    "\"arguments\":[{\"Lines\":{\"*\":LINE},\"CurrentLap\":true,\"TotalLaps\":true,\"Status\":true}]}";
static const char LINE_FILTER[] =
    "{\"Position\":true,\"GapToLeader\":true,\"TimeDiffToFastest\":true,\"BestLapTime\":{\"Value\":true},"
    "\"BestLapTimes\":true,\"Stats\":true}";

void F1LiveState::buildFilter(JsonDocument &filter) {
  // LINE appears twice: spell it out rather than keep a second copy of the template
  static char text[sizeof(FILTER) + 2 * sizeof(LINE_FILTER)];
  char *out = text;
  for (const char *p = FILTER; *p;) {
    if (strncmp(p, "LINE", 4) == 0) {
      size_t n = strlen(LINE_FILTER);
      memcpy(out, LINE_FILTER, n);
      out += n;
      p += 4;
    } else {
      *out++ = *p++;
    }
  }
  *out = 0;
  deserializeJson(filter, text);
}

void F1LiveState::reset() {
  for (Driver &d : _drivers) d = Driver();
  _lap = _totalLaps = 0;
  _flag = 0;
}

F1LiveState::Driver *F1LiveState::driver(int number) {
  if (number <= 0 || number > 255) return nullptr;
  Driver *free = nullptr;
  for (Driver &d : _drivers) {
    if (d.number == number) return &d;
    if (!d.number && !free) free = &d;
  }
  if (free) free->number = number;
  return free;
}

// Copies a string value, leaves the field alone for anything else (an update only holds what changed)
static void setText(char *to, size_t size, JsonVariantConst value) {
  const char *text = value.as<const char *>();
  if (!text) return;
  strncpy(to, text, size - 1);
  to[size - 1] = 0;
}

// "4781D7" as RGB565; colours too dark for the LEDs become white
static uint16_t hexColor(const char *hex) {
  if (!hex || strlen(hex) != 6) return 0xFFFF;
  long v = strtol(hex, nullptr, 16);
  int r = (v >> 16) & 255, g = (v >> 8) & 255, b = v & 255;
  if (r < 64 && g < 64 && b < 64) return 0xFFFF;
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

// An array in a snapshot, an object keyed by index in an update
template <class F>
static void eachIndexed(JsonVariantConst value, F f) {
  if (value.is<JsonArrayConst>()) {
    int i = 0;
    for (JsonVariantConst e : value.as<JsonArrayConst>()) f(i++, e);
  } else if (value.is<JsonObjectConst>()) {
    for (JsonPairConst kv : value.as<JsonObjectConst>()) f(atoi(kv.key().c_str()), kv.value());
  }
}

void F1LiveState::applyDriverList(JsonObjectConst list) {
  for (JsonPairConst kv : list) {
    Driver *d = driver(atoi(kv.key().c_str()));
    if (!d) continue;
    setText(d->code, sizeof(d->code), kv.value()["Tla"]);
    if (kv.value()["TeamColour"].is<const char *>()) d->color = hexColor(kv.value()["TeamColour"]);
  }
}

void F1LiveState::applyLine(int number, JsonObjectConst line) {
  Driver *d = driver(number);
  if (!d) return;
  JsonVariantConst position = line["Position"];
  if (position.is<const char *>()) d->position = atoi(position.as<const char *>());
  else if (position.is<int>()) d->position = position.as<int>();
  setText(d->gap, sizeof(d->gap), line["GapToLeader"]);
  setText(d->diff, sizeof(d->diff), line["TimeDiffToFastest"]);
  setText(d->best, sizeof(d->best), line["BestLapTime"]["Value"]);
  eachIndexed(line["BestLapTimes"], [d](int i, JsonVariantConst e) {
    if (i >= 0 && i < 3) setText(d->bestQ[i], sizeof(d->bestQ[i]), e["Value"]);
  });
  eachIndexed(line["Stats"], [d](int i, JsonVariantConst e) {
    if (i >= 0 && i < 3) setText(d->diffQ[i], sizeof(d->diffQ[i]), e["TimeDiffToFastest"]);
  });
}

void F1LiveState::applyTiming(JsonObjectConst timing) {
  for (JsonPairConst kv : timing["Lines"].as<JsonObjectConst>()) applyLine(atoi(kv.key().c_str()), kv.value());
}

// One topic's data, told apart by what it holds
void F1LiveState::applyTopic(JsonObjectConst data) {
  if (data.containsKey("Lines")) applyTiming(data);
  if (data.containsKey("CurrentLap")) _lap = data["CurrentLap"] | _lap;
  if (data.containsKey("TotalLaps")) _totalLaps = data["TotalLaps"] | _totalLaps;
  if (data.containsKey("Status")) {
    // 1 clear, 2 yellow, 4 safety car, 5 red, 6 and 7 virtual safety car
    int status = atoi(data["Status"] | "1");
    _flag = status == 5 ? 'r' : (status == 2 || status == 4 || status == 6 || status == 7) ? 'y' : 0;
  }
}

bool F1LiveState::apply(JsonDocument &message) {
  bool any = false;
  JsonObjectConst result = message["result"];
  if (!result.isNull()) {
    if (result["DriverList"].is<JsonObjectConst>()) applyDriverList(result["DriverList"]);
    for (const char *topic : {"TimingData", "LapCount", "TrackStatus"})
      if (result[topic].is<JsonObjectConst>()) applyTopic(result[topic]);
    any = true;
  }
  for (JsonVariantConst argument : message["arguments"].as<JsonArrayConst>())
    if (argument.is<JsonObjectConst>()) {
      applyTopic(argument);
      any = true;
    }
  return any;
}

bool F1LiveState::hasOrder() const {
  for (const Driver &d : _drivers)
    if (d.number && d.position == 1 && d.code[0]) return true;
  return false;
}

int F1LiveState::top(char tag, Row *rows) const {
  int count = 0;
  for (int i = 0; i < ROWS; i++) rows[i] = Row{"", 0xFFFF, ""};
  for (const Driver &d : _drivers) {
    if (!d.number || !d.code[0] || d.position < 1 || d.position > ROWS) continue;
    Row &row = rows[d.position - 1];
    strcpy(row.code, d.code);
    row.color = d.color;
    const char *text = "";
    bool leader = d.position == 1;
    if (tag == 'R' || tag == 'S') {
      text = leader ? "LEAD" : d.gap;
    } else if (tag == 'P') {
      text = leader ? d.best : d.diff;
    } else {  // qualifying: the latest part that has a time
      int part = -1;
      for (int p = 2; p >= 0 && part < 0; p--)
        if (d.bestQ[p][0]) part = p;
      if (part >= 0) text = leader ? d.bestQ[part] : d.diffQ[part];
    }
    strncpy(row.time, text, sizeof(row.time) - 1);
    row.time[sizeof(row.time) - 1] = 0;
    if (d.position > count) count = d.position;
  }
  return count;
}

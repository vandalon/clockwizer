#include "F1Ticker.h"

#include <algorithm>
#include <HTTPClient.h>
#include <TelnetStream.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ezTime.h>

static const unsigned long LIVE_REFRESH_MS = 15 * 1000UL;  // plus a request per driver for the times
static const unsigned long WEEKEND_REFRESH_MS = 5 * 60 * 1000UL;  // between sessions
static const unsigned long START_REFRESH_MS = 30 * 1000UL;        // a session is about to start
static const unsigned long IDLE_REFRESH_MS = 30 * 60 * 1000UL;
static const unsigned long STANDINGS_REFRESH_MS = 30 * 60 * 1000UL;
static const unsigned long SEASON_REFRESH_MS = 60 * 60 * 1000UL;
static const unsigned long RETRY_MS = 60 * 1000UL;
static const time_t WEEKEND_TAIL_SECS = 3 * 60 * 60;  // the weekend lasts this long after the race started

extern volatile bool firmwareUpdating;  // main.cpp
extern volatile bool liveEventOn;       // main.cpp: no automatic update while true

#define RGB565(r, g, b) ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

// The 2026 grid by surname, as ESPN spells it: the driver's code and team colour
static const struct { const char *surname, *code; uint16_t color; } DRIVERS[] = {
  {"Verstappen", "VER", RGB565(54, 113, 255)},  {"Hadjar", "HAD", RGB565(54, 113, 255)},      // Red Bull
  {"Russell", "RUS", RGB565(39, 244, 210)},     {"Antonelli", "ANT", RGB565(39, 244, 210)},   // Mercedes
  {"Leclerc", "LEC", RGB565(232, 0, 45)},       {"Hamilton", "HAM", RGB565(232, 0, 45)},      // Ferrari
  {"Norris", "NOR", RGB565(255, 128, 0)},       {"Piastri", "PIA", RGB565(255, 128, 0)},      // McLaren
  {"Alonso", "ALO", RGB565(34, 153, 113)},      {"Stroll", "STR", RGB565(34, 153, 113)},      // Aston Martin
  {"Gasly", "GAS", RGB565(255, 135, 188)},      {"Colapinto", "COL", RGB565(255, 135, 188)},  // Alpine
  {"Albon", "ALB", RGB565(100, 196, 255)},      {"Sainz", "SAI", RGB565(100, 196, 255)},      // Williams
  {"Lawson", "LAW", RGB565(170, 190, 255)},     {"Lindblad", "LIN", RGB565(170, 190, 255)},   // Racing Bulls
  {"H\xC3\xBClkenberg", "HUL", RGB565(210, 50, 0)}, {"Bortoleto", "BOR", RGB565(210, 50, 0)},  // Audi
  {"Ocon", "OCO", RGB565(255, 255, 255)},       {"Bearman", "BEA", RGB565(255, 255, 255)},    // Haas
  {"P\xC3\xA9rez", "PER", RGB565(255, 215, 0)},   {"Bottas", "BOT", RGB565(255, 215, 0)},     // Cadillac
};

uint16_t F1Ticker::driverColor(const char *code) {
  for (const auto &driver : DRIVERS)
    if (strcmp(driver.code, code) == 0) return driver.color;
  return 0xFFFF;
}

// Feeds the HTTP stream to ArduinoJson without busy-waiting, see YieldingReader in FootballTicker.cpp
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

static void f1log(const char *format, ...) {
  static std::mutex lock;
  char line[256];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  std::lock_guard<std::mutex> guard(lock);
  Serial.print(line);
  TelnetStream.print(line);
}

static time_t parseEspnDate(const char *date) {
  int y, mo, d, h, mi;
  if (sscanf(date, "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi) != 5) return 0;
  return ezt::makeTime(h, mi, 0, d, mo, y);
}

void F1Ticker::begin(CWDateTime *dateTime) {
  if (_started) return;
  _started = true;
  _dateTime = dateTime;
  _doc = new DynamicJsonDocument(20480);  // a Grand Prix weekend with 22 drivers per session needs ~10KB
  // Core 0, so downloading never stalls the display loop on core 1
  xTaskCreatePinnedToCore(task, "f1", 12288, this, 1, &_task, 0);
}

// After an update check the task is gone (see task()): start it again once the update is over
void F1Ticker::resume() {
  if (!_started || _task || firmwareUpdating) return;
  _doc = new (std::nothrow) DynamicJsonDocument(20480);
  if (!_doc || _doc->capacity() == 0) {
    delete _doc;
    _doc = nullptr;
    return;  // the heap has not recovered yet: try again at the next frame
  }
  xTaskCreatePinnedToCore(task, "f1", 12288, this, 1, &_task, 0);
}

// The running order from the live feed replaces ESPN's while it is up to date; ESPN still says which session it is
void F1Ticker::snapshot(Snapshot &out) {
  {
    std::lock_guard<std::mutex> guard(_lock);
    out = _snap;
  }
  F1LiveState::Row rows[F1LiveState::ROWS];
  int count = 0, lap = 0, totalLaps = 0;
  char flag = 0;
  if (out.live.valid && _live.get(out.live.tag, rows, count, lap, totalLaps, flag)) {
    for (int i = 0; i < ROWS; i++) {
      strlcpy(out.live.rows[i].code, rows[i].code, sizeof(out.live.rows[i].code));
      strlcpy(out.live.rows[i].time, rows[i].time, sizeof(out.live.rows[i].time));
      out.live.rows[i].color = rows[i].color;
    }
    out.live.count = count;
    if (lap > 0) out.live.period = lap;
    out.live.totalLaps = totalLaps;
    out.live.flag = flag;
  }
}

uint32_t F1Ticker::version() {
  resume();
  std::lock_guard<std::mutex> guard(_lock);
  return _version + _live.updates();
}

void F1Ticker::task(void *self) {
  F1Ticker *ticker = static_cast<F1Ticker *>(self);
  for (;;) {
    if (firmwareUpdating) {  // the update has the network and the heap to itself
      // The TLS handshake needs big free blocks: hand back the parse buffer and the task stack.
      // The data stays in the ticker; resume() starts the task again after the update.
      delete ticker->_doc;
      ticker->_doc = nullptr;
      ticker->_task = nullptr;
      vTaskDelete(nullptr);
    }
    // in slices, so an update that starts meanwhile is seen within a second
    for (unsigned long left = ticker->refresh(); left > 0 && !firmwareUpdating;) {
      unsigned long slice = std::min(left, 1000UL);
      vTaskDelay(pdMS_TO_TICKS(slice));
      left -= slice;
    }
  }
}

// Milliseconds until the millis() value at, 0 when it has passed
static unsigned long until(unsigned long at) {
  long left = (long)(at - millis());
  return left > 0 ? left : 0;
}

// Does whatever download is due. Returns how long to wait before looking again.
unsigned long F1Ticker::refresh() {
  if (WiFi.status() != WL_CONNECTED || ezt::timeStatus() != timeSet) return 10 * 1000UL;
  if (until(_weekendAt) == 0) _weekendAt = millis() + fetchWeekend();
  if (until(_standingsAt) == 0) _standingsAt = millis() + fetchStandings();
  if (until(_seasonAt) == 0) _seasonAt = millis() + fetchSeason();  
  return std::max(1000UL, std::min(until(_weekendAt), std::min(until(_standingsAt), until(_seasonAt))));
}

static void copyName(char *to, size_t size, const char *from) { strlcpy(to, from, size); }

// "M. Verstappen" -> "VER" in the team's colour, unknown drivers in white
static void fillRow(F1Ticker::Row &row, const char *shortName) {
  const char *surname = strrchr(shortName, ' ');
  surname = surname ? surname + 1 : shortName;
  for (const auto &driver : DRIVERS)
    if (strcmp(surname, driver.surname) == 0) {
      strlcpy(row.code, driver.code, sizeof(row.code));
      row.color = driver.color;
      return;
    }
  for (int i = 0; i < 3; i++) row.code[i] = toupper(surname[i]);
  row.code[3] = 0;
  row.color = 0xFFFF;
}

unsigned long F1Ticker::fetchWeekend() {
  StaticJsonDocument<768> filter;
  filter["events"][0]["id"] = true;
  filter["events"][0]["date"] = true;
  filter["events"][0]["endDate"] = true;
  JsonObject competition = filter["events"][0]["competitions"].createNestedObject();
  competition["id"] = true;
  competition["type"]["abbreviation"] = true;
  competition["date"] = true;
  competition["status"]["period"] = true;
  competition["status"]["displayClock"] = true;
  competition["status"]["type"]["state"] = true;
  JsonObject driver = competition["competitors"].createNestedObject();
  driver["id"] = true;
  driver["order"] = true;
  driver["athlete"]["shortName"] = true;

  if (!getJson("http://site.api.espn.com/apis/site/v2/sports/racing/f1/scoreboard", filter, *_doc)) return RETRY_MS;

  time_t nowUtc = ezt::now();
  JsonObject event = (*_doc)["events"][0];
  String eventId = event["id"] | "", liveId;
  time_t weekendStart = parseEspnDate(event["date"] | ""), weekendEnd = parseEspnDate(event["endDate"] | "");

  Snapshot snap;
  time_t lastDate = 0, nextDate = 0;
  for (JsonObject c : event["competitions"].as<JsonArray>()) {
    String state = c["status"]["type"]["state"] | "";
    time_t date = parseEspnDate(c["date"] | "");
    String type = c["type"]["abbreviation"] | "";

    Session s;
    s.valid = true;
    s.start = date;
    s.eventId = eventId.toInt();
    s.id = String(c["id"] | "").toInt();
    if (type.startsWith("FP")) {
      s.tag = 'P';
      copyName(s.name, sizeof(s.name), type.c_str());
    } else if (type == "Qual") {
      s.tag = 'Q';
      copyName(s.name, sizeof(s.name), "QUALI");
    } else if (type == "SS") {  // sprint qualifying
      s.tag = 'Q';
      copyName(s.name, sizeof(s.name), "SQ");
    } else if (type == "SR") {
      s.tag = 'S';
      copyName(s.name, sizeof(s.name), "SPRINT");
    } else {
      s.tag = 'R';
      copyName(s.name, sizeof(s.name), "RACE");
    }

    if (state == "pre") {
      if (date > nowUtc && (nextDate == 0 || date < nextDate)) {
        nextDate = date;
        snap.next = s;
      }
      continue;
    }

    for (JsonObject d : c["competitors"].as<JsonArray>()) {
      int order = d["order"] | 0;
      if (order < 1 || order > ROWS) continue;
      fillRow(s.rows[order - 1], d["athlete"]["shortName"] | "");
      s.rows[order - 1].athleteId = String(d["id"] | "").toInt();
      s.count = std::max((int)s.count, order);
    }
    s.period = c["status"]["period"] | 0;
    String clock = c["status"]["displayClock"] | "";
    if (clock != "0:00" && clock != "0.0") copyName(s.clock, sizeof(s.clock), clock.c_str());

    if (state == "in") {
      s.live = true;
      snap.live = s;
      liveId = c["id"] | "";
    } else if (state == "post" && date >= lastDate) {
      lastDate = date;
      snap.last = s;
    }
  }
  snap.weekend = snap.live.valid || (weekendStart && nowUtc >= weekendStart && nowUtc <= weekendEnd + WEEKEND_TAIL_SECS);

  // The flag is in the session's own status: a safety car shows as yellow, a red flag as red
  if (snap.live.valid && !eventId.isEmpty() && !liveId.isEmpty()) {
    char url[160];
    snprintf(url, sizeof(url),
             "http://sports.core.api.espn.com/v2/sports/racing/leagues/f1/events/%s/competitions/%s/status",
             eventId.c_str(), liveId.c_str());
    StaticJsonDocument<64> flagFilter;
    flagFilter["flag"] = true;
    StaticJsonDocument<128> flagDoc;
    if (getJson(url, flagFilter, flagDoc)) {
      String name = flagDoc["flag"] | "";
      name.toUpperCase();
      if (name.indexOf("RED") >= 0) snap.live.flag = 'r';
      else if (name.indexOf("YELLOW") >= 0 || name.indexOf("SAFETY") >= 0 || name.indexOf("CAUTION") >= 0 ||
               name == "SC" || name == "VSC") snap.live.flag = 'y';
      f1log("[F1] flag: %s\n", name.c_str());
    }
  }
  f1log("[F1] weekend %d, live %s %s, last %s, next %s\n", snap.weekend, snap.live.valid ? snap.live.name : "-",
        snap.live.valid ? snap.live.rows[0].code : "", snap.last.valid ? snap.last.name : "-",
        snap.next.valid ? snap.next.name : "-");

  {
    std::lock_guard<std::mutex> guard(_lock);
    _snap.weekend = snap.weekend;
    _snap.live = snap.live;
    liveEventOn = snap.live.valid;
    _snap.last = snap.last;
    _snap.next = snap.next;
    _version++;
  }

  // The live feed runs from shortly before a session until a few minutes after it
  _live.setWanted(snap.live.valid || (snap.weekend && snap.next.valid && snap.next.start - nowUtc < 10 * 60));

  // Times and gaps cost a request per driver: only for the drivers shown, the last session once
  bool timed = false;
  if (snap.live.valid && !_live.fresh()) {  // the live feed has them otherwise
    fetchTimes(snap.live, ROWS);
    timed = true;
  }
  if (snap.last.valid) {
    if (_lastTimed.valid && _lastTimed.id == snap.last.id) {
      snap.last = _lastTimed;
    } else {
      fetchTimes(snap.last, 3);
      if (snap.last.rows[0].time[0]) _lastTimed = snap.last;
    }
    timed = true;
  }
  if (timed) {
    std::lock_guard<std::mutex> guard(_lock);
    _snap.live = snap.live;
    _snap.last = snap.last;
    _version++;
  }

  // Be there when the next session starts
  unsigned long wait = IDLE_REFRESH_MS;
  if (snap.live.valid) return LIVE_REFRESH_MS;
  if (snap.weekend) wait = WEEKEND_REFRESH_MS;
  if (nextDate > nowUtc) {
    time_t left = nextDate - nowUtc;
    wait = left < 15 * 60 ? START_REFRESH_MS : std::min(wait, (unsigned long)(left - 14 * 60) * 1000UL);
  }
  return wait;
}

// The top of the drivers' championship from Jolpica (HTTPS only; the answer is tiny)
unsigned long F1Ticker::fetchStandings() {
  StaticJsonDocument<256> filter;
  JsonObject entry = filter["MRData"]["StandingsTable"]["StandingsLists"][0]["DriverStandings"].createNestedObject();
  entry["points"] = true;
  entry["Driver"]["code"] = true;
  DynamicJsonDocument doc(1536);
  if (!getJson("https://api.jolpi.ca/ergast/f1/current/driverstandings.json?limit=3", filter, doc)) return RETRY_MS;

  Standing standings[STANDINGS];
  uint8_t count = 0;
  for (JsonObject e : doc["MRData"]["StandingsTable"]["StandingsLists"][0]["DriverStandings"].as<JsonArray>()) {
    if (count >= STANDINGS) break;
    strlcpy(standings[count].code, e["Driver"]["code"] | "---", sizeof(standings[count].code));
    standings[count].color = driverColor(standings[count].code);
    standings[count].points = atoi(e["points"] | "0");
    count++;
  }
  f1log("[F1] standings: %d drivers, leader %s\n", count, count ? standings[0].code : "-");
  if (count == 0) return RETRY_MS;

  std::lock_guard<std::mutex> guard(_lock);
  for (int i = 0; i < count; i++) _snap.standings[i] = standings[i];
  _snap.standingsCount = count;
  _version++;
  return STANDINGS_REFRESH_MS;
}

// "1:35.130" (a lap), "1:47:14.808" (a race) or "35.130" in milliseconds, -1 when it isn't a time
static long parseTimeMs(const char *text) {
  if (!*text || !isdigit((unsigned char)*text)) return -1;
  long parts[3];
  int n = 0;
  const char *p = text;
  char *end;
  while (n < 3) {
    parts[n++] = strtol(p, &end, 10);
    if (*end != ':') break;
    p = end + 1;
  }
  long ms = 0;
  if (*end == '.') {
    char fraction[4] = "000";
    int i = 0;
    for (const char *q = end + 1; isdigit((unsigned char)*q) && i < 3; q++) fraction[i++] = *q;
    ms = atoi(fraction);
  }
  long seconds = 0;
  for (int i = 0; i < n; i++) seconds = seconds * 60 + parts[i];
  return seconds * 1000 + ms;
}

// A gap as the 3x5 font shows it: "+.088", "+1.234", "+12.3", "+1:05"
static void formatGap(char *out, size_t size, long ms) {
  if (ms < 1000) snprintf(out, size, "+.%03ld", ms);
  else if (ms < 10000) snprintf(out, size, "+%ld.%03ld", ms / 1000, ms % 1000);
  else if (ms < 60000) snprintf(out, size, "+%ld.%ld", ms / 1000, ms % 1000 / 100);
  else snprintf(out, size, "+%ld:%02ld", ms / 60000, ms / 1000 % 60);
}

// Fills in the times of the first count drivers: ESPN's totalTime is the best lap in practice and
// qualifying and the race time in a race, so the first place shows it (or LEAD in a race) and the others
// their gap to it.
void F1Ticker::fetchTimes(Session &session, int count) {
  if (!session.id || !session.eventId) return;
  StaticJsonDocument<128> filter;
  filter["splits"]["categories"][0]["stats"][0]["name"] = true;
  filter["splits"]["categories"][0]["stats"][0]["displayValue"] = true;

  long ms[ROWS];
  char leader[12] = "";  // the first place's own text, "1:35.130"
  for (int i = 0; i < ROWS; i++) ms[i] = -1;
  for (int i = 0; i < count && i < ROWS; i++) {
    if (!session.rows[i].athleteId) continue;
    char url[200];
    snprintf(url, sizeof(url),
             "http://sports.core.api.espn.com/v2/sports/racing/leagues/f1/events/%lu/competitions/%lu/competitors/%lu/statistics/0",
             (unsigned long)session.eventId, (unsigned long)session.id, (unsigned long)session.rows[i].athleteId);
    if (!getJson(url, filter, *_doc)) continue;
    for (JsonObject stat : (*_doc)["splits"]["categories"][0]["stats"].as<JsonArray>())
      if (strcmp(stat["name"] | "", "totalTime") == 0) {
        const char *text = stat["displayValue"] | "";
        ms[i] = parseTimeMs(text);
        if (i == 0) strlcpy(leader, text, sizeof(leader));
      }
  }

  bool race = session.tag == 'R' || session.tag == 'S';
  if (ms[0] >= 0) {
    strlcpy(session.rows[0].time, race ? "LEAD" : leader, sizeof(session.rows[0].time));
    for (int i = 1; i < count && i < ROWS; i++)
      if (ms[i] >= ms[0]) formatGap(session.rows[i].time, sizeof(session.rows[i].time), ms[i] - ms[0]);
  }
  f1log("[F1] times %s: leader %ld ms\n", session.name, ms[0]);
}

// The circuit's city as the 3x5 font can show it: ASCII capitals ("Sao paulo" with an accent too)
static void cityName(char *to, size_t size, const char *city) {
  static const char LATIN1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTSAAAAAAACEEEEIIIIDNOOOOO/OUUUUYTY";  // U+00C0..U+00FF
  size_t n = 0;
  for (const uint8_t *p = (const uint8_t *)city; *p && n + 1 < size; p++) {
    if (*p == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) {
      to[n++] = LATIN1[p[1] - 0x80];
      p++;
    } else if (*p < 0x80) {
      to[n++] = toupper(*p);
    }
  }
  to[n] = 0;
}

// The coming Grand Prix from Jolpica: one race, under a kilobyte (ESPN's season calendar is ~650KB)
unsigned long F1Ticker::fetchSeason() {
  StaticJsonDocument<192> filter;
  JsonObject race = filter["MRData"]["RaceTable"]["Races"].createNestedObject();
  race["raceName"] = true;
  race["FirstPractice"]["date"] = true;
  race["FirstPractice"]["time"] = true;
  DynamicJsonDocument doc(1024);
  if (!getJson("https://api.jolpi.ca/ergast/f1/current/next.json", filter, doc)) return RETRY_MS;

  Race upcoming;
  JsonObject next = doc["MRData"]["RaceTable"]["Races"][0];
  // "Mexico City Grand Prix" -> "MEXICO"
  String name = next["raceName"] | "";
  name.replace(" Grand Prix", "");
  name.replace(" City", "");  // Mexico City -> MEXICO
  cityName(upcoming.city, 12, name.c_str());  // 11 characters is what fits beside the days
  char start[24];
  snprintf(start, sizeof(start), "%sT%s", next["FirstPractice"]["date"] | "", next["FirstPractice"]["time"] | "");
  upcoming.start = parseEspnDate(start);  // "2026-10-09T08:30:00Z" reads the same way
  f1log("[F1] next race: %s\n", upcoming.city);
  if (!upcoming.city[0] || !upcoming.start) return RETRY_MS;

  std::lock_guard<std::mutex> guard(_lock);
  _snap.upcoming[0] = upcoming;
  _snap.upcomingCount = 1;
  _version++;
  return SEASON_REFRESH_MS;
}

bool F1Ticker::getJson(const char *url, JsonDocument &filter, JsonDocument &doc) {
  bool secure = strncmp(url, "https", 5) == 0;
  WiFiClient plain;
  WiFiClientSecure tls;
  if (secure) tls.setInsecure();  // public data, nothing to protect
  WiFiClient &client = secure ? (WiFiClient &)tls : plain;

  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding, so the JSON can be streamed
  http.setTimeout(10000);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    f1log("[F1] %s: HTTP %d\n", url, code);
    http.end();
    return false;
  }

  YieldingReader reader(http.getStream(), 10000);
  doc.clear();
  DeserializationError err = deserializeJson(doc, reader, DeserializationOption::Filter(filter),
                                             DeserializationOption::NestingLimit(20));
  http.end();
  if (err) {
    f1log("[F1] %s: %s after %u bytes (heap free %u, largest block %u)\n", url, err.c_str(), reader.count,
          ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return false;
  }
  return true;
}

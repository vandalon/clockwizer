#include "F1Ticker.h"

#include <algorithm>
#include <memory>
#include <HTTPClient.h>
#include <TelnetStream.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ezTime.h>

#include <EspnFeed.h>
#include <F1Drivers.h>
#include "JsonScan.h"

static const unsigned long LIVE_REFRESH_MS = 15 * 1000UL;  // plus a request per driver for the times
static const unsigned long WEEKEND_REFRESH_MS = 5 * 60 * 1000UL;  // between sessions
static const unsigned long START_REFRESH_MS = 30 * 1000UL;        // a session is about to start
static const unsigned long IDLE_REFRESH_MS = 30 * 60 * 1000UL;
static const unsigned long STANDINGS_REFRESH_MS = 30 * 60 * 1000UL;
static const unsigned long SEASON_REFRESH_MS = 60 * 60 * 1000UL;
static const unsigned long RETRY_MS = 60 * 1000UL;
// true = never start the F1 live-timing connection (the gaps and lap times), for finding memory problems
static const bool F1_LIVE_FEED_OFF = false;
static const uint32_t TASK_STACK = 9216;  // it uses about 5.5 KB, and every KB counts while the live feed is connected
static const time_t DELAY_GRACE_SECS = 3 * 60;     // a session this long past its start time and still not running is delayed
static const time_t DELAY_MAX_SECS = 6 * 60 * 60;  // and is forgotten this long after
static const time_t WEEKEND_TAIL_SECS = 3 * 60 * 60;  // the weekend lasts this long after the race started

extern volatile bool firmwareUpdating;  // main.cpp
extern volatile bool liveEventOn;       // main.cpp: no automatic update while true

uint16_t F1Ticker::driverColor(const char *code) { return f1DriverColor(code); }

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


void F1Ticker::begin(CWDateTime *dateTime) {
  if (_started) return;
  esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT)
    f1log("[F1] the last restart was a crash; F1 code was at: %.15s\n", f1Phase);
  f1At("begin");
  _started = true;
  _dateTime = dateTime;
  // Core 0, so downloading never stalls the display loop on core 1
  xTaskCreatePinnedToCore(task, "f1", TASK_STACK, this, 1, &_task, 0);
}

// After an update check the task is gone (see task()): start it again once the update is over
void F1Ticker::resume() {
  if (!_started || _task || firmwareUpdating || f1Park) return;
  if (xTaskCreatePinnedToCore(task, "f1", TASK_STACK, this, 1, &_task, 0) == pdPASS) return;
  static unsigned long loggedAt = 0;
  if (!loggedAt || millis() - loggedAt > 10000) {
    loggedAt = millis();
    f1log("[F1] task cannot restart (data RAM free %u, largest block %u)\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  }
}

// The running order from the live feed replaces ESPN's while it is up to date; ESPN still says which session it is
// How many places have a time or gap
static int timedRows(const F1Ticker::Row *rows) {
  int n = 0;
  for (int i = 0; i < F1Ticker::ROWS; i++)
    if (rows[i].time[0]) n++;
  return n;
}

void F1Ticker::snapshot(Snapshot &out) {
  {
    std::lock_guard<std::mutex> guard(_lock);
    out = _snap;
  }
  F1LiveState::Row rows[F1LiveState::ROWS];
  int count = 0, lap = 0, totalLaps = 0;
  char flag = 0;
  bool finished = false;
  if (out.live.valid && _live.get(out.live.tag, rows, count, lap, totalLaps, flag, finished)) {
    for (int i = 0; i < ROWS; i++) {
      strlcpy(out.live.rows[i].code, rows[i].code, sizeof(out.live.rows[i].code));
      strlcpy(out.live.rows[i].time, rows[i].time, sizeof(out.live.rows[i].time));
      out.live.rows[i].color = rows[i].color;
      out.live.rows[i].fastest = rows[i].fastest;
    }
    out.live.count = count;
    if (lap > 0) out.live.period = lap;
    out.live.totalLaps = totalLaps;
    out.live.flag = flag;
    out.live.finished = finished;
    if (out.live.id) {  // remember it, for the next boot and for when the feed has nothing new
      std::lock_guard<std::mutex> guard(_lock);
      if (_saved.id != out.live.id || memcmp(_saved.rows, out.live.rows, sizeof(_saved.rows)) != 0 || _saved.flag != flag) {
        _saved.id = out.live.id;
        _saved.count = count;
        _saved.flag = flag;
        memcpy(_saved.rows, out.live.rows, sizeof(_saved.rows));
        _savedDirty = true;
      }
    }
    int part = _live.part();
    if (out.live.tag == 'Q' && part >= 1 && part <= 3) snprintf(out.live.name, sizeof(out.live.name), "%s%d", out.live.name[0] == 'S' ? "SQ" : "Q", part);
    int left = _live.clockSecs((long)ezt::now());
    if (left >= 0 && (out.live.tag == 'P' || out.live.tag == 'Q')) snprintf(out.live.clock, sizeof(out.live.clock), "%d:%02d", left / 60, left % 60);
    std::lock_guard<std::mutex> guard(_lock);
    strlcpy(_saved.name, out.live.name, sizeof(_saved.name));
  } else {
    // No fresh data from the feed: the last known order and times of this session, from before a restart or a gap
    std::lock_guard<std::mutex> guard(_lock);
    Session *sessions[] = {&out.live, &out.last};
    for (Session *session : sessions)
      if (session->valid && session->id && session->id == _saved.id && _saved.count &&
          (!session->rows[0].time[0] || (session == &out.last && timedRows(_saved.rows) > timedRows(session->rows)))) {
        memcpy(session->rows, _saved.rows, sizeof(session->rows));
        session->count = _saved.count;
        if (session == &out.live) {
          session->flag = _saved.flag;
          if (_saved.name[0]) strlcpy(session->name, _saved.name, sizeof(session->name));
        }
      }
  }
}

uint32_t F1Ticker::version() {
  resume();
  std::lock_guard<std::mutex> guard(_lock);
  return _version + _live.updates();
}

void F1Ticker::task(void *self) {
  F1Ticker *ticker = static_cast<F1Ticker *>(self);
  f1TaskUp = true;
  for (;;) {
    if (firmwareUpdating || f1Park) {  // the update, or the live feed's handshake, has the network and the heap to itself
      // The TLS handshake needs big free blocks: hand back the task stack.
      // The data stays in the ticker; resume() starts the task again after the update.
      ticker->_task = nullptr;
      f1TaskUp = false;
      vTaskDelete(nullptr);
    }
    // in slices, so an update that starts meanwhile is seen within a second
    for (unsigned long left = ticker->refresh(); left > 0 && !firmwareUpdating && !f1Park;) {
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

// The standings and the next race change slowly, so the last download is kept in flash: it shows right after
// a boot and the HTTPS download only runs once it is older than its refresh time
void F1Ticker::loadCache() {
  _cacheLoaded = true;
  Preferences prefs;
  if (!prefs.begin("f1cache", true)) return;
  time_t now = ezt::now();
  Standing standings[STANDINGS];
  uint8_t count = prefs.getUChar("standN", 0);
  time_t at = prefs.getUInt("standAt", 0);
  if (count > 0 && count <= STANDINGS && prefs.getBytesLength("stand") == sizeof(standings) && at && now >= at) {
    prefs.getBytes("stand", standings, sizeof(standings));
    unsigned long age = now - at;  // seconds: in milliseconds it wraps after 49 days
    std::lock_guard<std::mutex> guard(_lock);
    for (int i = 0; i < count; i++) _snap.standings[i] = standings[i];
    _snap.standingsCount = count;
    _version++;
    if (age < STANDINGS_REFRESH_MS / 1000) _standingsAt = millis() + (STANDINGS_REFRESH_MS / 1000 - age) * 1000UL;
  }
  if (prefs.getBytesLength("liveRows") == sizeof(_saved)) {
    std::lock_guard<std::mutex> guard(_lock);
    prefs.getBytes("liveRows", &_saved, sizeof(_saved));
  }
  Race race;
  at = prefs.getUInt("raceAt", 0);
  if (prefs.getBytesLength("race") == sizeof(race) && at && now >= at) {
    prefs.getBytes("race", &race, sizeof(race));
    unsigned long age = now - at;  // seconds
    if (race.city[0] && race.start > now) {
      std::lock_guard<std::mutex> guard(_lock);
      _snap.upcoming[0] = race;
      _snap.upcomingCount = 1;
      _version++;
      if (age < SEASON_REFRESH_MS / 1000) _seasonAt = millis() + (SEASON_REFRESH_MS / 1000 - age) * 1000UL;
    }
  }
  prefs.end();
  f1log("[F1] from flash: %d standings, next race %s, next downloads in %lu and %lu s\n", _snap.standingsCount,
        _snap.upcomingCount ? _snap.upcoming[0].city : "-", until(_standingsAt) / 1000, until(_seasonAt) / 1000);
}

// While a session runs the order is saved every 2 minutes; when it has ended, at once
void F1Ticker::persistLive() {
  SavedRows copy;
  {
    std::lock_guard<std::mutex> guard(_lock);
    bool over = !_snap.live.valid;
    if (!_savedDirty || (!over && millis() - _savedAt < 2 * 60 * 1000UL && _savedAt)) return;
    copy = _saved;
    _savedDirty = false;
    _savedAt = millis();
  }
  Preferences prefs;
  if (!prefs.begin("f1cache", false)) return;
  prefs.putBytes("liveRows", &copy, sizeof(copy));
  prefs.end();
  f1log("[F1] saved the order of session %lu (%d places)\n", (unsigned long)copy.id, copy.count);
}

// Does whatever download is due. Returns how long to wait before looking again.
unsigned long F1Ticker::refresh() {
  if (WiFi.status() != WL_CONNECTED || ezt::timeStatus() != timeSet) return 10 * 1000UL;
  std::lock_guard<std::mutex> net(f1NetLock);  // the live feed connects between two refreshes, see F1Live.h
  if (!_cacheLoaded) loadCache();
  persistLive();
  if (until(_weekendAt) == 0) _weekendAt = millis() + fetchWeekend();
  // The two Jolpica downloads are HTTPS
  // The live feed's connection holds the TLS memory: these wait until it's closed (they are not urgent)
  bool liveOpen = _live.connected();
  if (until(_standingsAt) == 0 && !liveOpen) {
    _standingsAt = millis() + fetchStandings();
  }
  if (until(_seasonAt) == 0 && !liveOpen) {
    _seasonAt = millis() + fetchSeason();
  }
  f1At("idle");
  f1log("[F1] refresh done (stack left %u, heap free %u, largest block %u)\n", (unsigned)uxTaskGetStackHighWaterMark(nullptr),
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  if (liveOpen && (until(_standingsAt) == 0 || until(_seasonAt) == 0)) return std::max(1000UL, std::min(until(_weekendAt), 30 * 1000UL));
  return std::max(1000UL, std::min(until(_weekendAt), std::min(until(_standingsAt), until(_seasonAt))));
}

static void copyName(char *to, size_t size, const char *from) { strlcpy(to, from, size); }

// "M. Verstappen" -> "VER" in the team's colour, unknown drivers in white
static void fillRow(F1Ticker::Row &row, const char *shortName) {
  const char *surname = strrchr(shortName, ' ');
  surname = surname ? surname + 1 : shortName;
  if (const F1Driver *known = f1DriverBySurname(surname)) {
    strlcpy(row.code, known->code, sizeof(row.code));
    row.color = known->color;
    return;
  }
  size_t n = 0;
  for (; n < 3 && surname[n]; n++) row.code[n] = toupper((unsigned char)surname[n]);
  row.code[n] = 0;
  row.color = 0xFFFF;
}

// The ESPN scoreboard, read as it streams in. A session (a competition) is complete when the next one starts or the
// text ends, and so is a driver: only the ones being read are held, never the document.
struct WeekendScan {
  time_t nowUtc = 0;
  char eventId[16] = "", eventDate[24] = "", eventEnd[24] = "", liveId[16] = "", circuit[48] = "";
  F1Ticker::Snapshot snap;
  time_t lastDate = 0, nextDate = 0;

  int comp = -1, competitor = -1;  // which session and driver are being read
  F1Ticker::Session s;
  char id[16] = "", date[24] = "", type[8] = "", state[8] = "", clock[8] = "", statusName[24] = "";
  int period = 0;
  uint32_t delayed[8] = {};  // sessions seen delayed (in and out)
  uint8_t delayedCount = 0;
  bool anyDelayed = false;   // a session is delayed now
  uint32_t driverId = 0;
  int order = 0;
  char driverName[24] = "";

  void flushCompetitor() {
    if (competitor < 0) return;
    if (order >= 1 && order <= F1Ticker::ROWS) {
      fillRow(s.rows[order - 1], driverName);
      s.rows[order - 1].athleteId = driverId;
      s.count = std::max((int)s.count, order);
    }
    competitor = -1;
    driverId = 0;
    order = 0;
    driverName[0] = 0;
  }

  // The session just read is sorted into the live one, the last finished and the next to come
  void finish() {
    if (comp < 0) return;
    flushCompetitor();
    time_t when = parseEspnDate(date);
    s.valid = true;
    s.start = when;
    s.eventId = strtoul(eventId, nullptr, 10);
    s.id = strtoul(id, nullptr, 10);
    if (!strncmp(type, "FP", 2)) {
      s.tag = 'P';
      copyName(s.name, sizeof(s.name), type);
    } else if (!strcmp(type, "Qual")) {
      s.tag = 'Q';
      copyName(s.name, sizeof(s.name), "QUALI");
    } else if (!strcmp(type, "SS")) {  // sprint qualifying
      s.tag = 'Q';
      copyName(s.name, sizeof(s.name), "SQ");
    } else if (!strcmp(type, "SR")) {
      s.tag = 'S';
      copyName(s.name, sizeof(s.name), "SPRINT");
    } else {
      s.tag = 'R';
      copyName(s.name, sizeof(s.name), "RACE");
    }
    if (!strcmp(state, "pre")) {
      for (F1Ticker::Row &row : s.rows) row = F1Ticker::Row();
      s.count = 0;
      // ESPN does not flag a late start: the start time just stays in the past until a new one is set
      bool late = when + DELAY_GRACE_SECS <= nowUtc && when + DELAY_MAX_SECS > nowUtc;
      bool delayedNow = late || strstr(statusName, "DELAY") || strstr(statusName, "POSTPON");
      bool known = false;
      for (int i = 0; i < delayedCount; i++) known = known || delayed[i] == s.id;
      if (delayedNow) {
        s.status = 'd';
        anyDelayed = true;
        if (!known) {
          if (delayedCount == 8) {
            memmove(delayed, delayed + 1, 7 * sizeof(delayed[0]));
            delayedCount--;
          }
          delayed[delayedCount++] = s.id;
        }
      } else if (known && when > nowUtc) {
        s.status = 'n';
      }
      if ((delayedNow || when > nowUtc) && (nextDate == 0 || when < nextDate)) {
        nextDate = when;
        snap.next = s;
      }
      if ((delayedNow || when > nowUtc) && snap.comingCount < F1Ticker::Snapshot::COMING) {  // in order of start
        int at = snap.comingCount;
        while (at > 0 && snap.coming[at - 1].start > when) {
          snap.coming[at] = snap.coming[at - 1];
          at--;
        }
        copyName(snap.coming[at].name, sizeof(snap.coming[at].name), s.name);
        snap.coming[at].start = when;
        snap.coming[at].status = s.status;
        snap.comingCount++;
      }
    } else {
      s.period = period;
      if (strcmp(clock, "0:00") && strcmp(clock, "0.0")) copyName(s.clock, sizeof(s.clock), clock);
      if (!strcmp(state, "in")) {
        s.live = true;
        snap.live = s;
        strlcpy(liveId, id, sizeof(liveId));
      } else if (!strcmp(state, "post") && when >= lastDate) {
        lastDate = when;
        snap.last = s;
      }
    }
    comp = -1;
  }

  void start(int index) {
    finish();
    comp = index;
    s = F1Ticker::Session();
    id[0] = date[0] = type[0] = state[0] = clock[0] = statusName[0] = 0;
    period = 0;
  }

  static void leaf(void *sink, const char (*p)[JsonScan::KEY], int n, const char *v, bool) {
    WeekendScan &w = *(WeekendScan *)sink;
    if (n < 3 || strcmp(p[0], "events") || strcmp(p[1], "0")) return;  // the first event only
    if (n == 3) {
      if (!strcmp(p[2], "id")) strlcpy(w.eventId, v, sizeof(w.eventId));
      else if (!strcmp(p[2], "date")) strlcpy(w.eventDate, v, sizeof(w.eventDate));
      else if (!strcmp(p[2], "endDate")) strlcpy(w.eventEnd, v, sizeof(w.eventEnd));
      return;
    }
    if (!strcmp(p[2], "circuit")) {  // the name, then the city
      if (jsonPathIs(p, n, "events.0.circuit.fullName") || jsonPathIs(p, n, "events.0.circuit.address.city")) {
        if (w.circuit[0]) strlcat(w.circuit, " ", sizeof(w.circuit));
        strlcat(w.circuit, v, sizeof(w.circuit));
      }
      return;
    }
    if (n < 5 || strcmp(p[2], "competitions")) return;
    int c = atoi(p[3]);
    if (c != w.comp) w.start(c);
    if (jsonPathIs(p, n, "events.0.competitions.*.id")) strlcpy(w.id, v, sizeof(w.id));
    else if (jsonPathIs(p, n, "events.0.competitions.*.date")) strlcpy(w.date, v, sizeof(w.date));
    else if (jsonPathIs(p, n, "events.0.competitions.*.type.abbreviation")) strlcpy(w.type, v, sizeof(w.type));
    else if (jsonPathIs(p, n, "events.0.competitions.*.status.period")) w.period = atoi(v);
    else if (jsonPathIs(p, n, "events.0.competitions.*.status.displayClock")) strlcpy(w.clock, v, sizeof(w.clock));
    else if (jsonPathIs(p, n, "events.0.competitions.*.status.type.name")) {
      strlcpy(w.statusName, v, sizeof(w.statusName));
      for (char *c = w.statusName; *c; c++) *c = toupper((unsigned char)*c);
    } else if (jsonPathIs(p, n, "events.0.competitions.*.status.type.state")) strlcpy(w.state, v, sizeof(w.state));
    else if (n >= 7 && !strcmp(p[4], "competitors")) {
      int k = atoi(p[5]);
      if (k != w.competitor) {
        w.flushCompetitor();
        w.competitor = k;
      }
      if (jsonPathIs(p, n, "events.0.competitions.*.competitors.*.id")) w.driverId = strtoul(v, nullptr, 10);
      else if (jsonPathIs(p, n, "events.0.competitions.*.competitors.*.order")) w.order = atoi(v);
      else if (jsonPathIs(p, n, "events.0.competitions.*.competitors.*.athlete.shortName")) strlcpy(w.driverName, v, sizeof(w.driverName));
    }
  }
};

unsigned long F1Ticker::fetchWeekend() {
  f1At("fetch weekend");
  std::unique_ptr<WeekendScan> scanHeap(new WeekendScan);  // big: not on the task's stack
  WeekendScan &scan = *scanHeap;
  scan.nowUtc = ezt::now();
  memcpy(scan.delayed, _delayed, sizeof(_delayed));
  scan.delayedCount = _delayedCount;
  if (!getScan("http://site.api.espn.com/apis/site/v2/sports/racing/f1/scoreboard", WeekendScan::leaf, &scan)) return RETRY_MS;
  scan.finish();
  memcpy(_delayed, scan.delayed, sizeof(_delayed));
  _delayedCount = scan.delayedCount;

  time_t nowUtc = scan.nowUtc, nextDate = scan.nextDate;
  String eventId = scan.eventId, liveId = scan.liveId;
  time_t weekendStart = parseEspnDate(scan.eventDate), weekendEnd = parseEspnDate(scan.eventEnd);
  Snapshot &snap = scan.snap;
  snap.weekend = snap.live.valid || (weekendStart && nowUtc >= weekendStart && nowUtc <= weekendEnd + WEEKEND_TAIL_SECS);

  // The flag is in the session's own status: a safety car shows as yellow, a red flag as red
  if (snap.live.valid && !eventId.isEmpty() && !liveId.isEmpty()) {
    char url[160];
    snprintf(url, sizeof(url),
             "http://sports.core.api.espn.com/v2/sports/racing/leagues/f1/events/%s/competitions/%s/status",
             eventId.c_str(), liveId.c_str());
    char flagText[24] = "";
    if (getScan(url, [](void *sink, const char (*p)[JsonScan::KEY], int n, const char *v, bool) {
          if (n == 1 && !strcmp(p[0], "flag")) strlcpy((char *)sink, v, 24);
        }, flagText)) {
      String name = flagText;
      name.toUpperCase();
      if (name.indexOf("RED") >= 0) snap.live.flag = 'r';
      else if (name.indexOf("YELLOW") >= 0 || name.indexOf("SAFETY") >= 0 || name.indexOf("CAUTION") >= 0 ||
               name == "SC" || name == "VSC") snap.live.flag = 'y';
      f1log("[F1] flag: %s\n", name.c_str());
    }
  }
  f1log("[F1] circuit: %s\n", scan.circuit);
  f1log("[F1] weekend %d, live %s %s, last %s, next %s\n", snap.weekend, snap.live.valid ? snap.live.name : "-",
        snap.live.valid ? snap.live.rows[0].code : "", snap.last.valid ? snap.last.name : "-",
        snap.next.valid ? snap.next.name : "-");

  {
    std::lock_guard<std::mutex> guard(_lock);
    _snap.weekend = snap.weekend;
    strlcpy(_snap.circuit, scan.circuit, sizeof(_snap.circuit));
    _snap.live = snap.live;
    liveEventOn = snap.live.valid;
    _snap.last = snap.last;
    _snap.next = snap.next;
    for (int i = 0; i < Snapshot::COMING; i++) _snap.coming[i] = snap.coming[i];
    _snap.comingCount = snap.comingCount;
    _version++;
  }

  // The live feed runs from shortly before a session until a few minutes after it; a delayed one has to wait for ESPN to say it is on
  _live.setWanted(!F1_LIVE_FEED_OFF && (snap.live.valid || (snap.weekend && snap.next.valid && snap.next.status != 'd' && snap.next.start - nowUtc < 10 * 60)));

  // Times and gaps cost a request per driver: only for the drivers shown, the last session once
  bool timed = false;
  if (snap.live.valid && !_live.fresh() && !_live.connected()) {  // the live feed has them otherwise
    fetchTimes(snap.live, TIMED);
    timed = true;
  }
  if (snap.last.valid) {
    if (_lastTimed.valid && _lastTimed.id == snap.last.id) {
      snap.last = _lastTimed;
    } else {
      fetchTimes(snap.last, ROWS);  // everyone: the result lists show them all
      if (snap.last.rows[0].time[0]) _lastTimed = snap.last;
    }
    // A race or sprint: who had the fastest lap. Jolpica has the results some time after the finish: look again
    // every 10 minutes until they are there
    bool race = snap.last.tag == 'R' || snap.last.tag == 'S';
    bool have = false;
    for (const Row &row : snap.last.rows) have = have || row.fastest;
    if (race && !have && (_fastestId != snap.last.id || millis() - _fastestAt > 10 * 60 * 1000UL)) {
      _fastestId = snap.last.id;
      _fastestAt = millis();
      if (fetchFastest(snap.last) && _lastTimed.valid && _lastTimed.id == snap.last.id) _lastTimed = snap.last;
    }
    timed = true;
  }

  // Published once, with the times in: a first publish without them made the time column blank for the
  // seconds the requests above take, on every refresh
  {
    std::lock_guard<std::mutex> guard(_lock);
    _snap.weekend = snap.weekend;
    _snap.live = snap.live;
    _snap.last = snap.last;
    _snap.next = snap.next;
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
  if (scan.anyDelayed) wait = std::min(wait, START_REFRESH_MS * 4);  // look for the new start time
  return wait;
}

// The top of the drivers' championship from Jolpica (HTTPS only; the answer is tiny)
struct StandingsScan {
  F1Ticker::Standing standings[F1Ticker::STANDINGS];
  uint8_t count = 0;
  int index = -1;
  char code[4] = "---", points[8] = "0";

  void flush() {
    if (index >= 0 && count < F1Ticker::STANDINGS) {
      strlcpy(standings[count].code, code, sizeof(standings[count].code));
      standings[count].color = F1Ticker::driverColor(code);
      standings[count].points = atoi(points);
      count++;
    }
    index = -1;
    strcpy(code, "---");
    strcpy(points, "0");
  }

  static void leaf(void *sink, const char (*p)[JsonScan::KEY], int n, const char *v, bool) {
    StandingsScan &s = *(StandingsScan *)sink;
    bool isPoints = jsonPathIs(p, n, "MRData.StandingsTable.StandingsLists.0.DriverStandings.*.points");
    if (!isPoints && !jsonPathIs(p, n, "MRData.StandingsTable.StandingsLists.0.DriverStandings.*.Driver.code")) return;
    int k = atoi(p[5]);
    if (k != s.index) {
      s.flush();
      s.index = k;
    }
    if (isPoints) strlcpy(s.points, v, sizeof(s.points));
    else strlcpy(s.code, v, sizeof(s.code));
  }
};

unsigned long F1Ticker::fetchStandings() {
  f1At("fetch standings");
  StandingsScan scan;
  if (!getScan("https://api.jolpi.ca/ergast/f1/current/driverstandings.json?limit=30", StandingsScan::leaf, &scan)) return RETRY_MS;
  scan.flush();
  Standing *standings = scan.standings;
  uint8_t count = scan.count;
  f1log("[F1] standings: %d drivers, leader %s\n", count, count ? standings[0].code : "-");
  if (count == 0) return RETRY_MS;

  {
    std::lock_guard<std::mutex> guard(_lock);
    for (int i = 0; i < count; i++) _snap.standings[i] = standings[i];
    _snap.standingsCount = count;
    _version++;
  }
  // Flash writes are slow: not while the display thread waits for the lock
  Preferences prefs;
  if (prefs.begin("f1cache", false)) {
    prefs.putBytes("stand", standings, sizeof(standings));
    prefs.putUChar("standN", count);
    prefs.putUInt("standAt", ezt::now());
    prefs.end();
  }
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
  if (ms < 1000) snprintf(out, size, "+0.%03ld", ms);
  else if (ms < 10000) snprintf(out, size, "+%ld.%03ld", ms / 1000, ms % 1000);
  else if (ms < 60000) snprintf(out, size, "+%ld.%ld", ms / 1000, ms % 1000 / 100);
  else snprintf(out, size, "+%ld:%02ld", ms / 60000, ms / 1000 % 60);
}

// One driver's statistics: the entry called totalTime holds the time
struct TimesScan {
  int index = -1;
  char name[16] = "", value[16] = "", total[16] = "";
  bool have = false;

  void flush() {
    if (index >= 0 && !strcmp(name, "totalTime")) {
      strlcpy(total, value, sizeof(total));
      have = true;
    }
    index = -1;
    name[0] = value[0] = 0;
  }

  static void leaf(void *sink, const char (*p)[JsonScan::KEY], int n, const char *v, bool) {
    TimesScan &t = *(TimesScan *)sink;
    bool isName = jsonPathIs(p, n, "splits.categories.0.stats.*.name");
    if (!isName && !jsonPathIs(p, n, "splits.categories.0.stats.*.displayValue")) return;
    int k = atoi(p[4]);
    if (k != t.index) {
      t.flush();
      t.index = k;
    }
    if (isName) strlcpy(t.name, v, sizeof(t.name));
    else strlcpy(t.value, v, sizeof(t.value));
  }
};

// Fills in the times of the first count drivers: ESPN's totalTime is the best lap in practice and
// qualifying and the race time in a race, so the first place shows it (or LEAD in a race) and the others
// their gap to it.
void F1Ticker::fetchTimes(Session &session, int count) {
  f1At("fetch times");
  if (!session.id || !session.eventId) return;
  long ms[ROWS];
  char leader[12] = "";  // the first place's own text, "1:35.130"
  for (int i = 0; i < ROWS; i++) ms[i] = -1;
  for (int i = 0; i < count && i < ROWS; i++) {
    if (!session.rows[i].athleteId) continue;
    char url[200];
    snprintf(url, sizeof(url),
             "http://sports.core.api.espn.com/v2/sports/racing/leagues/f1/events/%lu/competitions/%lu/competitors/%lu/statistics/0",
             (unsigned long)session.eventId, (unsigned long)session.id, (unsigned long)session.rows[i].athleteId);
    TimesScan scan;
    if (!getScan(url, TimesScan::leaf, &scan)) continue;
    scan.flush();
    if (!scan.have) continue;
    ms[i] = parseTimeMs(scan.total);
    if (i == 0) strlcpy(leader, scan.total, sizeof(leader));
  }

  bool race = session.tag == 'R' || session.tag == 'S';
  if (ms[0] >= 0) {
    strlcpy(session.rows[0].time, race ? "LEAD" : leader, sizeof(session.rows[0].time));
    for (int i = 1; i < count && i < ROWS; i++)
      if (ms[i] >= ms[0]) formatGap(session.rows[i].time, sizeof(session.rows[i].time), ms[i] - ms[0]);
  }
  f1log("[F1] times %s: leader %ld ms\n", session.name, ms[0]);
}

// The results of the last race (or sprint) from Jolpica: the driver whose fastest lap has rank 1
struct FastestScan {
  const char *list;  // "Results" or "SprintResults"
  char date[12] = "", code[4] = "", rank[4] = "", fastest[4] = "";
  int index = -1;

  void flush() {
    if (index >= 0 && !strcmp(rank, "1")) strlcpy(fastest, code, sizeof(fastest));
    index = -1;
    code[0] = rank[0] = 0;
  }

  static void leaf(void *sink, const char (*p)[JsonScan::KEY], int n, const char *v, bool) {
    FastestScan &s = *(FastestScan *)sink;
    if (n < 5 || strcmp(p[0], "MRData") || strcmp(p[1], "RaceTable") || strcmp(p[2], "Races") || strcmp(p[3], "0")) return;
    if (n == 5 && !strcmp(p[4], "date")) strlcpy(s.date, v, sizeof(s.date));
    if (n != 8 || strcmp(p[4], s.list)) return;
    bool isCode = !strcmp(p[6], "Driver") && !strcmp(p[7], "code");
    bool isRank = !strcmp(p[6], "FastestLap") && !strcmp(p[7], "rank");
    if (!isCode && !isRank) return;
    int k = atoi(p[5]);
    if (k != s.index) {
      s.flush();
      s.index = k;
    }
    if (isCode) strlcpy(s.code, v, sizeof(s.code));
    else strlcpy(s.rank, v, sizeof(s.rank));
  }
};

bool F1Ticker::fetchFastest(Session &session) {
  f1At("fetch fastest");
  FastestScan scan;
  scan.list = session.tag == 'S' ? "SprintResults" : "Results";
  const char *url = session.tag == 'S' ? "https://api.jolpi.ca/ergast/f1/current/last/sprint.json?limit=30"
                                       : "https://api.jolpi.ca/ergast/f1/current/last/results.json?limit=30";
  if (!getScan(url, FastestScan::leaf, &scan)) return false;
  scan.flush();
  char start[24];
  snprintf(start, sizeof(start), "%sT00:00", scan.date);
  time_t day = parseEspnDate(start);
  if (!scan.fastest[0] || !day || labs((long)(session.start - day)) > 2 * 24 * 3600L) return false;  // the results of another race
  for (Row &row : session.rows)
    if (!strcmp(row.code, scan.fastest)) {
      row.fastest = true;
      f1log("[F1] fastest lap: %s\n", row.code);
      return true;
    }
  return false;
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
struct SeasonScan {
  char name[32] = "", date[16] = "", time[16] = "", circuit[48] = "";

  static void leaf(void *sink, const char (*p)[JsonScan::KEY], int n, const char *v, bool) {
    SeasonScan &s = *(SeasonScan *)sink;
    if (jsonPathIs(p, n, "MRData.RaceTable.Races.0.raceName")) strlcpy(s.name, v, sizeof(s.name));
    else if (jsonPathIs(p, n, "MRData.RaceTable.Races.0.FirstPractice.date")) strlcpy(s.date, v, sizeof(s.date));
    else if (jsonPathIs(p, n, "MRData.RaceTable.Races.0.FirstPractice.time")) strlcpy(s.time, v, sizeof(s.time));
    else if (jsonPathIs(p, n, "MRData.RaceTable.Races.0.Circuit.circuitName") || jsonPathIs(p, n, "MRData.RaceTable.Races.0.Circuit.Location.locality")) {
      if (s.circuit[0]) strlcat(s.circuit, " ", sizeof(s.circuit));
      strlcat(s.circuit, v, sizeof(s.circuit));
    }
  }
};

unsigned long F1Ticker::fetchSeason() {
  f1At("fetch season");
  SeasonScan scan;
  if (!getScan("https://api.jolpi.ca/ergast/f1/current/next.json", SeasonScan::leaf, &scan)) return RETRY_MS;

  Race upcoming;
  // "Mexico City Grand Prix" -> "MEXICO"
  String name = scan.name;
  name.replace(" Grand Prix", "");
  name.replace(" City", "");  // Mexico City -> MEXICO
  cityName(upcoming.city, sizeof(upcoming.city), name.c_str());  // longer than the screen: it scrolls
  strlcpy(upcoming.circuit, scan.circuit, sizeof(upcoming.circuit));
  char start[48];
  snprintf(start, sizeof(start), "%sT%s", scan.date, scan.time);
  upcoming.start = parseEspnDate(start);  // "2026-10-09T08:30:00Z" reads the same way
  f1log("[F1] next race: %s\n", upcoming.city);
  if (!upcoming.city[0] || !upcoming.start) return RETRY_MS;

  {
    std::lock_guard<std::mutex> guard(_lock);
    _snap.upcoming[0] = upcoming;
    _snap.upcomingCount = 1;
    _version++;
  }
  Preferences prefs;
  if (prefs.begin("f1cache", false)) {
    prefs.putBytes("race", &upcoming, sizeof(upcoming));
    prefs.putUInt("raceAt", ezt::now());
    prefs.end();
  }
  return SEASON_REFRESH_MS;
}

bool F1Ticker::getScan(const char *url, JsonScan::Leaf leaf, void *sink) {
  bool secure = strncmp(url, "https", 5) == 0;
  std::unique_lock<std::mutex> handshake(f1TlsLock, std::defer_lock);  // see F1Live.h; plain HTTP needs no turn
  if (secure) handshake.lock();
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
  bool ok = JsonScan::read([](void *r) { return ((YieldingReader *)r)->read(); }, &reader, leaf, sink);
  http.end();
  if (!ok) f1log("[F1] %s: cut off or not JSON after %u bytes (heap free %u, largest block %u)\n", url, (unsigned)reader.count,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  return ok;
}

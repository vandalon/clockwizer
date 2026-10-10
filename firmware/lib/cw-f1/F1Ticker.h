#pragma once

#include <Arduino.h>
#include <CWDateTime.h>
#include <mutex>

#include "F1Live.h"
#include "JsonScan.h"

// Formula 1 data for a 64x64 panel, downloaded by a background task:
//  - the current Grand Prix weekend from ESPN's scoreboard: the session being run (order, lap or clock,
//    flag), the last session that finished and the next one
//  - the coming Grand Prix, from ESPN's season calendar
//  - the top of the drivers' championship, from Jolpica (the Ergast successor)
// ESPN gives the running order but no gaps or lap times.
class F1Ticker {
  public:
    static const int ROWS = 22;     // positions kept per session: the whole grid
    static const int TIMED = 9;     // of those, the first ones whose times ESPN is asked for (a request each)
    static const int STANDINGS = 22;  // the whole championship, so a favourite driver can be shown too
    static const int UPCOMING = 4;

    struct Row {
      char code[4] = "";  // "VER"
      uint16_t color = 0xFFFF;  // the team's colour
      uint32_t athleteId = 0;   // ESPN's, for the times
      char time[9] = "";        // first place: the best lap or "LEAD"; others: the gap, "+.088" or "+12.3"
      bool fastest = false;     // a race: the fastest lap of all
    };

    struct Session {
      bool valid = false;
      bool live = false;
      char tag = 'R';         // 'P' practice, 'Q' qualifying, 'S' sprint race, 'R' race
      char name[8] = "";      // FP1, QUALI, SQ, SPRINT, RACE
      char flag = 0;          // while live: 'y' safety car or yellow flag, 'r' red flag
      bool finished = false;  // while live: the feed says the session is over (a race: the leader took the chequered flag)
      int period = 0;         // the lap, for races
      int totalLaps = 0;      // of the race, from the live feed
      char clock[8] = "";     // time left, for practice and qualifying
      time_t start = 0;       // UTC
      uint32_t eventId = 0, id = 0;  // ESPN's, for the times
      Row rows[ROWS];
      uint8_t count = 0;
    };

    struct Standing {
      char code[4] = "";
      uint16_t color = 0xFFFF;
      uint16_t points = 0;
    };

    struct Race {
      char city[20] = "";  // upper case, ASCII
      char circuit[48] = "";  // the circuit's name and place, to find its outline
      time_t start = 0;    // UTC, the first practice session
    };

    struct Snapshot {
      bool weekend = false;  // a Grand Prix weekend is on, whether or not a session is running
      char circuit[48] = "";  // ESPN's name of the circuit and its city, to find its outline
      Session live, last, next;
      // The sessions still to come this weekend, soonest first (the first is `next`)
      struct Coming { char name[8] = ""; time_t start = 0; };
      static const int COMING = 6;
      Coming coming[COMING];
      uint8_t comingCount = 0;
      Standing standings[STANDINGS];
      uint8_t standingsCount = 0;
      Race upcoming[UPCOMING];  // soonest first
      uint8_t upcomingCount = 0;
    };

    void begin(CWDateTime *dateTime);
    void snapshot(Snapshot &out);
    // Changes whenever a download changed anything
    uint32_t version();
    void resume();  // restarts the task after a firmware update; version() does it

    // The team colour of a driver by his 3-letter code, white when unknown
    static uint16_t driverColor(const char *code);

  private:
    static void task(void *self);
    unsigned long refresh();
    unsigned long fetchWeekend();
    unsigned long fetchStandings();
    unsigned long fetchSeason();
    void fetchTimes(Session &session, int count);
    bool fetchFastest(Session &session);  // marks the driver with the fastest lap of a finished race or sprint
    bool getScan(const char *url, JsonScan::Leaf leaf, void *sink);  // streams the JSON of a download through the scanner
    void persistLive();  // the latest order and times of a session to flash
    void loadCache();  // the standings and the next race of the last download, from flash

    CWDateTime *_dateTime = nullptr;
    std::mutex _lock;
    Snapshot _snap;
    F1Live _live;
    uint32_t _fastestId = 0;  // the session the fastest lap was last looked up for, and when
    unsigned long _fastestAt = 0;
    Session _lastTimed;  // the last session once its times are in, so they are downloaded once
    // The latest live order with its times, kept in flash too: shown while no new data is there
    struct SavedRows {
      uint32_t id = 0;  // the session
      char name[8] = "";
      char flag = 0;
      uint8_t count = 0;
      Row rows[ROWS];
    };
    SavedRows _saved;
    bool _savedDirty = false;
    unsigned long _savedAt = 0;
    uint32_t _version = 0;
    bool _started = false;
    bool _cacheLoaded = false;
    TaskHandle_t _task = nullptr;  // the download task; none while a firmware update needs the memory
    unsigned long _weekendAt = 0, _standingsAt = 0, _seasonAt = 0;  // millis() of the next download
};

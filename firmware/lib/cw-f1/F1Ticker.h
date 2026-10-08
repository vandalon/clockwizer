#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <CWDateTime.h>
#include <mutex>

#include "F1Live.h"

// Formula 1 data for a 64x64 panel, downloaded by a background task:
//  - the current Grand Prix weekend from ESPN's scoreboard: the session being run (order, lap or clock,
//    flag), the last session that finished and the next one
//  - the coming Grand Prix, from ESPN's season calendar
//  - the top of the drivers' championship, from Jolpica (the Ergast successor)
// ESPN gives the running order but no gaps or lap times.
class F1Ticker {
  public:
    static const int ROWS = 9;      // positions kept per session
    static const int STANDINGS = 3;
    static const int UPCOMING = 4;

    struct Row {
      char code[4] = "";  // "VER"
      uint16_t color = 0xFFFF;  // the team's colour
      uint32_t athleteId = 0;   // ESPN's, for the times
      char time[9] = "";        // first place: the best lap or "LEAD"; others: the gap, "+.088" or "+12.3"
    };

    struct Session {
      bool valid = false;
      bool live = false;
      char tag = 'R';         // 'P' practice, 'Q' qualifying, 'S' sprint race, 'R' race
      char name[8] = "";      // FP1, QUALI, SQ, SPRINT, RACE
      char flag = 0;          // while live: 'y' safety car or yellow flag, 'r' red flag
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
      char city[14] = "";  // upper case, ASCII
      time_t start = 0;    // UTC, the first practice session
    };

    struct Snapshot {
      bool weekend = false;  // a Grand Prix weekend is on, whether or not a session is running
      Session live, last, next;
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
    bool getJson(const char *url, JsonDocument &filter, JsonDocument &doc);

    CWDateTime *_dateTime = nullptr;
    DynamicJsonDocument *_doc = nullptr;
    std::mutex _lock;
    Snapshot _snap;
    F1Live _live;
    Session _lastTimed;  // the last session once its times are in, so they are downloaded once
    uint32_t _version = 0;
    bool _started = false;
    TaskHandle_t _task = nullptr;  // the download task; none while a firmware update needs the memory
    unsigned long _weekendAt = 0, _standingsAt = 0, _seasonAt = 0;  // millis() of the next download
};

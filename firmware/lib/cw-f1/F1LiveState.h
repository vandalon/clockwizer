#pragma once

#include <ArduinoJson.h>
#include <stdint.h>

// The state of a live F1 session as the live timing feed (livetiming.formula1.com, SignalR) reports it:
// drivers with their code and team colour, running order, gaps and best laps. Pure logic, no network and
// no Arduino: a message goes in, the top of the order comes out.
class F1LiveState {
  public:
    static const int ROWS = 9;
    static const int MAX_DRIVERS = 24;

    struct Row {
      char code[4];
      uint16_t color;
      char time[9];  // first place: the best lap, or LEAD in a race; others: the gap
    };

    // Keeps just what is used of a message, so a 40KB snapshot fits in the JSON document
    static void buildFilter(JsonDocument &filter);

    void reset();

    // One message: the answer to Subscribe ({"type":3,"result":{...}}) or a live update
    // ({"type":1,"target":"feed","arguments":[...]}), parsed with buildFilter(). Returns true when it held data.
    bool apply(JsonDocument &message);

    // The first places. tag: 'R' race, 'S' sprint, 'P' practice, 'Q' qualifying. Returns how many rows are filled
    // (the highest place known, 0 when there are none yet).
    int top(char tag, Row *rows) const;

    int lap() const { return _lap; }
    int totalLaps() const { return _totalLaps; }
    char flag() const { return _flag; }  // 'y' safety car or yellow flag, 'r' red flag, 0 clear
    bool hasOrder() const;

  private:
    struct Driver {
      uint8_t number = 0;  // 0 = free
      char code[4] = "";
      uint16_t color = 0xFFFF;
      uint8_t position = 0;
      char gap[10] = "";   // race: gap to the leader
      char diff[10] = "";  // practice: gap to the fastest
      char best[10] = "";  // best lap
      char bestQ[3][10] = {"", "", ""};  // qualifying: best lap and gap to the fastest per part
      char diffQ[3][10] = {"", "", ""};
    };

    Driver *driver(int number);
    void applyDriverList(JsonObjectConst list);
    void applyTiming(JsonObjectConst timing);
    void applyLine(int number, JsonObjectConst line);
    void applyTopic(JsonObjectConst data);

    Driver _drivers[MAX_DRIVERS];
    int _lap = 0, _totalLaps = 0;
    char _flag = 0;
};

#include "EspnParse.h"

#include <stdlib.h>
#include <string.h>

namespace espn {

namespace {

// The index in path[level] when it is a number
inline int indexAt(const char (*p)[JsonScan::KEY], int level) { return atoi(p[level]); }

struct NextMatchScan {
  NextMatch *out;
  static void leaf(void *sink, const char (*p)[JsonScan::KEY], int n, const char *v, bool) {
    NextMatchScan &s = *(NextMatchScan *)sink;
    if (jsonPathIs(p, n, "team.nextEvent.0.date")) s.out->date = v;
    else if (jsonPathIs(p, n, "team.nextEvent.0.league.slug")) s.out->league = v;
  }
};

struct StandingsScan {
  Groups *out;
  int group = -1;
  static void leaf(void *sink, const char (*p)[JsonScan::KEY], int n, const char *v, bool) {
    StandingsScan &s = *(StandingsScan *)sink;
    // Any value of an entry counts the entry, so a team without an abbreviation keeps its place in the table
    if (n < 6 || !jsonPathIs(p, 5, "children.*.standings.entries.*")) return;
    int g = indexAt(p, 1), k = indexAt(p, 5 - 1);
    if (g != s.group) {
      s.group = g;
      s.out->emplace_back();
    }
    std::vector<std::string> &teams = s.out->back();
    if ((int)teams.size() <= k) teams.resize(k + 1);
    if (jsonPathIs(p, n, "children.*.standings.entries.*.team.abbreviation")) teams[k] = v;
  }
};

struct RaceScan {
  Weekend *out;
  int session = -1, driver = -1;
  static void leaf(void *sink, const char (*p)[JsonScan::KEY], int n, const char *v, bool) {
    RaceScan &s = *(RaceScan *)sink;
    if (n < 3 || strcmp(p[0], "events") || strcmp(p[1], "0")) return;  // the first event only
    if (n == 3) {
      if (!strcmp(p[2], "id")) s.out->eventId = v;
      return;
    }
    if (n < 5 || strcmp(p[2], "competitions")) return;
    int c = indexAt(p, 3);
    if (c != s.session) {
      s.session = c;
      s.driver = -1;
      s.out->sessions.emplace_back();
    }
    RaceSession &session = s.out->sessions.back();
    if (jsonPathIs(p, n, "events.0.competitions.*.id")) session.id = v;
    else if (jsonPathIs(p, n, "events.0.competitions.*.type.abbreviation")) session.type = v;
    else if (jsonPathIs(p, n, "events.0.competitions.*.date")) session.date = v;
    else if (jsonPathIs(p, n, "events.0.competitions.*.status.type.state")) session.state = v;
    else if (n >= 7 && !strcmp(p[4], "competitors")) {
      int d = indexAt(p, 5);
      if ((int)session.drivers.size() <= d) session.drivers.resize(d + 1);
      if (jsonPathIs(p, n, "events.0.competitions.*.competitors.*.order")) session.drivers[d].order = atoi(v);
      else if (jsonPathIs(p, n, "events.0.competitions.*.competitors.*.athlete.shortName")) session.drivers[d].shortName = v;
    }
  }
};

struct FlagScan {
  std::string *flag;
  static void leaf(void *sink, const char (*p)[JsonScan::KEY], int n, const char *v, bool) {
    if (jsonPathIs(p, n, "flag")) *((FlagScan *)sink)->flag = v;
  }
};

}  // namespace

bool parseNextMatch(JsonScan::Next next, void *source, NextMatch &out) {
  NextMatchScan scan{&out};
  return JsonScan::read(next, source, NextMatchScan::leaf, &scan);
}

bool parseStandings(JsonScan::Next next, void *source, Groups &out) {
  StandingsScan scan;
  scan.out = &out;
  return JsonScan::read(next, source, StandingsScan::leaf, &scan);
}

bool parseRace(JsonScan::Next next, void *source, Weekend &out) {
  RaceScan scan;
  scan.out = &out;
  return JsonScan::read(next, source, RaceScan::leaf, &scan);
}

bool parseFlag(JsonScan::Next next, void *source, std::string &flag) {
  FlagScan scan{&flag};
  return JsonScan::read(next, source, FlagScan::leaf, &scan);
}

}  // namespace espn

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

}  // namespace espn

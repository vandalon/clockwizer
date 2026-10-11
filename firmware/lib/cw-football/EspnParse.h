#pragma once

#include <string>
#include <vector>

#include <JsonScan.h>

// What the football ticker reads from ESPN's feeds, one function per feed. Each reads the text through
// JsonScan (a few hundred bytes of memory whatever the size of the feed) and returns just the fields the ticker
// uses. No Arduino types in here, so the host tests can run it on saved feeds.
//
// Every function takes the reader JsonScan wants: next(source) gives the next character, -1 at the end.
// They return false when the text ended early or isn't JSON; what was read before that is in the result.
namespace espn {

// teams/<id>: the team's next match
struct NextMatch {
  std::string league;  // the league's slug, "" when not there
  std::string date;    // "2026-10-09T08:30Z", "" when not there
};
bool parseNextMatch(JsonScan::Next next, void *source, NextMatch &out);

// <league>/standings: the teams' abbreviations, group by group in table order
typedef std::vector<std::vector<std::string>> Groups;
bool parseStandings(JsonScan::Next next, void *source, Groups &out);

}  // namespace espn

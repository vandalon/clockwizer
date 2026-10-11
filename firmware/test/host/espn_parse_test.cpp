// Host test for lib/cw-football/EspnParse: every saved ESPN feed is read twice, once the way the ticker used to
// (ArduinoJson with a filter, the oracle) and once with EspnParse, and the results must be the same.
//   ./run.sh [directory with saved feeds]
#include <ArduinoJson.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../../lib/cw-football/EspnParse.h"

static int failures = 0, checks = 0, compared = 0;  // compared: lines of result that matched

static std::string slurp(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

struct Text {
  const std::string &s;
  size_t at = 0;
  explicit Text(const std::string &t) : s(t) {}
  static int next(void *self) {
    Text *t = (Text *)self;
    return t->at < t->s.size() ? (unsigned char)t->s[t->at++] : -1;
  }
};

static void same(const std::string &what, const std::string &oracle, const std::string &mine) {
  checks++;
  if (oracle == mine) { compared += std::count(oracle.begin(), oracle.end(), '\n'); return; }
  failures++;
  printf("FAIL %s\n--- oracle\n%s\n--- espn::parse\n%s\n", what.c_str(), oracle.c_str(), mine.c_str());
}

// ---- teams/<id> -------------------------------------------------------------------------------------------------
static std::string oracleNextMatch(const std::string &text) {
  StaticJsonDocument<256> filter;
  JsonObject event = filter["team"]["nextEvent"].createNestedObject();
  event["date"] = true;
  event["league"]["slug"] = true;
  DynamicJsonDocument doc(16384);
  if (deserializeJson(doc, text, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(20))) return "ERROR";
  JsonObject next = doc["team"]["nextEvent"][0];
  return std::string("league=") + (const char *)(next["league"]["slug"] | "") + " date=" + (const char *)(next["date"] | "");
}

static std::string mineNextMatch(const std::string &text) {
  Text t(text);
  espn::NextMatch m;
  if (!espn::parseNextMatch(Text::next, &t, m)) return "ERROR";
  return "league=" + m.league + " date=" + m.date;
}

// ---- <league>/standings -----------------------------------------------------------------------------------------
static std::string oracleStandings(const std::string &text) {
  StaticJsonDocument<256> filter;
  filter["children"][0]["standings"]["entries"][0]["team"]["abbreviation"] = true;
  DynamicJsonDocument doc(65536);  // generous: the oracle only has to be right, not small
  if (deserializeJson(doc, text, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(20))) return "ERROR";
  std::string out;
  for (JsonObject child : doc["children"].as<JsonArray>()) {
    std::vector<std::string> teams;
    for (JsonObject e : child["standings"]["entries"].as<JsonArray>()) teams.push_back(e["team"]["abbreviation"] | "");
    if (teams.empty()) continue;
    out += "[";
    for (auto &t : teams) out += t + ",";
    out += "]\n";
  }
  return out;
}

static std::string mineStandings(const std::string &text) {
  Text t(text);
  espn::Groups g;
  if (!espn::parseStandings(Text::next, &t, g)) return "ERROR";
  std::string out;
  for (auto &group : g) {
    out += "[";
    for (auto &team : group) out += team + ",";
    out += "]\n";
  }
  return out;
}

// ---- racing/f1/scoreboard and the flag --------------------------------------------------------------------------
static std::string oracleRace(const std::string &text) {
  StaticJsonDocument<4096> filter;  // the firmware has 512 on a 32-bit chip: the 64-bit host needs more room for the same filter
  JsonObject competition = filter["events"][0]["competitions"].createNestedObject();
  filter["events"][0]["id"] = true;
  competition["id"] = true;
  competition["type"]["abbreviation"] = true;
  competition["date"] = true;
  competition["status"]["type"]["state"] = true;
  JsonObject driver = competition["competitors"].createNestedObject();
  driver["order"] = true;
  driver["athlete"]["shortName"] = true;
  DynamicJsonDocument doc(65536);
  if (deserializeJson(doc, text, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(20))) return "ERROR";
  std::string out = std::string("event=") + (const char *)(doc["events"][0]["id"] | "") + "\n";
  for (JsonObject c : doc["events"][0]["competitions"].as<JsonArray>()) {
    out += std::string("session id=") + (const char *)(c["id"] | "") + " type=" + (const char *)(c["type"]["abbreviation"] | "") +
           " date=" + (const char *)(c["date"] | "") + " state=" + (const char *)(c["status"]["type"]["state"] | "") + "\n";
    for (JsonObject d : c["competitors"].as<JsonArray>())
      out += "  order=" + std::to_string((int)(d["order"] | 0)) + " name=" + (const char *)(d["athlete"]["shortName"] | "") + "\n";
  }
  return out;
}

static std::string mineRace(const std::string &text) {
  Text t(text);
  espn::Weekend w;
  if (!espn::parseRace(Text::next, &t, w)) return "ERROR";
  std::string out = "event=" + w.eventId + "\n";
  for (auto &c : w.sessions) {
    out += "session id=" + c.id + " type=" + c.type + " date=" + c.date + " state=" + c.state + "\n";
    for (auto &d : c.drivers) out += "  order=" + std::to_string(d.order) + " name=" + d.shortName + "\n";
  }
  return out;
}

static std::string oracleFlag(const std::string &text) {
  StaticJsonDocument<64> filter;
  filter["flag"] = true;
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, text, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(20))) return "ERROR";
  return std::string("flag=") + (const char *)(doc["flag"] | "");
}

static std::string mineFlag(const std::string &text) {
  Text t(text);
  std::string flag;
  if (!espn::parseFlag(Text::next, &t, flag)) return "ERROR";
  return "flag=" + flag;
}

// ---- <league>/scoreboard ----------------------------------------------------------------------------------------
static std::string str(JsonVariantConst v, const char *fallback) { return v.is<const char *>() ? v.as<const char *>() : fallback; }
static std::string flt(JsonVariantConst v) { char b[32]; snprintf(b, sizeof b, "%.3f", (double)(v | 0.0f)); return b; }
static std::string present(JsonVariantConst v) { return v.is<const char *>() ? "+" : "-"; }

static std::string oracleScoreboard(const std::string &text, bool withEvents) {
  StaticJsonDocument<8192> filter;  // the firmware has 1024 on a 32-bit chip
  JsonObject event = filter["events"].createNestedObject();
  event["id"] = true;
  event["date"] = true;
  event["status"]["type"]["state"] = true;
  event["status"]["type"]["name"] = true;
  event["status"]["type"]["shortDetail"] = true;
  JsonObject team = event["competitions"][0]["competitors"].createNestedObject();
  team["homeAway"] = true;
  team["score"] = true;
  team["team"]["abbreviation"] = true;
  team["team"]["color"] = true;
  team["team"]["alternateColor"] = true;
  if (withEvents) {
    team["id"] = true;
    JsonObject detail = event["competitions"][0]["details"].createNestedObject();
    detail["clock"]["value"] = true;
    detail["team"]["id"] = true;
    detail["scoringPlay"] = true;
    detail["yellowCard"] = true;
    detail["redCard"] = true;
    JsonObject player = detail["athletesInvolved"].createNestedObject();
    player["jersey"] = true;
    player["displayName"] = true;
  }
  DynamicJsonDocument doc(1 << 20);
  if (deserializeJson(doc, text, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(20))) return "ERROR";
  std::string out;
  for (JsonObject e : doc["events"].as<JsonArray>()) {
    out += "event id=" + str(e["id"], "") + " date=" + str(e["date"], "") + " state=" + str(e["status"]["type"]["state"], "") +
           " name=" + str(e["status"]["type"]["name"], "") + " detail=" + str(e["status"]["type"]["shortDetail"], "") + "\n";
    for (JsonObject c : e["competitions"][0]["competitors"].as<JsonArray>())
      out += "  team " + str(c["homeAway"], "") + " score=" + str(c["score"], "0") + " abbr=" + str(c["team"]["abbreviation"], "?") +
             " color=" + present(c["team"]["color"]) + str(c["team"]["color"], "") + " alt=" + present(c["team"]["alternateColor"]) +
             str(c["team"]["alternateColor"], "") + " id=" + str(c["id"], "") + "\n";
    for (JsonObject d : e["competitions"][0]["details"].as<JsonArray>()) {
      if (!(d["scoringPlay"] | false) && !(d["yellowCard"] | false) && !(d["redCard"] | false)) continue;  // the ticker skips these
      JsonObject player = d["athletesInvolved"][0];
      out += std::string("  detail clock=") + flt(d["clock"]["value"]) + " team=" + str(d["team"]["id"], "") + " g=" +
             ((d["scoringPlay"] | false) ? "1" : "0") + " y=" + ((d["yellowCard"] | false) ? "1" : "0") + " r=" +
             ((d["redCard"] | false) ? "1" : "0") + " jersey=" + present(player["jersey"]) + str(player["jersey"], "0") +
             " name=" + present(player["displayName"]) + str(player["displayName"], "") + "\n";
    }
  }
  return out;
}

static std::string mineScoreboard(const std::string &text, bool withEvents) {
  Text t(text);
  std::vector<espn::ScoreEvent> events;
  if (!espn::parseScoreboard(Text::next, &t, events, withEvents)) return "ERROR";
  std::string out;
  for (auto &e : events) {
    out += "event id=" + e.id + " date=" + e.date + " state=" + e.state + " name=" + e.name + " detail=" + e.shortDetail + "\n";
    for (auto &c : e.competitors)
      out += "  team " + c.homeAway + " score=" + (c.score.empty() ? "0" : c.score) + " abbr=" + (c.hasAbbreviation ? c.abbreviation : "?") +
             " color=" + (c.hasColor ? "+" : "-") + c.color + " alt=" + (c.hasAlternateColor ? "+" : "-") + c.alternateColor +
             " id=" + c.id + "\n";
    for (auto &d : e.details) {
      char b[32];
      snprintf(b, sizeof b, "%.3f", (double)(float)d.clock);
      out += std::string("  detail clock=") + b + " team=" + d.teamId + " g=" + (d.scoringPlay ? "1" : "0") + " y=" +
             (d.yellowCard ? "1" : "0") + " r=" + (d.redCard ? "1" : "0") + " jersey=" + (d.hasJersey ? "+" : "-") +
             (d.hasJersey ? d.jersey : "0") + " name=" + (d.hasDisplayName ? "+" : "-") + d.displayName + "\n";
    }
  }
  return out;
}

// ---- <league>/summary -------------------------------------------------------------------------------------------
static std::string oracleSummary(const std::string &text) {
  StaticJsonDocument<8192> filter;  // the firmware has 512 on a 32-bit chip
  JsonObject key = filter["keyEvents"].createNestedObject();
  key["type"]["type"] = true;
  key["clock"]["value"] = true;
  key["team"]["id"] = true;
  key["participants"][0]["athlete"]["id"] = true;
  JsonObject squad = filter["rosters"].createNestedObject();
  squad["homeAway"] = true;
  squad["team"]["id"] = true;
  squad["roster"][0]["athlete"]["id"] = true;
  squad["roster"][0]["jersey"] = true;
  DynamicJsonDocument doc(1 << 20);
  if (deserializeJson(doc, text, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(20))) return "ERROR";
  std::string out;
  for (JsonObject sq : doc["rosters"].as<JsonArray>()) {
    out += "squad " + str(sq["homeAway"], "") + " team=" + str(sq["team"]["id"], "") + "\n";
    for (JsonObject player : sq["roster"].as<JsonArray>())
      out += "  player " + str(player["athlete"]["id"], "") + " jersey=" + str(player["jersey"], "0") + "\n";
  }
  for (JsonObject e : doc["keyEvents"].as<JsonArray>()) {
    if (strcmp(e["type"]["type"] | "", "substitution") != 0) continue;
    out += "sub clock=" + flt(e["clock"]["value"]) + " team=" + str(e["team"]["id"], "") + " on=" +
           str(e["participants"][0]["athlete"]["id"], "") + " off=" + str(e["participants"][1]["athlete"]["id"], "") + "\n";
  }
  return out;
}

static std::string mineSummary(const std::string &text) {
  Text t(text);
  espn::Summary s;
  if (!espn::parseSummary(Text::next, &t, s)) return "ERROR";
  std::string out;
  for (auto &sq : s.squads) {
    out += "squad " + sq.homeAway + " team=" + sq.teamId + "\n";
    for (auto &p : sq.players) out += "  player " + p.athleteId + " jersey=" + (p.jersey.empty() ? "0" : p.jersey) + "\n";
  }
  for (auto &u : s.substitutions) {
    char b[32];
    snprintf(b, sizeof b, "%.3f", (double)(float)u.clock);
    out += std::string("sub clock=") + b + " team=" + u.teamId + " on=" + u.on + " off=" + u.off + "\n";
  }
  return out;
}

static std::vector<std::string> files(const std::string &dir, const std::string &prefix) {
  std::vector<std::string> out;
  std::string cmd = "ls " + dir + " 2>/dev/null";
  FILE *p = popen(cmd.c_str(), "r");
  char line[512];
  while (p && fgets(line, sizeof(line), p)) {
    std::string name(line);
    while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.pop_back();
    if (name.rfind(prefix, 0) == 0) out.push_back(dir + "/" + name);
  }
  if (p) pclose(p);
  return out;
}

int main(int argc, char **argv) {
  std::string dir = argc > 1 ? argv[1] : "espn-samples";
  for (auto &f : files(dir, "team-")) { std::string t = slurp(f); same(f, oracleNextMatch(t), mineNextMatch(t)); }
  for (auto &f : files(dir, "standings-")) { std::string t = slurp(f); same(f, oracleStandings(t), mineStandings(t)); }
  for (auto &f : files(dir, "f1-scoreboard")) { std::string t = slurp(f); same(f, oracleRace(t), mineRace(t)); }
  for (auto &f : files(dir, "flag-")) { std::string t = slurp(f); same(f, oracleFlag(t), mineFlag(t)); }
  for (auto &f : files(dir, "scoreboard-"))
    for (bool details : {true, false}) { std::string t = slurp(f); same(f + (details ? " (with details)" : " (scores only)"), oracleScoreboard(t, details), mineScoreboard(t, details)); }
  for (auto &f : files(dir, "summary-")) { std::string t = slurp(f); same(f, oracleSummary(t), mineSummary(t)); }
  printf("%d checks (%d lines compared), %d failed\n", checks, compared, failures);
  return failures ? 1 : 0;
}

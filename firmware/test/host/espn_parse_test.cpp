// Host test for lib/cw-football/EspnParse: every saved ESPN feed is read twice, once the way the ticker used to
// (ArduinoJson with a filter, the oracle) and once with EspnParse, and the results must be the same.
//   ./run.sh [directory with saved feeds]
#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../../lib/cw-football/EspnParse.h"

static int failures = 0, checks = 0;

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
  if (oracle == mine) return;
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
  printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}

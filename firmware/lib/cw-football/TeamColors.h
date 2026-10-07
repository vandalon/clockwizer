#pragma once

#include <Arduino.h>
#include "TeamKit.h"

// Home kits (shirt, shorts and second colour) of the clubs and national teams
// the football ticker can show, looked up by ESPN abbreviation. ESPN's own team
// colours are often off for the shorts (and sometimes the shirt), so these
// take precedence; teams that aren't listed fall back to ESPN.
//
// league is the ESPN competition code, for abbreviations ESPN uses for two
// teams (SCP is Sporting in the Champions League, Paderborn in the
// Bundesliga). Returns false when the team isn't listed.
bool lookupTeamKit(const String &team, const char *league, TeamKit &kit);

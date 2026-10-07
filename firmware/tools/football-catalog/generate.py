#!/usr/bin/env python3
"""Regenerates lib/cw-commons/FootballCatalog.h: the competitions the football ticker can follow and
the clubs and national teams that can be picked as favourites, all from ESPN's team lists.

    python3 tools/football-catalog/generate.py

Run it again when a new season changes the leagues (promotion, relegation). To add a competition,
add a line to LEAGUES. The label is the short code the clock shows; clubs are found through the
leagues marked teams=True, national teams through NATIONAL.
"""
import json
import os
import urllib.request

API = "http://site.api.espn.com/apis/site/v2/sports/soccer/%s/teams"

# (ESPN code, label, name, group, colour as 0xRRGGBB, take its clubs for the favourites list)
LEAGUES = [
    ("uefa.champions", "CL", "Champions League", "Europe", 0xFFFFFF, True),
    ("uefa.europa", "EL", "Europa League", "Europe", 0xFF8C00, True),
    ("uefa.europa.conf", "ECL", "Conference League", "Europe", 0x00C864, True),
    ("uefa.super_cup", "USC", "UEFA Super Cup", "Europe", 0xFFD700, False),
    ("uefa.nations", "UNL", "Nations League", "National teams", 0x00FFFF, False),
    ("fifa.world", "WC", "World Cup", "National teams", 0x00FFFF, False),
    ("uefa.euro", "EURO", "European Championship", "National teams", 0x00FFFF, False),
    ("fifa.worldq.uefa", "WCQ", "World Cup qualifying (Europe)", "National teams", 0x00FFFF, False),
    ("uefa.euroq", "EQ", "Euro qualifying", "National teams", 0x00FFFF, False),
    ("eng.1", "PL", "Premier League", "England", 0xA040FF, True),
    ("eng.2", "CH", "Championship", "England", 0xC080FF, True),
    ("eng.fa", "FA", "FA Cup", "England", 0xFF6080, False),
    ("eng.league_cup", "LC", "League Cup", "England", 0x00A0A0, False),
    ("esp.1", "LL", "La Liga", "Spain", 0xFF8040, True),
    ("esp.2", "LL2", "La Liga 2", "Spain", 0xFFA070, True),
    ("esp.copa_del_rey", "CDR", "Copa del Rey", "Spain", 0xFFD040, False),
    ("ger.1", "BL", "Bundesliga", "Germany", 0xFF0000, True),
    ("ger.2", "BL2", "2. Bundesliga", "Germany", 0xFF6060, True),
    ("ger.dfb_pokal", "DFB", "DFB-Pokal", "Germany", 0xFFB000, False),
    ("ita.1", "SA", "Serie A", "Italy", 0x2080FF, True),
    ("ita.2", "SB", "Serie B", "Italy", 0x60A0FF, True),
    ("ita.coppa_italia", "CI", "Coppa Italia", "Italy", 0x00D0A0, False),
    ("fra.1", "L1", "Ligue 1", "France", 0x6060FF, True),
    ("fra.2", "L2", "Ligue 2", "France", 0x9090FF, True),
    ("fra.coupe_de_france", "CDF", "Coupe de France", "France", 0xFF60C0, False),
    ("ned.1", "ERE", "Eredivisie", "Netherlands", 0x3C82FF, True),
    ("ned.2", "KKD", "Keuken Kampioen Divisie", "Netherlands", 0x70A8FF, True),
    ("ned.cup", "KNVB", "KNVB Beker", "Netherlands", 0xA000FF, False),
    ("por.1", "PPL", "Primeira Liga", "Portugal", 0x00B040, True),
    ("por.taca.portugal", "TP", "Taça de Portugal", "Portugal", 0x80D040, False),
    ("bel.1", "BEL", "Pro League", "Belgium", 0xFFD000, True),
    ("sco.1", "SPL", "Scottish Premiership", "Scotland", 0x0060FF, True),
    ("sco.2", "SCH", "Scottish Championship", "Scotland", 0x5090FF, True),
    ("tur.1", "TUR", "Süper Lig", "Turkey", 0xE00020, True),
    ("gre.1", "GRE", "Super League", "Greece", 0x00B0FF, True),
    ("aut.1", "AUT", "Bundesliga", "Austria", 0xFF3030, True),
    ("sui.1", "SUI", "Super League", "Switzerland", 0xFF4040, True),
    ("den.1", "DEN", "Superliga", "Denmark", 0xFF5050, True),
    ("swe.1", "SWE", "Allsvenskan", "Sweden", 0xFFE000, True),
    ("nor.1", "NOR", "Eliteserien", "Norway", 0xFF4060, True),
    ("rus.1", "RUS", "Premier League", "Russia", 0xFF6040, True),
    ("rou.1", "ROU", "Liga 1", "Romania", 0xFFC800, True),
    ("cyp.1", "CYP", "First Division", "Cyprus", 0xFFA000, True),
    ("isr.1", "ISR", "Premier League", "Israel", 0x40A0FF, True),
]
NATIONAL = ["uefa.nations", "fifa.world", "fifa.worldq.uefa", "uefa.euroq"]

def rgb565(c):
    return ((c >> 19) & 0x1F) << 11 | ((c >> 10) & 0x3F) << 5 | ((c >> 3) & 0x1F)

def teams(code):
    with urllib.request.urlopen(API % code, timeout=30) as r:
        return [t["team"] for t in json.load(r)["sports"][0]["leagues"][0]["teams"]]

clubs, nations = {}, {}
for code, label, name, group, color, take in LEAGUES:
    if take:
        for t in teams(code):
            clubs.setdefault(t["id"], (t["abbreviation"], t["displayName"]))
for code in NATIONAL:
    for t in teams(code):
        nations.setdefault(t["id"], (t["abbreviation"], t["displayName"]))
for tid in nations:
    clubs.pop(tid, None)

rows = [[int(i), a, n, "c"] for i, (a, n) in clubs.items()] + [[int(i), a, n, "n"] for i, (a, n) in nations.items()]
rows.sort(key=lambda r: (r[3] == "n", r[2].lower()))
teams_json = json.dumps(rows, ensure_ascii=False, separators=(",", ":"))
leagues_json = json.dumps([[c, l, n, g] for c, l, n, g, _, _ in LEAGUES], ensure_ascii=False, separators=(",", ":"))

out = os.path.join(os.path.dirname(__file__), "..", "..", "lib", "cw-commons", "FootballCatalog.h")
with open(out, "w", encoding="utf-8") as f:
    f.write("// Generated by tools/football-catalog/generate.py, don't edit.\n#pragma once\n\n#include <Arduino.h>\n\n")
    f.write("// The competitions the football ticker can follow: ESPN code, short code on the clock, colour (RGB565)\n")
    f.write("struct FootballLeague {\n  const char *code;\n  const char *label;\n  uint16_t color;\n};\n\n")
    f.write("static const FootballLeague FOOTBALL_LEAGUES[] = {\n")
    for c, l, n, g, col, _ in LEAGUES:
        f.write('  {"%s", "%s", 0x%04X},\n' % (c, l, rgb565(col)))
    f.write("};\n\n")
    f.write("// For the settings page: [code, label, name, group]\n")
    f.write('static const char FOOTBALL_LEAGUES_JSON[] PROGMEM = R"JSON(%s)JSON";\n\n' % leagues_json)
    f.write("// Favourites to pick from: [ESPN team id, abbreviation, name, 'c'lub or 'n'ational team]\n")
    # Raw string literals are limited to 64KB by the compiler: split in pieces
    pieces = [teams_json[i:i + 8000] for i in range(0, len(teams_json), 8000)]
    f.write("static const char *const FOOTBALL_TEAMS_JSON[] PROGMEM = {\n")
    for p in pieces:
        f.write('  R"JSON(%s)JSON",\n' % p)
    f.write("};\n")
print("%d clubs, %d national teams, %d bytes" % (len(clubs), len(nations), len(teams_json)))

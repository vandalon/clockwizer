#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <CWDateTime.h>
#include <ArduinoJson.h>
#include <map>
#include <mutex>
#include <vector>

#include "TeamKit.h"

struct FootballLeague;  // FootballCatalog.h

// Live football scores for the bottom row of a 64x32 panel: the competitions
// and favourite teams chosen on the settings page (by default Eredivisie,
// Bundesliga, Champions League, KNVB Beker, Nations League, World Cup,
// European Championship, and the Dutch and German national teams). While a Formula 1 race is on, its top 3 is one of the pages.
//
// A background task polls ESPN's scoreboard for today's matches. The clockface
// calls draw() every frame: it shows the live matches one at a time or, when
// none are live, the ones that finished today; when a score goes up,
// nextGoal() hands out a goal to celebrate full screen.
class FootballTicker {
  public:
    struct Goal {
      String home, away;
      int homeScore, awayScore;
      bool homeScored;
      TeamKit homeKit, awayKit;
      unsigned long detectedAt;
    };

    // A yellow or red card, or a substitution, to show full screen (nextIncident)
    struct Incident {
      char kind;               // 'y' yellow card, 'r' red card, 's' substitution
      String home, away;
      TeamKit homeKit, awayKit;
      bool homeTeam;           // the home team's
      uint8_t minute;
      char name[11];           // a card: the player's surname, "" when unknown
      uint8_t number;          // a card: the shirt number, a substitution: the player leaving; 0 = unknown
      uint8_t numberOn;        // a substitution: the player entering; 0 = unknown
      unsigned long detectedAt;
    };

    // A goal or card, for the timeline of faces that draw their own layout
    struct Event {
      uint8_t minute;  // 1-based playing minute, stoppage time counts on
      char kind;       // 'g' goal, 'y' yellow card, 'r' red card
      bool home;       // the home team's
      uint8_t number;  // a card: the player's shirt number, 0 = unknown
      char name[11];   // a card: the player's surname
    };

    // One match as shown on the bottom row
    struct Entry {
      String home, away, score;
      TeamKit homeKit, awayKit;
      uint16_t scoreColor;  // the competition's colour
      float progress;       // playing time 0..1, drawn as a bar under the text
      uint16_t barColor;
      bool live;            // being played now, not finished
      String detail;        // live: "67'" or "HT"
      String competition;   // short code: "ERE", "BL", "CL", "KNVB", "UNL", "WC", "EURO", or "INT" for other internationals
      // Only filled with begin(..., extras = true):
      std::vector<Event> events;  // in order of play
      uint8_t homePos, awayPos;   // places in the table or group, 0 when there is none (cups, knockouts)
      bool favourite;             // one of the teams is a favourite from the settings
    };

    // A match that hasn't started yet
    struct Upcoming {
      String home, away;
      TeamKit homeKit, awayKit;
      time_t kickoff;  // UTC
      uint16_t color;  // the competition's colour
      uint8_t homePos, awayPos;  // as in Entry
    };

    // Everything a face with its own layout needs, see overview()
    struct Overview {
      std::vector<Entry> live;          // being played now
      std::vector<Entry> finished;      // most recent first
      std::vector<Upcoming> upcoming;   // soonest first
    };

    // withRace = false leaves out the Formula 1 downloads. extras = true also
    // downloads goals and cards, the league tables and the coming matches.
    // resultWindowSecs > 0 keeps finished matches for that long instead of
    // until midnight (it then also downloads the scoreboards of the days before).
    void begin(CWDateTime *dateTime, bool withRace = true, bool extras = false, uint32_t resultWindowSecs = 0);

    // Changes the result window while running; the next refresh uses it
    void setResultWindow(uint32_t secs);

    // Live, finished and coming matches at once, for faces that draw their own
    // layout. version() changes whenever any of it does, so a face only needs
    // to copy it then.
    void overview(Overview &out);
    uint32_t version();

    // How the downloads are going: nothing yet, fine, or the last attempt failed with nothing to show
    enum Status { LOADING, OK, FAILED };
    Status status();
    // True while a download runs, and a moment after it so a quick one still shows
    bool updating();

    // The match to show now, one at a time like draw() does, for faces that
    // draw their own layout. Returns false when there is none.
    bool current(Entry &entry);

    // Draws one frame into the 8px high row at y (the text uses the row above
    // it as well, to leave a gap above the time bar). Returns false when there is
    // no match to show, so the caller can draw something else there.
    bool draw(Adafruit_GFX *display, int16_t y);

    // Takes the next goal to celebrate, if any. Goals that weren't picked up
    // within a couple of minutes (night mode, notifications) are dropped.
    bool nextGoal(Goal &goal);
    // The next card or substitution of a live match to show, false when there is none
    bool nextIncident(Incident &incident);

    // The Formula 1 race, shown as one more page of the ticker while it runs
    struct Race {
      bool live = false;
      char tag = 0;           // the session: 'P' practice, 'Q' qualifying, 'S' sprint race, 0 = the race
      char flag = 0;          // while live: 'y' = safety car or yellow flag, 'r' = red flag
      String top[3];  // 3-letter codes of the leaders
      uint16_t color[3];  // and their teams' colours
    };

  private:
    struct Match {
      const char *label;  // for the log
      const char *competition = "";  // short code shown by faces that draw their own layout
      uint16_t color;     // the competition's colour, for the ticker
      TeamKit homeKit, awayKit;  // the clubs' colours
      time_t kickoff;     // UTC
      char state;         // 'p'= not started, 'i' = live, 'f' = finished
      String detail;      // live: "67'" or "HT"
      String home, away;
      int homeScore, awayScore;
      time_t endedAt = 0; // UTC, finished matches only
      std::vector<Event> events;
      String league;      // ESPN code of the competition, for its table
      String id;          // ESPN's number of the match, for its summary

      String key() const { return home + "-" + away + "@" + String((uint32_t)kickoff); }
    };

    typedef std::vector<Match *> MatchList;  // pointers into _boards, valid for one refresh

    CWDateTime *_dateTime = nullptr;
    bool _started = false;
    bool _withRace = true;
    bool _extras = false;
    uint32_t _resultWindowSecs = 0;
    // Parse buffer, allocated once at startup so a fragmented heap later on
    // can't make a download fail.
    DynamicJsonDocument *_doc = nullptr;

    // Only used by the background task
    std::map<String, std::pair<int, int>> _scores;  // highest score seen per match
    std::map<String, time_t> _endedAt;              // when finished matches ended

    // Last download of each competition's scoreboard. Only competitions with
    // a match on (or about to start) are downloaded on every refresh.
    struct Board {
      std::vector<Match> matches;
      uint32_t day = 0;            // local day number the matches are for
      unsigned long fetchedAt = 0;
    };
    std::map<String, Board> _boards;
    bool _haveScores = false;
    unsigned long _nationalCheckedAt = 0;
    bool _quietFetched = false;  // a quiet board was downloaded in this round: the others wait for the next one
    bool _anyLive = false;       // a match was on at the last refresh: only matches that are on or about to start are downloaded
    bool _deferredFirst = false;  // ...and one of them has no data at all yet
    bool _deferred = false;      // some download was left for the next round

    // What to follow, from the settings (only used by the background task)
    struct Favourite {
      String id, abbreviation;  // ESPN team id and the abbreviation its matches show
      bool national = false;
      String league;            // ESPN code of the competition of today's match
      uint32_t day = 0;         // local day number of that match
      String nextLeague;        // competition and kick-off of its next match, today or later
      time_t nextKickoff = 0;
    };
    std::vector<const FootballLeague *> _leagues;
    std::vector<Favourite> _favourites;
    String _config;
    volatile uint32_t _pageMs = 8000;  // how long a match is shown, from the settings (read by the display)

    // The Formula 1 race, shown as one more page of the ticker while it runs
    Race _race;                       // the session being run, guarded by _lock
    std::vector<Race> _results;       // the sessions finished today, guarded by _lock
    unsigned long _raceNextAt = 0;    // millis() of the next download, 0 = now
    uint32_t _day = 0;                // local day number the scores on show are from, 0 = not known yet
    unsigned long _footballNextAt = 0;  // millis() of the next football refresh, 0 = now

    // Only used by the background task (extras)
    // ESPN league code -> its groups (a league is one group), each the team abbreviations from the top
    std::map<String, std::vector<std::vector<String>>> _tables;
    std::map<String, unsigned long> _tablesAt;  // millis() of each table's last download
    bool _lastNoMemory = false;       // the last getJson() ran out of room in _doc

    std::mutex _lock;
    Overview _overview;               // guarded by _lock
    uint32_t _version = 0;            // guarded by _lock
    volatile bool _fetching = false;
    volatile unsigned long _fetchEnded = 0;  // millis() of the last download's end
    Status _status = LOADING;         // guarded by _lock
    void setStatus(Status status);
    std::vector<Entry> _entries;      // guarded by _lock
    std::vector<Goal> _goals;         // guarded by _lock
    std::vector<Incident> _incidents; // guarded by _lock
    std::map<String, uint8_t> _cardCount, _subCount;  // seen so far per live match; only used by the background task
    std::map<String, unsigned long> _subChecked;      // millis() of the last summary download per match

    static void task(void *self);
    void dropOldDay();
    unsigned long refresh();
    unsigned long refreshRace();
    unsigned long fetchRace();
    bool loadConfig();
    bool followsLeague(const String &code);
    bool isFavourite(const String &abbreviation);
    bool nationalFavourite(const char *abbreviation);
    void checkFavourites(time_t today);
    bool fetchNextMatch(const char *teamId, String &league, time_t &kickoff);
    bool updateBoard(const String &key, const char *league, time_t today, const char *label, uint16_t color,
                     const char *onlyTeam);
    bool fetchScoreboard(const char *league, time_t localDay, const char *label, uint16_t color,
                         const char *onlyTeam, std::vector<Match> &out, bool withEvents = true);
    bool updatePastBoard(const String &key, const char *league, time_t day, const char *label, uint16_t color);
    bool updateNextBoard(const char *league, const char *label, uint16_t color);
    bool updateNextFavouriteBoard(const Favourite &fav, const String &key);
    void refreshTables(const MatchList &matches, const std::vector<const Match *> &coming);
    void tablePositions(const Match &m, uint8_t &homePos, uint8_t &awayPos);
    bool getJson(const char *url, JsonDocument &filter);
    bool getJsonNow(const char *url, JsonDocument &filter);
    void trackEndTimes(MatchList &matches, time_t nowUtc);
    void detectGoals(const MatchList &matches);
    void detectCards(const MatchList &matches);
    void checkSubstitutions(const MatchList &matches);
    bool fetchSubstitutions(const Match &match, std::vector<Incident> &found, uint8_t &total);
    void addIncidents(const std::vector<Incident> &incidents);
    bool buildEntries(const MatchList &matches, time_t nowUtc);
    void buildOverview(const MatchList &matches, const std::vector<const Match *> &coming, time_t nowUtc);
    Entry toEntry(const Match &m);
};

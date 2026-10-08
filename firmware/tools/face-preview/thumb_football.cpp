// Renders one picture of the Football clockface (for the web flasher) from made-up matches.
// Compiles the real clockface and goal animation, only the Arduino/ESP parts are faked.
#include <Arduino.h>
#include <stdio.h>
#include <string>
#include <vector>

#include <TelnetStream.h>
#include "../../clockfaces/cw-cf-0x0B/Clockface.h"
#include "FootballTicker.h"

// ---- The fakes ----
static unsigned long virtualMs = 0;
unsigned long millis() { return virtualMs; }
void delay(unsigned long ms) { virtualMs += ms; }
namespace ezt { time_t fakeNow = 0; }
TelnetStreamStub TelnetStream;
void (*onFlip)(MatrixPanel_I2S_DMA *) = nullptr;

static FootballTicker::Overview theOverview;
void FootballTicker::begin(CWDateTime *, bool, bool, uint32_t) {}
void FootballTicker::setResultWindow(uint32_t) {}
bool FootballTicker::nextIncident(Incident &) { return false; }
void FootballTicker::overview(Overview &out) { out = theOverview; }
uint32_t FootballTicker::version() { return 1; }
FootballTicker::Status FootballTicker::status() { return FootballTicker::OK; }
bool FootballTicker::nextGoal(Goal &) { return false; }

// ---- Pictures ----
static std::string outDir;
static void save(MatrixPanel_I2S_DMA &d, const std::string &name) {
  FILE *f = fopen((outDir + "/" + name + ".ppm").c_str(), "wb");
  fprintf(f, "P6\n64 64\n255\n");
  for (int y = 0; y < 64; y++)
    for (int x = 0; x < 64; x++) {
      uint16_t c = d.getPixel(x, y);
      unsigned char rgb[3] = {(unsigned char)((c >> 11) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63),
                              (unsigned char)((c & 31) * 255 / 31)};
      fwrite(rgb, 1, 3, f);
    }
  fclose(f);
}

static MatrixPanel_I2S_DMA display;
static CWDateTime clockTime;
static int goalFrame = 0;

// The clock shows 21:34 at the start, then runs with the virtual time
static void setTime(unsigned long ms) {
  virtualMs = ms;
  long secs = 21 * 3600 + 34 * 60 + ms / 1000;
  clockTime.hour = secs / 3600 % 24;
  clockTime.minute = secs / 60 % 60;
  clockTime.second = secs % 60;
  ezt::fakeNow = 1791149640 + ms / 1000;  // Sun 4 Oct 2026 21:34 UTC
}


static constexpr uint16_t rgb(int r, int g, int b) { return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); }

static FootballTicker::Entry match(const char *home, const char *away, const char *score, TeamKit hk, TeamKit ak, bool live,
                                   const char *detail, float progress) {
  FootballTicker::Entry e;
  e.home = home; e.away = away; e.score = score; e.homeKit = hk; e.awayKit = ak;
  e.scoreColor = rgb(80, 220, 120); e.barColor = rgb(80, 220, 120);
  e.progress = progress; e.live = live; e.detail = detail; e.competition = "INT";
  e.homePos = e.awayPos = 0; e.favourite = false;
  return e;
}

static FootballTicker::Event ev(int minute, char kind, bool home, int number, const char *name) {
  FootballTicker::Event e = {};
  e.minute = minute; e.kind = kind; e.home = home; e.number = number;
  strncpy(e.name, name, sizeof(e.name) - 1);
  return e;
}

int main(int argc, char **argv) {
  outDir = argc > 1 ? argv[1] : ".";
  const TeamKit orange = {rgb(255, 120, 0), rgb(255, 255, 255), 0}, white = {rgb(255, 255, 255), rgb(30, 30, 30), 0};
  const TeamKit red = {rgb(220, 30, 40), rgb(255, 255, 255), 0}, blue = {rgb(30, 80, 200), rgb(255, 255, 255), 0};
  FootballTicker::Entry live = match("NED", "ENG", "2-1", orange, white, true, "67'", 0.74f);
  live.events.push_back(ev(12, 'g', true, 0, ""));
  live.events.push_back(ev(34, 'g', false, 0, ""));
  live.events.push_back(ev(52, 'y', false, 4, "SMITH"));
  live.events.push_back(ev(60, 'g', true, 0, ""));
  theOverview.live.push_back(live);
  theOverview.finished.push_back(match("ESP", "ITA", "1-0", red, blue, false, "FT", 1));
  theOverview.finished.push_back(match("FRA", "BEL", "2-2", blue, red, false, "FT", 1));
  Clockface face(&display);
  setTime(0);
  face.setup(&clockTime);
  for (int i = 0; i < 40; i++) {  // let the screen settle
    setTime(i * 100UL);
    face.update();
  }
  save(display, "football");
  return 0;
}

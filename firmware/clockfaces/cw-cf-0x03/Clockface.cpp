
#include "Clockface.h"
#include <CWPreferences.h>

const char* FORMAT_TWO_DIGITS = "%02d";

// The map of a 64x64 panel is 120x56 (5 px per hour: it moves 1 px every 12 minutes),
// the one of a 64x32 panel is 72x32 (3 px per hour: 1 px every 20 minutes)
short BRAZIL_TZ = 32;  // map column of the UTC-3 meridian
short MAP_WIDTH = 120;
short MAP_HEIGHT = 56;
long SECS_PER_PIXEL = 12 * 60;
const unsigned short* worldMap = _WORLD_MAP;
short panelHeight = 64;
const long NOON_AT_BRAZIL_UTC = 15 * 3600;   // 12:00 in UTC-3

int lastNoonCol = -1;  // map column under the red line when the map was drawn

unsigned long lastMillis = 0;
int lastMinute = -1;  // minute the time text shows


Clockface::Clockface(Adafruit_GFX* display) {
  _display = display;

  Locator::provide(display);
}

void Clockface::setup(CWDateTime *dateTime) {
  this->_dateTime = dateTime;
  panelHeight = ClockwiseParams::getInstance()->displayHeight;
  if (panelHeight == 32) {
    BRAZIL_TZ = 19;
    MAP_WIDTH = WORLD_MAP_SMALL_WIDTH;
    MAP_HEIGHT = WORLD_MAP_SMALL_HEIGHT;
    SECS_PER_PIXEL = 20 * 60;
    worldMap = _WORLD_MAP_SMALL;
  }
  Locator::getDisplay()->setTextWrap(true);
  Locator::getDisplay()->fillRect(0, 0, 64, 64, 0x0000);  
  lastMinute = -1;  // the screen was cleared: draw the time again
  updateMap();
}

void Clockface::update() 
{  
  if (millis() - lastMillis >= 1000) { 
    lastMillis = millis();
    if (noonColumn() != lastNoonCol) updateMap();

    int minute = _dateTime->getMinute();
    if (minute == lastMinute) return;  // only hours and minutes show: no need to wipe and redraw each second
    lastMinute = minute;

    int top = panelHeight - 9;  // the time sits on the bottom row of the map
    Locator::getDisplay()->fillRect(0, top, 31, 9, 0x0000);  
    Locator::getDisplay()->setFont(&small4pt7b);  
    Locator::getDisplay()->setTextColor(0xffff);    
    Locator::getDisplay()->setCursor(1, top + 7);    
    Locator::getDisplay()->print(String(_dateTime->getHour()));
    Locator::getDisplay()->print(":");
    Locator::getDisplay()->print(_dateTime->getMinute(FORMAT_TWO_DIGITS));
  }
}

// Map column that is at noon right now. It follows UTC, so it is the same
// wherever the clock is.
int Clockface::noonColumn()
{
  long secs = (NOON_AT_BRAZIL_UTC - (long)(ezt::now() % 86400) + 86400) % 86400;
  return (BRAZIL_TZ + secs / SECS_PER_PIXEL) % MAP_WIDTH;
}

void Clockface::updateMap() 
{
  int noonCol = noonColumn();
  int left = (noonCol - 32 + MAP_WIDTH) % MAP_WIDTH;  // map column at screen x=0

  for (int y = 0; y < MAP_HEIGHT; y++)
  {
    for (int x = 0; x < 64; x++)
    {
      Locator::getDisplay()->drawPixel(x, y, _WORLD_MAP[y * MAP_WIDTH + (left + x) % MAP_WIDTH]);
    }
  }

  Locator::getDisplay()->drawFastVLine(32, 0, panelHeight, 0xf000);

  lastNoonCol = noonCol;
}

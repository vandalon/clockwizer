
#include "Clockface.h"

const char* FORMAT_TWO_DIGITS = "%02d";

const short BRAZIL_TZ = 32;  // map column of the UTC-3 meridian
const short TZ_SIZE = 5;     // map pixels per hour
const short MAP_WIDTH = 120;
const short MAP_HEIGHT = 56;
const long SECS_PER_PIXEL = 3600 / TZ_SIZE;  // the map moves 1 px every 12 minutes
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

    Locator::getDisplay()->fillRect(0, 55, 31, 9, 0x0000);  
    Locator::getDisplay()->setFont(&small4pt7b);  
    Locator::getDisplay()->setTextColor(0xffff);    
    Locator::getDisplay()->setCursor(1, 62);    
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

  Locator::getDisplay()->drawFastVLine(32, 0, 64, 0xf000);

  lastNoonCol = noonCol;
}

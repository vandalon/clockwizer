
#include "Clockface.h"
#include <CWPreferences.h>

const char* FORMAT_TWO_DIGITS = "%02d";

EventBus eventBus;

unsigned long lastMillis = 0;
int lastMinute = -1;  // minute on screen; comparing it can't miss a change like testing for second 0 can

char hInWords[20];
char mInWords[20]; 
char formattedDate[20];

DateI18nEN i18n;

Clockface::Clockface(Adafruit_GFX* display)
{
  _display = display;

  Locator::provide(display);
  Locator::provide(&eventBus);
}

void Clockface::setup(CWDateTime *dateTime) {
  this->_dateTime = dateTime;
  Locator::getDisplay()->setTextWrap(true);
  Locator::getDisplay()->fillRect(0, 0, 64, 64, 0x0000);  

  lastMinute = _dateTime->getMinute();
  updateTime();
  updateDate();
}


void Clockface::update() 
{  
  if (millis() - lastMillis >= 1000) {

    int minute = _dateTime->getMinute();
    if (minute != lastMinute) {
      lastMinute = minute;
      updateTime();

      if (minute == 0) {
        updateDate();
      }
    }

    lastMillis = millis();
  }  
}

// A 64x32 panel has room for the hour and the minutes on a line each (the minutes in the
// small font when they are too wide), but not for the date.
static bool isShort() { return ClockwiseParams::getInstance()->displayHeight == 32; }

static int textWidth(const GFXfont* font, const char* text) {
  int width = 0;
  for (; *text; text++) width += font->glyph[*text - font->first].xAdvance;
  return width;
}

static void oneLine(char* words) {
  for (char* c = words; *c; c++) if (*c == '\n') *c = ' ';
}

void Clockface::updateTime() 
{
  if (isShort()) {
    updateTimeShort();
    return;
  }
  Locator::getDisplay()->fillRect(0, 0, 64, 48, 0x0000);  

  i18n.timeInWords(_dateTime->getHour24(), _dateTime->getMinute(), hInWords, mInWords);  
  
  // Hour
  Locator::getDisplay()->setFont(&hour8pt7b);  
  Locator::getDisplay()->setCursor(1, 15);
  Locator::getDisplay()->setTextColor(0x02ed);
  Locator::getDisplay()->println(hInWords);
  
  // Minute
  Locator::getDisplay()->setFont(&minute7pt7b);
  Locator::getDisplay()->setCursor(0, 28);
  Locator::getDisplay()->setTextColor(0xffff);
  Locator::getDisplay()->println(mInWords);

  // Separator line
  Locator::getDisplay()->drawFastHLine(1, 48, 62, 0xffff);

}

void Clockface::updateTimeShort()
{
  Adafruit_GFX* d = Locator::getDisplay();
  d->fillRect(0, 0, 64, 32, 0x0000);

  i18n.timeInWords(_dateTime->getHour(), _dateTime->getMinute(), hInWords, mInWords);
  char* minuteLine2 = strchr(mInWords, '\n');
  oneLine(hInWords);

  // Big hour and a line of minutes when they fit, else three lines in the smaller font
  char oneLineMinutes[20];
  strcpy(oneLineMinutes, mInWords);
  oneLine(oneLineMinutes);
  bool big = textWidth(&hour8pt7b, hInWords) <= 62 && textWidth(&minute7pt7b, oneLineMinutes) <= 62;

  d->setTextColor(0x02ed);
  d->setFont(big ? &hour8pt7b : &minute7pt7b);
  d->setCursor(1, big ? 13 : 8);
  d->print(hInWords);

  d->setFont(&minute7pt7b);
  d->setTextColor(0xffff);
  if (big) {
    d->setCursor(1, 26);
    d->print(oneLineMinutes);
  } else if (minuteLine2) {
    *minuteLine2 = '\0';
    d->setCursor(1, 18);
    d->print(mInWords);
    d->setCursor(1, 28);
    d->print(minuteLine2 + 1);
  } else {
    d->setCursor(1, 18);
    d->print(mInWords);
  }
}

void Clockface::updateDate() 
{
  Locator::getDisplay()->fillRect(0, 51, 64, 13, 0x0000);

  // Date
  Locator::getDisplay()->setFont(&minute7pt7b);
  Locator::getDisplay()->setCursor(0, 61);
  Locator::getDisplay()->setTextColor(0x02ed);
    
  const char* fmt = i18n.formatDate(_dateTime->getDay(), _dateTime->getMonth());

  Locator::getDisplay()->print(fmt);

  uint16_t dateWidth, h = 0;
  int16_t x1, y1 = 0;
  Locator::getDisplay()->getTextBounds(fmt, 0, 0, &x1, &y1, &dateWidth, &h);

  // Weekday
  Locator::getDisplay()->setFont(&small4pt7b);
  //Locator::getDisplay()->setFont(&minute7pt7b);
  Locator::getDisplay()->setCursor(dateWidth + 4, 61);
  Locator::getDisplay()->setTextColor(0xffff);
  Locator::getDisplay()->println(i18n.weekDayName(_dateTime->getWeekday()));  
}

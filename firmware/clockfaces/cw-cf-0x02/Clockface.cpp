
#include "Clockface.h"

#include <CWPreferences.h>
#include "WordLayout.h"

EventBus eventBus;

unsigned long lastMillis = 0;
int lastMinute = -1;  // minute on screen; comparing it can't miss a change like testing for second 0 can
uint8_t shownLanguage = 255;  // the language on screen, to redraw at once when the settings page changes it

Clockface::Clockface(Adafruit_GFX* display)
{
  _display = display;

  Locator::provide(display);
  Locator::provide(&eventBus);
}

// The language chosen on the settings page, English when it is one this firmware doesn't know
const WordLanguage& Clockface::language() {
  uint8_t chosen = ClockwiseParams::getInstance()->wordLang;
  return WORD_LANGUAGES[chosen < WORD_LANGUAGE_COUNT ? chosen : 0];
}

void Clockface::setup(CWDateTime *dateTime) {
  this->_dateTime = dateTime;
  Locator::getDisplay()->fillRect(0, 0, 64, 64, 0x0000);  

  lastMinute = _dateTime->getMinute();
  shownLanguage = ClockwiseParams::getInstance()->wordLang;
  updateTime();
  updateDate();
}


void Clockface::update() 
{  
  if (millis() - lastMillis >= 1000) {

    int minute = _dateTime->getMinute();
    uint8_t chosen = ClockwiseParams::getInstance()->wordLang;
    if (minute != lastMinute || chosen != shownLanguage) {
      lastMinute = minute;
      updateTime();

      if (minute == 0 || chosen != shownLanguage) {
        updateDate();
      }
      shownLanguage = chosen;
    }

    lastMillis = millis();
  }  
}

void Clockface::updateTime() 
{
  Locator::getDisplay()->fillRect(0, 0, 64, 49, 0x0000);  

  WordPhrase phrase;
  language().say(_dateTime->getHour24(), _dateTime->getMinute(), phrase);

  // The hour in blue, the rest in white, centred above the line
  WordLayout layout(Locator::getDisplay(), &hour8pt7b, &minute7pt7b, 64, 48);
  layout.build(phrase);
  layout.draw(0x02ed, 0xffff);

  // Separator line
  Locator::getDisplay()->drawFastHLine(1, 49, 62, 0xffff);

}

void Clockface::updateDate() 
{
  Locator::getDisplay()->fillRect(0, 52, 64, 12, 0x0000);

  // Date
  char date[12];
  int day = _dateTime->getDay(), month = _dateTime->getMonth();
  snprintf(date, sizeof(date), language().dateFormat, language().monthFirst ? month : day,
           language().monthFirst ? day : month);
  Locator::getDisplay()->setFont(&minute7pt7b);
  Locator::getDisplay()->setCursor(0, 62);
  Locator::getDisplay()->setTextColor(0x02ed);
  Locator::getDisplay()->print(date);

  uint16_t dateWidth, h = 0;
  int16_t x1, y1 = 0;
  Locator::getDisplay()->getTextBounds(date, 0, 0, &x1, &y1, &dateWidth, &h);

  // Weekday
  char weekday[8];
  WordLayout::toFontCodes(language().weekdays[_dateTime->getWeekday() % 7], weekday, sizeof(weekday));
  Locator::getDisplay()->setFont(&small4pt7b);
  Locator::getDisplay()->setCursor(dateWidth + 4, 62);
  Locator::getDisplay()->setTextColor(0xffff);
  Locator::getDisplay()->print(weekday);
}

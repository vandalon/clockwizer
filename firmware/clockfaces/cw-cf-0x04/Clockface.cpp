#include "Clockface.h"

const byte CLOCK_POINTER_SIZE = 10;
const byte CLOCK_POINTER_POSX = 32;
const byte CLOCK_POINTER_POSY = 28;

const uint16_t BG_COLOR = 0x00E1;
const uint16_t POINTER_COLOR = 0xB58C;
const uint16_t SECOND_COLOR = 0x6B47;
const byte SECOND_POINTER_SIZE = 11;

const int HOUR_OFFSET = -30;
const int MIN_OFFSET = -6;

unsigned long lastMillis = 0;
int lastSecond = -1;  // second the hands show

float clock_x1 = CLOCK_POINTER_POSX;
float clock_y1 = CLOCK_POINTER_POSY;

float lastHourAngle = 0;
float lastMinAngle = 0;
float lastSecAngle = 0;

Clockface::Clockface(Adafruit_GFX* display) {
  _display = display;
  Locator::provide(display);
}

void Clockface::setup(CWDateTime *dateTime) {
  this->_dateTime = dateTime;
  Locator::getDisplay()->fillRect(0, 0, 64, 64, 0x0000);  
  updateClock();
  drawHands(false);  // the tower was just redrawn, so there is nothing to erase
}

void Clockface::update()
{ 
  if (millis() - lastMillis >= 1000) { 
    lastMillis = millis();
    if (_dateTime->getSecond() != lastSecond) drawHands(true);
  }
}

void Clockface::drawHands(bool erasePrevious)
{
  int minute = _dateTime->getMinute();
  float additional_offset = (HOUR_OFFSET * minute)/60;

  float hourAngle = degreesToRadians((_dateTime->getHour()*HOUR_OFFSET)+180+additional_offset);
  float minAngle = degreesToRadians((minute*MIN_OFFSET)+180);
  int second = _dateTime->getSecond();
  float secAngle = degreesToRadians((second*MIN_OFFSET)+180);

  if (erasePrevious) {
    drawClockPointer(lastSecAngle, SECOND_POINTER_SIZE, BG_COLOR);
    drawClockPointer(lastMinAngle, CLOCK_POINTER_SIZE, BG_COLOR);
    drawClockPointer(lastHourAngle, CLOCK_POINTER_SIZE-3, BG_COLOR);
  }

  drawClockPointer(minAngle, CLOCK_POINTER_SIZE, POINTER_COLOR);
  drawClockPointer(hourAngle, CLOCK_POINTER_SIZE-3, POINTER_COLOR);
  drawClockPointer(secAngle, SECOND_POINTER_SIZE, SECOND_COLOR);

  lastHourAngle = hourAngle;
  lastMinAngle = minAngle;
  lastSecAngle = secAngle;
  lastSecond = second;
}

void Clockface::drawClockPointer(float angle, byte pointerSize, uint16_t color) 
{
    clock_x1 = CLOCK_POINTER_POSX + (sin(angle) * pointerSize);
    clock_y1 = CLOCK_POINTER_POSY + (cos(angle) * pointerSize);
   
    Locator::getDisplay()->drawLine(CLOCK_POINTER_POSX,CLOCK_POINTER_POSY,clock_x1,clock_y1,color);
}

float Clockface::degreesToRadians(float degrees)
{
    return (degrees * 3.14) / 180;
}

void Clockface::updateClock() 
{
  Locator::getDisplay()->drawRGBBitmap(0, 0, _CLOCK_TOWER, 64, 64);
}

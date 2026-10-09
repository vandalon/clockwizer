#include "Clockface.h"
#include <CWPreferences.h>
#include <TelnetStream.h>
#include "FootballTicker.h"
#include "GoalAnimation.h"
#include <BackgroundWait.h>

unsigned long oneSecondLoopDue = 0;
bool showColon = true;
static String lastTimeStamp = "";
static String lastDate = "";
bool fullRefresh = false;

// The Tetris animations run one frame per update() call instead of in a loop,
// so the rest of the clock (telnet, notifications, WiFi) keeps running while
// the blocks fall.
static enum { IDLE, ANIMATING_DATE, ANIMATING_TIME } animation = IDLE;
static unsigned long nextFrameAt = 0;
static String pendingTimeStamp;
static String smallDate;  // only set on 64x32 panels
static int smallDateDay = -1;  // day smallDate was built for
static int timePos, timePosY, colonPos, datePos, dateSepPos;

uint16_t myBLACK = 0;
uint16_t myBLUE = 0x4bd6;

FootballTicker footballTicker;

// Bottom row of a 64x32 panel: football scores when there are any, else the date
static void drawBottomRow(MatrixPanel_I2S_DMA *display, const String &date) {
  if (footballTicker.draw(display, 24)) return;
  display->fillRect(0, 23, 64, 9, 0);
  display->setFont(NULL);
  display->setTextSize(1);
  display->setTextColor(0xad75);
  display->setCursor(6, 24);
  display->print(date);
}

Clockface::Clockface(MatrixPanel_I2S_DMA* display) {
  _display = display;
  Locator::provide(display);
  tetris = new TetrisMatrixDraw(*_display);
}

void Clockface::setup(CWDateTime *dateTime) {
  this->_dateTime = dateTime;
  _display->fillRect(0, 0, 64, 64, 0x0000);
  lastDate = "";
  lastTimeStamp = "";
  smallDateDay = -1;
  fullRefresh = true;
  animation = IDLE;
  tetris->scale = 2;
  if (ClockwiseParams::getInstance()->displayHeight == 32) footballTicker.begin(dateTime, ClockwiseParams::getInstance()->showF1);
}

// Draws the next frame of the running animation, if it's time for one
void Clockface::animate() {
  if (millis() < nextFrameAt) return;
  int second = _dateTime->getSecond();
  bool finished;

  if (animation == ANIMATING_DATE) {
    _display->fillRect(0, 0, 64, 64, tetris->tetrisBLACK);
    finished = tetris->drawNumbers(datePos, 58, false);
    _display->flipDMABuffer();
    nextFrameAt = millis() + (second < 58 ? 30 : 0);
    if (finished) {
      _display->drawRect(dateSepPos, 45, 4, 2, tetris->tetrisWHITE);
      _display->flipDMABuffer();
      _display->drawRect(dateSepPos, 45, 4, 2, tetris->tetrisWHITE);
      _display->flipDMABuffer();
      animation = IDLE;
    }
    return;
  }

  _display->drawLine(0, 32, 63, 32, 0);
  _display->fillRect(0, 0, 64, 32, 0);
  finished = tetris->drawNumbers(timePos, timePosY, false);
  if (!smallDate.isEmpty()) drawBottomRow(_display, smallDate);
  _display->flipDMABuffer();
  nextFrameAt = millis() + (second >= 59 ? 0 : fullRefresh ? 30 : 50);
  if (finished) {
    lastTimeStamp = pendingTimeStamp;
    fullRefresh = false;
    animation = IDLE;
  }
}

void Clockface::update()
{
  // A goal interrupts a running time animation; setup() starts it over
  FootballTicker::Goal goal;
  if (footballTicker.nextGoal(goal)) {
    playGoalAnimation(_display, goal.home.c_str(), goal.away.c_str(), goal.homeScore, goal.awayScore,
                      goal.homeScored, goal.homeKit, goal.awayKit, [this](int ms) {
                        _display->flipDMABuffer();
                        cwWait(ms);
                      });
    setup(_dateTime);  // rebuild the time from scratch
    return;
  }

  if (animation != IDLE) {
    animate();
    return;
  }

  static unsigned long loopDue = 0;
  unsigned long now = millis();
  int year = this->_dateTime->getYear();
  int month = this->_dateTime->getMonth();
  int day = this->_dateTime->getDay();
  int hour = this->_dateTime->getHour();
  int minute = this->_dateTime->getMinute();
  int second = this->_dateTime->getSecond();

  if ((long)(now - loopDue) >= 0) {

    String strHour = "";

    if (hour < 10) {
      strHour = " " + String(hour);
      timePos = -5;
    } else if (hour < 20 and hour >= 10) {
      strHour = String(hour);
      timePos = -2;
    } else {
      strHour = String(hour);
      timePos = 2;
    }

    String timeStamp;
    if (minute < 10) {
      timeStamp = strHour + ":0" + String(minute);
    } else {
      timeStamp = strHour + ":" + String(minute);
    }

    static uint16_t color = tetris->tetrisBLACK;
    int x;

    if ((minute >= 10) && (minute < 20))
      x = timePos + 4;
    else
      x = timePos;

    if (ClockwiseParams::getInstance()->displayHeight == 64) {
      String dateString;
      if (month == 1 and day == 1) {
        datePos = -2;
        dateSepPos = 30;
        dateString = " " + String(day) + ":" + String(month) + " ";
      } else if (month < 10 and day < 10) {
        datePos = 2;
        if (month == 1)
          dateSepPos = 34;
        else
          dateSepPos = 30;
        dateString = " " + String(day) + ":" + String(month) + " ";
      } else if (day >= 20 and month < 10) {
        datePos = 8;
        dateSepPos = 36;
        dateString = String(day) + ":" + String(month) + " ";
      } else if (month < 10 and day >= 10) {
        datePos = 5;
        if (month == 1)
          dateSepPos = 37;
        else
          dateSepPos = 33;
        dateString = String(day) + ":" + String(month) + " ";
      } else if (month >= 10 and day < 10) {
        datePos = -5;
        dateSepPos = 27;
        dateString = " " + String(day) + ":" + String(month);
      } else if (day >= 10 and day < 20) {
        datePos = -2;
        dateSepPos = 30;
        dateString = String(day) + ":" + String(month);
      } else {
        datePos = 2;
        dateSepPos = 34;
        dateString = String(day) + ":" + String(month);
      }
        
      if (lastDate != dateString) {
        TelnetStream.println("Updating date on screen");
        tetris->setTime(dateString, false);
        lastDate = dateString;
        animation = ANIMATING_DATE;
        nextFrameAt = 0;
        return;
      }
      timePosY = 26;
      colonPos = -6;
    } else {
      if (day != smallDateDay) {  // not every pass: it would churn the heap
        smallDate = String(day) + "-" + String(month) + "-" + String(year);
        smallDateDay = day;
      }
      drawBottomRow(_display, smallDate);
      timePosY = 21;
      colonPos = -11;
    }

    if (timeStamp != lastTimeStamp) {
      TelnetStream.println("Updating time on screen");
      tetris->setTime(timeStamp, fullRefresh);
      pendingTimeStamp = timeStamp;
      animation = ANIMATING_TIME;
      nextFrameAt = 0;
      return;
    }

    if (ClockwiseParams::getInstance()->displayHeight == 32) {
      // 64x32: pulse the colon smoothly, one full cycle every 2 seconds
      float level = (1 - cosf(2 * PI * (millis() % 2000) / 2000.0f)) / 2;
      uint8_t v = (uint8_t)(level * level * 255);  // gamma, so the dark end fades evenly
      color = ((v >> 3) << 11) | ((v >> 2) << 5) | (v >> 3);
    } else if (second % 2 == 0)
      color = tetris->tetrisWHITE;
    else
      color = tetris->tetrisBLACK;
    tetris->drawColon(x, colonPos, color);
    _display->flipDMABuffer();
    loopDue = now + (ClockwiseParams::getInstance()->displayHeight == 32 ? 20 : 50);
  }
}

#pragma once

#include <Arduino.h>

#include <Adafruit_GFX.h>
#include <Tile.h>
#include <Locator.h>
#include <Game.h>
#include <Object.h>
#include <ImageUtils.h>
#include <WiFi.h>

#include "hour8pt7b.h"
#include "minute7pt7b.h"
#include "small4pt7b.h"
#include "DateI18nEN.h"

// Commons
#include "IClockface.h"
#include "Icons.h"


class Clockface: public IClockface {
  private:
    Adafruit_GFX* _display;
    CWDateTime* _dateTime;
    void updateTime();
    void updateTimeShort();
    void updateDate();

  public:
    Clockface(Adafruit_GFX* display);
    void setup(CWDateTime *dateTime);
    void update();
};

#pragma once

#include <Arduino.h>

#include <Adafruit_GFX.h>
#include <Tile.h>
#include <Locator.h>
#include <Game.h>
#include <Object.h>
#include <ImageUtils.h>
#include <WiFi.h>

// Commons
#include "IClockface.h"
#include "assets.h"
#include "Icons.h"
#include "PKMN_RBYGSC4pt7b.h"




class Clockface: public IClockface {
  private:
    Adafruit_GFX* _display;
    CWDateTime* _dateTime;
    char hours[3] = {0};
    char minutes[3] = {0};

  public:
    Clockface(Adafruit_GFX* display);
    void setup(CWDateTime *dateTime);
    void update();
    void drawShortBackground();
    void refreshDate(uint8_t weekday, uint16_t color);
    void refreshTime();
    void updatePokemon(bool reveal);
    void drawPokemon(uint8_t stage, bool bob);
    void updateLoadingBar(uint8_t seconds);
    void updateLens(uint8_t seconds);
    void updateSpare(uint8_t minute, uint8_t seconds);
};

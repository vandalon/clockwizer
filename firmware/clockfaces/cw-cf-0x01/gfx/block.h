#pragma once

#include <Arduino.h>
#include <Game.h>
#include <Locator.h>
#include <EventTask.h>
#include "assets.h"

const uint8_t MOVE_PACE = 2;
const uint8_t MAX_MOVE_HEIGHT = 4;
const uint8_t COIN_WIDTH = 6;
const uint8_t COIN_HEIGHT = 8;
const uint8_t COIN_STEPS = 8;
const uint8_t MUSHROOM_WIDTH = 8;

class Block: public Sprite, public EventTask {
  private:
    enum State {
      IDLE,
      HIT
    };   

    Direction direction; 

    String _text;

    unsigned long lastMillis = 0;
    State _state = IDLE; 
    State _lastState = IDLE; 
    uint8_t _lastY;
    uint8_t _firstY;

    bool _mushroom = false;  // pop out a 1-UP instead of a coin
    bool _coinActive = false;
    uint8_t _coinStep = 0;
    unsigned long _coinMillis = 0;
    
    void idle();
    void hit();
    void setTextBlock();
    void redraw();
    void updateCoin();
    void drawCoin(int y, bool edgeOn);

  public:
    Block(int x, int y);
    void setText(String text);
    void setMushroom(bool mushroom);
    void init();
    void update();    
    const char* name();
    void execute(EventType event, Sprite* caller);

};

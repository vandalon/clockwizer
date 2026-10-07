#pragma once

#include <Arduino.h>
#include <Game.h>
#include <Locator.h>
#include <EventBus.h>
#include <ImageUtils.h>
#include "assets.h"


const uint8_t MARIO_PACE = 3;
const uint8_t MARIO_JUMP_HEIGHT = 14;
const uint8_t MARIO_WALK_STEP = 2;

// Luigi is floaty: same rise, slower fall
#ifdef CW_LUIGI
const uint8_t MARIO_FALL_MS = 85;
#else
const uint8_t MARIO_FALL_MS = 50;
#endif


class Mario: public Sprite, public EventTask {
  private:

    enum State {
      IDLE,
      WALKING,
      JUMPING
    };

    Direction direction;

    int _lastX;
    int _lastY;
    int _targetX = 0;

    // Called after Mario erases his old spot so the owner can repaint scenery
    void (*_restoreBg)(int x, int y, int w, int h) = nullptr;

    const unsigned short* _sprite;
    unsigned long lastMillis = 0;
    State _state = IDLE; 
    State _lastState = IDLE; 
    
    void idle();
    void erase();
    void draw();

  public:
    Mario(int x, int y);
    void init();
    void move(Direction dir, int times);
    void jump();
    void walkTo(int x);
    bool isIdle();
    bool isJumping();
    void setBackground(void (*restoreBg)(int x, int y, int w, int h));
    void update();
    const char* name();
    void execute(EventType event, Sprite* caller);
    
};

#include "mario.h"

Mario::Mario(int x, int y) {
  _x = x;
  _y = y;
}

void Mario::move(Direction dir, int times) {
  
  if (dir == RIGHT) {
    _x += MARIO_PACE;
  } else if (dir == LEFT) {
    _x -= MARIO_PACE;
  }  

}

void Mario::setBackground(void (*restoreBg)(int x, int y, int w, int h)) {
  _restoreBg = restoreBg;
}

bool Mario::isIdle() {
  return _state == IDLE;
}

bool Mario::isJumping() {
  return _state == JUMPING;
}

void Mario::erase() {
  Locator::getDisplay()->fillRect(_x, _y, _width, _height, SKY_COLOR);
  if (_restoreBg) _restoreBg(_x, _y, _width, _height);
}

// Skips the sky-coloured pixels so Mario can stand in front of the hill
void Mario::draw() {
  for (int row = 0; row < _height; row++) {
    for (int col = 0; col < _width; col++) {
      unsigned short color = pgm_read_word(&_sprite[row * _width + col]);
      if (color != SKY_COLOR) {
        Locator::getDisplay()->drawPixel(_x + col, _y + row, color);
      }
    }
  }
}

void Mario::walkTo(int x) {
  if (_state == IDLE && x != _x) {
    _lastState = _state;
    _state = WALKING;
    _targetX = x;
  }
}

void Mario::jump() {
  if (_state != JUMPING && (millis() - lastMillis > 500) ) {
    // Serial.println("Jump - Start");

    _lastState = _state;
    _state = JUMPING;

    erase();
    
    _width = MARIO_JUMP_SIZE[0];
    _height = MARIO_JUMP_SIZE[1];
    _sprite = MARIO_JUMP;

    direction = UP;

    _lastY = _y;
    _lastX = _x;
  }  
}

void Mario::idle() {
  if (_state != IDLE) {
    // Serial.println("Idle - Start");

    _lastState = _state;
    _state = IDLE;

    erase();

    _width = MARIO_IDLE_SIZE[0];
    _height = MARIO_IDLE_SIZE[1];
    _sprite = MARIO_IDLE;
  }
}


void Mario::init() {
  Locator::getEventBus()->subscribe(this);
  _width = MARIO_IDLE_SIZE[0];
  _height = MARIO_IDLE_SIZE[1];
  _sprite = MARIO_IDLE;
  draw();
}

void Mario::update() {
  

  if (_state == IDLE && _state != _lastState) {
    draw();
    _lastState = _state;
  } else if (_state == WALKING) {

    if (millis() - lastMillis >= 40) {
      int step = min((int)MARIO_WALK_STEP, abs(_targetX - _x));

      erase();
      _x += (_targetX > _x ? step : -step);
      draw();

      if (_x == _targetX) {
        idle();
      }

      lastMillis = millis();
    }

  } else if (_state == JUMPING) {
    
    if (millis() - lastMillis >= (direction == DOWN ? MARIO_FALL_MS : 50)) {

      //Serial.println(_y);
      
      erase();
      
      _y = _y + (MARIO_PACE * (direction == UP ? -1 : 1));

      draw();
      
      Locator::getEventBus()->broadcast(MOVE, this);

     
      if (floor(_lastY - _y) >= MARIO_JUMP_HEIGHT) {
        direction = DOWN;
      }

      if (_y+_height >= 56) {
        idle();
      }

      lastMillis = millis();
    }

  }
}

void Mario::execute(EventType event, Sprite* caller) {
  if (event == EventType::COLLISION) {
    //Serial.println("MARIO - Collision detected");
    direction = DOWN;
  }
}

const char* Mario::name() {
  return "MARIO";
}
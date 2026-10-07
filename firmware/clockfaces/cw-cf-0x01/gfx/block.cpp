#include "block.h"

// Coin popping out of the block: '.' is transparent, K outline, Y gold, L highlight
const char* COIN_FULL[COIN_HEIGHT] = {
  "..KK..",
  ".KYYK.",
  "KYLYYK",
  "KYLYYK",
  "KYLYYK",
  "KYLYYK",
  ".KYYK.",
  "..KK.."
};
const char* COIN_EDGE[COIN_HEIGHT] = {
  "KK",
  "KY",
  "KY",
  "KY",
  "KY",
  "KY",
  "KY",
  "KK"
};

// 1-UP mushroom: G green, W white, S cream
const char* MUSHROOM[COIN_HEIGHT] = {
  "..KKKK..",
  ".KGGGGK.",
  "KGWWGGWK",
  "KGWWGGWK",
  "KGGGGGGK",
  "KKSSSSKK",
  ".KSKKSK.",
  "..KKKK.."
};

// Coin top edge per step: rises out of the block, then hovers at the top of the screen
const int8_t COIN_Y[COIN_STEPS] = {4, 2, 0, 0, 0, 0, 0, 0};

Block::Block(int x, int y) {
  _x = x;
  _y = y;
  _firstY = y;
  _width = 19;
  _height = 19;
}

void Block::idle() {
  if (_state != IDLE) {
    // Serial.println("Block - Idle - Start");

    _lastState = _state;
    _state = IDLE;

    _y = _firstY;
  }
} 

void Block::hit() {
  if (_state != HIT) {
    // Serial.println("Hit - Start");

    _lastState = _state;
    _state = HIT;

    _lastY = _y;

    direction = UP;

    _coinActive = true;
    _coinStep = 0;
    _coinMillis = 0;
  }
}

void Block::setTextBlock() {
  Locator::getDisplay()->setTextColor(0x0000);       
  
  
  if (_text.length() == 1) {
    Locator::getDisplay()->setCursor(_x+6, _y+12);
  }  else {
    Locator::getDisplay()->setCursor(_x+2, _y+12);
  }

  Locator::getDisplay()->print(_text);
}

void Block::redraw() {
  Locator::getDisplay()->drawRGBBitmap(_x, _y, BLOCK, _width, _height);
  setTextBlock();
}

void Block::drawCoin(int y, bool edgeOn) {
  const char** rows = _mushroom ? MUSHROOM : (edgeOn ? COIN_EDGE : COIN_FULL);
  int w = _mushroom ? MUSHROOM_WIDTH : (edgeOn ? 2 : COIN_WIDTH);
  int x = _x + (_width - w) / 2;

  for (int row = 0; row < COIN_HEIGHT; row++) {
    for (int col = 0; col < w; col++) {
      char c = rows[row][col];
      unsigned short color;
      if (c == 'K') color = 0x0000;
      else if (c == 'Y') color = 0xFDC0;
      else if (c == 'L') color = 0xFFE0;
      else if (c == 'G') color = 0x0560;
      else if (c == 'W') color = 0xFFFF;
      else if (c == 'S') color = 0xFF9C;
      else continue;
      Locator::getDisplay()->drawPixel(x + col, y + row, color);
    }
  }
}

void Block::updateCoin() {
  if (!_coinActive || millis() - _coinMillis < 60) return;

  int oldY = COIN_Y[_coinStep > 0 ? _coinStep - 1 : 0];
  int x = _x + (_width - MUSHROOM_WIDTH) / 2;
  Locator::getDisplay()->fillRect(x, oldY, MUSHROOM_WIDTH, COIN_HEIGHT, SKY_COLOR);
  redraw();  // the coin overlaps the top of the block

  if (_coinStep < COIN_STEPS) {
    drawCoin(COIN_Y[_coinStep], !_mushroom && _coinStep % 2 == 1);
    _coinStep++;
  } else {
    _coinActive = false;
  }

  _coinMillis = millis();
}

void Block::setMushroom(bool mushroom) {
  _mushroom = mushroom;
}

void Block::setText(String text) {
  _text = text;
}

void Block::init() {
  Locator::getEventBus()->subscribe(this);
  Locator::getDisplay()->drawRGBBitmap(_x, _y, BLOCK, _width, _height);
  setTextBlock();  
}

void Block::update() {

  updateCoin();

  if (_state == IDLE && _lastState != _state) {
    redraw();

    _lastState= _state;

  } else if (_state == HIT) {
    
    if (millis() - lastMillis >= 60) {

      // Serial.print("BLOCK Y = ");
      // Serial.println(_y);
      
      Locator::getDisplay()->fillRect(_x, _y, _width, _height, SKY_COLOR);
      
      _y = _y + (MOVE_PACE * (direction == UP ? -1 : 1));
 
      Locator::getDisplay()->drawRGBBitmap(_x, _y, BLOCK, _width, _height);
      setTextBlock();
                 
      if (floor(_firstY - _y) >= MAX_MOVE_HEIGHT) {
        // Serial.println("DOWN");
        direction = DOWN;
      }

      if (_y >= _firstY && direction == DOWN) {
        idle();
      }

      lastMillis = millis();
    }

  }
}


void Block::execute(EventType event, Sprite* caller) {
  //Serial.println("Checking collision");

  if (event == EventType::MOVE) {
    if (this->collidedWith(caller)) {
      Serial.println("Collision detected");
      hit();
      Locator::getEventBus()->broadcast(EventType::COLLISION, this);
    }
  }
  
}


const char* Block::name() {
  return "BLOCK";
}
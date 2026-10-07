
#include "Clockface.h"

EventBus eventBus;

const char* FORMAT_TWO_DIGITS = "%02d";

// Graphical elements
Tile ground(GROUND, 8, 8); 

Object cloud1(CLOUD1, 13, 12);
Object cloud2(CLOUD2, 13, 12);
// The hill sits on the right, so draw it mirrored
unsigned short hillMirrored[20 * 22];
Object hill(hillMirrored, 20, 22);


// Mario's jump sprite is 17px wide, so these spots put him under exactly one block
const int HOUR_X = 14;
const int MINUTE_X = 33;
const int HILL_X = 44;
const int HILL_Y = 34;

Mario mario(MINUTE_X, 56 - MARIO_IDLE_SIZE[1]);  // standing on the ground
Block hourBlock(13, 8);
Block minuteBlock(32, 8);
Plant plant(1, 42);

unsigned long lastMillis = 0;
int lastSecond = -1;
int shownHour = -1;

enum Step {
  STEP_NONE,
  WALK_TO_HOUR,
  JUMP_HOUR,
  WALK_TO_MINUTE,
  JUMP_MINUTE
};
Step step = STEP_NONE;
bool jumpRequested = false;

// Mario erases a rectangle around himself; put back the hill if he stood in front of it
void restoreBackground(int x, int y, int w, int h) {
  if (x + w > HILL_X && y + h > HILL_Y) {
    hill.draw(HILL_X, HILL_Y);
  }
}

Clockface::Clockface(Adafruit_GFX* display) {
  _display = display;

  Locator::provide(display);
  Locator::provide(&eventBus);
}

void Clockface::setup(CWDateTime *dateTime) {
  _dateTime = dateTime;

  for (int row = 0; row < 22; row++) {
    for (int col = 0; col < 20; col++) {
      hillMirrored[row * 20 + col] = HILL[row * 20 + (19 - col)];
    }
  }

  Locator::getDisplay()->setFont(&Super_Mario_Bros__24pt7b);
  Locator::getDisplay()->fillRect(0, 0, 64, 64, SKY_COLOR);

  ground.fillRow(DISPLAY_HEIGHT - ground._height);

  hill.draw(HILL_X, HILL_Y);
  cloud1.draw(0, 9);
  cloud2.draw(51, 7);

  updateTime();


  hourBlock.init();
  minuteBlock.init();
#ifdef CW_LUIGI
  minuteBlock.setMushroom(true);  // Luigi gets a 1-UP from the minute block
#endif

  mario.setBackground(restoreBackground);
  mario.init();
  plant.init();
}

void Clockface::update() {
  hourBlock.update();
  minuteBlock.update();
  mario.update();
  plant.update();

  int second = _dateTime->getSecond();
  if (second != lastSecond) {
    lastSecond = second;
    plant.setSecond(second);
  }

  if (second == 0 && millis() - lastMillis > 1000) {
    lastMillis = millis();
    startSequence();
  }

  runSequence();
}

void Clockface::updateTime() {
  shownHour = _dateTime->getHour();
  hourBlock.setText(String(shownHour));
  minuteBlock.setText(String(_dateTime->getMinute(FORMAT_TWO_DIGITS)));
}

// Hour changed: walk to the hour block, hit it, walk to the minute block, hit it.
// Otherwise Mario just hits the minute block from where he stands.
void Clockface::startSequence() {
  if (step != STEP_NONE) return;

  jumpRequested = false;
  if (_dateTime->getHour() != shownHour) {
    mario.walkTo(HOUR_X);
    step = WALK_TO_HOUR;
  } else {
    step = JUMP_MINUTE;
  }
}

void Clockface::runSequence() {
  switch (step) {
    case WALK_TO_HOUR:
      if (mario.isIdle()) {
        shownHour = _dateTime->getHour();
        hourBlock.setText(String(shownHour));
        step = JUMP_HOUR;
      }
      break;

    case JUMP_HOUR:
    case JUMP_MINUTE:
      if (!jumpRequested) {
        if (step == JUMP_MINUTE) {
          minuteBlock.setText(String(_dateTime->getMinute(FORMAT_TWO_DIGITS)));
        }
        mario.jump();  // may be refused right after landing, so keep asking
        jumpRequested = mario.isJumping();
      } else if (mario.isIdle()) {
        jumpRequested = false;
        if (step == JUMP_HOUR) {
          mario.walkTo(MINUTE_X);
          step = WALK_TO_MINUTE;
        } else {
          step = STEP_NONE;
        }
      }
      break;

    case WALK_TO_MINUTE:
      if (mario.isIdle()) {
        step = JUMP_MINUTE;
      }
      break;

    default:
      break;
  }
}

void Clockface::externalEvent(int type) {
  if (type == 0) {  //TODO create an enum
    startSequence();
  }
}

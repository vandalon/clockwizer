
#include "Clockface.h"


#define LIGHT_GREEN 0x754d
#define DARK_GREEN 0x0264

#define DARK_BLUE 0x016D
#define LIGHT_BLUE 0x24fe

#define LIGHT_BLACK 0X10c4

#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

#define POKEMON_X 8
#define POKEMON_Y 21

#define TICK_MS 250           // screen update rate; the bob and the reveal need more than 1 Hz
#define REVEAL_DARK_MS 1000   // "?" silhouette after a minute change
#define REVEAL_STEP_MS 250    // each mosaic step: 4px blocks, 2px blocks, full colour

const uint16_t LENS_COLORS[3] = {RGB565(0xe8, 0x3a, 0x2a), RGB565(0xf0, 0xc0, 0x20), RGB565(0x30, 0xc0, 0x40)};
const uint8_t QMARK[5] = {0b110, 0b001, 0b010, 0b000, 0b010};

unsigned long lastMillis = 0;
int lastMinute = -1;  // minute on screen; comparing it can't miss a change like testing for second 0 can
int lastSecond = -1;
int lastWeekday = -1;
int pokemonIdx = -1;
unsigned long revealStart = 0;
int shownStage = -1;  // what is drawn in the frame; -1 forces a redraw
bool shownBob = false;
bool spareLit = false;

const uint16_t* const pokemons[] PROGMEM = {pokemon1, pokemon2, pokemon3, pokemon4, pokemon5, pokemon6, pokemon7,
  pokemon8, pokemon9, pokemon10, pokemon11, pokemon12, pokemon13, pokemon14, pokemon15, pokemon16};

Clockface::Clockface(Adafruit_GFX* display)
{
  _display = display;
  Locator::provide(display);
}

void Clockface::setup(CWDateTime *dateTime) {
  this->_dateTime = dateTime;

  randomSeed(dateTime->getMilliseconds() + millis());

  // Clear screen
  Locator::getDisplay()->fillRect(0, 0, 64, 64, 0x0000);

  // Draw background
  Locator::getDisplay()->drawRGBBitmap(0, 0, POKEDEX_BG, 64, 64);

  Locator::getDisplay()->setFont(&PKMN_RBYGSC4pt7b);

  lastMinute = _dateTime->getMinute();
  lastSecond = -1;
  lastWeekday = this->_dateTime->getWeekday();
  spareLit = false;
  refreshTime();
  refreshDate(lastWeekday, DARK_BLUE);
  updatePokemon(false);
}

void Clockface::update() 
{
  if (millis() - lastMillis < TICK_MS) return;
  lastMillis = millis();

  uint8_t seconds = this->_dateTime->getSecond();
  int minute = this->_dateTime->getMinute();
  bool minuteChanged = minute != lastMinute;

  if (minuteChanged) {
    lastMinute = minute;
    refreshTime();
    updatePokemon(true);
  }

  int wd = this->_dateTime->getWeekday();
  if (wd != lastWeekday) {
    // clean up the previous square
    refreshDate(lastWeekday, LIGHT_BLUE);
    refreshDate(wd, DARK_BLUE);
    lastWeekday = wd;
  }

  // Who's that Pokemon: dark "?", then mosaic, then the real sprite
  unsigned long since = millis() - revealStart;
  uint8_t stage = since < REVEAL_DARK_MS ? 0 : since < REVEAL_DARK_MS + REVEAL_STEP_MS ? 1 : since < REVEAL_DARK_MS + 2 * REVEAL_STEP_MS ? 2 : 3;
  bool bob = stage == 3 && (seconds & 1);
  if (stage != shownStage || bob != shownBob) drawPokemon(stage, bob);

  if (seconds != lastSecond || minuteChanged) {
    lastSecond = seconds;
    updateLoadingBar(minuteChanged ? 0 : seconds);
    updateLens(seconds);
  }

  updateSpare(minute, seconds);
}

void Clockface::refreshDate(uint8_t weekday, uint16_t color) {
  // Update weekday
  uint8_t x = 36 + ((weekday > 3 ? (weekday-4) : weekday) * 6);
  uint8_t y = 35 + (weekday > 3 ? 5 : 0);

  Locator::getDisplay()->fillRect(x, y, 5, 4, color);
}


void Clockface::refreshTime() { 

  // Clean up the clock area
  Locator::getDisplay()->fillRect(35, 17, 26, 14, LIGHT_BLACK);

  snprintf(hours, sizeof(hours), "%02d", _dateTime->getHour());
  snprintf(minutes, sizeof(minutes), "%02d", _dateTime->getMinute());

  Locator::getDisplay()->setCursor(35, 22);
  Locator::getDisplay()->print(hours);

  Locator::getDisplay()->setCursor(46, 30);
  Locator::getDisplay()->print(minutes);

  if (!_dateTime->is24hFormat())
    Locator::getDisplay()->drawBitmap(55, 18, (_dateTime->isAM() ? AM_SIGN : PM_SIGN), 4, 4, 0xffff);
}

// Picks a new Pokemon (never the same one twice in a row). With reveal it starts
// from the "?" silhouette, without it (boot) the sprite is shown straight away.
void Clockface::updatePokemon(bool reveal) { 
  int count = sizeof(pokemons) / sizeof(pokemons[0]);
  int idx;
  do {
    idx = random(count);
  } while (idx == pokemonIdx);
  pokemonIdx = idx;

  revealStart = reveal ? millis() : millis() - (REVEAL_DARK_MS + 2 * REVEAL_STEP_MS);
  shownStage = -1;
}

// stage 0 = "?" silhouette, 1 = 4px blocks, 2 = 2px blocks, 3 = full sprite.
// The bob slides the sprite down 1px inside its 16x16 window (top row repeated,
// bottom row dropped), so the frame border below it is never overwritten.
void Clockface::drawPokemon(uint8_t stage, bool bob) {
  static uint16_t src[256], out[256];
  const uint16_t* sprite = pokemons[pokemonIdx];

  if (stage == 0) {
    for (int i = 0; i < 256; i++) src[i] = LIGHT_BLACK;
  } else if (stage == 3) {
    for (int i = 0; i < 256; i++) src[i] = sprite[i];
  } else {
    uint8_t b = stage == 1 ? 4 : 2;
    uint8_t n = b * b;
    for (uint8_t by = 0; by < 16; by += b) {
      for (uint8_t bx = 0; bx < 16; bx += b) {
        uint16_t r = 0, g = 0, bl = 0;
        for (uint8_t j = 0; j < b; j++) {
          for (uint8_t i = 0; i < b; i++) {
            uint16_t c = sprite[(by + j) * 16 + bx + i];
            r += c >> 11;
            g += (c >> 5) & 0x3F;
            bl += c & 0x1F;
          }
        }
        uint16_t color = ((r / n) << 11) | ((g / n) << 5) | (bl / n);
        for (uint8_t j = 0; j < b; j++)
          for (uint8_t i = 0; i < b; i++) src[(by + j) * 16 + bx + i] = color;
      }
    }
  }

  for (int j = 0; j < 16; j++)
    for (int i = 0; i < 16; i++)
      out[j * 16 + i] = src[((bob && j > 0) ? j - 1 : j) * 16 + i];

  Locator::getDisplay()->drawRGBBitmap(POKEMON_X, POKEMON_Y, out, 16, 16);

  if (stage == 0) {
    for (int r = 0; r < 5; r++)
      for (int i = 0; i < 3; i++)
        if (QMARK[r] & (4 >> i))
          Locator::getDisplay()->fillRect(POKEMON_X + 5 + i * 2, POKEMON_Y + 4 + r * 2, 2, 2, 0x9CD3);
  }

  shownStage = stage;
  shownBob = bob;
}

// HP-style bar: drains over the minute, green then yellow then red
void Clockface::updateLoadingBar(uint8_t seconds) {
  uint8_t left = 60 - seconds;
  uint16_t color = left > 30 ? RGB565(0x3c, 0xc8, 0x50) : left > 12 ? RGB565(0xf0, 0xc8, 0x28) : RGB565(0xe0, 0x30, 0x30);

  uint8_t width = (11 * left) / 60;

  Locator::getDisplay()->fillRect(9, 53, 11, 5, DARK_GREEN);
  // a zero-width fillRect must be skipped, it can smear across the screen
  if (width > 0) Locator::getDisplay()->fillRect(9, 53, width, 5, color);
}

// Lens blink, one colour per second
void Clockface::updateLens(uint8_t seconds) {
  uint16_t color = LENS_COLORS[seconds % 3];
  Locator::getDisplay()->fillRect(5, 4, 2, 4, color);
  Locator::getDisplay()->fillRect(4, 5, 4, 2, color);
}

// The spare blue square flashes for the first 6 seconds of every hour
void Clockface::updateSpare(uint8_t minute, uint8_t seconds) {
  bool lit = minute == 0 && seconds < 6 && ((millis() / 500) % 2 == 0);
  if (lit == spareLit) return;
  spareLit = lit;
  Locator::getDisplay()->fillRect(54, 40, 5, 4, lit ? RGB565(0xff, 0xf3, 0xa0) : LIGHT_BLUE);
}

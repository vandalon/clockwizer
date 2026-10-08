#include "Clockface.h"
#include <TelnetStream.h>
#include <CWPreferences.h>


unsigned long lastMillis = 0;
int shownMinute = -1;
unsigned long lastMillisSec = 0;


Pacman *pacman;

// The fallback maze, used when the generator finds nothing; generateLevel() makes its mazes in the same format.
// 11x11 characters: a junction every row/column of COL_X/ROW_Y.
// The first and last line hold '=' above/below the columns with a tunnel to the top/bottom edge of the screen.
// The lines in between are the maze: junction rows hold '.' food, 'o' power pellet, 'P' pacman, ' ' none, with '-' for a corridor to the next junction
// and '=' at both ends of the row for a tunnel to the side edges; the rows in between hold '|' for a corridor down. The clock box sits where the junctions are left out.
static const char* const FALLBACK_MAP[11] = {
  "           ",
  " o-.-.-.-o ",
  " | | | | | ",
  " . .-. .-. ",
  " |       | ",
  "=.       .=",
  " |       | ",
  " .-. P-.-. ",
  " | | |   | ",
  " o-.-.-.-o ",
  "           "
};

// Corridors are 5px wide (pacman just fits), the lanes sit on these pixels.
// Junctions are always 12px apart, so the dots (one every 4px, 3px apart) fall evenly in every corridor,
// and the gap between two lanes is 7px: every wall is an outline 2px thick.
static const int COL_X[5] = {6, 18, 30, 42, 54};
static const int ROW_Y[5] = {5, 17, 29, 41, 53};

static int colAt(int x) {
  for (int i = 0; i < 5; i++) if (COL_X[i] == x) return i;
  return -1;
}

static int rowAt(int y) {
  for (int i = 0; i < 5; i++) if (ROW_Y[i] == y) return i;
  return -1;
}

// ghosts sit on the clock (at the start, and when eaten) right above the lanes of the second and fourth column
static int ghostHouseX(int i) { return COL_X[i == 0 ? 1 : 3]; }

// the clock and the seconds dots while a ghost sits on the clock
static const uint16_t CLOCK_DIM = 0x4208;   // 0xFE40 blended towards 0x0000
static const uint16_t DEFAULT_DOT_COLOR = 0xB58C;
static uint16_t clockColor(uint8_t dim) {
  // blend 0xFE40 (bright) towards CLOCK_DIM per RGB565 channel
  uint16_t a = 0xFE40, b = CLOCK_DIM;
  int r = ((a >> 11) * (255 - dim) + (b >> 11) * dim) / 255;
  int g = (((a >> 5) & 0x3F) * (255 - dim) + ((b >> 5) & 0x3F) * dim) / 255;
  int bl = ((a & 0x1F) * (255 - dim) + (b & 0x1F) * dim) / 255;
  return (r << 11) | (g << 5) | bl;
}
static uint16_t ghostColor(int first, uint16_t wall, uint16_t other);

Clockface::Clockface(Adafruit_GFX* display) {
  _display = display;
  Locator::provide(display);
}

void Clockface::setup(CWDateTime *dateTime) {
  this->_dateTime = dateTime;
  Locator::getDisplay()->setFont(&hourFont);
  randomSeed(esp_random());
  if (!generateLevel()) loadLevel(FALLBACK_MAP, 2);
  drawMap();
  updateClock();
}

void Clockface::update()
{

  // Seconds blink  
  if ((millis() - lastMillisSec) >= 1000) {
    
    if (show_seconds) {
      uint16_t dots = clockColor(_dimLevel);
      Locator::getDisplay()->fillRect(31, _clockY + 2, 2, 2, dots);
      Locator::getDisplay()->fillRect(31, _clockY + 7, 2, 2, dots);
    } else  {
      Locator::getDisplay()->fillRect(31, _clockY + 2, 2, 2, 0);
      Locator::getDisplay()->fillRect(31, _clockY + 7, 2, 2, 0);
    }

    show_seconds = !show_seconds;
    lastMillisSec = millis();
  }
  refreshColors();

  // ease the clock towards dim (ghost on the clock) or bright
  if (millis() - lastDimStep >= DIM_STEP_MS) {
    lastDimStep = millis();
    int target = ghostInHouse() ? 255 : 0;
    if (_dimLevel != target) {
      int step = DIM_STEP;
      _dimLevel = target > _dimLevel ? min(target, _dimLevel + step) : max(target, _dimLevel - step);
      updateClock(false);
    }
  }

  // Clock
  if (_dateTime->getMinute() != shownMinute) {
    updateClock();
  }


  // Pacman got caught: blink, then respawn
  if (_deathUntil) {
    updateDeath();
    return;
  }

  // Pacman
  if (millis() - lastMillis >= 55) {
    
    // on a junction (not between two, and not in the tunnel past the edge of the map)
    int col = colAt(pacman->getX());
    int row = rowAt(pacman->getY());

    if (col >= 0 && row >= 0) {

      // power up only when pacman is on the pill
      if (_MAP[row][col] == MapBlock::SUPER_FOOD) {
        pacman->setState(Pacman::State::INVENCIBLE);
      }

      //change block to empty where pacman passes
      _MAP[row][col] = MapBlock::EMPTY;

      directionDecision();

      if (countBlocks(MapBlock::FOOD) == 0 && pillsLeft() == 0) {
        resetMap();
      }
    }


    pacman->update();
    eatPills();

    // fully off the screen: come back on the other side
    if (pacman->getX() <= -5) {
      pacman->setX(64);
    } else if (pacman->getX() >= 64) {
      pacman->setX(-5);
    }
    if (pacman->getY() <= -5) {
      pacman->setY(64);
    } else if (pacman->getY() >= 64) {
      pacman->setY(-5);
    }

    lastMillis = millis();
  }

  updateGhosts();
  checkGhostCollisions();
}


const char* Clockface::weekDayName(int weekday) {
  strncpy(weekDayTemp, _weekDayWords + (weekday*4), 4);
  return weekDayTemp;
}

const char* Clockface::monthName(int month) {
  strncpy(monthTemp, _monthWords + ((month-1)*4), 4);
  return monthTemp;
}



void Clockface::updateClock(bool clear) {

    // a fade step only changes the colour: draw over the old digits, no blank frame in between
    if (clear) Locator::getDisplay()->fillRect(14, _clockY, 36, 11, 0x0000);

    Locator::getDisplay()->setFont(&hourFont);

    Locator::getDisplay()->setTextColor(clockColor(_dimLevel));
    Locator::getDisplay()->setCursor(15, _clockY + 6);

    Locator::getDisplay()->print(this->_dateTime->getHour("00"));
    Locator::getDisplay()->print(" ");
    Locator::getDisplay()->print(this->_dateTime->getMinute("00"));
    shownMinute = this->_dateTime->getMinute();
}

// At a junction, score every open way and take the best: food nearby is good, a ghost nearby is bad (good while powered up),
// going straight on is slightly preferred and turning back is not. A dash of noise, and pacman only looks out for ghosts 3 times in 4, so he is no genius.
void Clockface::directionDecision() {
  int row = rowAt(pacman->getY());
  int col = colAt(pacman->getX());
  Direction cur = pacman->_direction;
  bool powered = pacman->_state == Pacman::State::INVENCIBLE;
  bool lookOut = random(100) < 75;

  Direction back = cur == Direction::LEFT ? Direction::RIGHT : cur == Direction::RIGHT ? Direction::LEFT :
                   cur == Direction::UP ? Direction::DOWN : Direction::UP;

  int best = -1;
  int bestScore = -10000;
  for (int d = 0; d < 4; d++) {
    Direction dir = static_cast<Direction>(d);
    if (contains(nextBlock(dir), PACMAN_BLOCKING_BLOCKS)) continue;

    int nc = col + (dir == Direction::RIGHT) - (dir == Direction::LEFT);
    int nr = row + (dir == Direction::DOWN) - (dir == Direction::UP);
    bool inMap = nc >= 0 && nc < MAP_COLS && nr >= 0 && nr < MAP_ROWS;
    int nx = nc < 0 ? -5 : nc >= MAP_COLS ? 64 : COL_X[nc];
    int ny = nr < 0 ? -5 : nr >= MAP_ROWS ? 64 : ROW_Y[nr];

    int score = random(12);
    if (dir == cur) score += 4;
    if (dir == back) score -= 8;

    if (linkHasPills(row, col, dir)) score += 8;

    if (inMap) {
      if (_MAP[nr][nc] == MapBlock::FOOD) score += 10;
      if (_MAP[nr][nc] == MapBlock::SUPER_FOOD) score += 14;

      // head for the nearest food, as the crow flies
      int nearest = 99;
      for (int r = 0; r < MAP_ROWS; r++) {
        for (int c = 0; c < MAP_COLS; c++) {
          if (_MAP[r][c] == MapBlock::FOOD || _MAP[r][c] == MapBlock::SUPER_FOOD) {
            nearest = min(nearest, abs(r - nr) + abs(c - nc));
          }
        }
      }
      for (int i = 0; i < _pillCount; i++) {
        if (!_pills[i].alive) continue;
        int r2 = _pills[i].row + _pills[i].vertical;
        int c2 = _pills[i].col + !_pills[i].vertical;
        nearest = min(nearest, min(abs(_pills[i].row - nr) + abs(_pills[i].col - nc), abs(r2 - nr) + abs(c2 - nc)));
      }
      score -= nearest < 99 ? nearest * 2 : 0;
    }

    if (lookOut || powered) {
      for (int i = 0; i < GHOST_COUNT; i++) {
        if (_ghostHouseUntil[i]) continue;
        int dist = abs(_ghosts[i].getX() - nx) + abs(_ghosts[i].getY() - ny);
        if (dist < 28) score += (powered ? 1 : -1) * (28 - dist) * 2;
      }
    }

    if (score > bestScore) {
      bestScore = score;
      best = d;
    }
  }

  if (best < 0) {
    turnRandom();
  } else if (static_cast<Direction>(best) != cur) {
    pacman->turn(static_cast<Direction>(best));
  }
}

void Clockface::resetMap() {

  // a new random maze
  if (!generateLevel()) loadLevel(FALLBACK_MAP, 2);
  drawMap();
  updateClock();

  // new level: the ghosts start in the middle
  for (int i = 0; i < GHOST_COUNT; i++) {
    sendGhostHome(i);
  }
}


int Clockface::countBlocks(Clockface::MapBlock elem) {
  int count = 0;
  for (int i = 0; i<MAP_ROWS; i++) {
    for (int j = 0; j<MAP_COLS; j++) {
      if (_MAP[i][j] == elem)
        count++;
    }
  }

  return count;
}


void Clockface::turnRandom() {
  int dir = random(4);
  //int dir = 3;
  //pacman->_state = Pacman::State::TURNING;

  do {
    pacman->turn(static_cast<Direction>(dir));
    dir = random(4);
    //dir++;

    
  } while (!contains(nextBlock(), PACMAN_MOVING_BLOCKS));

  Serial.print("New direction: ");
  Serial.println(pacman->_direction);
}


Clockface::MapBlock Clockface::nextBlock() {
  return nextBlock(pacman->_direction);
}

Clockface::MapBlock Clockface::nextBlock(Direction dir) {

  int row = rowAt(pacman->getY());
  int col = colAt(pacman->getX());
  MapBlock map_block = blockAt(row, col, dir);

  // past the edge of the map: a tunnel leads to the other side
  if (map_block == MapBlock::OUT_OF_MAP &&
      (((dir == Direction::LEFT || dir == Direction::RIGHT) && _TUNNEL[row]) || ((dir == Direction::UP || dir == Direction::DOWN) && _TUNNEL_V[col]))) {
    map_block = MapBlock::GATE;
  }

  return map_block;

}

bool Clockface::contains(int v, const int* values) {
  
  for (int i = 1; i<values[0]+1; i++) {
    if (v == values[i])
      return true;
  }

  return false;
}

void Clockface::loadLevel(const char* const* rows, int clockRow) {
  _clockRow = clockRow;
  _clockY = ROW_Y[clockRow] - 3;   // the clock box starts 5px above the lane and has a 2px margin
  _holeY = ROW_Y[clockRow] + 10;
  for (int c = 0; c < MAP_COLS; c++) {
    _TUNNEL_V[c] = rows[0][2*c+1] == '=' && rows[10][2*c+1] == '=';
  }

  for (int r = 0; r < MAP_ROWS; r++) {
    const char* nodes = rows[1+2*r];
    const char* links = r < MAP_ROWS-1 ? rows[1+2*r+1] : nullptr;
    _TUNNEL[r] = nodes[0] == '=';

    for (int c = 0; c < MAP_COLS; c++) {
      char ch = nodes[2*c+1];
      _MAP[r][c] = ch == '.' ? MapBlock::FOOD : ch == 'o' ? MapBlock::SUPER_FOOD : ch == 'P' ? MapBlock::PACMAN : MapBlock::WALL;
      _LINK_H[r][c] = c < MAP_COLS-1 && nodes[2*c+2] == '-';
      _LINK_V[r][c] = links && links[2*c+1] == '|';
    }
  }

  buildPills();
}

// ---- random mazes ----
// Mirrored left/right (some mazes get a few one-sided corridor changes, so they are asymmetrical), junctions may be left out (solid), every junction has at least 2 corridors (the tunnel counts as one),
// 1 or 2 side tunnels on the middle rows (never next to each other), 4 power pellets at or next to the corners.
// The ghosts' exits (the row below the clock, columns 1 and 3) always exist. The clock box sits on junction row 1, 2 or 3. A new maze must differ from the last one.

static int mazeDegree(const bool h[5][4], const bool v[4][5], const bool* tun, int r, int c) {
  return (c > 0 && h[r][c-1]) + (c < 4 && h[r][c]) + (r > 0 && v[r-1][c]) + (r < 4 && v[r][c]) + (tun[r] && (c == 0 || c == 4));
}

// Empty space: pixels that drawWalls() leaves black, i.e. no corridor and no wall outline (2px around a corridor).
// Mirrors drawWalls(). randomMaze() adds corridors until no black area is bigger than MAX_EMPTY_PX pixels,
// also where the maze steps inward from the edge of the screen. The margin around the screen (outside the walls
// of the outermost corridors) counts as empty only where that outer wall is missing.
static const int MAX_EMPTY_PX = 48;

// how many pixels the black areas of this maze are over MAX_EMPTY_PX (0 = all fine)
static int emptyExcess(const bool node[5][5], const bool h[5][4], const bool v[4][5], const bool* tun, int clockRow) {
  uint64_t open[64] = {};
  auto rect = [&](int x0, int y0, int x1, int y1) {
    uint64_t bits = (x1 - x0 >= 63 ? ~0ULL : ((1ULL << (x1 - x0 + 1)) - 1)) << x0;
    for (int y = max(y0, 0); y <= min(y1, 63); y++) open[y] |= bits;
  };
  for (int r = 0; r < 5; r++) {
    for (int c = 0; c < 5; c++) {
      if (!node[r][c]) continue;
      rect(COL_X[c], ROW_Y[r], COL_X[c] + 4, ROW_Y[r] + 4);
      if (c < 4 && h[r][c]) rect(COL_X[c] + 5, ROW_Y[r], COL_X[c+1] - 1, ROW_Y[r] + 4);
      if (r < 4 && v[r][c]) rect(COL_X[c], ROW_Y[r] + 5, COL_X[c] + 4, ROW_Y[r+1] - 1);
    }
    if (tun[r]) {
      rect(0, ROW_Y[r], COL_X[0], ROW_Y[r] + 4);
      rect(COL_X[4] + 4, ROW_Y[r], 63, ROW_Y[r] + 4);
    }
  }
  rect(13, ROW_Y[clockRow] - 5, 51, ROW_Y[clockRow] + 9);  // inside the clock box

  static const int X_LO = 4, X_HI = 60, Y_LO = 3, Y_HI = 59;   // where the outer walls of the outermost corridors sit
  uint64_t blank[64], rem[64] = {};
  for (int y = 0; y < 64; y++) {
    uint64_t nearOpen = 0;
    for (int k = max(y - 2, 0); k <= min(y + 2, 63); k++) nearOpen |= open[k];
    nearOpen |= (nearOpen << 1) | (nearOpen << 2) | (nearOpen >> 1) | (nearOpen >> 2);
    blank[y] = ~nearOpen;
  }
  // the margin is empty only when the nearest pixel of the frame is
  for (int y = 0; y < 64; y++) {
    int ry = min(max(y, Y_LO), Y_HI);
    for (int x = 0; x < 64; x++) {
      int rx = min(max(x, X_LO), X_HI);
      if (((blank[y] >> x) & 1) && ((blank[ry] >> rx) & 1)) rem[y] |= 1ULL << x;
    }
  }

  int excess = 0;
  for (int y0 = 0; y0 < 64; y0++) {
    while (rem[y0]) {
      uint64_t reg[64] = {};
      reg[y0] = rem[y0] & -rem[y0];   // the first empty pixel
      for (bool grown = true; grown;) {   // grow the area from this pixel until it stops
        grown = false;
        for (int y = 0; y < 64; y++) {
          uint64_t n = reg[y] | (reg[y] << 1) | (reg[y] >> 1) | (y > 0 ? reg[y-1] : 0) | (y < 63 ? reg[y+1] : 0);
          n &= rem[y];
          if (n != reg[y]) { reg[y] = n; grown = true; }
        }
      }
      int count = 0;
      for (int y = 0; y < 64; y++) { count += __builtin_popcountll(reg[y]); rem[y] &= ~reg[y]; }
      if (count > MAX_EMPTY_PX) excess += count - MAX_EMPTY_PX;
    }
  }
  return excess;
}

bool Clockface::randomMaze(Maze& m) {
  memset(&m, 0, sizeof(m));
  m.clockRow = 1 + random(3);
  static const uint8_t TUNNEL_SETS[4] = {1 << 1, 1 << 2, 1 << 3, (1 << 1) | (1 << 3)};
  uint8_t tmask = TUNNEL_SETS[random(4)];
  for (int r = 0; r < MAP_ROWS; r++) m.tun[r] = (tmask >> r) & 1;

  for (int r = 0; r < MAP_ROWS; r++) {
    for (int c = 0; c < MAP_COLS; c++) m.node[r][c] = !(r == m.clockRow && c >= 1 && c <= 3);
  }
  // leave out up to 3 junctions (and their mirror), not the ghost exits or the ends of a tunnel
  for (int k = 0; k < 3; k++) {
    int r = random(MAP_ROWS), c = random(3);
    if (r == m.clockRow && c >= 1) continue;
    if (r == m.clockRow + 1 && (c == 1 || c == 2)) continue;   // the ghost exit, and pacman's start below the clock
    if (m.tun[r] && c == 0) continue;
    if (random(2)) { m.node[r][c] = false; m.node[r][4-c] = false; }
  }

  // one switch per mirrored pair of corridors: h[r][0..1] with its mirror h[r][3..2], v[r][0..2] with v[r][4..2]
  bool hOpen[MAP_ROWS][2] = {};
  bool vOpen[MAP_ROWS-1][3] = {};
  bool hOk[MAP_ROWS][2] = {};
  bool vOk[MAP_ROWS-1][3] = {};
  int closedPct = 20 + random(26);
  for (int r = 0; r < MAP_ROWS; r++) {
    for (int c = 0; c < 2; c++) {
      hOk[r][c] = m.node[r][c] && m.node[r][c+1];
      hOpen[r][c] = hOk[r][c] && random(100) >= closedPct;
    }
  }
  for (int r = 0; r < MAP_ROWS-1; r++) {
    for (int c = 0; c < 3; c++) {
      vOk[r][c] = m.node[r][c] && m.node[r+1][c];
      vOpen[r][c] = vOk[r][c] && random(100) >= closedPct;
    }
  }

  auto expand = [&]() {
    for (int r = 0; r < MAP_ROWS; r++) {
      for (int c = 0; c < 2; c++) { m.h[r][c] = hOpen[r][c]; m.h[r][3-c] = hOpen[r][c]; }
    }
    for (int r = 0; r < MAP_ROWS-1; r++) {
      for (int c = 0; c < 3; c++) { m.v[r][c] = vOpen[r][c]; m.v[r][4-c] = vOpen[r][c]; }
    }
  };
  expand();

  // junctions with fewer than 2 corridors: open a closed one next to them
  for (int round = 0; round < 60; round++) {
    int bad[25], nbad = 0;
    for (int r = 0; r < MAP_ROWS; r++) {
      for (int c = 0; c < MAP_COLS; c++) {
        if (m.node[r][c] && mazeDegree(m.h, m.v, m.tun, r, c) < 2) bad[nbad++] = r * MAP_COLS + c;
      }
    }
    if (!nbad) break;
    int pick = bad[random(nbad)];
    int r = pick / MAP_COLS, c = pick % MAP_COLS;
    // the closed switches around it: left, right, up, down
    bool* sw[4]; int n = 0;
    if (c > 0)            { int k = c-1; int u = k <= 1 ? k : 3-k; if (hOk[r][u] && !hOpen[r][u]) sw[n++] = &hOpen[r][u]; }
    if (c < 4)            { int k = c;   int u = k <= 1 ? k : 3-k; if (hOk[r][u] && !hOpen[r][u]) sw[n++] = &hOpen[r][u]; }
    if (r > 0)            { int u = c <= 2 ? c : 4-c; if (vOk[r-1][u] && !vOpen[r-1][u]) sw[n++] = &vOpen[r-1][u]; }
    if (r < MAP_ROWS-1)   { int u = c <= 2 ? c : 4-c; if (vOk[r][u] && !vOpen[r][u]) sw[n++] = &vOpen[r][u]; }
    if (!n) return false;
    *sw[random(n)] = true;
    expand();
  }

  // black areas that are too big: open another corridor (or bring back a left-out junction with its corridors).
  // Everything is mirrored, so a black area on one side is halved on both sides, and one in the middle is split by a corridor down the middle.
  // Each round takes a random change that leaves fewer black pixels over the limit.
  // candidates: 0..9 a closed horizontal corridor, 10..21 a closed vertical one, 22..46 a left-out junction
  auto apply = [&](int cand) {
    if (cand < 10) { hOpen[cand / 2][cand % 2] = true; return; }
    if (cand < 22) { vOpen[(cand - 10) / 3][(cand - 10) % 3] = true; return; }
    int r = (cand - 22) / 5, c = (cand - 22) % 5, u = c <= 2 ? c : 4-c;
    m.node[r][c] = m.node[r][4-c] = true;
    for (int rr = 0; rr < MAP_ROWS; rr++) for (int cc = 0; cc < 2; cc++) hOk[rr][cc] = m.node[rr][cc] && m.node[rr][cc+1];
    for (int rr = 0; rr < MAP_ROWS-1; rr++) for (int cc = 0; cc < 3; cc++) vOk[rr][cc] = m.node[rr][cc] && m.node[rr+1][cc];
    for (int cc = 0; cc < 2; cc++) if (hOk[r][cc] && (cc == u || cc+1 == u)) hOpen[r][cc] = true;
    for (int rr = 0; rr < MAP_ROWS-1; rr++) if (vOk[rr][u] && (rr == r || rr+1 == r)) vOpen[rr][u] = true;
  };
  for (int round = 0; round < 20; round++) {
    int before = emptyExcess(m.node, m.h, m.v, m.tun, m.clockRow), better[47], nbetter = 0;
    if (!before) break;
    bool nodeSave[MAP_ROWS][MAP_COLS], hOkSave[MAP_ROWS][2], vOkSave[MAP_ROWS-1][3], hSave[MAP_ROWS][2], vSave[MAP_ROWS-1][3];
    memcpy(nodeSave, m.node, sizeof(nodeSave));
    memcpy(hOkSave, hOk, sizeof(hOkSave));
    memcpy(vOkSave, vOk, sizeof(vOkSave));
    memcpy(hSave, hOpen, sizeof(hSave));
    memcpy(vSave, vOpen, sizeof(vSave));
    auto restore = [&]() {
      memcpy(m.node, nodeSave, sizeof(nodeSave));
      memcpy(hOk, hOkSave, sizeof(hOkSave));
      memcpy(vOk, vOkSave, sizeof(vOkSave));
      memcpy(hOpen, hSave, sizeof(hSave));
      memcpy(vOpen, vSave, sizeof(vSave));
    };
    for (int cand = 0; cand < 47; cand++) {
      if (cand < 10) {
        if (!hOk[cand / 2][cand % 2] || hOpen[cand / 2][cand % 2]) continue;
      } else if (cand < 22) {
        if (!vOk[(cand - 10) / 3][(cand - 10) % 3] || vOpen[(cand - 10) / 3][(cand - 10) % 3]) continue;
      } else {
        int r = (cand - 22) / 5, c = (cand - 22) % 5;
        if (m.node[r][c] || (r == m.clockRow && c >= 1 && c <= 3)) continue;   // not the clock
      }
      apply(cand);
      expand();
      int size = emptyExcess(m.node, m.h, m.v, m.tun, m.clockRow);
      if (size < before) better[nbetter++] = cand;
      restore();
    }
    if (!nbetter) return false;
    apply(better[random(nbetter)]);
    expand();
  }
  if (emptyExcess(m.node, m.h, m.v, m.tun, m.clockRow) > 0) return false;

  // about 1 in 3 mazes: break the symmetry by flipping 1 to 3 corridors on one side only.
  // The checks below (2+ corridors per junction, reachability) and the black-area check here throw out a flip that goes wrong.
  if (random(3) == 0) {
    for (int k = 1 + random(3); k > 0; k--) {
      if (random(2)) {
        int r = random(MAP_ROWS), c = random(MAP_COLS-1);
        if (m.node[r][c] && m.node[r][c+1]) m.h[r][c] = !m.h[r][c];
      } else {
        int r = random(MAP_ROWS-1), c = random(MAP_COLS);
        if (m.node[r][c] && m.node[r+1][c]) m.v[r][c] = !m.v[r][c];
      }
    }
    if (emptyExcess(m.node, m.h, m.v, m.tun, m.clockRow) > 0) return false;
  }

  // every junction ok, and all of them reachable from the ghost exit (the tunnel joins its two ends)
  int total = 0;
  for (int r = 0; r < MAP_ROWS; r++) {
    for (int c = 0; c < MAP_COLS; c++) {
      if (!m.node[r][c]) continue;
      total++;
      if (mazeDegree(m.h, m.v, m.tun, r, c) < 2) return false;
    }
  }
  bool seen[MAP_ROWS][MAP_COLS] = {};
  int stack[25], sp = 0, reached = 0;
  stack[sp++] = (m.clockRow + 1) * MAP_COLS + 1;
  seen[m.clockRow + 1][1] = true;
  while (sp) {
    int cur = stack[--sp];
    int r = cur / MAP_COLS, c = cur % MAP_COLS;
    reached++;
    int nr[5], nc[5], n = 0;
    if (c > 0 && m.h[r][c-1]) { nr[n] = r; nc[n++] = c-1; }
    if (c < 4 && m.h[r][c])   { nr[n] = r; nc[n++] = c+1; }
    if (r > 0 && m.v[r-1][c]) { nr[n] = r-1; nc[n++] = c; }
    if (r < 4 && m.v[r][c])   { nr[n] = r+1; nc[n++] = c; }
    if (m.tun[r] && (c == 0 || c == 4)) { nr[n] = r; nc[n++] = 4-c; }
    for (int i = 0; i < n; i++) {
      if (!seen[nr[i]][nc[i]]) { seen[nr[i]][nc[i]] = true; stack[sp++] = nr[i] * MAP_COLS + nc[i]; }
    }
  }
  return reached == total;
}

// how many corridors (a mirrored pair counts once), junctions and tunnels differ
int Clockface::mazeDiff(const Maze& a, const Maze& b) {
  int d = 0;
  for (int r = 0; r < MAP_ROWS; r++) {
    for (int c = 0; c < MAP_COLS; c++) d += a.node[r][c] != b.node[r][c];
    for (int c = 0; c < MAP_COLS-1; c++) d += a.h[r][c] != b.h[r][c];
    d += a.tun[r] != b.tun[r];
  }
  for (int r = 0; r < MAP_ROWS-1; r++) {
    for (int c = 0; c < MAP_COLS; c++) d += a.v[r][c] != b.v[r][c];
  }
  d += (a.clockRow != b.clockRow) * 6;
  return d / 2;
}

// the maze as FALLBACK_MAP text, with a power pellet in every corner (next to it when that junction is left out) and pacman below the clock
bool Clockface::mazeToLevel(const Maze& m) {
  for (int i = 0; i < 11; i++) {
    memset(_levelRows[i], ' ', 11);
    _levelRows[i][11] = '\0';
    _levelRowPtrs[i] = _levelRows[i];
  }
  for (int r = 0; r < MAP_ROWS; r++) {
    char* line = _levelRows[1+2*r];
    if (m.tun[r]) { line[0] = '='; line[10] = '='; }
    for (int c = 0; c < MAP_COLS; c++) {
      if (m.node[r][c]) line[1+2*c] = '.';
      if (c < MAP_COLS-1 && m.h[r][c]) line[2+2*c] = '-';
      if (r < MAP_ROWS-1 && m.v[r][c]) _levelRows[2+2*r][1+2*c] = '|';
    }
  }

  for (int k = 0; k < 4; k++) {
    int cr = k < 2 ? 0 : 4, cc = k % 2 ? 4 : 0;
    int pick = cr * MAP_COLS + cc;
    if (!m.node[cr][cc]) {
      int cand[2], n = 0;
      for (int r = 0; r < MAP_ROWS; r++) {
        for (int c = 0; c < MAP_COLS; c++) {
          if (m.node[r][c] && abs(r - cr) + abs(c - cc) == 1) cand[n++] = r * MAP_COLS + c;
        }
      }
      if (!n) return false;
      pick = cand[random(n)];
    }
    _levelRows[1+2*(pick / MAP_COLS)][1+2*(pick % MAP_COLS)] = 'o';
  }

  _levelRows[1+2*(m.clockRow+1)][1+2*2] = 'P';   // always the same start, right below the clock
  return true;
}

// make a new random maze that is not too much like the last one, and load it; false when nothing came out (use the fallback)
bool Clockface::generateLevel() {
  for (int attempt = 0; attempt < 300; attempt++) {
    Maze m;
    if (!randomMaze(m)) continue;
    if (_hasPrevMaze && mazeDiff(m, _prevMaze) < MIN_MAZE_DIFF) continue;
    if (!mazeToLevel(m)) continue;
    _prevMaze = m;
    _hasPrevMaze = true;
    loadLevel(_levelRowPtrs, m.clockRow);
    return true;
  }
  _hasPrevMaze = false;
  return false;
}

// Along every corridor between two junctions: a dot every 4px (3px apart), starting with the junction's own dot
void Clockface::buildPills() {
  _pillCount = 0;

  for (int r = 0; r < MAP_ROWS; r++) {
    for (int c = 0; c < MAP_COLS; c++) {
      if (_MAP[r][c] == MapBlock::WALL) continue;

      for (int v = 0; v < 2; v++) {
        if (!(v ? _LINK_V[r][c] : _LINK_H[r][c])) continue;

        int len = v ? ROW_Y[r+1] - ROW_Y[r] : COL_X[c+1] - COL_X[c];
        for (int along = 4; along < len && _pillCount < MAX_PILLS; along += 4) {
          Pill& p = _pills[_pillCount++];
          p.x = v ? COL_X[c] + 2 : COL_X[c] + 2 + along;
          p.y = v ? ROW_Y[r] + 2 + along : ROW_Y[r] + 2;
          p.row = r;
          p.col = c;
          p.vertical = v;
          p.alive = true;
        }
      }
    }
  }
}

// the dots: the colour from the settings, or the default
uint16_t Clockface::dotColor() {
  uint16_t color = ClockwiseParams::getInstance()->pacmanColor(ClockwiseParams::getInstance()->dotColor);
  return color ? color : DEFAULT_DOT_COLOR;
}

// the ghosts: a colour from the settings, or one of the pastels that is not close to the wall or to the other ghost
void Clockface::applyGhostColors() {
  ClockwiseParams* params = ClockwiseParams::getInstance();
  uint16_t wall = params->wallColor();
  uint16_t first = params->pacmanColor(params->ghost1Color);
  if (!first) first = ghostColor(0, wall, 0x0000);
  uint16_t second = params->pacmanColor(params->ghost2Color);
  if (!second) second = ghostColor(1, wall, first);
  _ghosts[0].init(first);
  _ghosts[1].init(second);
}

// the colours can be changed on the settings page while the face runs
void Clockface::refreshColors() {
  ClockwiseParams* params = ClockwiseParams::getInstance();
  uint32_t key = params->ghost1Color | (params->ghost2Color << 8) | (params->dotColor << 16) | ((uint32_t)params->color << 24);
  if (key == _colorKey) return;
  _colorKey = key;

  applyGhostColors();
  drawWalls(params->wallColor());
  updateClock();

  for (int r = 0; r < MAP_ROWS; r++) {
    for (int c = 0; c < MAP_COLS; c++) {
      if (_MAP[r][c] == MapBlock::FOOD) drawFoodBlock(r, c);
    }
  }
  for (int i = 0; i < _pillCount; i++) {
    if (_pills[i].alive) drawPill(_pills[i]);
  }
}

void Clockface::drawPill(const Pill& p) {
  Locator::getDisplay()->drawPixel(p.x, p.y, dotColor());
}

// pacman covers a pill, even partly: eaten
void Clockface::eatPills() {
  int px = pacman->getX();
  int py = pacman->getY();
  for (int i = 0; i < _pillCount; i++) {
    Pill& p = _pills[i];
    if (!p.alive) continue;
    if (p.x >= px && p.x < px + 5 && p.y >= py && p.y < py + 5) {
      p.alive = false;
    }
  }
}

int Clockface::pillsLeft() {
  int n = 0;
  for (int i = 0; i < _pillCount; i++) if (_pills[i].alive) n++;
  return n;
}

// is there food left in the corridor leading from this junction in this direction
bool Clockface::linkHasPills(int row, int col, Direction dir) {
  bool vertical = dir == Direction::UP || dir == Direction::DOWN;
  int r = dir == Direction::UP ? row - 1 : row;
  int c = dir == Direction::LEFT ? col - 1 : col;
  for (int i = 0; i < _pillCount; i++) {
    if (_pills[i].alive && _pills[i].row == r && _pills[i].col == c && _pills[i].vertical == vertical) return true;
  }
  return false;
}

void Clockface::drawMap() 
{
  Locator::getDisplay()->fillRect(0, 0, 64, 64, 0x0000);

  uint16_t wall_color = ClockwiseParams::getInstance()->wallColor();

  for (int j=0; j<MAP_ROWS; j++) {
    for (int i=0; i<MAP_COLS; i++) {
      if (_MAP[j][i] == MapBlock::FOOD || _MAP[j][i] == MapBlock::SUPER_FOOD) {
        drawFoodBlock(j, i);
      } else if (_MAP[j][i] == MapBlock::PACMAN) {
        delete pacman;  // from the previous map
        pacman = new Pacman(COL_X[i], ROW_Y[j]);
        _pacmanStartX = COL_X[i];
        _pacmanStartY = ROW_Y[j];
      }
    }
  }

  for (int i = 0; i < _pillCount; i++) {
    drawPill(_pills[i]);
  }

  drawWalls(wall_color);

  resetGhosts();
}

// Everything that is not corridor (or the clock box inside) is solid, and drawn as an outline 2px thick:
// a 2px gap between two corridors becomes one thin line, a bigger block becomes hollow.
void Clockface::drawWalls(uint16_t color) {
  Adafruit_GFX* d = Locator::getDisplay();
  uint64_t open[64] = {};   // one bit per pixel, bit x of row y

  auto rect = [&](int x0, int y0, int x1, int y1) {
    uint64_t bits = (x1 - x0 >= 63 ? ~0ULL : ((1ULL << (x1 - x0 + 1)) - 1)) << x0;
    for (int y = max(y0, 0); y <= min(y1, 63); y++) open[y] |= bits;
  };

  for (int r = 0; r < MAP_ROWS; r++) {
    for (int c = 0; c < MAP_COLS; c++) {
      if (_MAP[r][c] == MapBlock::WALL) continue;
      rect(COL_X[c], ROW_Y[r], COL_X[c] + 4, ROW_Y[r] + 4);
      if (_LINK_H[r][c]) rect(COL_X[c] + 5, ROW_Y[r], COL_X[c+1] - 1, ROW_Y[r] + 4);
      if (_LINK_V[r][c]) rect(COL_X[c], ROW_Y[r] + 5, COL_X[c] + 4, ROW_Y[r+1] - 1);
    }
    if (_TUNNEL[r]) {   // the tunnel runs out to the edge of the screen
      rect(0, ROW_Y[r], COL_X[0], ROW_Y[r] + 4);
      rect(COL_X[MAP_COLS-1] + 4, ROW_Y[r], 63, ROW_Y[r] + 4);
    }
  }
  for (int c = 0; c < MAP_COLS; c++) {
    if (_TUNNEL_V[c]) {   // up and down to the edge of the screen
      rect(COL_X[c], 0, COL_X[c] + 4, ROW_Y[0]);
      rect(COL_X[c], ROW_Y[MAP_ROWS-1] + 4, COL_X[c] + 4, 63);
    }
  }
  rect(13, ROW_Y[_clockRow] - 5, 51, ROW_Y[_clockRow] + 9);  // inside the clock box: 39x15, the time is 34x11

  for (int y = 0; y < 64; y++) {
    uint64_t nearOpen = 0;   // pixels with corridor within 2px
    for (int k = max(y - 2, 0); k <= min(y + 2, 63); k++) nearOpen |= open[k];
    nearOpen |= (nearOpen << 1) | (nearOpen << 2) | (nearOpen >> 1) | (nearOpen >> 2);
    uint64_t wall = nearOpen & ~open[y];

    for (int x = 0; x < 64; x++) {
      if (!((wall >> x) & 1)) continue;
      int len = 1;
      while (x + len < 64 && ((wall >> (x + len)) & 1)) len++;
      d->drawFastHLine(x, y, len, color);
      x += len;
    }
  }
}

void Clockface::drawFoodBlock(int row, int col) {
  if (_MAP[row][col] == MapBlock::FOOD) {
    Locator::getDisplay()->drawPixel(COL_X[col]+2, ROW_Y[row]+2, dotColor());
  } else if (_MAP[row][col] == MapBlock::SUPER_FOOD) {
    Locator::getDisplay()->fillRect(COL_X[col]+1, ROW_Y[row]+1, 3, 3, 0xFBE0);
  }
}

// squared distance between two RGB565 colours
static int colorDistance(uint16_t a, uint16_t b) {
  int dr = (int)((a >> 11) & 0x1F) * 8 - (int)((b >> 11) & 0x1F) * 8;
  int dg = (int)((a >> 5) & 0x3F) * 4 - (int)((b >> 5) & 0x3F) * 4;
  int db = (int)(a & 0x1F) * 8 - (int)(b & 0x1F) * 8;
  return dr*dr + dg*dg + db*db;
}

// the first pastel from 'first' on that is not close to the wall, or to the other ghost
static uint16_t ghostColor(int first, uint16_t wall, uint16_t other) {
  static const uint16_t PASTELS[] = {0x9FD3, 0xFDDF, 0x965F, 0xFFB2, 0xFDF2, 0xD4FF};  // green, pink, blue, lemon, peach, purple
  static const int COUNT = sizeof(PASTELS) / sizeof(PASTELS[0]);
  static const int CLOSE = 70 * 70;

  for (int i = 0; i < COUNT; i++) {
    uint16_t color = PASTELS[(first + i) % COUNT];
    if (colorDistance(color, wall) > CLOSE && colorDistance(color, other) > CLOSE) return color;
  }
  return PASTELS[first];
}

void Clockface::resetGhosts() {
  // both ghosts wait on the clock and leave through the bottom wall, one after the other
  applyGhostColors();
  _ghostsFrightened = false;

  for (int i = 0; i < GHOST_COUNT; i++) {
    if (_ghostExiting[i]) closeHole(i);
    _ghostExiting[i] = false;
    _ghosts[i].reset(ghostHouseX(i), _clockY + 3, Direction::DOWN);
    _ghostHouseUntil[i] = millis() + GHOUSE_MS;
    _ghostGraceUntil[i] = millis() + GRACE_MS;  // pacman starts close by
  }
  _nextExitAt = 0;

  updateClock();
  for (int i = 0; i < GHOST_COUNT; i++) {
    _ghosts[i].draw(false, false);
  }
}

bool Clockface::ghostInHouse() {
  for (int i = 0; i < GHOST_COUNT; i++) {
    if (_ghostHouseUntil[i] || _ghostExiting[i]) return true;
  }
  return false;
}

// eaten: the ghost sits on the clock for a moment, the clock dims and the date goes
void Clockface::sendGhostHome(int i) {
  eraseGhost(_ghosts[i]);
  _ghostsFrightened = false;
  _ghosts[i].reset(ghostHouseX(i), _clockY + 3, Direction::DOWN);
  _ghostHouseUntil[i] = millis() + GHOUSE_MS;
  updateClock();
}

// erase the ghost and put back the food it was covering
void Clockface::eraseGhost(Ghost& ghost) {
  int x = ghost.getX();
  int y = ghost.getY();

  Locator::getDisplay()->fillRect(x, y, Ghost::SPRITE_SIZE, Ghost::SPRITE_SIZE, 0);

  for (int row = 0; row < MAP_ROWS; row++) {
    for (int col = 0; col < MAP_COLS; col++) {
      if (abs(x - COL_X[col]) < Ghost::SPRITE_SIZE && abs(y - ROW_Y[row]) < Ghost::SPRITE_SIZE) {
        drawFoodBlock(row, col);
      }
    }
  }

  for (int i = 0; i < _pillCount; i++) {
    const Pill& p = _pills[i];
    if (p.alive && p.x < x + Ghost::SPRITE_SIZE && p.x >= x && p.y >= y && p.y < y + Ghost::SPRITE_SIZE) {
      drawPill(p);
    }
  }
}

// what is next to a junction: a wall when there is no corridor that way, out of the map past the edge
Clockface::MapBlock Clockface::blockAt(int row, int col, Direction dir) {
  if (dir == Direction::RIGHT) {
    if (col >= MAP_COLS-1) return MapBlock::OUT_OF_MAP;
    return _LINK_H[row][col] ? static_cast<MapBlock>(_MAP[row][col+1]) : MapBlock::WALL;
  } else if (dir == Direction::LEFT) {
    if (col <= 0) return MapBlock::OUT_OF_MAP;
    return _LINK_H[row][col-1] ? static_cast<MapBlock>(_MAP[row][col-1]) : MapBlock::WALL;
  } else if (dir == Direction::DOWN) {
    if (row >= MAP_ROWS-1) return MapBlock::OUT_OF_MAP;
    return _LINK_V[row][col] ? static_cast<MapBlock>(_MAP[row+1][col]) : MapBlock::WALL;
  }

  if (row <= 0) return MapBlock::OUT_OF_MAP;
  return _LINK_V[row-1][col] ? static_cast<MapBlock>(_MAP[row-1][col]) : MapBlock::WALL;
}

void Clockface::openHole(int i) {
  Locator::getDisplay()->fillRect(ghostHouseX(i), _holeY, Ghost::SPRITE_SIZE, 2, 0);
}

void Clockface::closeHole(int i) {
  Locator::getDisplay()->fillRect(ghostHouseX(i), _holeY, Ghost::SPRITE_SIZE, 2, ClockwiseParams::getInstance()->wallColor());
}

// straight down through the hole onto the lane below, then the wall closes behind it
void Clockface::exitGhost(int i) {
  Ghost& ghost = _ghosts[i];
  eraseGhost(ghost);
  ghost._direction = Direction::DOWN;
  ghost.move();

  if (ghost.getY() < _holeY) {
    updateClock(false);  // the ghost wiped part of the time on its way: draw over it, no blank frame
  }
  if (ghost.getY() >= ROW_Y[_clockRow+1]) {
    closeHole(i);
    _ghostExiting[i] = false;
    _ghostGraceUntil[i] = millis() + GRACE_MS;
    _nextExitAt = millis() + EXIT_GAP_MS;
    updateClock();  // undim when it was the last one
  }
}

void Clockface::moveGhost(Ghost& ghost) {
  int gcol = colAt(ghost.getX());
  int grow = rowAt(ghost.getY());

  // on a junction: pick a new direction, never straight back unless stuck
  if (gcol >= 0 && grow >= 0) {
    Ghost probe = ghost;
    probe.reverse();
    Direction back = probe._direction;

    Direction options[4];
    int count = 0;
    for (int d = 0; d < 4; d++) {
      Direction dir = static_cast<Direction>(d);
      if (dir != back && !contains(blockAt(grow, gcol, dir), PACMAN_BLOCKING_BLOCKS)) {
        options[count++] = dir;
      }
    }

    ghost._direction = count > 0 ? options[random(count)] : back;
  }

  eraseGhost(ghost);
  ghost.move();
}

void Clockface::updateGhosts() {
  bool frightened = pacman->_state == Pacman::State::INVENCIBLE;

  // ghosts turn around when pacman powers up
  if (frightened && !_ghostsFrightened) {
    for (int i = 0; i < GHOST_COUNT; i++) {
      _ghosts[i].reverse();
    }
  }
  _ghostsFrightened = frightened;

  static unsigned long lastGhostMillis = 0;
  if (millis() - lastGhostMillis >= (frightened ? 100 : 80)) {
    for (int i = 0; i < GHOST_COUNT; i++) {
      if (_ghostHouseUntil[i]) continue;
      if (_ghostExiting[i]) exitGhost(i); else moveGhost(_ghosts[i]);
    }
    lastGhostMillis = millis();
  }

  // time is up: open the wall under the ghost and let it out, one ghost at a time with a pause in between
  bool anyExiting = false;
  for (int i = 0; i < GHOST_COUNT; i++) {
    if (_ghostExiting[i]) anyExiting = true;
  }
  for (int i = 0; i < GHOST_COUNT && !anyExiting; i++) {
    if (_ghostHouseUntil[i] && millis() >= _ghostHouseUntil[i] && millis() >= _nextExitAt) {
      _ghostHouseUntil[i] = 0;
      _ghostExiting[i] = true;
      anyExiting = true;
      openHole(i);
    }
  }

  bool flash = frightened && pacman->invincibleLeft() < 2000 && (millis() / 200) % 2 == 0;
  for (int i = 0; i < GHOST_COUNT; i++) {
    _ghosts[i].draw(frightened && !_ghostHouseUntil[i], flash);
  }
}

void Clockface::checkGhostCollisions() {
  for (int i = 0; i < GHOST_COUNT; i++) {
    if (_ghostHouseUntil[i] || _ghostExiting[i] || millis() < _ghostGraceUntil[i]) continue;
    if (abs(_ghosts[i].getX() - pacman->getX()) < 4 && abs(_ghosts[i].getY() - pacman->getY()) < 4) {

      if (pacman->_state == Pacman::State::INVENCIBLE) {
        // eaten: back to the middle, then the start
        sendGhostHome(i);
      } else {
        _deathUntil = millis() + 1200;
        _deathBlink = 0;
        return;
      }
    }
  }
}

void Clockface::updateDeath() {
  if (millis() < _deathUntil) {
    if (millis() - _deathBlink >= 150) {
      static bool visible = false;
      if (visible) {
        pacman->init();
      } else {
        Locator::getDisplay()->fillRect(pacman->getX(), pacman->getY(), pacman->SPRITE_SIZE, pacman->SPRITE_SIZE, 0);
      }
      visible = !visible;
      _deathBlink = millis();
    }
    return;
  }

  // respawn pacman, ghosts go back home
  Locator::getDisplay()->fillRect(pacman->getX(), pacman->getY(), pacman->SPRITE_SIZE, pacman->SPRITE_SIZE, 0);
  for (int i = 0; i < GHOST_COUNT; i++) {
    eraseGhost(_ghosts[i]);
  }
  pacman->respawn(_pacmanStartX, _pacmanStartY);
  resetGhosts();
  _deathUntil = 0;
}

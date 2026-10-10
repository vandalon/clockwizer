#include "strip.h"

// The clock box in the middle (border included), as wide as the time needs; whatever is behind it shows dimmed
static const int BOX_Y = 10, BOX_H = 11;  // 2 px of margin round the 5 rows of the time
static const uint16_t BOX_COLOR = 0x9CD3;  // not the colour of the walls

static const float PACMAN_SPEED = 18;  // pixels per second
static const unsigned long WARP_MS = 1000;  // going through a tunnel takes this long
static const float GHOST_SPEED = 12.5;
static const float SCARED_SPEED = 10;
static const unsigned long TICK_MS = 40;
static const unsigned long POWER_MS = 10000;
static const unsigned long DEATH_MS = 1200;
static const unsigned long GHOST_BACK_MS = 3000;
static const uint16_t PELLET_COLOR = 0xFBE0;
static const uint16_t PACMAN_COLOR = 0xFE40;

// 5x5 sprites, one byte per row, bit 4 is the leftmost pixel; they face right
static const uint8_t PACMAN_CLOSED[5] = {0b01110, 0b11011, 0b11111, 0b11111, 0b01110};
static const uint8_t PACMAN_OPEN[5] = {0b01111, 0b11010, 0b11100, 0b11110, 0b01111};
static const uint8_t GHOST_BODY[5] = {0b01110, 0b11111, 0b11111, 0b11111, 0b10101};

typedef BigMaze M;

PacmanStrip::PacmanStrip() {}

// ---- Where everybody is ----

void PacmanStrip::position(const Walker& w, float& x, float& y) {
  x = M::jx(w.c) + (w.dir == M::RIGHT ? w.t : w.dir == M::LEFT ? -w.t : 0);
  y = M::jx(w.r) + (w.dir == M::DOWN ? w.t : w.dir == M::UP ? -w.t : 0);
}

// is the corridor he is on the one across the seam of the world: a tunnel
bool PacmanStrip::inTunnel(const Walker& w) {
  return (w.dir == M::RIGHT && w.c == M::N - 1) || (w.dir == M::LEFT && w.c == 0) ||
         (w.dir == M::DOWN && w.r == M::N - 1) || (w.dir == M::UP && w.r == 0);
}

// the shortest way from a to b on the world that wraps, in pixels
float PacmanStrip::wrapDiff(float d) { return d - M::SIZE * floorf((d + M::SIZE / 2) / M::SIZE); }
int PacmanStrip::wrapDist(float a, float b) { return (int)fabsf(wrapDiff(a - b)); }

// how far (as the crow flies, along both axes) a spot is from somebody
int PacmanStrip::distance(const Walker& a, float x, float y) const {
  float ax, ay;
  position(a, ax, ay);
  return wrapDist(ax, x) + wrapDist(ay, y);
}

void PacmanStrip::reverse(Walker& w) {
  w.r = M::step(w.r, (w.dir == M::DOWN) - (w.dir == M::UP));
  w.c = M::step(w.c, (w.dir == M::RIGHT) - (w.dir == M::LEFT));
  w.dir = (w.dir + 2) % 4;
  w.t = M::PITCH - w.t;
}

void PacmanStrip::placeRandom(Walker& w) {
  do {
    w.r = random(M::N);
    w.c = random(M::N);
  } while (!_maze.node[w.r][w.c]);
  int dirs[4], n = 0;
  for (int d = 0; d < 4; d++)
    if (_maze.linkOpen(w.r, w.c, d)) dirs[n++] = d;
  w.dir = dirs[random(n)];
  w.t = 0;
}

// somewhere at least minDist away from Pacman (the best of a few tries)
void PacmanStrip::placeFar(Walker& w, int minDist) {
  float px, py;
  position(_pac, px, py);
  for (int tries = 0; tries < 40; tries++) {
    placeRandom(w);
    if (distance(w, px, py) >= minDist) return;
  }
}

// ---- Starting over ----

void PacmanStrip::begin() {
  _last = millis();
  startLevel(_last, true);
}

void PacmanStrip::setColors(uint16_t wall, uint16_t ghost1, uint16_t ghost2, uint16_t dot) {
  _wall = wall;
  _ghostColor[0] = ghost1;
  _ghostColor[1] = ghost2;
  _dot = dot;
}

void PacmanStrip::startLevel(unsigned long now, bool newMaze) {
  if (newMaze) _maze.generate();
  respawn(now);
}

// Pacman somewhere new, the ghosts a good way off
void PacmanStrip::respawn(unsigned long now) {
  _warpStart = 0;
  _warp = 0;
  _mazeVis = 255;
  _powerUntil = 0;
  _deathUntil = 0;
  placeRandom(_pac);
  _wasInTunnel = inTunnel(_pac);  // when he starts in a tunnel he just walks through it
  for (int i = 0; i < GHOSTS; i++) {
    placeFar(_ghosts[i].w, 56);
    _ghosts[i].hidden = false;
    _ghosts[i].graceUntil = now + 1000;
  }
  follow(0, true);
}

// ---- Moving ----

// one step along the corridor; at the next junction who is to choose a way (0 Pacman, 1.. a ghost)
void PacmanStrip::advance(Walker& w, float dist, unsigned long now, int who) {
  w.t += dist;
  if (w.t < M::PITCH) return;
  w.t -= M::PITCH;
  w.r = M::step(w.r, (w.dir == M::DOWN) - (w.dir == M::UP));
  w.c = M::step(w.c, (w.dir == M::RIGHT) - (w.dir == M::LEFT));
  if (who == 0) decidePacman(now);
  else decideGhost(_ghosts[who - 1], now);
}

// At a junction, score every open way and take the best: food nearby is good, a ghost nearby is bad (good while
// powered up), going straight on is slightly preferred and turning back is not. A dash of noise, and Pacman only
// looks out for ghosts 3 times in 4, so he is no genius. The same as the 64x64 Pacman.
void PacmanStrip::decidePacman(unsigned long now) {
  int r = _pac.r, c = _pac.c, cur = _pac.dir, back = (cur + 2) % 4;
  bool powered = now < _powerUntil, lookOut = random(100) < 75;

  // the junctions that have food on them or in a corridor that starts there
  uint8_t anchors[M::N * M::N * 5][2];  // a junction adds up to 5
  int count = 0;
  for (int rr = 0; rr < M::N; rr++) {
    for (int cc = 0; cc < M::N; cc++) {
      if (!_maze.node[rr][cc]) continue;
      if (_maze.nodeHasFood(rr, cc)) {
        anchors[count][0] = rr;
        anchors[count++][1] = cc;
      }
      if (_maze.linkHasDots(rr, cc, M::RIGHT)) {
        anchors[count][0] = rr;
        anchors[count++][1] = cc;
        anchors[count][0] = rr;
        anchors[count++][1] = M::step(cc, 1);
      }
      if (_maze.linkHasDots(rr, cc, M::DOWN)) {
        anchors[count][0] = rr;
        anchors[count++][1] = cc;
        anchors[count][0] = M::step(rr, 1);
        anchors[count++][1] = cc;
      }
    }
  }

  int best = -1, bestScore = -10000;
  for (int d = 0; d < 4; d++) {
    if (!_maze.linkOpen(r, c, d)) continue;
    int nr = M::step(r, (d == M::DOWN) - (d == M::UP)), nc = M::step(c, (d == M::RIGHT) - (d == M::LEFT));

    int score = random(12);
    if (d == cur) score += 4;
    if (d == back) score -= 8;
    if (_maze.linkHasDots(r, c, d)) score += 8;
    if (_maze.nodeHasFood(nr, nc)) score += (_maze.at(M::jx(nc) + 2, M::jx(nr) + 2) & M::PELLET) ? 14 : 10;

    int nearest = 99;
    for (int i = 0; i < count; i++) {
      int dr = abs(anchors[i][0] - nr), dc = abs(anchors[i][1] - nc);
      nearest = min(nearest, min(dr, M::N - dr) + min(dc, M::N - dc));
    }
    score -= nearest < 99 ? nearest * 2 : 0;

    if (lookOut || powered) {
      for (int i = 0; i < GHOSTS; i++) {
        if (_ghosts[i].hidden) continue;
        int dist = distance(_ghosts[i].w, M::jx(nc), M::jx(nr));
        if (dist < 28) score += (powered ? 1 : -1) * (28 - dist) * 2;
      }
    }

    if (score > bestScore) {
      bestScore = score;
      best = d;
    }
  }
  if (best >= 0) _pac.dir = best;
}

// A ghost picks a way at random, never straight back unless it is stuck. Now and then, when Pacman is not
// powered up, it takes the way that gets it closer to him.
void PacmanStrip::decideGhost(Ghost& g, unsigned long now) {
  int back = (g.w.dir + 2) % 4, opts[4], n = 0;
  for (int d = 0; d < 4; d++)
    if (d != back && _maze.linkOpen(g.w.r, g.w.c, d)) opts[n++] = d;
  if (!n) {
    g.w.dir = back;
    return;
  }
  int pick = opts[random(n)];
  if (now >= _powerUntil && random(100) < 35) {
    float px, py;
    position(_pac, px, py);
    int bestDist = 100000;
    for (int i = 0; i < n; i++) {
      int nr = M::step(g.w.r, (opts[i] == M::DOWN) - (opts[i] == M::UP)), nc = M::step(g.w.c, (opts[i] == M::RIGHT) - (opts[i] == M::LEFT));
      int dist = wrapDist(M::jx(nc), px) + wrapDist(M::jx(nr), py);
      if (dist < bestDist) {
        bestDist = dist;
        pick = opts[i];
      }
    }
  }
  g.w.dir = pick;
}

// Where on the screen Pacman is held when he goes in a direction: in the middle of the space between the clock
// and the edge of the screen that way (right of the clock when he goes right, above it when he goes up, ...)
float PacmanStrip::holdX(int dir) const {
  if (dir == M::RIGHT) return _boxX + _boxW + 2;
  if (dir == M::LEFT) return _boxX - 5 - 2;
  return (WIDTH - 5) / 2.0f;
}

float PacmanStrip::holdY(int dir) const {
  if (dir == M::DOWN) return BOX_Y + BOX_H + 1;
  if (dir == M::UP) return BOX_Y - 5 - 1;
  return (HEIGHT - 5) / 2.0f;
}

// The screen only follows Pacman when he goes outward: once he has got to the middle of the space between the clock
// and the edge he is heading for, he stays there and the maze scrolls. While he goes inward (after a turn, over the
// screen and behind the clock) the screen stands still. At once with snap: he is somewhere new.
void PacmanStrip::follow(float dt, bool snap) {
  float px, py;
  position(_pac, px, py);
  int dir = _pac.dir;

  if (snap) {
    _camX = px - holdX(dir);
    _camY = py - holdY(dir);
  } else {
    float relX = wrapDiff(px - _camX), relY = wrapDiff(py - _camY);
    if (dir == M::RIGHT) _camX += max(0.0f, relX - holdX(dir));
    else if (dir == M::LEFT) _camX += min(0.0f, relX - holdX(dir));
    else if (dir == M::DOWN) _camY += max(0.0f, relY - holdY(dir));
    else _camY += min(0.0f, relY - holdY(dir));
  }
  _camX = M::mod((int)floorf(_camX * 16), M::SIZE * 16) / 16.0f;
  _camY = M::mod((int)floorf(_camY * 16), M::SIZE * 16) / 16.0f;
}

void PacmanStrip::startWarp(unsigned long now) {
  _warpStart = now;
  _warpMoved = false;
  _warpT = 0;
  _warpDx = (_pac.dir == M::RIGHT) - (_pac.dir == M::LEFT);
  _warpDy = (_pac.dir == M::DOWN) - (_pac.dir == M::UP);
}

void PacmanStrip::warpStep(unsigned long now) {
  float t = (now - _warpStart) / (float)WARP_MS;
  if (t >= 1) {
    _warpStart = 0;
    _warp = 0;
    _mazeVis = 255;
    _wasInTunnel = inTunnel(_pac);
    return;
  }

  // the maze is out of view for most of it; Pacman is at the other end when it comes back
  if (!_warpMoved && t >= 0.88f) {
    _warpMoved = true;
    for (int k = 6; k <= 10; k += 4) {  // the dots in the tunnel
      Walker w = _pac;
      w.t = k;
      float x, y;
      position(w, x, y);
      bool pellet;
      _maze.eat((int)floorf(x), (int)floorf(y), pellet);
    }
    _pac.r = M::step(_pac.r, _warpDy);
    _pac.c = M::step(_pac.c, _warpDx);
    _pac.t = 0;
    decidePacman(now);
    follow(0, true);
  }
  _warpT = t;
  _warp = sinf(3.14159f * t);
  _mazeVis = t < 0.12f ? (int)(255 * (1 - t / 0.12f)) : (t > 0.88f ? (int)(255 * (t - 0.88f) / 0.12f) : 0);
}

void PacmanStrip::step(float dt, unsigned long now) {
  if (_deathUntil) {
    if (now >= _deathUntil) respawn(now);  // Pacman starts over somewhere else
    return;
  }

  if (_warpStart) {  // everything waits for the warp
    warpStep(now);
    return;
  }

  bool powered = now < _powerUntil;
  advance(_pac, PACMAN_SPEED * dt, now, 0);

  float px, py;
  position(_pac, px, py);
  bool pellet;
  _maze.eat((int)floorf(px), (int)floorf(py), pellet);
  if (pellet) {
    _powerUntil = now + POWER_MS;
    for (int i = 0; i < GHOSTS; i++)
      if (!_ghosts[i].hidden) reverse(_ghosts[i].w);  // the ghosts turn round
  }
  if (!_maze.pills()) {  // all eaten: a new maze
    startLevel(now, true);
    return;
  }

  bool tunnel = inTunnel(_pac);
  if (tunnel && !_wasInTunnel) {
    _wasInTunnel = true;
    startWarp(now);
    follow(dt, false);
    return;
  }
  _wasInTunnel = tunnel;

  for (int i = 0; i < GHOSTS; i++) {
    Ghost& g = _ghosts[i];
    if (g.hidden) {
      if (now >= g.backAt) {
        g.hidden = false;
        placeFar(g.w, 48);
        g.graceUntil = now + 1000;
      }
      continue;
    }
    advance(g.w, (powered ? SCARED_SPEED : GHOST_SPEED) * dt, now, i + 1);

    float gx, gy;
    position(g.w, gx, gy);
    if (now >= g.graceUntil && wrapDist(gx, px) < 4 && wrapDist(gy, py) < 4) {
      if (powered) {
        g.hidden = true;  // eaten: gone for a bit, then back from somewhere else
        g.backAt = now + GHOST_BACK_MS;
      } else {
        _deathUntil = now + DEATH_MS;
        return;
      }
    }
  }

  follow(dt, false);
}

// ---- Drawing ----

// What is on top of the maze this frame: the ghosts, then Pacman
void PacmanStrip::collectSprites(unsigned long now) {
  _spriteCount = 0;
  int cx = (int)floorf(_camX), cy = (int)floorf(_camY);
  bool powered = now < _powerUntil;
  bool flash = powered && _powerUntil - now < 2000 && (now / 200) % 2 == 0;

  // the world wraps: a sprite is where it is closest to the screen, as far as 16 px off the left or the top
  auto screen = [&](float wx, float wy, int& sx, int& sy) {
    sx = M::mod((int)floorf(wx) - cx + 16, M::SIZE) - 16;
    sy = M::mod((int)floorf(wy) - cy + 16, M::SIZE) - 16;
  };

  bool warping = _warpStart != 0, warpView = warping && _warpT >= 0.12f && _warpT < 0.88f;

  for (int i = 0; i < GHOSTS; i++) {
    const Ghost& g = _ghosts[i];
    if (g.hidden || warping) continue;
    float x, y;
    position(g.w, x, y);
    Sprite5& sp = _sprites[_spriteCount++];
    screen(x, y, sp.x, sp.y);
    sp.kind = 'g';
    sp.facing = 'r';
    sp.body = _ghostColor[i];
    sp.eye = 0xFFFF;
    if (powered) {
      sp.body = flash ? 0xFFFF : 0x001F;
      sp.eye = flash ? 0xF800 : 0xFFFF;
    }
  }

  float x, y;
  position(_pac, x, y);
  int px, py;
  screen(x, y, px, py);
  if (warpView) {  // in the warp he rushes along a row above the clock, or a column beside it
    px = _warpDx ? 30 : 8;
    py = _warpDx ? 3 : 13;
  }
  if (warping) {  // a trail of fading copies behind him
    for (int k = 3; k >= 1; k--) {
      Sprite5& t = _sprites[_spriteCount++];
      t.x = px - _warpDx * 3 * k;
      t.y = py - _warpDy * 3 * k;
      t.kind = 'p';
      t.facing = _warpDy < 0 ? 'u' : _warpDy > 0 ? 'd' : _warpDx < 0 ? 'l' : 'r';
      t.open = true;
      uint8_t level = (uint8_t)(150 * max(_warp, 0.3f) / k);
      t.body = ((level >> 3) << 11) | ((level >> 2) << 5) | (level >> 3);
      t.body |= 0x001F * (level > 60);  // a bluish tail
    }
  }
  Sprite5& pac = _sprites[_spriteCount++];
  pac.x = px;
  pac.y = py;
  pac.kind = 'p';
  pac.facing = _pac.dir == M::UP ? 'u' : _pac.dir == M::DOWN ? 'd' : _pac.dir == M::LEFT ? 'l' : 'r';
  pac.open = (now / 150) % 2 == 0;
  pac.body = PACMAN_COLOR;
  if (_deathUntil && (now / 150) % 2) pac.x = -100;  // blinks when he is caught
}

bool PacmanStrip::spritePixel(int sx, int sy, uint16_t& color) const {
  for (int i = _spriteCount - 1; i >= 0; i--) {  // Pacman is the last one: on top
    const Sprite5& sp = _sprites[i];
    int c = sx - sp.x, r = sy - sp.y;
    if (c < 0 || c >= 5 || r < 0 || r >= 5) continue;

    if (sp.kind == 'p') {
      const uint8_t* rows = sp.open ? PACMAN_OPEN : PACMAN_CLOSED;
      int sr = r, sc = c;  // the pixel of the right-facing picture that ends up here
      if (sp.facing == 'l') sc = 4 - c;
      else if (sp.facing == 'd') { sr = 4 - c; sc = r; }
      else if (sp.facing == 'u') { sr = c; sc = 4 - r; }
      if ((rows[sr] >> (4 - sc)) & 1) {
        color = sp.body;
        return true;
      }
    } else {
      if (r == 2 && (c == 1 || c == 3)) {
        color = sp.eye;
        return true;
      }
      if ((GHOST_BODY[r] >> (4 - c)) & 1) {
        color = sp.body;
        return true;
      }
    }
  }
  return false;
}

uint16_t PacmanStrip::backgroundPixel(int camX, int camY, int sx, int sy) const {
  if (_mazeVis <= 0) return 0;
  uint8_t p = _maze.at(camX + sx, camY + sy);
  uint16_t color = 0;
  switch (p & M::TYPE) {
    case M::WALL: color = _wall; break;
    case M::FLOOR:
      if (p & M::DOT) color = _dot;
      else if ((p & M::PELLET) && (_frameMs / 250) % 2 == 0) color = PELLET_COLOR;
      break;
    default: break;
  }
  if (color && _mazeVis < 255) {  // fading in or out
    int r = ((color >> 11) & 31) * _mazeVis / 255, g = ((color >> 5) & 63) * _mazeVis / 255, b = (color & 31) * _mazeVis / 255;
    color = (r << 11) | (g << 5) | b;
  }
  return color;
}

// Speed lines across the screen while Pacman is in a tunnel: short streaks that rush the other way
uint16_t PacmanStrip::warpPixel(int sx, int sy, uint16_t base) const {
  if (_warp < 0.05f) return base;
  bool across = _warpDx != 0;  // lines run in the direction he goes
  int line = across ? sy : sx, pos = across ? sx : sy, span = across ? WIDTH : HEIGHT;
  uint32_t hs = (uint32_t)line * 2654435761u;
  hs ^= hs >> 15;
  if (hs % (_mazeVis < 40 ? 2 : 3)) return base;  // more lines when the maze is out of view
  int len = 8 + (hs >> 4) % 14, speed = 3 + (hs >> 9) % 5;
  int sign = -(_warpDx + _warpDy);  // against his direction
  int travelled = (int)((_frameMs * speed / 16) % (span + len));
  int start = (int)(((hs >> 12) % (span + len) + sign * travelled + 4 * (span + len)) % (span + len)) - len;
  int along = pos - start;
  if (along < 0 || along >= len) return base;
  int bright = (sign > 0 ? along : len - 1 - along) * 255 / len;  // brightest at the head of the streak
  int k = bright * (int)(_warp * 200) / 255;
  uint16_t streak = 0x7FFF;  // light cyan
  int r = (((base >> 11) & 31) * (255 - k) + ((streak >> 11) & 31) * k) / 255;
  int g = (((base >> 5) & 63) * (255 - k) + ((streak >> 5) & 63) * k) / 255;
  int b = ((base & 31) * (255 - k) + (streak & 31) * k) / 255;
  return (r << 11) | (g << 5) | b;
}

// half the brightness, for what is behind the clock
static uint16_t dimmed(uint16_t c) { return (c >> 1) & 0x7BEF; }

// A rectangle of the screen, built in the buffer and put on the display in one go, so no flicker
void PacmanStrip::blit(int x, int y, int w, int h) {
  int cam = (int)floorf(_camX), camY = (int)floorf(_camY);
  const int innerW = _boxW - 2, innerH = BOX_H - 2;
  for (int r = 0; r < h; r++) {
    for (int c = 0; c < w; c++) {
      int sx = x + c, sy = y + r;
      uint16_t color = 0;
      bool sprite = spritePixel(sx, sy, color);
      int ix = sx - _boxX - 1, iy = sy - BOX_Y - 1;
      if (ix >= 0 && ix < innerW && iy >= 0 && iy < innerH) {
        uint16_t text = _clock ? _clock[iy * _clockStride + ix] : 0;
        // everything behind the clock shows, dimmed: Pacman, the ghosts, the walls and the dots
        color = text ? text : dimmed(sprite ? color : backgroundPixel(cam, camY, sx, sy));
      } else if (sx >= _boxX && sx < _boxX + _boxW && sy >= BOX_Y && sy < BOX_Y + BOX_H) {
        color = BOX_COLOR;
      } else if (!sprite) {
        color = warpPixel(sx, sy, backgroundPixel(cam, camY, sx, sy));
      }
      _buf[r * w + c] = color;
    }
  }
  Locator::getDisplay()->drawRGBBitmap(x, y, _buf, w, h);
}

void PacmanStrip::update() {
  unsigned long now = millis();
  if (now - _last < TICK_MS) return;
  float dt = (now - _last) / 1000.0f;
  if (dt > 0.2f) dt = 0.2f;
  _last = now;
  _frameMs = now;

  step(dt, now);
  collectSprites(now);

  blit(0, 0, WIDTH, HEIGHT / 2);
  blit(0, HEIGHT / 2, WIDTH, HEIGHT / 2);
}

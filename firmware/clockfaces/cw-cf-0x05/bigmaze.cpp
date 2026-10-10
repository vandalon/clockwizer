#include "bigmaze.h"

static const uint8_t NEAR = 0x10;     // while the walls are made: corridor within 2 px to the left or right
static const uint8_t VISITED = 0x20;  // while the black areas are measured
static const uint8_t OUTSIDE = 0x40;  // black between the outer walls of the two ends of the world: not a black area

BigMaze::BigMaze() {
  _map = (uint8_t*)calloc(SIZE * SIZE, 1);
  memset(node, 0, sizeof(node));
  memset(h, 0, sizeof(h));
  memset(v, 0, sizeof(v));
  memset(_tunH, 0, sizeof(_tunH));
  memset(_tunV, 0, sizeof(_tunV));
}

BigMaze::~BigMaze() {
  free(_map);
  free(_queue);
}

// ---- Looking at the maze ----

bool BigMaze::linkOpen(int r, int c, int dir) const {
  switch (dir) {
    case UP: return v[step(r, -1)][c];
    case RIGHT: return h[r][c];
    case DOWN: return v[r][c];
    default: return h[r][step(c, -1)];
  }
}

bool BigMaze::linkHasDots(int r, int c, int dir) const {
  if (!linkOpen(r, c, dir)) return false;
  // the corridor is stored from its top or left junction; the dots lie 6 and 10 px along it
  int rr = dir == UP ? step(r, -1) : r, cc = dir == LEFT ? step(c, -1) : c;
  bool down = dir == UP || dir == DOWN;
  for (int k = 1; k <= 2; k++) {
    int x = jx(cc) + 2 + (down ? 0 : 4 * k), y = jx(rr) + 2 + (down ? 4 * k : 0);
    if (at(x, y) & DOT) return true;
  }
  return false;
}

bool BigMaze::nodeHasFood(int r, int c) const { return node[r][c] && (at(jx(c) + 2, jx(r) + 2) & (DOT | PELLET)); }

int BigMaze::eat(int x, int y, bool& pellet) {
  int eaten = 0;
  pellet = false;
  for (int dy = 0; dy < 5; dy++) {
    for (int dx = 0; dx < 5; dx++) {
      uint8_t& p = _map[mod(y + dy, SIZE) * SIZE + mod(x + dx, SIZE)];
      if (p & DOT) {
        p &= ~DOT;
        eaten++;
      } else if (p & PELLET) {
        // the whole pellet goes: the 3x3 pixels that start 1 px into the junction
        int px = mod(x + dx, SIZE), py = mod(y + dy, SIZE);
        int ox = px - mod(px - OFFSET, PITCH) + 1, oy = py - mod(py - OFFSET, PITCH) + 1;
        for (int j = 0; j < 3; j++)
          for (int i = 0; i < 3; i++) _map[mod(oy + j, SIZE) * SIZE + mod(ox + i, SIZE)] &= ~PELLET;
        pellet = true;
        eaten++;
      }
    }
  }
  _pills -= eaten;
  return eaten;
}

// ---- Changing the maze: with sym, the mirror image changes with it ----

void BigMaze::setNode(int r, int c, bool on, bool sym) {
  node[r][c] = on;
  if (!on) {
    h[r][c] = h[r][step(c, -1)] = false;
    v[r][c] = v[step(r, -1)][c] = false;
  }
  if (sym && N - 1 - c != c) setNode(r, N - 1 - c, on, false);
}

void BigMaze::setH(int r, int c, bool on, bool sym) {
  if (on && !(canOpenH(r, c) && node[r][c] && node[r][step(c, 1)])) return;
  h[r][c] = on;
  int mc = mod(N - 2 - c, N);
  if (sym) h[r][mc] = on && canOpenH(r, mc) && node[r][mc] && node[r][mod(N - 1 - c, N)];
}

void BigMaze::setV(int r, int c, bool on, bool sym) {
  if (on && !(canOpenV(r, c) && node[r][c] && node[step(r, 1)][c])) return;
  v[r][c] = on;
  if (sym) v[r][N - 1 - c] = on && canOpenV(r, N - 1 - c) && node[r][N - 1 - c] && node[step(r, 1)][N - 1 - c];
}

int BigMaze::degree(int r, int c) const {
  return h[r][c] + h[r][step(c, -1)] + v[r][c] + v[step(r, -1)][c];
}

// ---- The pixels: corridors, and walls 2 px round them (the same as drawWalls() of the 64x64 maze) ----

void BigMaze::buildMap() {
  memset(_map, 0, SIZE * SIZE);
  auto rect = [&](int x0, int y0, int w, int hh) {
    for (int y = 0; y < hh; y++)
      for (int x = 0; x < w; x++) _map[mod(y0 + y, SIZE) * SIZE + mod(x0 + x, SIZE)] = FLOOR;
  };
  for (int r = 0; r < N; r++) {
    for (int c = 0; c < N; c++) {
      if (!node[r][c]) continue;
      rect(jx(c), jx(r), 5, 5);
      if (h[r][c]) rect(jx(c) + 5, jx(r), PITCH - 5, 5);
      if (v[r][c]) rect(jx(c), jx(r) + 5, 5, PITCH - 5);
    }
  }

  // wall: not corridor, but corridor within 2 px (a square, so the corners are round the way they are in 64x64)
  for (int y = 0; y < SIZE; y++) {
    for (int x = 0; x < SIZE; x++) {
      for (int k = -2; k <= 2; k++) {
        if ((_map[y * SIZE + mod(x + k, SIZE)] & TYPE) == FLOOR) {
          _map[y * SIZE + x] |= NEAR;
          break;
        }
      }
    }
  }
  for (int y = 0; y < SIZE; y++) {
    for (int x = 0; x < SIZE; x++) {
      if ((_map[y * SIZE + x] & TYPE) == FLOOR) continue;
      for (int k = -2; k <= 2; k++) {
        if (_map[mod(y + k, SIZE) * SIZE + x] & NEAR) {
          _map[y * SIZE + x] |= WALL;
          break;
        }
      }
    }
  }
  for (int i = 0; i < SIZE * SIZE; i++) _map[i] &= ~NEAR;

  // the black strip between the outer wall at one end of the world and the one at the other end
  for (int y = 0; y < SIZE; y++) {
    for (int x = 0; x < SIZE; x++) {
      if (_map[y * SIZE + x] & TYPE) continue;
      int mx = mod(x - OFFSET, SIZE), my = mod(y - OFFSET, SIZE);
      if ((mx >= SIZE - 5 && mx <= SIZE - 3) || (my >= SIZE - 5 && my <= SIZE - 3))  // the 3 px between the two walls
        _map[y * SIZE + x] |= OUTSIDE;
    }
  }
}

// ---- Black areas: pixels that are neither corridor nor wall ----

// the size of the black area that has this pixel (and, with collect, the junctions round every pixel of it in _queue's wake)
int BigMaze::flood(int sx, int sy, bool collect) {
  int head = 0, tail = 0, size = 0;
  _queue[tail++] = sy * SIZE + sx;
  _map[sy * SIZE + sx] |= VISITED;
  static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
  while (head < tail) {
    int p = _queue[head++], x = p % SIZE, y = p / SIZE;
    size++;
    for (int d = 0; d < 4; d++) {
      int nx = mod(x + DX[d], SIZE), ny = mod(y + DY[d], SIZE);
      uint8_t& q = _map[ny * SIZE + nx];
      if ((q & (TYPE | VISITED | OUTSIDE)) == 0) {
        q |= VISITED;
        _queue[tail++] = ny * SIZE + nx;
      }
    }
  }
  return collect ? tail : size;
}

// the size of the biggest black area, and a pixel of it
int BigMaze::biggestBlank(int& sx, int& sy) {
  if (!_queue) _queue = (uint16_t*)malloc(SIZE * SIZE * sizeof(uint16_t));
  int best = 0;
  for (int y = 0; y < SIZE; y++) {
    for (int x = 0; x < SIZE; x++) {
      if (_map[y * SIZE + x] & (TYPE | VISITED | OUTSIDE)) continue;
      int size = flood(x, y, false);
      if (size > best) {
        best = size;
        sx = x;
        sy = y;
      }
    }
  }
  for (int i = 0; i < SIZE * SIZE; i++) _map[i] &= ~VISITED;
  return best;
}

// ---- Ways to fix a black area: open a corridor round it, or bring back a junction that was left out ----
// ids: 0..99 a closed corridor to the right, 100..199 one down, 200..299 a junction

int BigMaze::collectCandidates(int sx, int sy, int* out) {
  int size = flood(sx, sy, true);
  bool cell[N][N] = {};
  for (int i = 0; i < size; i++) {
    int x = _queue[i] % SIZE, y = _queue[i] / SIZE;
    cell[((y - OFFSET + SIZE) % SIZE) / PITCH][((x - OFFSET + SIZE) % SIZE) / PITCH] = true;
  }
  for (int i = 0; i < SIZE * SIZE; i++) _map[i] &= ~VISITED;

  bool seen[300] = {};
  int n = 0;
  auto add = [&](int id) {
    if (!seen[id]) {
      seen[id] = true;
      out[n++] = id;
    }
  };
  for (int r = 0; r < N; r++) {
    for (int c = 0; c < N; c++) {
      if (!cell[r][c]) continue;
      int r1 = step(r, 1), c1 = step(c, 1);
      const int rs[4] = {r, r, r1, r1}, cs[4] = {c, c1, c, c1};
      for (int k = 0; k < 4; k++)
        if (!node[rs[k]][cs[k]]) add(200 + rs[k] * N + cs[k]);
      if (canOpenH(r, c) && node[r][c] && node[r][c1] && !h[r][c]) add(r * N + c);
      if (canOpenH(r1, c) && node[r1][c] && node[r1][c1] && !h[r1][c]) add(r1 * N + c);
      if (canOpenV(r, c) && node[r][c] && node[r1][c] && !v[r][c]) add(100 + r * N + c);
      if (canOpenV(r, c1) && node[r][c1] && node[r1][c1] && !v[r][c1]) add(100 + r * N + c1);
    }
  }
  return n;
}

void BigMaze::applyCandidate(int id, bool sym) {
  if (id < 100) {
    setH(id / N, id % N, true, sym);
  } else if (id < 200) {
    setV((id - 100) / N, (id - 100) % N, true, sym);
  } else {
    // a junction that is back: 2 of the corridors to its neighbours open
    int r = (id - 200) / N, c = (id - 200) % N;
    setNode(r, c, true, sym);
    int opened = 0;
    for (int tries = 0; tries < 12 && opened < 2; tries++) {
      int d = random(4);
      if (linkOpen(r, c, d)) continue;
      if (!node[step(r, (d == DOWN) - (d == UP))][step(c, (d == RIGHT) - (d == LEFT))]) continue;
      if (d == UP) setV(step(r, -1), c, true, sym);
      else if (d == DOWN) setV(r, c, true, sym);
      else if (d == LEFT) setH(r, step(c, -1), true, sym);
      else setH(r, c, true, sym);
      if (linkOpen(r, c, d)) opened++;
    }
  }
}

// ---- Making a maze ----

bool BigMaze::connected() const {
  bool seen[N][N] = {};
  int stack[N * N], sp = 0, total = 0, reached = 0;
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++) total += node[r][c];
  for (int r = 0; r < N && !sp; r++)
    for (int c = 0; c < N && !sp; c++)
      if (node[r][c]) {
        stack[sp++] = r * N + c;
        seen[r][c] = true;
      }
  while (sp) {
    int cur = stack[--sp], r = cur / N, c = cur % N;
    reached++;
    for (int d = 0; d < 4; d++) {
      if (!linkOpen(r, c, d)) continue;
      int nr = step(r, (d == DOWN) - (d == UP)), nc = step(c, (d == RIGHT) - (d == LEFT));
      if (!seen[nr][nc]) {
        seen[nr][nc] = true;
        stack[sp++] = nr * N + nc;
      }
    }
  }
  return reached == total;
}

bool BigMaze::tryGenerate() {
  // 2 mazes in 3 are mirrored left/right
  bool sym = random(3) != 0;
  memset(h, 0, sizeof(h));
  memset(v, 0, sizeof(v));
  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++) node[r][c] = true;

  // none, one or two tunnels, on the rows or columns that are not at the edge and never next to each other
  memset(_tunH, 0, sizeof(_tunH));
  memset(_tunV, 0, sizeof(_tunV));
  int roll = random(100), tunnels = roll < 30 ? 0 : roll < 70 ? 1 : 2;
  for (int k = 0; k < tunnels; k++) {
    int i = 1 + random(N - 2);
    if (random(2)) {
      if (!_tunH[i - 1] && !_tunH[i + 1]) _tunH[i] = true;
    } else if (!_tunV[i - 1] && !_tunV[i + 1] && !_tunV[N - 1 - i]) {
      _tunV[i] = true;
      if (sym) _tunV[N - 1 - i] = true;
    }
  }

  // leave out a few junctions, but not the ends of a tunnel
  for (int k = random(7); k > 0; k--) {
    int r = random(N), c = random(N);
    bool end = ((c == 0 || c == N - 1) && _tunH[r]) || ((r == 0 || r == N - 1) && _tunV[c]) || ((r == 0 || r == N - 1) && sym && _tunV[N - 1 - c]);
    if (!end) setNode(r, c, false, sym);
  }

  // open most of the corridors
  int closedPct = 20 + random(26);
  for (int r = 0; r < N; r++) {
    for (int c = 0; c < N; c++) {
      setH(r, c, c == N - 1 ? _tunH[r] : random(100) >= closedPct, sym);
      setV(r, c, r == N - 1 ? _tunV[c] : random(100) >= closedPct, sym);
    }
  }

  // junctions on fewer than 2 corridors: open a closed one next to them
  for (int round = 0; round < 300; round++) {
    int bad[N * N], nbad = 0;
    for (int r = 0; r < N; r++)
      for (int c = 0; c < N; c++)
        if (node[r][c] && degree(r, c) < 2) bad[nbad++] = r * N + c;
    if (!nbad) break;
    int pick = bad[random(nbad)], r = pick / N, c = pick % N;
    int opts[4], n = 0;
    for (int d = 0; d < 4; d++) {
      int nr = step(r, (d == DOWN) - (d == UP)), nc = step(c, (d == RIGHT) - (d == LEFT));
      bool allowed = d == UP ? canOpenV(step(r, -1), c) : d == DOWN ? canOpenV(r, c) : d == LEFT ? canOpenH(r, step(c, -1)) : canOpenH(r, c);
      if (!linkOpen(r, c, d) && node[nr][nc] && allowed) opts[n++] = d;
    }
    if (!n) return false;
    int d = opts[random(n)];
    if (d == UP) setV(step(r, -1), c, true, sym);
    else if (d == DOWN) setV(r, c, true, sym);
    else if (d == LEFT) setH(r, step(c, -1), true, sym);
    else setH(r, c, true, sym);
  }

  // black areas that are too big: open a corridor or bring back a junction round them, until there are none
  int sx = 0, sy = 0;
  for (int round = 0; round < 80; round++) {
    buildMap();
    if (biggestBlank(sx, sy) <= MAX_EMPTY_PX) break;
    int cand[300];
    int n = collectCandidates(sx, sy, cand);
    if (!n) return false;
    applyCandidate(cand[random(n)], sym);
    if (round == 79) return false;
  }

  // 1 in 3 of the mirrored mazes: break the symmetry with 1 to 3 corridors flipped on one side only
  if (sym && random(3) == 0) {
    for (int k = 1 + random(3); k > 0; k--) {
      int r = random(N), c = random(N);
      if (random(2)) setH(r, c, !h[r][c], false);
      else setV(r, c, !v[r][c], false);
    }
    buildMap();
    if (biggestBlank(sx, sy) > MAX_EMPTY_PX) return false;
  }

  for (int r = 0; r < N; r++)
    for (int c = 0; c < N; c++)
      if (node[r][c] && degree(r, c) < 2) return false;
  return connected();
}

// a dot on every junction and every 4 px along the corridors (3 px apart), and a power pellet in each quarter
void BigMaze::placeFood() {
  _pills = 0;
  for (int r = 0; r < N; r++) {
    for (int c = 0; c < N; c++) {
      if (!node[r][c]) continue;
      _map[mod(jx(r) + 2, SIZE) * SIZE + mod(jx(c) + 2, SIZE)] |= DOT;
      _pills++;
      for (int k = 1; k <= 2; k++) {
        if (h[r][c]) {
          _map[mod(jx(r) + 2, SIZE) * SIZE + mod(jx(c) + 2 + 4 * k, SIZE)] |= DOT;
          _pills++;
        }
        if (v[r][c]) {
          _map[mod(jx(r) + 2 + 4 * k, SIZE) * SIZE + mod(jx(c) + 2, SIZE)] |= DOT;
          _pills++;
        }
      }
    }
  }

  for (int q = 0; q < 4; q++) {
    int tr = q < 2 ? 2 : 7, tc = q % 2 ? 7 : 2, best = 1000, br = tr, bc = tc;
    for (int r = 0; r < N; r++) {
      for (int c = 0; c < N; c++) {
        int dist = min(abs(r - tr), N - abs(r - tr)) + min(abs(c - tc), N - abs(c - tc));
        if (node[r][c] && dist < best) {
          best = dist;
          br = r;
          bc = c;
        }
      }
    }
    uint8_t& centre = _map[mod(jx(br) + 2, SIZE) * SIZE + mod(jx(bc) + 2, SIZE)];
    if (centre & PELLET) continue;
    if (centre & DOT) {
      centre &= ~DOT;
      _pills--;
    }
    for (int j = 1; j <= 3; j++)
      for (int i = 1; i <= 3; i++) _map[mod(jx(br) + j, SIZE) * SIZE + mod(jx(bc) + i, SIZE)] |= PELLET;
    _pills++;
  }
}

bool BigMaze::generate() {
  bool random_maze = false;
  for (int attempt = 0; attempt < 40 && !random_maze; attempt++) {
    delay(1);  // let the rest of the clock run
    random_maze = tryGenerate();
  }
  if (!random_maze) {  // a plain grid always works
    for (int r = 0; r < N; r++)
      for (int c = 0; c < N; c++) node[r][c] = h[r][c] = v[r][c] = true;
  }
  buildMap();
  placeFood();
  free(_queue);  // only needed to measure black areas
  _queue = nullptr;
  return random_maze;
}

#pragma once

#include <Arduino.h>

// The maze of the Pacman clockface on a 64x32 panel: a bigger version of the 64x64 maze that the screen
// scrolls through. Same rules as the 64x64 one: junctions 12 px apart joined by corridors 5 px wide with walls
// of 2 px round them, mostly mirrored left/right, junctions that are left out, every junction on at least 2
// corridors, no big black areas, everything reachable. It is 10x10 junctions, 120x120 px (a multiple of 12 is
// what makes it fit together). The world wraps, but the maze has a wall all round it with 0 to 2 tunnels:
// corridors across the seam of the world, so what leaves on one side comes back on the other.
class BigMaze {
  public:
    static const int N = 10;                // junctions along each side
    static const int PITCH = 12;            // between two junctions
    static const int SIZE = N * PITCH;      // the world
    static const int OFFSET = 6;            // where junction 0 starts, like in the 64x64 maze
    static const int MAX_EMPTY_PX = 48;     // the biggest black area allowed

    // what a pixel of the map holds: what it is, and what lies on it
    enum : uint8_t { WALL = 1, FLOOR = 2, TYPE = 3, DOT = 4, PELLET = 8 };
    enum Dir { UP, RIGHT, DOWN, LEFT };

    BigMaze();
    ~BigMaze();

    // Makes a new random maze with its dots and power pellets. False when it gave up and made a plain grid.
    bool generate();

    bool node[N][N];   // is there a junction
    bool h[N][N];      // corridor from a junction to the one on its right (the last column joins the first: a tunnel)
    bool v[N][N];      // to the one below it (the last row joins the first: a tunnel)

    static int jx(int i) { return OFFSET + PITCH * i; }  // where junction i starts, along either axis
    static int mod(int a, int b) { return ((a % b) + b) % b; }
    static int step(int i, int d) { return mod(i + d, N); }

    uint8_t at(int x, int y) const { return _map[mod(y, SIZE) * SIZE + mod(x, SIZE)]; }
    bool linkOpen(int r, int c, int dir) const;
    bool linkHasDots(int r, int c, int dir) const;
    bool nodeHasFood(int r, int c) const;
    // eats what lies in the 5x5 box that starts at x, y; returns how many dots there were (a pellet counts as 1)
    int eat(int x, int y, bool& pellet);
    int pills() const { return _pills; }

  private:
    uint8_t* _map;
    uint16_t* _queue = nullptr;
    int _pills = 0;
    bool _tunH[N], _tunV[N];  // the tunnels of the maze that is being made: rows and columns

    bool canOpenH(int r, int c) const { return c != N - 1 || _tunH[r]; }  // across the seam only for a tunnel
    bool canOpenV(int r, int c) const { return r != N - 1 || _tunV[c]; }
    void setNode(int r, int c, bool on, bool sym);
    void setH(int r, int c, bool on, bool sym);
    void setV(int r, int c, bool on, bool sym);
    int degree(int r, int c) const;
    bool tryGenerate();
    bool connected() const;
    void buildMap();
    int biggestBlank(int& sx, int& sy);
    int collectCandidates(int sx, int sy, int* out);
    void applyCandidate(int id, bool sym);
    void placeFood();
    int flood(int sx, int sy, bool collect);
};

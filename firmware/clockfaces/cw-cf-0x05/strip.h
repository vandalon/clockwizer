#pragma once

#include <Arduino.h>
#include <Locator.h>

#include "bigmaze.h"

// Pacman on a 64x32 panel, where the 64x64 maze doesn't fit: the screen scrolls through a bigger maze
// (BigMaze, 120x120 px) that Pacman and the ghosts play in, like in the 64x64 version. The clock sits in a
// box in the middle of the screen; whatever passes behind it, Pacman too, shows dimmed.
class PacmanStrip {
  public:
    PacmanStrip();
    void begin();
    void setColors(uint16_t wall, uint16_t ghost1, uint16_t ghost2, uint16_t dot);
    // The picture of the time that goes in the clock box: innerWidth x 9 pixels in rows of 'stride' (black = see through).
    // The box is as wide as the time with its margin. Whoever hands it over keeps it and may change it at any
    // time: the maze, Pacman and the ghosts show dimmed behind it.
    void setClock(const uint16_t* pixels, int stride, int innerWidth) {
      _clock = pixels;
      _clockStride = stride;
      _boxW = innerWidth + 2;
      _boxX = (WIDTH - _boxW) / 2;
    }
    void update();

  private:
    static const int WIDTH = 64;
    static const int HEIGHT = 32;
    static const int GHOSTS = 2;

    // somebody on a corridor: the junction he left, the way he goes, and how far along the corridor he is
    struct Walker {
      int r = 0, c = 0;
      int dir = BigMaze::RIGHT;
      float t = 0;
    };
    struct Ghost {
      Walker w;
      bool hidden = false;         // eaten, until backAt
      unsigned long backAt = 0;
      unsigned long graceUntil = 0;  // can't catch Pacman (or be caught) for a moment after coming back
    };
    // what draws on top of the maze
    struct Sprite5 {
      int x, y;
      char kind;     // 'p' Pacman, 'g' ghost
      char facing;   // 'r', 'l', 'u', 'd'
      bool open;
      uint16_t body, eye;
    };

    BigMaze _maze;
    uint16_t _wall = 0x0016, _dot = 0xB58C;
    uint16_t _ghostColor[GHOSTS] = {0x9FD3, 0xFDDF};
    const uint16_t* _clock = nullptr;
    int _clockStride = 0;
    int _boxX = 18, _boxW = 28;  // the clock box, border included

    Walker _pac;
    Ghost _ghosts[GHOSTS];
    unsigned long _powerUntil = 0, _deathUntil = 0;
    float _camX = 0, _camY = 0;  // the world point at the top left of the screen
    // Going through a tunnel is a warp of a second: the maze fades out, the speed lines swell up and Pacman rushes
    // along a tunnel that seems very long, then the maze fades back in at the other end. Everything else waits.
    float _warp = 0;             // 0 normally, up to 1 in the middle of the warp
    float _warpT = 0;            // how far the warp is, 0 to 1
    unsigned long _warpStart = 0;  // 0 = no warp
    bool _warpMoved = false;     // Pacman is at the other end already
    bool _wasInTunnel = false;
    int _mazeVis = 255;          // how much of the maze shows
    int _warpDx = 0, _warpDy = 0;  // the way Pacman goes then
    unsigned long _last = 0, _frameMs = 0;
    uint16_t _buf[WIDTH * 16];  // the biggest piece of the screen that is drawn at once is 64x16
    Sprite5 _sprites[GHOSTS + 1 + 3];  // the ghosts, Pacman, and the trail he leaves in a tunnel
    int _spriteCount = 0;

    static void position(const Walker& w, float& x, float& y);
    static bool inTunnel(const Walker& w);
    uint16_t warpPixel(int sx, int sy, uint16_t base) const;
    static int wrapDist(float a, float b);
    static float wrapDiff(float d);
    int distance(const Walker& a, float x, float y) const;
    void advance(Walker& w, float dist, unsigned long now, int who);
    void reverse(Walker& w);
    void placeRandom(Walker& w);
    void placeFar(Walker& w, int minDist);
    void startLevel(unsigned long now, bool newMaze);
    void respawn(unsigned long now);
    void decidePacman(unsigned long now);
    void decideGhost(Ghost& g, unsigned long now);
    void step(float dt, unsigned long now);
    void startWarp(unsigned long now);
    void warpStep(unsigned long now);
    void follow(float dt, bool snap);
    float holdX(int dir) const;
    float holdY(int dir) const;
    void collectSprites(unsigned long now);
    bool spritePixel(int sx, int sy, uint16_t& color) const;
    uint16_t backgroundPixel(int camX, int camY, int sx, int sy) const;
    void blit(int x, int y, int w, int h);
};

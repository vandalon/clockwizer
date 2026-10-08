#pragma once

#include <Arduino.h>


#include "hour_font.h"
#include "../cw-commons/picopixel.h"

#include <Adafruit_GFX.h>
#include <Tile.h>
#include <Locator.h>
#include <Game.h>
#include <Object.h>
#include <ImageUtils.h>
#include <ColorUtil.h>
#include "IClockface.h"

//sprites
#include "pacman.h"
#include "ghost.h"


class Clockface: public IClockface {
  private:
    // junctions of the maze: 5 columns x 5 rows, the clock box takes the middle of row _clockRow (1..3)
    enum { MAP_COLS = 5, MAP_ROWS = 5 };
    Adafruit_GFX* _display;
    CWDateTime* _dateTime;
    bool pacmanState = true;
    bool show_seconds = true;
    // 0 = bright, 255 = fully dimmed; eases towards the target so the clock fades in and out
    int _dimLevel = 0;
    unsigned long lastDimStep = 0;
    static const unsigned long DIM_STEP_MS = 50;
    static const int DIM_STEP = 64;

    static const int GHOST_COUNT = 2;
    Ghost _ghosts[GHOST_COUNT];
    bool _ghostsFrightened = false;
    // eaten ghosts sit on the clock until this time (the clock dims), 0 = out and about
    unsigned long _ghostHouseUntil[GHOST_COUNT] = {0, 0};
    // an eaten ghost sits on the clock above the lane of junction col 1 / 3 of row 3 (ghostHouseX), and leaves through a hole in the bottom wall
    int _holeY = 39;                 // the bottom wall of the clock box is 2px thick
    bool _ghostExiting[GHOST_COUNT] = {false, false};
    // a ghost that just left the box cannot catch (or be caught) until this time
    unsigned long _ghostGraceUntil[GHOST_COUNT] = {0, 0};
    static const unsigned long GRACE_MS = 1000;
    // the next ghost leaves the box this long after the previous one is out
    static const unsigned long EXIT_GAP_MS = 2500;
    unsigned long _nextExitAt = 0;
    uint32_t _colorKey = 0xFFFFFFFF;
    int _clockRow = 2;               // the junction row the clock box sits on: 1, 2 or 3 (the ghosts' lane and pacman start are the row below)
    int _clockY = 26;                // top of the time, centred in the clock box (2px margin all round)
    static const unsigned long GHOUSE_MS = 3000;
    unsigned long _deathUntil = 0;
    unsigned long _deathBlink = 0;
    int _pacmanStartX = 0;
    int _pacmanStartY = 0;

    const char* _weekDayWords = "SUN\0MON\0TUE\0WED\0THU\0FRI\0SAT\0";
    const char* _monthWords = "JAN\0FEB\0MAR\0APR\0MAY\0JUN\0JUL\0AUG\0SEP\0OCT\0NOV\0DEC\0";
    char weekDayTemp[4]= "\0";
    char monthTemp[4]= "\0";



    enum MapBlock {
      EMPTY = 0,
      FOOD = 1,
      WALL = 2,
      GATE = 3,
      SUPER_FOOD = 4,
      CLOCK = 5,
      GHOST = 6,
      PACMAN = 7,
      OUT_OF_MAP = 99
    };


    // a random maze: mostly mirrored left/right, sometimes asymmetrical, see generateLevel()
    struct Maze {
      bool node[MAP_ROWS][MAP_COLS];
      bool h[MAP_ROWS][MAP_COLS-1];   // junction to the one on its right
      bool v[MAP_ROWS-1][MAP_COLS];   // junction to the one below it
      bool tun[MAP_ROWS];             // side tunnel on this row
      int clockRow;                   // the row the clock box sits on
    };
    static const int MIN_MAZE_DIFF = 12;   // a new maze differs at least this much from the last one
    Maze _prevMaze;
    bool _hasPrevMaze = false;
    char _levelRows[11][12];              // the generated maze in the FALLBACK_MAP format
    const char* _levelRowPtrs[11];

    // the map in play: what sits on each junction, which neighbours are linked by a corridor, which rows have a tunnel
    byte _MAP[MAP_ROWS][MAP_COLS] = {};
    bool _LINK_H[MAP_ROWS][MAP_COLS] = {};   // junction to the one on its right
    bool _LINK_V[MAP_ROWS][MAP_COLS] = {};   // junction to the one below it
    bool _TUNNEL[MAP_ROWS] = {};     // row with a tunnel through the side edges
    bool _TUNNEL_V[MAP_COLS] = {};   // column with a tunnel through the top and bottom edges

    // the dots in the corridors between the junctions, evenly spaced; the junctions hold their own pill in _MAP
    struct Pill {
      int8_t x, y;       // top left pixel
      uint8_t row, col;  // the junction the corridor starts from
      bool vertical;     // corridor down (else to the right), the dot itself is a single pixel
      bool alive;
    };
    static const int MAX_PILLS = 80;
    Pill _pills[MAX_PILLS];
    int _pillCount = 0;

    // first elem is the size
    const int PACMAN_MOVING_BLOCKS[6] = {5, MapBlock::EMPTY, MapBlock::FOOD, MapBlock::GATE, MapBlock::SUPER_FOOD, MapBlock::PACMAN};
    const int PACMAN_BLOCKING_BLOCKS[4] = {3, MapBlock::OUT_OF_MAP, MapBlock::WALL, MapBlock::CLOCK};

    void drawMap();
    Clockface::MapBlock nextBlock(Direction dir);
    Clockface::MapBlock nextBlock();
    void turnRandom();
    int countBlocks(Clockface::MapBlock elem);
    bool contains(int v, const int* values);
    void resetMap();
    void directionDecision();
    void updateClock(bool clear = true);
    void drawFoodBlock(int row, int col);
    void drawWalls(uint16_t color);
    void loadLevel(const char* const* rows, int clockRow);
    bool generateLevel();
    bool randomMaze(Maze& m);
    bool mazeToLevel(const Maze& m);
    static int mazeDiff(const Maze& a, const Maze& b);
    uint16_t dotColor();
    void applyGhostColors();
    void refreshColors();
    void buildPills();
    void drawPill(const Pill& p);
    void eatPills();
    int pillsLeft();
    bool linkHasPills(int row, int col, Direction dir);
    void resetGhosts();
    void openHole(int i);
    void closeHole(int i);
    void exitGhost(int i);
    bool ghostInHouse();
    void sendGhostHome(int i);
    void eraseGhost(Ghost& ghost);
    void moveGhost(Ghost& ghost);
    void updateGhosts();
    void checkGhostCollisions();
    void updateDeath();
    MapBlock blockAt(int row, int col, Direction dir);
    const char* weekDayName(int weekday);
    const char* monthName(int month);
    
    
  public:
    Clockface(Adafruit_GFX* display);
    void setup(CWDateTime *dateTime);
    void update();
};

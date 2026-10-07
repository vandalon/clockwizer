#pragma once

#include <Adafruit_GFX.h>
#include <functional>

#include "TeamKit.h"

// Full screen goal animation for a 64x32 or 64x64 panel, about 6 seconds: "net cam" from behind
// the goal. The ball flies out of the distance and grows until it hits the net (flash, ripples,
// shaking screen), then GOAL! slams in from the camera with fireworks over the cheering stands
// and the new score comes up.
//
// Only uses Adafruit_GFX drawing, so tools/goal-preview can render it on a
// computer. showFrame(ms) must put the drawn frame on screen and wait ms.
// homeKit and awayKit are both teams' colours: the scorer's kit colours the flash, the
// fireworks and the stripe, both are used for the names.
void playGoalAnimation(Adafruit_GFX *display, const char *home, const char *away, int homeScore, int awayScore,
                       bool homeScored, const TeamKit &homeKit, const TeamKit &awayKit,
                       const std::function<void(int ms)> &showFrame);

// Only the second half: GOAL! with confetti and the new score, about 2.5 seconds. Fits the
// panel's height: on a 64x64 panel the background fills it all, GOAL! sits in the middle and the
// score and names come up in a black band below it.
void playGoalCelebration(Adafruit_GFX *display, const char *home, const char *away, int homeScore, int awayScore,
                         bool homeScored, const TeamKit &homeKit, const TeamKit &awayKit,
                         const std::function<void(int ms)> &showFrame);

// Text in the classic 5x7 font with the top half of each letter in top and the
// bottom half in bottom (two-tone team names). size 1 or 2.
void drawTwoToneText(Adafruit_GFX *d, int x, int y, const char *text, uint16_t top, uint16_t bottom, uint8_t size);

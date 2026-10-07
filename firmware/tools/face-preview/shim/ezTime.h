#pragma once
#include <time.h>
#define SECS_PER_DAY 86400UL
namespace ezt {
extern time_t fakeNow;
inline time_t now() { return fakeNow; }
}

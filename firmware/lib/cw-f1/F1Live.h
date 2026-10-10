#pragma once

#include <Arduino.h>
#include <atomic>
#include <mutex>

#include "F1LiveState.h"

// Only one TLS handshake fits in the heap at a time: the ticker's downloads and the live feed's connect take turns
extern std::mutex f1TlsLock;

// Where the F1 code was last, kept in RTC memory: it survives a panic restart and is logged at the next boot
extern char f1Phase[16];
inline void f1At(const char *phase) { strlcpy(f1Phase, phase, sizeof(f1Phase)); }

// The live timing of a session, over the same feed the F1 apps use (livetiming.formula1.com, SignalR over a
// secure WebSocket, no key needed). A background task connects while a session is on or about to start,
// keeps the running order with its gaps and best laps up to date, and drops the connection afterwards.
class F1Live {
  public:
    // Called with whether a session is live or about to start; the connection follows
    void setWanted(bool wanted);

    // Is the feed up to date, with a running order?
    bool fresh();

    // The first places with their times, when the feed is up to date. Returns false when it isn't.
    bool get(char tag, F1LiveState::Row *rows, int &count, int &lap, int &totalLaps, char &flag, bool &finished);

    // Seconds left on the session clock, -1 when not known
    int clockSecs(long nowUtc);
    int part();  // qualifying: 1, 2 or 3, 0 when not known

    // Is the WebSocket open? Its TLS buffers leave no room for another TLS download meanwhile
    bool connected() const { return _connected; }

    // Changes with every update, for faces that only redraw when something did
    uint32_t updates() const { return _updates; }

  private:
    static void task(void *self);
    bool negotiate(char *token, size_t size, char *cookie, size_t cookieSize);
    void run();

    bool _started = false;
    std::mutex _lock;
    F1LiveState _state;
    std::atomic<bool> _wanted{false};
    std::atomic<bool> _connected{false};
    std::atomic<uint32_t> _updates{0};
    volatile unsigned long _lastData = 0;  // millis() of the last message with data, 0 = none
};

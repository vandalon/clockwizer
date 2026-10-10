#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <TelnetStream.h>
#include <CWPreferences.h>
#include "StatusController.h"
#include "ping/ping_sock.h"

// The radio can stay "connected" while nothing gets through (the router dropped the clock without telling it), and
// then the automatic reconnect never starts. Every 30 s the router is pinged; when that fails three times in a row
// the clock joins the network again, and when that doesn't help either it restarts.
class WifiWatchdog {
  public:
    static WifiWatchdog *getInstance() {
      static WifiWatchdog base;
      return &base;
    }

    void loop() {
      if (_done) finish();
      if (_session || (long)(millis() - _nextAt) < 0) return;
      if (WiFi.status() != WL_CONNECTED) {  // WiFiController retries and restarts by itself while the radio is down
        _fails = 0;
        _nextAt = millis() + CHECK_MS;
        return;
      }
      _nextAt = millis() + CHECK_MS;
      start();
    }

  private:
    static const unsigned long CHECK_MS = 30 * 1000;
    static const int REJOIN_AFTER = 3;    // failed pings in a row
    static const int RESTART_AFTER = 7;   // after the rejoin: three more minutes or so
    static const unsigned long MIN_UPTIME_MS = 10 * 60 * 1000;  // no restart loop when the router is simply away

    esp_ping_handle_t _session = nullptr;
    volatile bool _done = false, _replied = false;
    unsigned long _nextAt = 60 * 1000;
    int _fails = 0;

    void start() {
      IPAddress gw = WiFi.gatewayIP();
      if (gw == IPAddress(0, 0, 0, 0)) return;
      esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
      IP_ADDR4(&config.target_addr, gw[0], gw[1], gw[2], gw[3]);
      config.count = 1;
      config.timeout_ms = 2000;
      config.task_stack_size = 2048;
      esp_ping_callbacks_t callbacks = {};
      callbacks.cb_args = this;
      callbacks.on_ping_success = [](esp_ping_handle_t, void *self) { static_cast<WifiWatchdog *>(self)->_replied = true; };
      callbacks.on_ping_end = [](esp_ping_handle_t, void *self) { static_cast<WifiWatchdog *>(self)->_done = true; };
      _replied = false;
      _done = false;
      if (esp_ping_new_session(&config, &callbacks, &_session) != ESP_OK) {  // no memory for it just now: not the router's fault
        _session = nullptr;
        return;
      }
      esp_ping_start(_session);
    }

    void finish() {
      esp_ping_delete_session(_session);
      _session = nullptr;
      _done = false;
      if (_replied) {
        _fails = 0;
        return;
      }
      _fails++;
      if (_fails == 1 || _fails == REJOIN_AFTER || _fails == RESTART_AFTER) {
        log("[WiFi] the router does not answer (%d in a row), status %d, rssi %d, heap free %u\n", _fails, (int)WiFi.status(), (int)WiFi.RSSI(),
            (unsigned)ESP.getFreeHeap());
      }
      if (_fails == REJOIN_AFTER) {
        log("[WiFi] joining the network again\n");
        String ssid = ClockwiseParams::getInstance()->wifiSsid;
        WiFi.disconnect();
        if (ssid.length()) WiFi.begin(ssid.c_str(), ClockwiseParams::getInstance()->wifiPwd.c_str());
      } else if (_fails >= RESTART_AFTER && millis() > MIN_UPTIME_MS) {
        log("[WiFi] still no network, restarting\n");
        StatusController::getInstance()->forceRestart();
      }
    }

    static void log(const char *format, ...) {
      char line[160];
      va_list args;
      va_start(args, format);
      vsnprintf(line, sizeof(line), format, args);
      va_end(args);
      Serial.print(line);
      TelnetStream.print(line);
    }
};

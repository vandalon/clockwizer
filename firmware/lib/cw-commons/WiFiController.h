#pragma once

#include "ImprovWiFiLibrary.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include "CWWebServer.h"
#include "StatusController.h"
#include <WiFiManager.h>

// Makes the WiFiManager portal match the settings page (colours, cards, dark mode)
static const char PORTAL_STYLE[] PROGMEM =
  "<meta name='theme-color' content='#155799'><style>"
  ":root{--bg:#f2f4f7;--card:#fff;--text:#1c2430;--line:#dfe4ea;--accent:#155799}"
  "@media(prefers-color-scheme:dark){:root{--bg:#10151c;--card:#1a222c;--text:#e8edf3;--line:#2b3644;--accent:#4a9be8}}"
  "body{background:var(--bg)!important;color:var(--text)!important;font:16px/1.4 system-ui,sans-serif}"
  ".wrap{max-width:560px;padding:0 16px 24px}"
  "h1{margin:0 -16px 16px;padding:18px 16px;background:linear-gradient(120deg,#155799,#159957);color:#fff!important;font-size:22px;text-align:left}"
  "h3{color:var(--text)}"
  "a{color:var(--accent)}"
  "input,select{width:100%;padding:12px;font-size:16px;border:1px solid var(--line)!important;border-radius:8px;background:var(--card)!important;color:var(--text)!important;box-sizing:border-box}"
  "button,input[type=submit]{font:inherit;font-weight:600;padding:13px 14px;border-radius:10px;border:0;background:var(--accent)!important;color:#fff!important;width:100%}"
  "button.D{background:#c0392b!important}"
  ".msg{border-radius:10px;background:var(--card);border-left-color:var(--accent)!important;color:var(--text)}"
  ".q{color:var(--text)}"
  "</style>";

ImprovWiFi improvSerial(&Serial);

struct WiFiController
{
  // While offline, retry the saved network this often. Only restart after a
  // long outage, in case the WiFi stack itself got stuck.
  static const unsigned long RETRY_INTERVAL_MS = 30 * 1000;
  static const unsigned long OFFLINE_RESTART_MS = 15 * 60 * 1000;
  // Between two setup portals, try the saved network for this long
  static const unsigned long RETRY_SAVED_MS = 60 * 1000;

  unsigned long offlineSince = 0;
  unsigned long lastRetry = 0;
  bool connectionSucessfulOnce;
  // Set by main: shows what was just drawn (needed with double buffering)
  void (*showDisplay)() = nullptr;
  void (*setBrightness)(uint8_t) = nullptr;

  static void onImprovWiFiErrorCb(ImprovTypes::Error err)
  {
    ClockwiseWebServer::getInstance()->stopWebServer();
  }

  static void onImprovWiFiConnectedCb(const char *ssid, const char *password)
  {
    ClockwiseParams::getInstance()->load();
    ClockwiseParams::getInstance()->wifiSsid = String(ssid);
    ClockwiseParams::getInstance()->wifiPwd = String(password);

    ClockwiseParams::getInstance()->save();

    ClockwiseWebServer::getInstance()->startWebServer();
  }

  bool isConnected()
  {
    if (improvSerial.isConnected()) {
      if (!connectionSucessfulOnce) {
        // First connection came from a retry instead of begin()
        connectionSucessfulOnce = true;
        ClockwiseWebServer::getInstance()->startWebServer();
      }
      offlineSince = 0;
      return true;
    }

    unsigned long now = millis();
    if (offlineSince == 0) {
      offlineSince = now;
      lastRetry = now;
    }

    if (now - lastRetry > RETRY_INTERVAL_MS) {
      lastRetry = now;
      retry();
    }

    if (now - offlineSince > OFFLINE_RESTART_MS)
      StatusController::getInstance()->forceRestart();

    return false;
  }

  void retry()
  {
    String ssid = ClockwiseParams::getInstance()->wifiSsid;
    if (ssid.isEmpty()) return;

    Serial.printf("[WiFi] Offline, retrying %s\n", ssid.c_str());
    // No disconnect() first: begin() sets the config and connects by itself, and
    // a disconnect would cancel an attempt that is still running
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), ClockwiseParams::getInstance()->wifiPwd.c_str());
  }

  static void handleImprovWiFi()
  {
    improvSerial.handleSerial();
  }

  bool alternativeSetupMethod()
  {
    WiFiManager wifiManager;
    wifiManager.setConfigPortalTimeout(300); //Wait 5min to configure wifi via AP
    wifiManager.setConfigPortalBlocking(false); // a failed password leaves the portal open for another try
    // One clear screen in the same look as the settings page
    const char* menu[] = {"wifi"};
    wifiManager.setMenu(menu, 1);
    wifiManager.setTitle("Clockwizer");
    wifiManager.setCustomHeadElement(PORTAL_STYLE);
    wifiManager.startConfigPortal("Clockwizer-Wifi");

    if (ClockwiseParams::getInstance()->displayHeight == 64)
    {
      // Tested on a real panel: lit modules on black at half brightness scan reliably
      if (setBrightness) setBrightness(128);
      StatusController::getInstance()->wifiSetupQr("Clockwizer-Wifi");
    }
    else
    {
      StatusController::getInstance()->wifiConnectionFailed("Setup WiFi via AP");
    }
    if (showDisplay) showDisplay();

    bool success = false;

    // Stays active until it times out or a network is joined
    while (wifiManager.getConfigPortalActive())
    {
      if (wifiManager.process())
      {
        success = true;
        break;
      }
      delay(1);
    }

    if (success)
    {
      onImprovWiFiConnectedCb(WiFi.SSID().c_str(), WiFi.psk().c_str());
      Serial.printf("[WiFi] Connected via WiFiManager to %s, IP address %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
      connectionSucessfulOnce = success;
    }

    return success;
  }

  bool retrySavedNetwork()
  {
    unsigned long started = millis();
    do
    {
      if (improvSerial.tryConnectToWifi(ClockwiseParams::getInstance()->wifiSsid.c_str(), ClockwiseParams::getInstance()->wifiPwd.c_str()))
      {
        connectionSucessfulOnce = true;
        ClockwiseWebServer::getInstance()->startWebServer();
        Serial.printf("[WiFi] Reconnected to %s, IP address %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        return true;
      }
    } while (millis() - started < RETRY_SAVED_MS);
    return false;
  }

  bool begin()
  {
    WiFi.setHostname("clockwizer");  // before the mode is set, so DHCP sees it
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.setAutoReconnect(true);
    esp_wifi_set_ps(WIFI_PS_NONE);

    improvSerial.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32, CW_FW_NAME, CW_FW_VERSION, "Clockwizer");
    improvSerial.onImprovError(onImprovWiFiErrorCb);
    improvSerial.onImprovConnected(onImprovWiFiConnectedCb);

    ClockwiseParams::getInstance()->load();

    // "Change WiFi" in the web UI asks for the portal once, without erasing the saved network
    bool forcePortal = ClockwiseParams::getInstance()->preferences.getBool(ClockwiseParams::getInstance()->PREF_SETUP_WIFI, false);
    if (forcePortal)
      ClockwiseParams::getInstance()->preferences.remove(ClockwiseParams::getInstance()->PREF_SETUP_WIFI);

    if (!forcePortal && !ClockwiseParams::getInstance()->wifiSsid.isEmpty())
    {
      if (improvSerial.tryConnectToWifi(ClockwiseParams::getInstance()->wifiSsid.c_str(), ClockwiseParams::getInstance()->wifiPwd.c_str()))
      {
        connectionSucessfulOnce = true;
        ClockwiseWebServer::getInstance()->startWebServer();
        Serial.printf("[WiFi] Connected to %s, IP address %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        return true;
      }
    }

    // Nothing saved: keep the portal coming back until someone configures a network.
    // With a saved network (moved house, router off) alternate the portal with retries
    // of that network, so the QR is mostly visible and the clock still rejoins by itself.
    unsigned long setupStart = millis();
    bool connected = false;
    while (!connected)
    {
      connected = alternativeSetupMethod();
      if (connected || ClockwiseParams::getInstance()->wifiSsid.isEmpty()) continue;

      // An unconfirmed new firmware that never gets online rolls back through this restart
      if (millis() - setupStart > OFFLINE_RESTART_MS)
        StatusController::getInstance()->forceRestart();

      if (ClockwiseParams::getInstance()->displayHeight == 64)
      {
        if (setBrightness) setBrightness(ClockwiseParams::getInstance()->displayBright);
        StatusController::getInstance()->wifiRetrying();
        if (showDisplay) showDisplay();
      }
      connected = retrySavedNetwork();
    }

    if (setBrightness) setBrightness(ClockwiseParams::getInstance()->displayBright);
    if (ClockwiseParams::getInstance()->displayHeight == 64)
    {
      StatusController::getInstance()->wifiSetupDone();
      if (showDisplay) showDisplay();
    }
    return true;
  }
};

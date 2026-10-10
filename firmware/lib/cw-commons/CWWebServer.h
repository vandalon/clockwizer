#pragma once

#include <WiFi.h>
#include <ESPmDNS.h>
#include <CWPreferences.h>
#include "StatusController.h"
#include "SettingsWebPage.h"
#include "AppIcon.h"
#include "ClockPeers.h"
#include "Birthdays.h"
#include "Faces.h"
#ifdef CW_FOOTBALL_CATALOG
#include "FootballCatalog.h"
#endif

#ifndef CLOCKFACE_NAME
  #define CLOCKFACE_NAME "UNKNOWN"
#endif

WiFiServer server(80);

struct ClockwiseWebServer
{
  String httpBuffer;
  bool force_restart;
  bool update_requested = false;  // picked up by the main loop
  // Outcome of the last update check, for the web UI: "checking", "uptodate", "installing",
  // "noserver", "skipped" (a build that failed before) or "failed" (download failed)
  String update_status = "";
  bool brightness_changed = false;  // brightness settings saved, the main loop applies them
  String face_requested;          // clockface id like "0x05" to install, picked up by the main loop
  static const unsigned long HTTP_READ_TIMEOUT_MS = 2000;
  static const size_t HTTP_MAX_REQUEST_LINE = 512;
  const char* HEADER_TEMPLATE_D = "X-%s: %d\r\n";
  const char* HEADER_TEMPLATE_S = "X-%s: %s\r\n";
 
  static ClockwiseWebServer *getInstance()
  {
    static ClockwiseWebServer base;
    return &base;
  }

  void startWebServer()
  {
    server.begin();

    // Every path that brings the web UI up announces clockwizer.local as well
    // A second clock on the network is renamed (clockwizer-2.local) by mDNS itself; the _clockwise
    // service below is how clocks find each other whatever their host names became.
    static bool mdnsStarted = false;
    if (!mdnsStarted && MDNS.begin("clockwizer"))
    {
      MDNS.addService("http", "tcp", 80);
      MDNS.addService("clockwise", "tcp", 80);
      MDNS.addServiceTxt("clockwise", "tcp", "face", CW_FW_NAME);
      announceName();
      mdnsStarted = true;
    }
  }

  // What this clock is called: the name the user chose, else "Clockwizer"
  String displayName()
  {
    String name = ClockwiseParams::getInstance()->clockName;
    return name.length() ? name : String("Clockwizer");
  }

  // The name goes out as the mDNS instance name (what service browsers list) and as a TXT field
  void announceName()
  {
    String name = displayName();
    MDNS.setInstanceName(name);
    mdns_service_txt_item_set("_clockwise", "_tcp", "name", name.c_str());
  }

  // Names go into JSON and mDNS records: drop quotes, backslashes and control characters
  static String cleanName(const String &raw)
  {
    String out;
    for (size_t i = 0; i < raw.length() && out.length() < 24; i++)
    {
      char c = raw[i];
      if (c >= 32 && c != '"' && c != '\\' && c != '<' && c != '>' && c != '&' && c != 127) out += c;
    }
    out.trim();
    return out;
  }

  // %20 and + in a query value, as sent by the browser. Where a + can be part of the value (a password,
  // a Posix time zone) the callers pass plusIsSpace = false: the page encodes a real + as %2B anyway.
  static String urlDecode(const String &text, bool plusIsSpace = true)
  {
    String out;
    for (size_t i = 0; i < text.length(); i++)
    {
      char c = text[i];
      if (c == '+' && plusIsSpace) out += ' ';
      else if (c == '%' && i + 2 < text.length() && isxdigit(text[i + 1]) && isxdigit(text[i + 2]))
      {
        out += (char)strtol(text.substring(i + 1, i + 3).c_str(), nullptr, 16);
        i += 2;
      }
      else out += c;
    }
    return out;
  }

  static String jsonEscape(const String &text)
  {
    String out;
    for (size_t i = 0; i < text.length(); i++)
    {
      char c = text[i];
      if (c == '"' || c == '\\') out += '\\';
      if (c >= 32) out += c;
    }
    return out;
  }

  // A comma separated list of codes ("ned.1,449:NED:n"): nothing else gets through
  static String cleanList(const String &text)
  {
    String out;
    for (size_t i = 0; i < text.length() && out.length() < 400; i++)
    {
      char c = text[i];
      if (isalnum(c) || c == '.' || c == ':' || c == ',' || c == '_') out += c;
    }
    return out;
  }

  void stopWebServer()
  {
    server.stop();
  }

  void handleHttpRequest()
  {
    if (force_restart)
      StatusController::getInstance()->userRestart();


    WiFiClient client = server.available();
    if (client)
    {
      if (!accessAllowed(client.remoteIP()))
      {
        client.println("HTTP/1.0 403 Forbidden");
        client.println();
        client.stop();
        return;
      }
      // Only the request line matters. A client that stalls or sends junk must
      // not hold up the clock, so give up after a moment or a long line.
      unsigned long started = millis();
      httpBuffer = "";
      while (client.connected() && millis() - started < HTTP_READ_TIMEOUT_MS)
      {
        if (client.available())
        {
          char c = client.read();
          httpBuffer.concat(c);

          if (c == '\n')
          {
            int method_pos = httpBuffer.indexOf(' ');
            int path_pos = httpBuffer.indexOf(' ', method_pos + 1);

            if (method_pos > 0 && path_pos > method_pos)
            {
              String method = httpBuffer.substring(0, method_pos);
              String path = httpBuffer.substring(method_pos + 1, path_pos);
              String key = "";
              String value = "";

              int query = path.indexOf('?');
              if (query > 0)
              {
                // /set?key=value; "?key" alone is a key with an empty value
                int eq = path.indexOf('=', query);
                key = path.substring(query + 1, eq > 0 ? eq : path.length());
                if (eq > 0) value = path.substring(eq + 1);
                path = path.substring(0, query);
              }

              processRequest(client, method, path, key, value);
            }
            break;
          }
          if (httpBuffer.length() > HTTP_MAX_REQUEST_LINE)
          {
            client.print("HTTP/1.0 414 URI Too Long\r\n\r\n");
            break;
          }
        }
        else
        {
          delay(1);
        }
      }
      httpBuffer = "";
      delay(1);
      client.stop();
    }
  }

  // Same subnet as the clock itself (on the home network or on its own setup network)
  bool onOwnNetwork(IPAddress ip)
  {
    IPAddress sta = WiFi.localIP();
    if (sta != IPAddress(0, 0, 0, 0) && (uint32_t(ip) & uint32_t(WiFi.subnetMask())) == (uint32_t(sta) & uint32_t(WiFi.subnetMask()))) return true;
    IPAddress ap = WiFi.softAPIP();
    return ap != IPAddress(0, 0, 0, 0) && (uint32_t(ip) & 0x00FFFFFF) == (uint32_t(ap) & 0x00FFFFFF);
  }

  bool accessAllowed(IPAddress ip)
  {
    uint8_t mode = ClockwiseParams::getInstance()->webAccess;
    if (mode == 0) return true;
    if (onOwnNetwork(ip)) return true;
    if (mode == 2) return ip[0] == 10 || (ip[0] == 172 && (ip[1] & 0xF0) == 16) || (ip[0] == 192 && ip[1] == 168);
    return false;
  }

  void processRequest(WiFiClient client, String method, String path, String key, String value)
  {
    if (method == "GET" && path == "/") {
      client.println("HTTP/1.0 200 OK");
      client.println("Content-Type: text/html");
      client.println("Content-Encoding: gzip");
      client.printf("Content-Length: %u\r\n", (unsigned)SETTINGS_PAGE_GZ_SIZE);
      client.println();
      client.write(SETTINGS_PAGE_GZ, SETTINGS_PAGE_GZ_SIZE);
#ifdef CW_FOOTBALL_CATALOG
    } else if (method == "GET" && path == "/football/leagues") {
      client.println("HTTP/1.0 200 OK");
      client.println("Content-Type: application/json");
      client.println("Cache-Control: max-age=86400");
      client.println();
      client.print(FOOTBALL_LEAGUES_JSON);
    } else if (method == "GET" && path == "/football/teams") {
      client.println("HTTP/1.0 200 OK");
      client.println("Content-Type: application/json");
      client.println("Cache-Control: max-age=86400");
      client.println();
      for (const char *piece : FOOTBALL_TEAMS_JSON) client.print(piece);
#endif
    } else if (method == "GET" && path == "/faces") {
      // The clock faces that can be installed, with their little pictures (see Faces.h)
      client.println("HTTP/1.0 200 OK");
      client.println("Content-Type: application/json");
      client.println("Cache-Control: no-cache");
      client.println();
      client.print("[");
      for (size_t i = 0; i < CW_FACE_COUNT; i++) {
        const FaceInfo &face = CW_FACES[i];
        client.printf("%s{\"id\":\"%s\",\"name\":\"%s\",\"fits32\":%s,\"palette\":\"", i ? "," : "", face.id, face.name,
                      face.fits32 ? "true" : "false");
        if (face.iconPalette) client.print(face.iconPalette);
        client.print("\",\"pixels\":\"");
        if (face.iconPixels) client.print(face.iconPixels);
        client.print("\"}");
      }
      client.print("]");
    } else if (method == "GET" && path == "/peers") {
      client.println("HTTP/1.0 200 OK");
      client.println("Content-Type: application/json");
      client.println("Cache-Control: no-store");
      client.println();
      String json = "[";
      for (const ClockPeers::Peer &peer : ClockPeers::getInstance()->peers) {
        if (json.length() > 1) json += ",";
        json += "{\"name\":\"" + jsonEscape(peer.name) + "\",\"face\":\"" + jsonEscape(peer.face) +
                "\",\"ip\":\"" + peer.ip + "\",\"host\":\"" + jsonEscape(peer.host) + "\"}";
      }
      client.print(json + "]");
    } else if (method == "GET" && path == "/manifest.webmanifest") {
      // Makes "Add to Home Screen" open the page like an app
      client.println("HTTP/1.0 200 OK");
      client.println("Content-Type: application/manifest+json");
      client.println();
      client.print("{\"name\":\"" + jsonEscape(displayName()) + "\",\"short_name\":\"" + jsonEscape(displayName()) + "\",\"start_url\":\"/\",\"scope\":\"/\","
                   "\"display\":\"standalone\",\"background_color\":\"#10151c\",\"theme_color\":\"#155799\","
                   "\"icons\":[{\"src\":\"/icon.png\",\"sizes\":\"180x180\",\"type\":\"image/png\",\"purpose\":\"any\"}]}");
    } else if (method == "GET" && path == "/icon.png") {
      client.println("HTTP/1.0 200 OK");
      client.println("Content-Type: image/png");
      client.printf("Content-Length: %u\r\n", (unsigned)APP_ICON_SIZE);
      client.println("Cache-Control: max-age=86400");
      client.println();
      client.write(APP_ICON, APP_ICON_SIZE);
    } else if (method == "GET" && path == "/get") {
      getCurrentSettings(client);
    } else if (method == "GET" && path == "/read") {
      if (key == "pin") {
        readPin(client, key, value.toInt());
      }
    } else if (method == "POST" && path == "/restart") {
      client.println("HTTP/1.0 204 No Content");
      client.println();
      force_restart = true;
    } else if (method == "POST" && path == "/face") {
      client.println("HTTP/1.0 204 No Content");
      client.println();
      for (size_t i = 0; i < CW_FACE_COUNT; i++)
        if (value == CW_FACES[i].id) face_requested = value;
    } else if (method == "POST" && path == "/cmd") {
      client.println("HTTP/1.0 204 No Content");
      client.println();
      if (value == "reboot") {
        force_restart = true;
      } else if (value == "update") {
        update_requested = true;
        update_status = "checking";
      } else if (value == "wifi") {
        ClockwiseParams::getInstance()->preferences.putBool(ClockwiseParams::getInstance()->PREF_SETUP_WIFI, true);
        force_restart = true;
      } else if (value == "reset") {
        ClockwiseParams::getInstance()->factoryReset();
        force_restart = true;
      }
    } else if (method == "POST" && path == "/set") {
      ClockwiseParams::getInstance()->load();
      //a baby seal has died due this ifs
      if (key == ClockwiseParams::getInstance()->PREF_DISPLAY_BRIGHT) {
        ClockwiseParams::getInstance()->displayBright = constrain(value.toInt(), 0, 255);
      } else if (key == ClockwiseParams::getInstance()->PREF_DISPLAY_BRMIN) {
        ClockwiseParams::getInstance()->displayBrMin = constrain(value.toInt(), 3, 255);  // never fully dark
      } else if (key == ClockwiseParams::getInstance()->PREF_WIFI_SSID) {
        ClockwiseParams::getInstance()->wifiSsid = urlDecode(value, false);
      } else if (key == ClockwiseParams::getInstance()->PREF_WIFI_PASSWORD) {
        ClockwiseParams::getInstance()->wifiPwd = urlDecode(value, false);
      } else if (key == "autoBright") {   //autoBright=0010,0800
        ClockwiseParams::getInstance()->autoBrightMin = value.substring(0,4).toInt();
        ClockwiseParams::getInstance()->autoBrightMax = value.substring(5,9).toInt();
      } else if (key == ClockwiseParams::getInstance()->PREF_SWAP_BLUE_GREEN) {
        ClockwiseParams::getInstance()->swapBlueGreen = (value == "1");
      } else if (key == ClockwiseParams::getInstance()->PREF_USE_24H_FORMAT) {
        ClockwiseParams::getInstance()->use24hFormat = (value == "1");
      } else if (key == ClockwiseParams::getInstance()->PREF_LDR_PIN) {
        ClockwiseParams::getInstance()->ldrPin = value.toInt();
      } else if (key == ClockwiseParams::getInstance()->PREF_TIME_ZONE) {
        ClockwiseParams::getInstance()->timeZone = urlDecode(value, false);
        // A leftover Posix string would override the zone chosen here, and the web UI can't edit it
        ClockwiseParams::getInstance()->manualPosix = "";
      } else if (key == ClockwiseParams::getInstance()->PREF_NTP_SERVER) {
        ClockwiseParams::getInstance()->ntpServer = urlDecode(value, false);
      } else if (key == ClockwiseParams::getInstance()->PREF_MANUAL_POSIX) {
        ClockwiseParams::getInstance()->manualPosix = urlDecode(value, false);
      } else if (key == ClockwiseParams::getInstance()->PREF_CLOCK_NAME) {
        ClockwiseParams::getInstance()->clockName = cleanName(urlDecode(value));
      } else if (key == ClockwiseParams::getInstance()->PREF_UPD_QUIET_FROM) {
        ClockwiseParams::getInstance()->updQuietFrom = value.toInt() % 24;
      } else if (key == ClockwiseParams::getInstance()->PREF_UPD_QUIET_UNTIL) {
        ClockwiseParams::getInstance()->updQuietUntil = value.toInt() % 24;
      } else if (key == ClockwiseParams::getInstance()->PREF_FW_URL) {
        // A folder URL: http(s)://..., always ending in a slash. Empty, or the default itself, means the default
        String url = urlDecode(value);
        url.trim();
        if (url.length() && !url.endsWith("/")) url += "/";
        if (url == CW_DEFAULT_FW_URL) url = "";
        if (url.length() == 0 || ((url.startsWith("http://") || url.startsWith("https://")) && url.length() <= 120))
          ClockwiseParams::getInstance()->fwUrl = url;
      } else if (key == ClockwiseParams::getInstance()->PREF_WEB_ACCESS) {
        int mode = value.toInt();
        if (mode >= 0 && mode <= 2) ClockwiseParams::getInstance()->webAccess = mode;
      } else if (key == ClockwiseParams::getInstance()->PREF_MATCH_SECS) {
        ClockwiseParams::getInstance()->matchSecs = constrain(value.toInt(), 3, 60);
      } else if (key == ClockwiseParams::getInstance()->PREF_RESULT_MINS) {
        ClockwiseParams::getInstance()->resultMins = constrain(value.toInt(), 0, 1440);
      } else if (key == ClockwiseParams::getInstance()->PREF_FOOTBALL_LEAGUES) {
        ClockwiseParams::getInstance()->footballLeagues = cleanList(value);
      } else if (key == ClockwiseParams::getInstance()->PREF_FOOTBALL_TEAMS) {
        ClockwiseParams::getInstance()->footballTeams = cleanList(value);
      } else if (key == ClockwiseParams::getInstance()->PREF_F1_DRIVERS) {
        ClockwiseParams::getInstance()->f1Drivers = cleanList(value);
      } else if (key == ClockwiseParams::getInstance()->PREF_SHOW_F1) {
        ClockwiseParams::getInstance()->showF1 = (value == "1");
      } else if (key == ClockwiseParams::getInstance()->PREF_F1_FLAGS) {
        ClockwiseParams::getInstance()->f1Flags = (value == "1");
      } else if (key == ClockwiseParams::getInstance()->PREF_BALL_ROLL) {
        ClockwiseParams::getInstance()->ballRoll = (value == "1");
      } else if (key == ClockwiseParams::getInstance()->PREF_TIME_STYLE) {
        ClockwiseParams::getInstance()->timeStyle = constrain(value.toInt(), 0, 5);
      } else if (key == ClockwiseParams::getInstance()->PREF_COLOR) {
        int color = value.toInt();
        if (color >= 0 && color < ClockwiseParams::COLOR_COUNT) ClockwiseParams::getInstance()->color = color;
      } else if (key == ClockwiseParams::getInstance()->PREF_GHOST1_COLOR) {
        ClockwiseParams::getInstance()->ghost1Color = constrain(value.toInt(), 0, ClockwiseParams::PACMAN_COLOR_COUNT - 1);
      } else if (key == ClockwiseParams::getInstance()->PREF_GHOST2_COLOR) {
        ClockwiseParams::getInstance()->ghost2Color = constrain(value.toInt(), 0, ClockwiseParams::PACMAN_COLOR_COUNT - 1);
      } else if (key == ClockwiseParams::getInstance()->PREF_DOT_COLOR) {
        ClockwiseParams::getInstance()->dotColor = constrain(value.toInt(), 0, ClockwiseParams::PACMAN_COLOR_COUNT - 1);
      } else if (key == ClockwiseParams::getInstance()->PREF_BIRTHDAYS) {
        ClockwiseParams::getInstance()->birthdays = Birthdays::clean(urlDecode(value));
      } else if (key == ClockwiseParams::getInstance()->PREF_DISPLAY_ROTATION) {
        ClockwiseParams::getInstance()->displayRotation = value.toInt() & 3;  // 0-3 quarter turns
      } else if (key == ClockwiseParams::getInstance()->PREF_DISPLAY_HEIGHT) {
        // Anything but the two real panel sizes would break the display setup on every boot
        int height = value.toInt();
        if (height == 32 || height == 64) ClockwiseParams::getInstance()->displayHeight = height;
      }
      ClockwiseParams::getInstance()->save();
      if (key == ClockwiseParams::getInstance()->PREF_DISPLAY_BRIGHT || key == ClockwiseParams::getInstance()->PREF_DISPLAY_BRMIN ||
          key == "autoBright") brightness_changed = true;
      if (key == ClockwiseParams::getInstance()->PREF_CLOCK_NAME) announceName();
      client.println("HTTP/1.0 204 No Content");
      client.println();
    }
  }



  void readPin(WiFiClient client, String key, uint16_t pin) {
    client.println("HTTP/1.0 204 No Content");
    client.printf(HEADER_TEMPLATE_D, key, analogRead(pin));
    
    client.println();
  }


  // The settings in memory are the saved ones: only /set changes them, and it saves right away. No load()
  // here, it reassigns the Strings that the update check task reads (see firmwareUrl() in main.cpp).
  void getCurrentSettings(WiFiClient client) {
    client.println("HTTP/1.0 204 No Content");

    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_DISPLAY_BRIGHT, ClockwiseParams::getInstance()->displayBright);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_DISPLAY_BRMIN, ClockwiseParams::getInstance()->displayBrMin);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_DISPLAY_ABC_MIN, ClockwiseParams::getInstance()->autoBrightMin);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_DISPLAY_ABC_MAX, ClockwiseParams::getInstance()->autoBrightMax);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_SWAP_BLUE_GREEN, ClockwiseParams::getInstance()->swapBlueGreen);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_USE_24H_FORMAT, ClockwiseParams::getInstance()->use24hFormat);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_LDR_PIN, ClockwiseParams::getInstance()->ldrPin);    
    client.printf(HEADER_TEMPLATE_S, ClockwiseParams::getInstance()->PREF_TIME_ZONE, ClockwiseParams::getInstance()->timeZone.c_str());
    client.printf(HEADER_TEMPLATE_S, ClockwiseParams::getInstance()->PREF_WIFI_SSID, ClockwiseParams::getInstance()->wifiSsid.c_str());
    client.printf(HEADER_TEMPLATE_S, ClockwiseParams::getInstance()->PREF_NTP_SERVER, ClockwiseParams::getInstance()->ntpServer.c_str());
    client.printf(HEADER_TEMPLATE_S, ClockwiseParams::getInstance()->PREF_MANUAL_POSIX, ClockwiseParams::getInstance()->manualPosix.c_str());
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_DISPLAY_ROTATION, ClockwiseParams::getInstance()->displayRotation);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_DISPLAY_HEIGHT, ClockwiseParams::getInstance()->displayHeight);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_BALL_ROLL, ClockwiseParams::getInstance()->ballRoll);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_TIME_STYLE, ClockwiseParams::getInstance()->timeStyle);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_COLOR, ClockwiseParams::getInstance()->color);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_GHOST1_COLOR, ClockwiseParams::getInstance()->ghost1Color);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_GHOST2_COLOR, ClockwiseParams::getInstance()->ghost2Color);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_DOT_COLOR, ClockwiseParams::getInstance()->dotColor);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_SHOW_F1, ClockwiseParams::getInstance()->showF1);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_F1_FLAGS, ClockwiseParams::getInstance()->f1Flags);
    client.printf(HEADER_TEMPLATE_S, ClockwiseParams::getInstance()->PREF_CLOCK_NAME, ClockwiseParams::getInstance()->clockName.c_str());
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_UPD_QUIET_FROM, ClockwiseParams::getInstance()->updQuietFrom);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_UPD_QUIET_UNTIL, ClockwiseParams::getInstance()->updQuietUntil);
    client.printf(HEADER_TEMPLATE_S, ClockwiseParams::getInstance()->PREF_FW_URL,
                  ClockwiseParams::getInstance()->fwUrl.length() ? ClockwiseParams::getInstance()->fwUrl.c_str() : CW_DEFAULT_FW_URL);
    client.printf(HEADER_TEMPLATE_S, "fwUrlDefault", CW_DEFAULT_FW_URL);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_WEB_ACCESS, ClockwiseParams::getInstance()->webAccess);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_MATCH_SECS, ClockwiseParams::getInstance()->matchSecs);
    client.printf(HEADER_TEMPLATE_D, ClockwiseParams::getInstance()->PREF_RESULT_MINS, ClockwiseParams::getInstance()->resultMins);
    client.printf(HEADER_TEMPLATE_S, ClockwiseParams::getInstance()->PREF_FOOTBALL_LEAGUES, ClockwiseParams::getInstance()->footballLeagues.c_str());
    client.printf(HEADER_TEMPLATE_S, ClockwiseParams::getInstance()->PREF_FOOTBALL_TEAMS, ClockwiseParams::getInstance()->footballTeams.c_str());
    client.printf(HEADER_TEMPLATE_S, ClockwiseParams::getInstance()->PREF_F1_DRIVERS, ClockwiseParams::getInstance()->f1Drivers.c_str());
    client.printf(HEADER_TEMPLATE_S, ClockwiseParams::getInstance()->PREF_BIRTHDAYS, ClockwiseParams::getInstance()->birthdays.c_str());
    client.printf(HEADER_TEMPLATE_S, "updateStatus", update_status.c_str());

    client.printf(HEADER_TEMPLATE_S, "CW_FW_VERSION", CW_FW_VERSION);
    client.printf(HEADER_TEMPLATE_S, "CW_FW_NAME", CW_FW_NAME);
    client.printf(HEADER_TEMPLATE_S, "CLOCKFACE_NAME", CLOCKFACE_NAME);
    client.printf(HEADER_TEMPLATE_S, "CW_FW_ID", CW_FW_ID);
    client.println();
  }
  
};

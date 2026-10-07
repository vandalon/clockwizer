#pragma once

#include <Preferences.h>

#ifndef CW_PREF_DB_NAME
    #define CW_PREF_DB_NAME "clockwise"
#endif


// Where firmware updates come from until the settings page says otherwise (a folder URL ending in /).
// A private build can point this somewhere else from local_config.h, which is not part of the public repo.
#if __has_include("local_config.h")
    #include "local_config.h"
#endif
#ifndef CW_DEFAULT_FW_URL
    #define CW_DEFAULT_FW_URL "https://vandalon.github.io/clockwizer/"
#endif

// What the football ticker follows until the settings page says otherwise
#define CW_DEFAULT_FOOTBALL_LEAGUES "ned.1,ned.cup"
#define CW_DEFAULT_FOOTBALL_TEAMS "449:NED:n,143:FOR:c"

struct ClockwiseParams
{
    Preferences preferences;

    const char* const PREF_SWAP_BLUE_GREEN = "swapBlueGreen";
    const char* const PREF_USE_24H_FORMAT = "use24hFormat";
    const char* const PREF_DISPLAY_BRIGHT = "displayBright";
    const char* const PREF_DISPLAY_BRMIN = "displayBrMin";
    const char* const PREF_DISPLAY_ABC_MIN = "autoBrightMin";
    const char* const PREF_DISPLAY_ABC_MAX = "autoBrightMax";
    const char* const PREF_LDR_PIN = "ldrPin";
    const char* const PREF_TIME_ZONE = "timeZone";
    const char* const PREF_WIFI_SSID = "wifiSsid";
    const char* const PREF_WIFI_PASSWORD = "wifiPwd";
    const char* const PREF_NTP_SERVER = "ntpServer";
    const char* const PREF_MANUAL_POSIX = "manualPosix";
    const char* const PREF_DISPLAY_ROTATION = "displayRotation";
    const char* const PREF_DISPLAY_HEIGHT = "displayHeight";
    // One-shot flag: open the WiFi setup portal on the next boot, keeping the saved network
    const char* const PREF_SETUP_WIFI = "setupWifi";
    const char* const PREF_SHOW_QR = "showQr";
    const char* const PREF_CLOCK_NAME = "clockName";  // what the user calls this clock, announced over mDNS
    // The automatic update check stays quiet from this hour until that hour (local time); equal = never quiet
    const char* const PREF_UPD_QUIET_FROM = "updQuietFrom";
    const char* const PREF_UPD_QUIET_UNTIL = "updQuietUntil";
    // Folder URL the firmware updates are fetched from, empty = CW_DEFAULT_FW_URL
    const char* const PREF_FW_URL = "fwUrl";
    // Who may open the web page: 0 = anyone, 1 = this network only, 2 = any private (RFC 1918) network
    const char* const PREF_WEB_ACCESS = "webAccess";
    // Football face: seconds each live match is shown before the next one
    const char* const PREF_MATCH_SECS = "matchSecs";
    // Football face: minutes a finished match stays on, 0 = until midnight
    const char* const PREF_RESULT_MINS = "resultMins";
    // Football face: the competitions to follow (ESPN codes) and the favourite teams ("id:abbreviation:c|n"), comma separated
    const char* const PREF_FOOTBALL_LEAGUES = "fbLeagues";
    const char* const PREF_FOOTBALL_TEAMS = "fbTeams";
    // Tetris 64x32: show the Formula 1 race in the ticker
    const char* const PREF_SHOW_F1 = "showF1";
    // Football face: the ball on the main screen rolls (1) or pulses (0)
    const char* const PREF_BALL_ROLL = "ballRoll";
    // Football face: how the clock digits change (0 flip cards, 1 fade, 2 roll, 3 dissolve, 4 drift, 5 shimmer)
    const char* const PREF_TIME_STYLE = "timeStyle";
    // Accent colour (index into COLORS): Pacman walls and the night mode clock
    const char* const PREF_COLOR = "color";
    // Pacman face: colour (index into PACMAN_COLORS) of each ghost and of the dots, 0 = the default
    const char* const PREF_GHOST1_COLOR = "ghost1Color";
    const char* const PREF_GHOST2_COLOR = "ghost2Color";
    const char* const PREF_DOT_COLOR = "dotColor";

    static const uint8_t COLOR_COUNT = 10;
    // Accent colours, in the order of the settings page: RGB565 for the Pacman walls and for the night mode clock
    const uint16_t COLORS[COLOR_COUNT][2] = {
        {0x0016, 0x3800},  // default: blue walls, red night clock
        {0xD4FF, 0xD4FF},  // pastel purple
        {0x9FD3, 0x2A45},  // pastel green
        {0xFDBA, 0x49A8},  // pastel pink
        {0x965F, 0x29E9},  // pastel blue
        {0xFDF2, 0x49C5},  // peach
        {0xFFB2, 0x4A45},  // lemon
        {0xFC60, 0x4940},  // orange
        {0xD800, 0x4000},  // red
        {0x065B, 0x01E8},  // cyan
    };

    static const uint8_t PACMAN_COLOR_COUNT = 11;
    // Choices for the ghosts and the dots, in the order of the settings page; 0 = default, which the face works out itself
    const uint16_t PACMAN_COLORS[PACMAN_COLOR_COUNT] = {
        0x0000,  // default
        0x9FD3,  // pastel green
        0xFDDF,  // pastel pink
        0x965F,  // pastel blue
        0xFFB2,  // lemon
        0xFDF2,  // peach
        0xD4FF,  // pastel purple
        0xF800,  // red
        0xFC60,  // orange
        0x07FF,  // cyan
        0xFFFF,  // white
    };

    bool swapBlueGreen;
    bool use24hFormat;
    uint8_t displayBright;
    uint8_t displayBrMin;
    uint16_t autoBrightMin;
    uint16_t autoBrightMax;
    uint8_t ldrPin;
    String timeZone;
    String wifiSsid;
    String wifiPwd;
    String ntpServer;
    String manualPosix;
    uint8_t displayRotation;
    uint8_t displayHeight;
    bool showQrOnBoot;
    String clockName;
    uint8_t updQuietFrom;
    uint8_t updQuietUntil;
    String fwUrl;
    uint8_t webAccess;
    uint8_t matchSecs;
    uint16_t resultMins;
    String footballLeagues;
    String footballTeams;
    bool showF1;
    bool ballRoll;
    uint8_t timeStyle;
    uint8_t color;
    uint8_t ghost1Color;
    uint8_t ghost2Color;
    uint8_t dotColor;

    uint16_t wallColor() { return COLORS[color < COLOR_COUNT ? color : 0][0]; }
    // 0 when the default is wanted
    uint16_t pacmanColor(uint8_t choice) { return choice < PACMAN_COLOR_COUNT ? PACMAN_COLORS[choice] : 0; }
    uint16_t nightColor() { return COLORS[color < COLOR_COUNT ? color : 0][1]; }

    ClockwiseParams() {
        preferences.begin("clockwise", false); 
        //preferences.clear();
    }

    static ClockwiseParams* getInstance() {
        static ClockwiseParams base;
        return &base;
    }

   
    // Wipes everything the user can change, WiFi included. The panel wiring
    // (size, colour order, rotation, LDR pin) has no UI and can't be guessed, so it stays.
    void factoryReset()
    {
        load();
        bool keepSwap = swapBlueGreen;
        uint8_t keepLdrPin = ldrPin;
        uint8_t keepRotation = displayRotation;
        uint8_t keepHeight = displayHeight;

        preferences.clear();

        preferences.putBool(PREF_SWAP_BLUE_GREEN, keepSwap);
        preferences.putUInt(PREF_LDR_PIN, keepLdrPin);
        preferences.putUInt(PREF_DISPLAY_ROTATION, keepRotation);
        preferences.putUInt(PREF_DISPLAY_HEIGHT, keepHeight);
    }

    void save()
    {
        preferences.putBool(PREF_SWAP_BLUE_GREEN, swapBlueGreen);
        preferences.putBool(PREF_USE_24H_FORMAT, use24hFormat);
        preferences.putUInt(PREF_DISPLAY_BRIGHT, displayBright);
        preferences.putUInt(PREF_DISPLAY_BRMIN, displayBrMin);
        preferences.putUInt(PREF_DISPLAY_ABC_MIN, autoBrightMin);
        preferences.putUInt(PREF_DISPLAY_ABC_MAX, autoBrightMax);
        preferences.putUInt(PREF_LDR_PIN, ldrPin);        
        preferences.putString(PREF_TIME_ZONE, timeZone);
        preferences.putString(PREF_WIFI_SSID, wifiSsid);
        preferences.putString(PREF_WIFI_PASSWORD, wifiPwd);
        preferences.putString(PREF_NTP_SERVER, ntpServer);
        preferences.putString(PREF_MANUAL_POSIX, manualPosix);
        preferences.putUInt(PREF_DISPLAY_ROTATION, displayRotation);
        preferences.putUInt(PREF_DISPLAY_HEIGHT, displayHeight);
        preferences.putBool(PREF_SHOW_QR, showQrOnBoot);
        preferences.putString(PREF_CLOCK_NAME, clockName);
        preferences.putUInt(PREF_UPD_QUIET_FROM, updQuietFrom);
        preferences.putUInt(PREF_UPD_QUIET_UNTIL, updQuietUntil);
        preferences.putString(PREF_FW_URL, fwUrl);
        preferences.putUInt(PREF_WEB_ACCESS, webAccess);
        preferences.putUInt(PREF_MATCH_SECS, matchSecs);
        preferences.putUInt(PREF_RESULT_MINS, resultMins);
        preferences.putString(PREF_FOOTBALL_LEAGUES, footballLeagues);
        preferences.putString(PREF_FOOTBALL_TEAMS, footballTeams);
        preferences.putBool(PREF_SHOW_F1, showF1);
        preferences.putBool(PREF_BALL_ROLL, ballRoll);
        preferences.putUInt(PREF_TIME_STYLE, timeStyle);
        preferences.putUInt(PREF_COLOR, color);
        preferences.putUInt(PREF_GHOST1_COLOR, ghost1Color);
        preferences.putUInt(PREF_GHOST2_COLOR, ghost2Color);
        preferences.putUInt(PREF_DOT_COLOR, dotColor);
    }

    void load()
    {
        swapBlueGreen = preferences.getBool(PREF_SWAP_BLUE_GREEN, false);
        use24hFormat = preferences.getBool(PREF_USE_24H_FORMAT, true);
        displayBright = preferences.getUInt(PREF_DISPLAY_BRIGHT, 32);
        displayBrMin = preferences.getUInt(PREF_DISPLAY_BRMIN, 3);
        autoBrightMin = preferences.getUInt(PREF_DISPLAY_ABC_MIN, 0);
        autoBrightMax = preferences.getUInt(PREF_DISPLAY_ABC_MAX, 0);
        ldrPin = preferences.getUInt(PREF_LDR_PIN, 35);        
        timeZone = preferences.getString(PREF_TIME_ZONE, "UTC");
        wifiSsid = preferences.getString(PREF_WIFI_SSID, "");
        wifiPwd = preferences.getString(PREF_WIFI_PASSWORD, "");
        ntpServer = preferences.getString(PREF_NTP_SERVER, "time.cloudflare.com");
        manualPosix = preferences.getString(PREF_MANUAL_POSIX, "");
        displayRotation = preferences.getUInt(PREF_DISPLAY_ROTATION, 0);
        displayHeight = preferences.getUInt(PREF_DISPLAY_HEIGHT, 64);
        showQrOnBoot = preferences.getBool(PREF_SHOW_QR, true);
        clockName = preferences.getString(PREF_CLOCK_NAME, "");
        updQuietFrom = preferences.getUInt(PREF_UPD_QUIET_FROM, 22);
        updQuietUntil = preferences.getUInt(PREF_UPD_QUIET_UNTIL, 8);
        fwUrl = preferences.getString(PREF_FW_URL, "");
        webAccess = preferences.getUInt(PREF_WEB_ACCESS, 1);
        matchSecs = preferences.getUInt(PREF_MATCH_SECS, 8);
        resultMins = preferences.getUInt(PREF_RESULT_MINS, 120);
        footballLeagues = preferences.getString(PREF_FOOTBALL_LEAGUES, CW_DEFAULT_FOOTBALL_LEAGUES);
        footballTeams = preferences.getString(PREF_FOOTBALL_TEAMS, CW_DEFAULT_FOOTBALL_TEAMS);
        showF1 = preferences.getBool(PREF_SHOW_F1, true);
        ballRoll = preferences.getBool(PREF_BALL_ROLL, true);
        timeStyle = preferences.getUInt(PREF_TIME_STYLE, 2);
        color = preferences.getUInt(PREF_COLOR, 0);
        ghost1Color = preferences.getUInt(PREF_GHOST1_COLOR, 0);
        ghost2Color = preferences.getUInt(PREF_GHOST2_COLOR, 0);
        dotColor = preferences.getUInt(PREF_DOT_COLOR, 0);
    }

};

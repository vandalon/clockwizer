#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

// Clockface
#include <Clockface.h>

// Commons
#include <WiFiController.h>
#include <CWDateTime.h>
#include <CWPreferences.h>
#include <CWWebServer.h>
#include <StatusController.h>
#include <TelnetStream.h>
#include <movingAvg.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <uptime_formatter.h>
#include "NotificationServer.h"

#include "messageFont.h"  // Include the smallMessageFont font

// #define MIN_BRIGHT_DISPLAY_ON 3 // lowest = 3
#define MIN_BRIGHT_DISPLAY_OFF 128

#define ESP32_LED_BUILTIN 2

// How often the clockface's update() is called; a face can ask for more
// frames with a build flag (Tetris does, for its smoother animations)
#ifndef CLOCKFACE_UPDATE_MS
#define CLOCKFACE_UPDATE_MS 100
#endif

// Keep a freshly installed firmware "pending" instead of letting the Arduino
// core mark it valid at boot. If it resets before checkFirmwareValid() has
// confirmed it, the bootloader falls back to the previous firmware.
extern "C" bool verifyRollbackLater() { return true; }

MatrixPanel_I2S_DMA *dma_display = nullptr;

Clockface *clockface;

WiFiController wifi;
CWDateTime cwDateTime;

bool forceRefresh;
bool nightMode;
bool updateInProgress = false;
volatile bool liveEventOn = false;      // set by the football/F1 tickers while a match or session is live: no automatic update then
volatile bool firmwareUpdating = false;  // from the start of a firmware update: the football downloads wait (FootballTicker)
bool logLDR = false;
int64_t lastNow;
int64_t ldrCheckDue = 0;
int64_t loopDue = 0;
unsigned long updateCheckDue = 30000; // first check 30s after boot, then daily; compared wrap-safe
#define UPDATE_CHECK_MS 86400000
#define UPDATE_MAX_TRIES 3        // failed downloads per build before giving up
#define UPDATE_MAX_RESTARTS 2     // restarts in a row for the same update before giving up
#define SETUP_INFO_MS 10000       // how long the screen with the clock's address stays up after the WiFi setup
#define VALIDATE_AFTER_MS 60000   // from this long on, running + on WiFi, try to prove the update check works
#define VALIDATE_RETRY_MS 30000   // between tries
#define ROLLBACK_AFTER_MS 300000  // never proved it: go back to the old firmware
bool fwValidated = false;
int64_t validateDue = 0;
int64_t wifiUpAt = 0;  // the rollback deadline counts from here, not from boot: setup portals take minutes
unsigned int altDisplay;
unsigned int curBrightness;
unsigned int currentLDRValue;
unsigned int avgLDRValue;
unsigned int panelResY;
uint8_t curBright;

String lastTime;
String currentTime;
String currentTimeWithSeconds;

movingAvg ldrAverage(10);

NotificationServer notificationServer;
bool showingNotification = false;
bool flashState = true;
String currentNotification;
unsigned long notificationStartTime = 0;
bool forceFullRefresh = false;

const unsigned long DEFAULT_NOTIFICATION_DURATION = 5000;  // Default 5 seconds
unsigned long notificationDuration = DEFAULT_NOTIFICATION_DURATION;

bool setupRefresh = false;  // the display runs fast for the WiFi setup QR code (see displaySetup())

void displaySetup(bool swapBlueGreen, uint8_t displayBright, uint8_t displayRotation)
{
  panelResY = ClockwiseParams::getInstance()->displayHeight;
  HUB75_I2S_CFG mxconfig(64, panelResY, 1);

  if (swapBlueGreen)
  {
    // Swap Blue and Green pins because the panel is RBG instead of RGB.
    mxconfig.gpio.b1 = 26;
    mxconfig.gpio.b2 = 12;
    mxconfig.gpio.g1 = 27;
    mxconfig.gpio.g2 = 13;
  }

  mxconfig.gpio.e = 18;
  mxconfig.clkphase = false;
  // Only for the WiFi setup QR code: a faster panel clock and a high minimum refresh rate keep a phone camera
  // from seeing stripes. That costs colour depth, so every other start uses the library defaults (8 MHz, 60 Hz).
  // The clock can ghost or garble some panels: remove these two lines if the picture looks wrong.
  if (setupRefresh)
  {
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_20M;
    mxconfig.min_refresh_rate = 255;
  }
  #ifdef DOUBLE_BUFFER_ON
    mxconfig.double_buff = true;
  #endif

  // Display Setup
  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness8(displayBright);
  dma_display->clearScreen();
  dma_display->setRotation(displayRotation);
  #ifdef DOUBLE_BUFFER_ON
    dma_display->flipDMABuffer();
  #endif
}

#include "BirthdayAnim.h"
#include "Birthdays.h"

#define BIRTHDAY_FRAME_MS 60  // ~16 frames per second for the birthday animation

// Today's birthday: the fixed ones of the build, then the ones from the settings. nullptr when there is none.
// Called on every frame, so the list is only parsed again when the setting has changed.
// A 29 February birthday falls on 1 March in the years that have no 29 February.
const Birthdays::Entry *todaysBirthday(int year, int month, int day) {
  static Birthdays::Entry list[Birthdays::MAX * 2];
  static String parsedFrom;  // the setting the list was parsed from
  static int count = -1;     // -1 = not parsed yet
  const String &setting = ClockwiseParams::getInstance()->birthdays;
  if (count < 0 || setting != parsedFrom) {
    parsedFrom = setting;
    count = Birthdays::parse(CW_FIXED_BIRTHDAYS, list);
    count += Birthdays::parse(setting, list + count);
  }
  bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  for (int i = 0; i < count; i++) {
    if (list[i].month == month && list[i].day == day) return &list[i];
    if (!leap && month == 3 && day == 1 && list[i].month == 2 && list[i].day == 29) return &list[i];
  }
  return nullptr;
}

// Telnet T: preview the birthday screen for a minute, whatever the date (T again stops it)
const Birthdays::Entry BIRTHDAY_PREVIEW = {0, 0, "TEST", 2013};
unsigned long birthdayPreviewUntil = 0;

void printCenterPico(const char *buf, int y)
{
  int16_t x1, y1;
  uint16_t w, h;
  dma_display->setFont(&Picopixel);
  dma_display->getTextBounds(buf, 0, y, &x1, &y1, &w, &h);
  dma_display->setCursor(32 - (w / 2), y);
  dma_display->print(buf);
}

void printCenter(const char *buf, int y) {
  int16_t x1, y1;
  uint16_t w, h;
  dma_display->setTextSize(1);
  dma_display->getTextBounds(buf, 0, y, &x1, &y1, &w, &h);
  dma_display->setCursor(32 - (w / 2), y);
  dma_display->print(buf);
}

void automaticBrightControl()
{

  ldrCheckDue = millis() + 100;
  int16_t currentValue = analogRead(ClockwiseParams::getInstance()->ldrPin);
  int16_t lastValue;
  uint16_t ldrMin = ClockwiseParams::getInstance()->autoBrightMin;
  uint16_t ldrMax = ClockwiseParams::getInstance()->autoBrightMax;
  int16_t avgValue = ldrAverage.reading(currentValue);
  uint8_t maxBright = ClockwiseParams::getInstance()->displayBright;

  char hour[3] = {0};
  snprintf(hour, sizeof(hour), "%02d", cwDateTime.getHour());
  char minute[3] = {0};
  snprintf(minute, sizeof(minute), "%02d", cwDateTime.getMinute());
  char second[3] = {0};
  snprintf(second, sizeof(second), "%02d", cwDateTime.getSecond());
  currentTimeWithSeconds = String(hour) + ":" + String(minute) + ":" + String(second);
  
  if (ldrMax == 0) return;  // auto brightness is on when the bright threshold is set (see applyBrightnessSettings())

  uint8_t mapBright;

  if (avgValue < ldrMin) nightMode=true;
  if (avgValue > (ldrMin + 1)) nightMode=false;

  // uint8_t minBright = (nightMode == true ? MIN_BRIGHT_DISPLAY_OFF : ClockwiseParams::getInstance()->displayBrMin);
  uint8_t minBright = max<uint8_t>(3, ClockwiseParams::getInstance()->displayBrMin);  // never fully dark, whatever is stored

  if (ldrMax != 1) {
    // map() extrapolates outside the range (and divides by zero when min == max)
    if (ldrMax <= ldrMin) mapBright = maxBright;
    else mapBright = map(constrain((long)avgValue, (long)ldrMin, (long)ldrMax), ldrMin, ldrMax, minBright, maxBright);
  } else {
    mapBright = (currentValue > ldrMin) ? maxBright : minBright;
  }
  avgLDRValue = avgValue;
  currentLDRValue = currentValue;
  curBrightness = mapBright;
  if (logLDR == true) TelnetStream.printf("%s [INFO] LDR: %d (avg: %d), dark: %d, bright: %d, Bright: %d, Nightmode: %d\n", currentTimeWithSeconds.c_str(), currentValue, avgValue, ldrMin, ldrMax, mapBright, nightMode);

  if (abs(curBright - mapBright) > 1 || (curBright == 0 && mapBright !=0)) {
    dma_display->setBrightness8(mapBright);
    curBright = mapBright;
  }
}

// Brightness settings saved from the web UI take effect right away. The auto brightness
// thresholds are read on every check anyway; switching auto off, or changing the manual
// brightness, needs the display set here.
void applyBrightnessSettings() {
  ClockwiseParams *params = ClockwiseParams::getInstance();
  if (params->autoBrightMax == 0) {
    nightMode = false;
    dma_display->setBrightness8(params->displayBright);
    curBright = params->displayBright;
  }
}

static void bootUpdateCheck();
static bool inBootCheck = false;  // the check right after a restart: a failure there doesn't restart again
static void crashGuard();

void setup()
{
  Serial.begin(115200);
  pinMode(ESP32_LED_BUILTIN, INPUT);
  crashGuard();

  ClockwiseParams::getInstance()->load();

  pinMode(ClockwiseParams::getInstance()->ldrPin, INPUT);

  // The WiFi setup QR code is only shown when there is no network to join (or when asked for), and then the
  // display starts in its fast mode. A start that needs the setup late (saved network gone) restarts into it.
  if (ClockwiseParams::getInstance()->displayHeight == 64)
  {
    Preferences prefs;
    prefs.begin("fwupdate", false);
    setupRefresh = ClockwiseParams::getInstance()->wifiSsid.isEmpty() ||
                   ClockwiseParams::getInstance()->preferences.getBool(ClockwiseParams::getInstance()->PREF_SETUP_WIFI, false) ||
                   prefs.getBool("setupRefresh", false);
    prefs.remove("setupRefresh");
    prefs.end();
  }
  wifi.highRefresh = setupRefresh;

  displaySetup(ClockwiseParams::getInstance()->swapBlueGreen, ClockwiseParams::getInstance()->displayBright, ClockwiseParams::getInstance()->displayRotation);
  clockface = new Clockface(dma_display);

  ldrAverage.begin();
  automaticBrightControl();

  wifi.showDisplay = []() {
    #ifdef DOUBLE_BUFFER_ON
      dma_display->flipDMABuffer();
    #endif
  };
  StatusController::getInstance()->showDisplay = wifi.showDisplay;
  if (panelResY == 64) StatusController::getInstance()->wifiConnecting();
  wifi.setBrightness = [](uint8_t b) { dma_display->setBrightness8(b); };
  wifi.begin();
  
  // isConnected() keeps retrying the saved network while we wait
  while (!wifi.isConnected()) {
      printCenterPico("NO NETWORK", (panelResY / 2) - 4);
      #ifdef DOUBLE_BUFFER_ON
        dma_display->flipDMABuffer();
      #endif
      delay(500);
  }
  
  wifiUpAt = millis();
  if (panelResY == 64) {
    StatusController::getInstance()->wifiConnected();  // solid green icon
    delay(1000);
  }

  // After a WiFi setup: tell where the settings page is, because the phone just left the setup network
  bool setupInfo;
  {
    Preferences prefs;
    prefs.begin("fwupdate", false);
    setupInfo = prefs.getBool("setupInfo", false);
    if (setupInfo) prefs.remove("setupInfo");
    prefs.end();
  }
  if (setupInfo && panelResY == 64) {
    StatusController::getInstance()->wifiConnectedInfo(WiFi.localIP().toString().c_str());
    #ifdef DOUBLE_BUFFER_ON
      dma_display->flipDMABuffer();
    #endif
    for (unsigned long until = millis() + SETUP_INFO_MS; (long)(millis() - until) < 0; delay(5))
      ClockwiseWebServer::getInstance()->handleHttpRequest();
    dma_display->fillScreen(0);
    #ifdef DOUBLE_BUFFER_ON
      dma_display->flipDMABuffer();
    #endif
  }
  notificationServer.begin();
  TelnetStream.println("[Main] Device IP: " + WiFi.localIP().toString());
  
  char hour[3] = {0};
  snprintf(hour, sizeof(hour), "%02d", cwDateTime.getHour());
  char minute[3] = {0};
  snprintf(minute, sizeof(minute), "%02d", cwDateTime.getMinute());
  char second[3] = {0};
  snprintf(second, sizeof(second), "%02d", cwDateTime.getSecond());
  currentTimeWithSeconds = String(hour) + ":" + String(minute) + ":" + String(second);

  TelnetStream.begin();
  bootUpdateCheck();  // before the clockface starts its downloads: the heap is still whole

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  StatusController::getInstance()->ntpConnecting();
  cwDateTime.begin(ClockwiseParams::getInstance()->timeZone.c_str(), 
      ClockwiseParams::getInstance()->use24hFormat, 
      ClockwiseParams::getInstance()->ntpServer.c_str(),
      ClockwiseParams::getInstance()->manualPosix.c_str());
  clockface->setup(&cwDateTime);
}

// Where firmware comes from: the "Firmware location" setting, else the built-in default.
// The URL is a folder; per-panel files live in a sub-folder named after the MAC address.
static String firmwareUrl(const String &file, bool perPanel) {
  String base = ClockwiseParams::getInstance()->fwUrl;
  if (base.length() == 0) base = CW_DEFAULT_FW_URL;
  return base + (perPanel ? WiFi.macAddress() + "/" : String("")) + file;
}

// HTTPS downloads are not certificate-checked: the md5 comparison guards against corrupt files,
// not against someone on the path to the server
static std::unique_ptr<WiFiClient> makeClient(const String &url) {
  if (url.startsWith("https://")) {
    WiFiClientSecure *secure = new WiFiClientSecure();
    secure->setInsecure();
    return std::unique_ptr<WiFiClient>(secure);
  }
  return std::unique_ptr<WiFiClient>(new WiFiClient());
}

void updateFirmware( String id ) {
  firmwareUpdating = true;

  dma_display->setFont(&smallMessageFont);
  dma_display->setTextColor(0x0412);
  
  dma_display->fillScreen(0);
  printCenter("UPDATING...", (panelResY / 2) - 5);
  #ifdef DOUBLE_BUFFER_ON
    dma_display->flipDMABuffer();
  #endif

  // The football/F1 tasks hand back their parse buffers once they see firmwareUpdating (the update
  // check cleared it a moment ago, so they may have taken them again); the TLS handshake needs the block
  if (!inBootCheck) delay(2500);  // at boot the football/F1 tasks have not started yet

  httpUpdate.rebootOnUpdate(false); // remove automatic update
  TelnetStream.println(("Updating to " + id + " now!"));
  Update.onProgress([](size_t progresso, size_t total){
    int percentage = (progresso / (total / 100));
    int progressBar = (percentage * 48 / 100);
    static int prevPercentage = -1;
    static int prevProgress;

    // Called for every chunk written: only print when the percentage changes
    if (percentage == prevPercentage && updateInProgress) return;
    prevPercentage = percentage;

    Serial.printf("%s [FW Update] Progress: %u%%\r", currentTimeWithSeconds.c_str(), percentage);
    TelnetStream.printf("%s [FW Update] Progress: %u%%\r", currentTimeWithSeconds.c_str(), percentage);

    if (updateInProgress == false) {
      dma_display->fillScreen(0);
      dma_display->setTextColor(0x0412);
      dma_display->fillRect(0, 0, 64, 64, 0);
      dma_display->fillRect(6, (panelResY / 2) + 3, 52, 8, 0x0412);
      dma_display->fillRect(8, (panelResY / 2) + 5, 48, 4, 0);
      printCenter("UPDATING...", (panelResY / 2) - 5);
      #ifdef DOUBLE_BUFFER_ON
        dma_display->flipDMABuffer();
      #endif
      updateInProgress = true;
    }

    if (progressBar > 0 && prevProgress != progressBar) {
      #ifdef DOUBLE_BUFFER_ON
        dma_display->fillScreen(0);
        dma_display->setTextColor(0x0412);
        dma_display->fillRect(0, 0, 64, 64, 0);
        dma_display->fillRect(6, (panelResY / 2) + 3, 52, 8, 0x0412);
        dma_display->fillRect(8, (panelResY / 2) + 5, 48, 4, 0);
        printCenter("UPDATING...", (panelResY / 2) - 5);
      #endif
      dma_display->fillRect(8, (panelResY / 2) + 5, progressBar, 4, 0xd660);
      #ifdef DOUBLE_BUFFER_ON
        dma_display->flipDMABuffer();
      #endif
    }
    prevProgress = progressBar;

  });
  
  String file = "cw-cf-" + id + ".bin";
  String panelUrl = firmwareUrl(file, true), sharedUrl = firmwareUrl(file, false);
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  std::unique_ptr<WiFiClient> client = makeClient(panelUrl);
  t_httpUpdate_return customRet = httpUpdate.update(*client, panelUrl);
  if (customRet == HTTP_UPDATE_FAILED) client = makeClient(sharedUrl);
  t_httpUpdate_return ret = (customRet == HTTP_UPDATE_FAILED) ? httpUpdate.update(*client, sharedUrl) : customRet;
  dma_display->fillScreen(0);
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      TelnetStream.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
      dma_display->setTextColor(0xfb80);
      printCenter("UPDATE!", (panelResY / 2) - 5);
      printCenter("FAILED!", (panelResY / 2) + 5);
      #ifdef DOUBLE_BUFFER_ON
        dma_display->flipDMABuffer();
      #endif
      delay(5000);
      firmwareUpdating = false;
      updateInProgress = false;  // set by the progress callback; the clock loop needs it clear
      forceRefresh = true;
      break;

    case HTTP_UPDATE_NO_UPDATES:
      TelnetStream.println("HTTP_UPDATE_NO_UPDATES");
      dma_display->setTextColor(0xfb80);
      printCenter("UPDATE!", (panelResY / 2) - 5);
      printCenter("FAILED!", (panelResY / 2) + 5);
      #ifdef DOUBLE_BUFFER_ON
        dma_display->flipDMABuffer();
      #endif
      delay(5000);
      firmwareUpdating = false;
      updateInProgress = false;
      forceRefresh = true;
      break;

    case HTTP_UPDATE_OK:
      updateInProgress = false;
      printCenter("RESTARTING...", (panelResY / 2) - 5);
      #ifdef DOUBLE_BUFFER_ON
        dma_display->flipDMABuffer();
      #endif
      esp_restart();  
  }
}

static String md5Failure;  // why the last md5 fetch failed, for the log (a 404 on the per-panel file is overwritten by the shared one)

// Fetch the md5 that update-fw.sh publishes next to each firmware image.
// reachable is false when the server could not be connected to at all.
static String fetchServerMd5(const String &url, bool &reachable) {
  HTTPClient http;
  std::unique_ptr<WiFiClient> client = makeClient(url);
  String md5 = "";
  http.setConnectTimeout(5000);  // an HTTPS handshake is slow on the ESP32
  http.setTimeout(8000);
  reachable = false;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  size_t freeBefore = ESP.getFreeHeap(), blockBefore = ESP.getMaxAllocHeap();  // before the handshake
  size_t lowestBefore = ESP.getMinFreeHeap();
  if (http.begin(*client, url)) {
    int code = http.GET();
    reachable = code > 0;
    if (code == HTTP_CODE_OK) {
      md5 = http.getString();
      md5.trim();
    }
    if (md5.length() != 32) {
      // Negative codes are connection errors (e.g. -1 refused or handshake failed, -11 read timeout)
      String why = code > 0 ? "HTTP " + String(code) : HTTPClient::errorToString(code);
      md5Failure = why + (code == HTTP_CODE_OK ? ", " + String(md5.length()) + " chars" : "");
    }
  } else {
    md5Failure = "could not start the request";
  }
  if (md5.length() != 32 && !reachable) {
    // -1 covers DNS, TCP, TLS and timeouts alike: say which one it was
    char tlsError[100] = "";
    if (url.startsWith("https://")) static_cast<WiFiClientSecure *>(client.get())->lastError(tlsError, sizeof(tlsError));
    int hostStart = url.indexOf("//") + 2;
    String host = url.substring(hostStart, url.indexOf('/', hostStart));
    IPAddress ip;
    md5Failure += " [tls: " + String(tlsError[0] ? tlsError : "none") + ", dns " + host + ": " +
                  (WiFi.hostByName(host.c_str(), ip) == 1 ? ip.toString() : String("failed")) +
                  ", wifi " + String(WiFi.status()) + ", rssi " + String(WiFi.RSSI()) +
                  ", before the handshake free " + String(freeBefore) + " block " + String(blockBefore) +
                  ", lowest before " + String(lowestBefore) +
                  ", lowest ever " + String(ESP.getMinFreeHeap()) + "]";
  }
  if (md5.length() != 32) {
    md5Failure += " (" + url + ", heap free " + String(ESP.getFreeHeap()) + ", largest block " +
                  String(ESP.getMaxAllocHeap()) + ")";
  }
  http.end();
  return md5;
}

// The md5 of the latest build for this panel: its own file, else the shared
// one. Empty when there is none or the server can't be reached.
static String fetchLatestMd5() {
  String file = "cw-cf-" CW_FW_ID ".md5";
  bool reachable;
  String md5 = fetchServerMd5(firmwareUrl(file, true), reachable);
  // No per-panel file (404) falls back to the shared one; no server at all doesn't
  if (md5.length() == 0 && reachable) md5 = fetchServerMd5(firmwareUrl(file, false), reachable);
  return md5;
}

// The md5 lookup blocks on the network (seconds when the update server is busy), so it runs in
// its own task and the main loop, which animates the clockface, just polls for the result.
enum Md5Fetch { MD5_IDLE, MD5_RUNNING, MD5_DONE };
static volatile Md5Fetch md5Fetch = MD5_IDLE;
static String md5Result;               // written by the task before it sets MD5_DONE
static bool md5ForValidation = false;  // what the running fetch is for: proving a new build, or an update check
static bool md5IgnoreSkips = false;
static bool checkPending = false;      // an update check was asked for and waits for the fetch task
static bool checkPendingForce = false;

static bool md5HoldsDownloads = false;  // this fetch set firmwareUpdating, so it must clear it

static void md5Task(void *) {
  // The football/F1 tasks stop starting downloads while firmwareUpdating is set; give one that is
  // already running a moment to finish and free its memory, the TLS handshake needs a big block
  if (md5HoldsDownloads) vTaskDelay(pdMS_TO_TICKS(2500));
  md5Result = fetchLatestMd5();
  md5Fetch = MD5_DONE;
  vTaskDelete(NULL);
}

static bool startMd5Fetch(bool forValidation, bool ignoreSkips) {
  if (md5Fetch != MD5_IDLE) return false;
  md5ForValidation = forValidation;
  md5IgnoreSkips = ignoreSkips;
  md5Fetch = MD5_RUNNING;
  if (!firmwareUpdating) {
    firmwareUpdating = true;
    md5HoldsDownloads = true;
  }
  if (xTaskCreate(md5Task, "md5fetch", 10240, NULL, 1, NULL) != pdPASS) {
    md5Fetch = MD5_IDLE;
    if (md5HoldsDownloads) firmwareUpdating = false;
    md5HoldsDownloads = false;
    return false;
  }
  return true;
}

// A new firmware must prove itself before the bootloader keeps it: it has to
// run and be able to fetch version info from the update server, the same way
// checkForUpdate() does. A build that can't update itself would be stuck on
// the panel, so it rolls back instead.
void checkFirmwareValid(int64_t now) {
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY) {
    fwValidated = true;
    return;
  }
  if (now > VALIDATE_AFTER_MS && now >= validateDue && wifi.isConnected()) {
    validateDue = now + VALIDATE_RETRY_MS;
    startMd5Fetch(true, false);  // the result is handled in pollMd5Fetch()
  }
  if (now - wifiUpAt > ROLLBACK_AFTER_MS) {
    TelnetStream.println(currentTimeWithSeconds + " [Update] New firmware could not check for updates, rolling back");
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}

// The automatic daily check stays quiet between two hours of the day (22:00 to 08:00 unless changed
// in the web UI), so nobody is woken by a restart. From = until means never quiet. Checking by hand
// (web UI, telnet) still works. Without a synced clock the hour is unknown.
static bool inUpdateQuietHours() {
  int from = ClockwiseParams::getInstance()->updQuietFrom, until = ClockwiseParams::getInstance()->updQuietUntil;
  if (from == until || ezt::timeStatus() != timeSet) return false;
  int hour = cwDateTime.getHour24();
  return from < until ? (hour >= from && hour < until) : (hour >= from || hour < until);
}

static unsigned long restartAt = 0;  // millis() of a restart for a clean update check, 0 for none
static String bootLog;               // what the check right after a restart said: nobody is on telnet yet, so it is replayed later

static void updateLog(const String &msg) {
  TelnetStream.println(currentTimeWithSeconds + msg);
  if (inBootCheck) bootLog += "[after the restart]" + msg + "\n";
}

// Restarts so the update check runs again right after the boot (see bootUpdateCheck()), where it can install.
// Safety net: the same restart reason (the build, or "memory") is only tried UPDATE_MAX_RESTARTS times in a
// row, so a boot that fails to install can never turn into an endless restart loop. The count is cleared
// when the firmware is up to date (see applyUpdateCheck()). Returns false when it gave up.
static bool restartForUpdateCheck(const char *msg, const String &knownMd5 = "") {
  Preferences prefs;
  prefs.begin("fwupdate", false);
  String reason = knownMd5.length() == 32 ? knownMd5 : String("memory");
  uint8_t restarts = (prefs.getString("restartFor", "") == reason) ? prefs.getUChar("restarts", 0) : 0;
  if (restarts >= UPDATE_MAX_RESTARTS) {
    prefs.end();
    return false;
  }
  prefs.putString("restartFor", reason);
  prefs.putUChar("restarts", restarts + 1);
  prefs.putBool("bootCheck", true);
  if (knownMd5.length() == 32) prefs.putString("bootMd5", knownMd5);  // the boot installs it without asking the server again
  prefs.end();
  ClockwiseWebServer::getInstance()->update_status = "restarting";
  restartAt = millis() + (knownMd5.length() ? 1000 : 3000);  // the web UI sees the status first
  if (msg) updateLog(msg);
  return true;
}

// Switching clockface restarts first, like an update: the boot downloads the new face with a whole heap
// (see bootUpdateCheck()), whatever the current face (Football, F1) has allocated.
static void restartForFace(const String &id) {
  Preferences prefs;
  prefs.begin("fwupdate", false);
  prefs.putString("bootFace", id);
  prefs.end();
  ClockwiseWebServer::getInstance()->update_status = "restarting";
  restartAt = millis() + 1000;  // the web UI sees the status first
  TelnetStream.println(currentTimeWithSeconds + " [Face] Switching to " + id + ", restarting to install it");
}

static bool userCheck = false;  // asked for by a person (web UI, telnet): goes through even during a live match

// Compare the running firmware with the one on the update server and install
// it when they differ.
// ignoreSkips = telnet 'X': also retry builds that failed or rolled back.
static void applyUpdateCheck(const String &md5, bool ignoreSkips) {
  String &status = ClockwiseWebServer::getInstance()->update_status;  // what the web UI shows

  if (md5.length() != 32) {
    updateLog(" [Update] No version info on server: " + md5Failure);
    status = "noserver";
    // Out of memory for the TLS handshake: a restart starts with a whole heap, so check again there
    if (md5Failure.indexOf("emory") >= 0 && !inBootCheck && fwValidated) {
      if (!restartForUpdateCheck(" [Update] Low on memory, restarting to check again"))
        updateLog(" [Update] Still low on memory after restarts, giving up until the next check");
    }
    return;
  }
  if (md5.equalsIgnoreCase(ESP.getSketchMD5())) {
    updateLog(" [Update] Firmware is up to date");
    status = "uptodate";
    Preferences done;
    done.begin("fwupdate", false);
    done.remove("restarts");
    done.end();
    return;
  }
  // An install of this exact build was started and we are still on the old
  // firmware, so it was rolled back: leave it alone until a new build is up
  Preferences prefs;
  prefs.begin("fwupdate", false);
  if (!ignoreSkips && prefs.getString("tried", "") == md5) {
    updateLog(" [Update] Skipping build that failed before (X to force)");
    status = "skipped";
    prefs.end();
    return;
  }
  if (!ignoreSkips && prefs.getString("failMd5", "") == md5 && prefs.getUChar("fails", 0) >= UPDATE_MAX_TRIES) {
    updateLog(" [Update] Giving up on this build after repeated download failures (X to force)");
    status = "skipped";
    prefs.end();
    return;
  }
  if (liveEventOn && !userCheck) {  // a match started while the check ran: look again once it is over
    prefs.end();
    updateLog(" [Update] New firmware available, waiting until the live match is over");
    updateCheckDue = millis();
    return;
  }
  // Every clock installs from a fresh boot: the heap is whole there, so the download and the TLS
  // handshake fit whatever the clockface (Football, F1) has allocated
  if (!inBootCheck && !ignoreSkips && fwValidated) {  // telnet X installs on the spot
    prefs.end();
    if (restartForUpdateCheck(" [Update] New firmware available, restarting to install it", md5)) return;
    updateLog(" [Update] New firmware available, but restarting did not install it, giving up (X to force)");
    status = "skipped";
    return;
  }
  prefs.putString("tried", md5);
  prefs.end();

  updateLog(" [Update] New firmware available, installing");
  status = "installing";
  updateFirmware(CW_FW_ID);
  status = "failed";  // only reached when the download failed, success restarts

  // Only reached when the download failed (success restarts): allow a retry,
  // but count it so one broken upload does not retry forever
  prefs.begin("fwupdate", false);
  prefs.remove("tried");
  uint8_t fails = (prefs.getString("failMd5", "") == md5) ? prefs.getUChar("fails", 0) + 1 : 1;
  prefs.putString("failMd5", md5);
  prefs.putUChar("fails", fails);
  prefs.end();
}

// Asks for an update check; the fetch runs in the background and pollMd5Fetch() finishes the job.
void checkForUpdate(bool ignoreSkips = false, bool byUser = false) {
  checkPending = true;
  userCheck = userCheck || byUser;
  checkPendingForce = checkPendingForce || ignoreSkips;
}

// Called every loop: starts a wanted check and handles a finished fetch.
static void pollMd5Fetch() {
  if (md5Fetch == MD5_IDLE && checkPending) {
    if (liveEventOn && !userCheck) return;  // not during a live match or session: starts when it ends
    if (startMd5Fetch(false, checkPendingForce)) {
      checkPending = false;
      checkPendingForce = false;
    }
    return;
  }
  if (md5Fetch != MD5_DONE) return;
  String md5 = md5Result;
  bool forValidation = md5ForValidation, ignoreSkips = md5IgnoreSkips;
  md5Fetch = MD5_IDLE;
  if (md5HoldsDownloads) firmwareUpdating = false;  // applyUpdateCheck sets it again for an install
  md5HoldsDownloads = false;
  if (forValidation) {
    if (md5.length() == 32) {
      esp_ota_mark_app_valid_cancel_rollback();
      fwValidated = true;
      updateLog(" [Update] New firmware confirmed");
    } else {
      updateLog(" [Update] New firmware can't reach the update server yet: " + md5Failure);
    }
    return;
  }
  applyUpdateCheck(md5, ignoreSkips);
  userCheck = false;
}

// Early in the boot, before the clockface starts its downloads, the heap is still whole and the secure
// connection to the update server fits. Two things happen here:
//  - a build that has not been confirmed yet proves it can reach the update server (the loop does it
//    too, but there the heap is often too small and the build would roll back after a few minutes)
//  - a check that ran out of memory leaves a flag and restarts (see applyUpdateCheck()); here the next
//    boot runs the check once more and installs what it finds
static void waitForFetch(const char *text) {
  dma_display->setFont(&smallMessageFont);
  dma_display->setTextColor(0x0412);
  dma_display->fillScreen(0);
  printCenter(text, (panelResY / 2) - 5);
  #ifdef DOUBLE_BUFFER_ON
    dma_display->flipDMABuffer();
  #endif
  unsigned long giveUp = millis() + 30000;
  while (md5Fetch != MD5_DONE && (long)(millis() - giveUp) < 0) {
    ClockwiseWebServer::getInstance()->handleHttpRequest();  // the web page and telnet stay reachable meanwhile
    delay(50);
  }
  pollMd5Fetch();  // handles the result; the loop picks up a fetch that is still running
  dma_display->fillScreen(0);
  #ifdef DOUBLE_BUFFER_ON
    dma_display->flipDMABuffer();
  #endif
}

static void bootUpdateCheck() {
  inBootCheck = true;
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  bool pending = esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
  if (!pending) fwValidated = true;  // already confirmed: the restart for an update must be able to install right here
  if (pending && startMd5Fetch(true, false))
    waitForFetch("CHECKING...");

  Preferences prefs;
  prefs.begin("fwupdate", false);
  bool wanted = prefs.getBool("bootCheck", false);
  if (wanted) prefs.remove("bootCheck");
  String knownMd5 = prefs.getString("bootMd5", "");
  prefs.remove("bootMd5");
  String face = prefs.getString("bootFace", "");  // removed first: a failed download can't restart again
  prefs.remove("bootFace");
  prefs.end();
  if (face.length()) {
    TelnetStream.println("[Face] Installing the clockface chosen before the restart");
    updateFirmware(face);  // restarts on success; on failure the current firmware carries on
  }
  if (wanted && fwValidated) {
    if (knownMd5.length() == 32) {  // the check before the restart already found the build: skip a second TLS handshake
      TelnetStream.println("[Update] Installing the update found before the restart");
      applyUpdateCheck(knownMd5, false);
    } else {
      TelnetStream.println("[Update] Checking for an update right after a restart");
      if (startMd5Fetch(false, false)) waitForFetch("CHECKING...");
    }
  }
  inBootCheck = false;
}

// A build that crashes over and over (panic, watchdog) without staying up for CRASH_STABLE_MS in between is
// rolled back to the previous firmware. That build is also marked as tried, so the older firmware
// doesn't install it again at its next check. Restarts we ask for ourselves don't count.
static const uint8_t CRASH_LIMIT = 3;
static const unsigned long CRASH_STABLE_MS = 10 * 60 * 1000UL;
static bool crashesCleared = false;

static void crashGuard() {
  esp_reset_reason_t reason = esp_reset_reason();
  if (reason != ESP_RST_PANIC && reason != ESP_RST_INT_WDT && reason != ESP_RST_TASK_WDT && reason != ESP_RST_WDT) return;
  Preferences prefs;
  prefs.begin("fwupdate", false);
  uint8_t crashes = prefs.getUChar("crashes", 0) + 1;
  if (crashes >= CRASH_LIMIT) {
    prefs.putString("tried", ESP.getSketchMD5());
    prefs.remove("crashes");
    prefs.end();
    esp_ota_mark_app_invalid_rollback_and_reboot();  // returns when there is no earlier firmware to go back to
    return;
  }
  prefs.putUChar("crashes", crashes);
  prefs.end();
}

// For the uptime lines: the crashes still counted against this build
static String crashCountText() {
  Preferences prefs;
  prefs.begin("fwupdate", true);
  uint8_t crashes = prefs.getUChar("crashes", 0);
  prefs.end();
  return String(", crash count ") + crashes + "/" + CRASH_LIMIT;
}

void loop() {
    int64_t now = millis();  // Keep this single now declaration
    if (!crashesCleared && now > CRASH_STABLE_MS) {  // it stayed up: earlier crashes no longer count
      crashesCleared = true;
      Preferences prefs;
      prefs.begin("fwupdate", false);
      prefs.remove("crashes");
      prefs.end();
    }
    if (bootLog.length() && millis() > 20000) {  // a telnet session is probably open by now; 'U' shows it again
      TelnetStream.print(bootLog);
      bootLog = "";
    }
    if (restartAt && (long)(millis() - restartAt) >= 0) {
      TelnetStream.stop();
      delay(100);
      ESP.restart();
    }
    if (!fwValidated) checkFirmwareValid(now);
    pollMd5Fetch();
#ifdef CW_TEST_CRASH
    if (now > 10000) abort(); // rollback test build: die before it can be confirmed
#endif
    wifi.handleImprovWiFi();
    char hour[3] = {0};
    char minute[3] = {0};
    char second[3] = {0};
    snprintf(hour, sizeof(hour), "%02d", cwDateTime.getHour());
    snprintf(minute, sizeof(minute), "%02d", cwDateTime.getMinute());
    snprintf(second, sizeof(second), "%02d", cwDateTime.getSecond());
    currentTimeWithSeconds = String(hour) + ":" + String(minute) + ":" + String(second);
    currentTime = String(hour) + ":" + String(minute);
    if ((altDisplay == 1 && nightMode == false) || (altDisplay > 1 && nightMode == true)) lastTime = false;

    if (now > ldrCheckDue) automaticBrightControl();
    
    if (wifi.isConnected())
    {
        ClockwiseWebServer::getInstance()->handleHttpRequest();
        if (ClockwiseWebServer::getInstance()->brightness_changed) {
          ClockwiseWebServer::getInstance()->brightness_changed = false;
          applyBrightnessSettings();
        }
        if (ClockwiseWebServer::getInstance()->update_requested) {
          ClockwiseWebServer::getInstance()->update_requested = false;
          TelnetStream.println(currentTimeWithSeconds + " [INFO] Web UI asked for a firmware update check...");
          checkForUpdate(false, true);
        }
        if (ClockwiseWebServer::getInstance()->face_requested.length() > 0 && md5Fetch == MD5_IDLE) {  // not while a fetch holds the heap
          String id = ClockwiseWebServer::getInstance()->face_requested;
          ClockwiseWebServer::getInstance()->face_requested = "";
          restartForFace(id);
        }
        ClockPeers::getInstance()->loop();
        ezt::events();
        // Daily update check, not in the quiet hours and not during a live match or session. The timer
        // only advances when it runs, so a check that comes due during them happens when they end.
        if ((long)(millis() - updateCheckDue) >= 0 && fwValidated && updateInProgress == false && !liveEventOn && !inUpdateQuietHours()) {
          updateCheckDue = millis() + UPDATE_CHECK_MS;
          checkForUpdate();
        }
        int c = TelnetStream.read();  // -1 when nothing was typed
        switch (c) {
          case 'R':
            TelnetStream.println(currentTimeWithSeconds + " [INFO] Restarting device...");
            esp_ota_mark_app_valid_cancel_rollback();  // asked for, so not a failed update
            TelnetStream.stop();
            delay(100);
            ESP.restart();
            break;
          case 'C':
            TelnetStream.println(currentTimeWithSeconds + " [INFO] Checking for a firmware update...");
            checkForUpdate();
            break;
          case 'X':
            TelnetStream.println(currentTimeWithSeconds + " [INFO] Forcing a firmware update, ignoring earlier failures...");
            checkForUpdate(true, true);
            break;
          case 'L':
            logLDR = (logLDR == false) ? true : false;
            if (logLDR == false) {
              TelnetStream.println(currentTimeWithSeconds + " [INFO] Turning off LDR sensor output...");
            } else {
              TelnetStream.println(currentTimeWithSeconds + " [INFO] Turning on LDR sensor output...");
            }
            break;
          case 'U':
            TelnetStream.println(currentTimeWithSeconds + " [INFO] Uptime: " + uptime_formatter::getUptime() + crashCountText() + ", reset reason " +
                                 String((int)esp_reset_reason()) + " (1 power on, 3 software, 4 panic/exception, 5-7 watchdog, 9 brownout), heap free " +
                                 String(ESP.getFreeHeap()) + ", largest block " + String(ESP.getMaxAllocHeap()) + ", lowest ever " +
                                 String(ESP.getMinFreeHeap()));
            break;
          case 'P': {
            // Panel height has no web UI and can't be detected: flip it here and reboot
            uint8_t newHeight = (ClockwiseParams::getInstance()->displayHeight == 64) ? 32 : 64;
            ClockwiseParams::getInstance()->displayHeight = newHeight;
            ClockwiseParams::getInstance()->save();
            TelnetStream.println(currentTimeWithSeconds + " [INFO] Panel height set to " + String(newHeight) + " rows, restarting...");
            esp_ota_mark_app_valid_cancel_rollback();
            TelnetStream.stop();
            delay(100);
            ESP.restart();
            break;
          }
          case 'T':
            if (millis() < birthdayPreviewUntil) {
              birthdayPreviewUntil = 0;
              TelnetStream.println(currentTimeWithSeconds + " [INFO] Birthday preview stopped");
            } else {
              birthdayPreviewUntil = millis() + 60000;
              TelnetStream.println(currentTimeWithSeconds + " [INFO] Showing the birthday screen for a minute (T again to stop)");
            }
            break;
#ifdef CW_FOOTBALL_SIM
          case 'S':
            TelnetStream.println(currentTimeWithSeconds + " [INFO] Football simulator: " + clockface->simulate());
            break;
          case 'N':
            TelnetStream.println(currentTimeWithSeconds + " [INFO] Football simulator: " + clockface->simulateNext());
            break;
          case 'G':
            TelnetStream.println(currentTimeWithSeconds + " [INFO] Celebrating a test goal...");
            clockface->testGoal();
            break;
          case 'Y':
            TelnetStream.println(currentTimeWithSeconds + " [INFO] Showing a test yellow card...");
            clockface->testIncident('y');
            break;
          case 'D':
            TelnetStream.println(currentTimeWithSeconds + " [INFO] Showing a test red card...");
            clockface->testIncident('r');
            break;
          case 'W':
            TelnetStream.println(currentTimeWithSeconds + " [INFO] Showing a test substitution...");
            clockface->testIncident('s');
            break;
#endif
          case 'h':
            TelnetStream.println("R - Restart device");
            TelnetStream.println("C - Check for a firmware update now");
            TelnetStream.println("X - Same, but retry builds that failed before");
            TelnetStream.println("L - Toggle LDR Data");
            TelnetStream.println("U - Print uptime");
            TelnetStream.println("P - Switch panel height to " + String(ClockwiseParams::getInstance()->displayHeight == 64 ? 32 : 64) + " rows and restart");
            for (size_t i = 0; i < CW_FACE_COUNT; i++)
              TelnetStream.println(String(CW_FACES[i].key) + " - Install latest '" + CW_FACES[i].name + "' firmware");
            TelnetStream.println("T - Show the birthday screen for a minute (again to stop)");
#ifdef CW_FOOTBALL_SIM
            TelnetStream.println("S, N - Football simulator of the Formula 1 face");
            TelnetStream.println("G - Celebrate a test goal");
            TelnetStream.println("Y - Show a test yellow card");
            TelnetStream.println("D - Show a test red card");
            TelnetStream.println("W - Show a test substitution");
#endif
            break;
          default:  // the key of a clock face (faces.json) installs it
            for (size_t i = 0; i < CW_FACE_COUNT; i++)
              if (CW_FACES[i].key == c) restartForFace(CW_FACES[i].id);
            break;
        }
        
        // Handle notifications
        notificationServer.handle();
        
        if (!showingNotification && notificationServer.hasNotification()) {
            auto notification = notificationServer.getNextNotification();
            currentNotification = notification.first;
            notificationDuration = notification.second;  // Use duration from message
            showingNotification = true;
            notificationStartTime = now;
            flashState = true;
        }

        if (showingNotification) {
            bool shouldShowText = (now % 1000) < 500;
            if (shouldShowText != flashState) {
                flashState = shouldShowText;
                if (shouldShowText) {
                    dma_display->fillRect(0, 0, 64, 64, 0xffff);  // green background
                    dma_display->setFont(&smallMessageFont);  // Use smallMessageFont instead of Picopixel
                    dma_display->setTextColor(0x0000);  // Black text
                } else {
                    dma_display->fillRect(0, 0, 64, 64, 0x0000);  // Black background
                    dma_display->setFont(&smallMessageFont);  // Use smallMessageFont instead of Picopixel
                    dma_display->setTextColor(0xffff);  // Green text
                }
                
                // Display text in both cases
                String words[20];
                int wordCount = 0;
                String temp = currentNotification;
                
                while (temp.length() > 0 && wordCount < 20) {
                    int spaceIndex = temp.indexOf(' ');
                    if (spaceIndex == -1) {
                        words[wordCount++] = temp;
                        break;
                    }
                    words[wordCount++] = temp.substring(0, spaceIndex);
                    temp = temp.substring(spaceIndex + 1);
                }
                
                int16_t y = 5;
                String currentLine = "";
                
                for (int i = 0; i < wordCount; i++) {
                    String testLine = currentLine;
                    if (testLine.length() > 0) testLine += " ";
                    testLine += words[i];
                    
                    int16_t x1, y1;
                    uint16_t w, h;
                    dma_display->getTextBounds(testLine.c_str(), 0, 0, &x1, &y1, &w, &h);
                    
                    if (w > 58 && currentLine.length() > 0) {
                        dma_display->setCursor(1, y);
                        // dma_display->print(currentLine);
                        printCenter(currentLine.c_str(), y);

                        y += 8;
                        currentLine = words[i];
                    } else {
                        if (currentLine.length() > 0) currentLine += " ";
                        currentLine += words[i];
                    }
                }

                if (currentLine.length() > 0) {
                    dma_display->setCursor(1, y);
                    printCenter(currentLine.c_str(), y);
                }
                
                #ifdef DOUBLE_BUFFER_ON
                    dma_display->flipDMABuffer();
                #endif
            }
            
            if (now - notificationStartTime >= notificationDuration) {
                showingNotification = false;
                forceFullRefresh = true;
                // dma_display->fillRect(0, 0, 64, 64, 0);
                dma_display->setTextColor(0xFFFF);
                clockface->setup(&cwDateTime);
                #ifdef DOUBLE_BUFFER_ON
                    dma_display->flipDMABuffer();
                #endif
                lastTime = "";  // Force time redraw
            }
            return;
        }
    }

    if (wifi.connectionSucessfulOnce && ( now > loopDue || now < lastNow ) && updateInProgress == false)
    {
      // Not while a match or an F1 session is live: that is what the clock is for then
      // Not before the time is known either: until the first NTP sync the date reads 1 January 1970
      const Birthdays::Entry *birthday = (liveEventOn || ezt::timeStatus() != timeSet) ? nullptr
                                         : todaysBirthday(cwDateTime.getYear(), cwDateTime.getMonth(), cwDateTime.getDay());
      if (millis() < birthdayPreviewUntil) birthday = &BIRTHDAY_PREVIEW;
      if (nightMode == true) {
        if (currentTime != lastTime || altDisplay != 1) {
          dma_display->fillRect(0, 0, 64, 64, 0);
          dma_display->setTextColor(ClockwiseParams::getInstance()->nightColor());

          // dma_display->setTextColor(0x01c0); // green
          // dma_display->setTextColor(0x0007); // blue
          dma_display->setFont(&nightFont);
          printCenter(currentTime.c_str(), panelResY/2);
          #ifdef DOUBLE_BUFFER_ON
            dma_display->flipDMABuffer();
          #endif

          altDisplay = 1;
        }
      } else if (birthday != nullptr && BirthdayAnim::begin()) {
        // Animated, so drawn on every pass of the loop, not only when the minute changes
        int age = birthday->year ? cwDateTime.getYear() - birthday->year : 0;
        BirthdayAnim::draw(dma_display, panelResY, birthday->name, age, currentTime.c_str());
        #ifdef DOUBLE_BUFFER_ON
          dma_display->flipDMABuffer();
        #endif

        altDisplay = 6;
      } else {
        if (altDisplay > 0 || forceRefresh == true) {
          dma_display->setTextColor(0xFFFF);
          clockface->setup(&cwDateTime);
          #ifdef DOUBLE_BUFFER_ON
            dma_display->flipDMABuffer();
          #endif
          altDisplay = 0;
          lastTime = false;
          forceRefresh = false;
        }
        clockface->update();
      }
      if (altDisplay != 6) BirthdayAnim::release();
      if (currentTime != lastTime) {
        TelnetStream.println(currentTimeWithSeconds + " [INFO] Uptime: " + uptime_formatter::getUptime() + crashCountText());
      }
      lastTime = currentTime;
      loopDue = now + (altDisplay == 6 ? BIRTHDAY_FRAME_MS : CLOCKFACE_UPDATE_MS);
    }
    lastNow = now;
}

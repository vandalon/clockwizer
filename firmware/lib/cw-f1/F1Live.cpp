#include "F1Live.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <TelnetStream.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ezTime.h>

extern volatile bool firmwareUpdating;  // main.cpp

std::mutex f1TlsLock;
std::mutex f1NetLock;
std::atomic<bool> f1Park{false};
std::atomic<bool> f1TaskUp{false};
void (*f1PauseMdns)(bool pause) = nullptr;
RTC_NOINIT_ATTR char f1Phase[16];

static const char HOST[] = "livetiming.formula1.com";
static const unsigned long PING_MS = 15 * 1000UL;          // SignalR drops a client that stays quiet
static const unsigned long SILENCE_MS = 40 * 1000UL;       // nothing at all from the server: reconnect
static const unsigned long FRESH_MS = 45 * 1000UL;         // data older than this isn't shown
static const unsigned long UNWANTED_MS = 3 * 60 * 1000UL;  // how long after the session the connection stays
static const unsigned long RETRY_MS = 15 * 1000UL;
// Memory that malloc can hand out for data: ESP.getFreeHeap() and getMaxAllocHeap() include the 45 KB of unused
// instruction RAM, which a TLS handshake or a socket buffer cannot use
static size_t dataFree() { return heap_caps_get_free_size(MALLOC_CAP_8BIT); }
static size_t dataBlock() { return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT); }

// A TLS handshake needs about 55 KB of data RAM when it starts (measured: it works with 64 KB and fails with 49 KB) and
// the connection keeps about 45 KB. Below that the feed stays off rather than starve the clock: a failed handshake
// fragments the heap and the other downloads fail too.
static const uint32_t TASK_STACK = 7168;     // it uses 4.6 KB
static const size_t TLS_MIN_FREE = 56000;
static const size_t TLS_MIN_BLOCK = 17500;   // one of the connection's two 16 KB buffers
static const size_t LOW_HEAP = 5000;         // data RAM under which a running connection is dropped
static const char RS = 0x1E;  // SignalR ends every message with this

static void liveLog(const char *format, ...) {
  char line[160];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  Serial.print(line);
  TelnetStream.print(line);
}

// The task, with its stack, only exists once a session has been wanted: a quiet week doesn't pay for it
void F1Live::setWanted(bool wanted) {
  _wanted = wanted;
  if (wanted && !_started) {
    _started = true;
    // Core 0, like the other downloads.
    xTaskCreatePinnedToCore(task, "f1live", TASK_STACK, this, 1, nullptr, 0);
  }
}

bool F1Live::fresh() {
  std::lock_guard<std::mutex> guard(_lock);
  return _lastData && millis() - _lastData <= FRESH_MS && _state.hasOrder();
}

bool F1Live::get(char tag, F1LiveState::Row *rows, int &count, int &lap, int &totalLaps, char &flag, bool &finished) {
  std::lock_guard<std::mutex> guard(_lock);
  if (!_lastData || millis() - _lastData > FRESH_MS || !_state.hasOrder()) return false;
  count = _state.top(tag, rows);
  lap = _state.lap();
  totalLaps = _state.totalLaps();
  flag = _state.flag();
  finished = _state.finished();
  return count > 0;
}

int F1Live::part() {
  std::lock_guard<std::mutex> guard(_lock);
  return _state.part();
}

int F1Live::clockSecs(long nowUtc) {
  std::lock_guard<std::mutex> guard(_lock);
  return _state.clockSecs(nowUtc);
}

void F1Live::task(void *self) {
  F1Live *live = static_cast<F1Live *>(self);
  for (;;) {
    if (firmwareUpdating || !live->_wanted || WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }
    live->run();
    {
      std::lock_guard<std::mutex> guard(live->_lock);
      live->_lastData = 0;
    }
    vTaskDelay(pdMS_TO_TICKS(RETRY_MS));
  }
}

// SignalR's first step: a connection token for the WebSocket
bool F1Live::negotiate(char *token, size_t size, char *cookie, size_t cookieSize) {
  WiFiClientSecure client;
  client.setInsecure();  // public data, nothing to protect
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, "https://livetiming.formula1.com/signalrcore/negotiate?negotiateVersion=1")) return false;
  http.setUserAgent("BestHTTP");
  const char *keep[] = {"Set-Cookie"};
  http.collectHeaders(keep, 1);
  int code = http.POST("");
  if (code != HTTP_CODE_OK) {
    liveLog("[F1 live] negotiate: HTTP %d (%s), heap free %u, largest block %u\n", code, HTTPClient::errorToString(code).c_str(),
            (unsigned)dataFree(), (unsigned)dataBlock());
    http.end();
    return false;
  }
  // The load balancer's cookie: the WebSocket must reach the same server that issued the token
  strlcpy(cookie, http.header("Set-Cookie").c_str(), cookieSize);
  char *end = strchr(cookie, ';');
  if (end) *end = 0;
  StaticJsonDocument<32> filter;
  filter["connectionToken"] = true;
  StaticJsonDocument<192> doc;
  DeserializationError err = deserializeJson(doc, http.getString(), DeserializationOption::Filter(filter));
  http.end();
  const char *value = doc["connectionToken"];
  if (err || !value) return false;
  strlcpy(token, value, size);
  return true;
}

// A WebSocket's messages as one stream of text: frames come and go underneath, a SignalR
// message ends at RS. Keeps the connection alive meanwhile.
struct WsStream {
  WiFiClientSecure &client;
  uint8_t buffer[512];
  size_t at = 0, have = 0;
  uint64_t remaining = 0;  // payload left in the current frame
  bool atEnd = false;      // read the RS of the current message
  bool failed = false;
  unsigned long lastPing = 0, lastData = 0;
  size_t count = 0;

  explicit WsStream(WiFiClientSecure &c) : client(c) { lastPing = lastData = millis(); }

  void send(uint8_t opcode, const uint8_t *data, size_t length) {
    uint8_t frame[280];  // control frames and our few messages are short; the server needs them masked
    if (length > 250) return;
    uint32_t mask = esp_random();
    size_t at = 2;
    frame[0] = 0x80 | opcode;
    if (length < 126) {
      frame[1] = 0x80 | length;
    } else {
      frame[1] = 0x80 | 126;  // 16 bit length
      frame[2] = length >> 8;
      frame[3] = length & 0xFF;
      at = 4;
    }
    memcpy(frame + at, &mask, 4);
    for (size_t i = 0; i < length; i++) frame[at + 4 + i] = data[i] ^ ((uint8_t *)&mask)[i % 4];
    client.write(frame, at + 4 + length);
  }

  void sendText(const char *text) { send(0x1, (const uint8_t *)text, strlen(text)); }

  void keepAlive() {
    if (millis() - lastPing >= PING_MS) {
      lastPing = millis();
      static const char ping[] = "{\"type\":6}\x1e";
      sendText(ping);
    }
  }

  // The next byte off the socket, waiting for it
  int raw() {
    while (at >= have) {
      if (failed) return -1;
      int n = client.read(buffer, sizeof(buffer));
      if (n > 0) {
        at = 0;
        have = n;
        lastData = millis();
        break;
      }
      if (!client.connected() && !client.available()) { failed = true; return -1; }
      if (millis() - lastData > SILENCE_MS) { failed = true; return -1; }
      keepAlive();
      vTaskDelay(pdMS_TO_TICKS(5));
    }
    return buffer[at++];
  }

  // The next byte of payload, over frame headers and control frames
  int payload() {
    while (remaining == 0) {
      int b0 = raw(), b1 = raw();
      if (b0 < 0 || b1 < 0) return -1;
      uint8_t opcode = b0 & 0x0F;
      uint64_t length = b1 & 0x7F;
      int extra = length == 126 ? 2 : length == 127 ? 8 : 0;
      if (extra) {
        length = 0;
        for (int i = 0; i < extra; i++) {
          int b = raw();
          if (b < 0) return -1;
          length = length << 8 | b;
        }
      }
      if (b1 & 0x80)  // the server doesn't mask; skip a mask if there is one
        for (int i = 0; i < 4; i++) raw();
      if (opcode == 0x8) { failed = true; return -1; }  // close
      if (opcode == 0x9 || opcode == 0xA) {              // ping: answer with the same payload; pong: ignore
        uint8_t data[125];
        size_t n = length > sizeof(data) ? sizeof(data) : length;
        for (size_t i = 0; i < length; i++) {
          int b = raw();
          if (b < 0) return -1;
          if (i < n) data[i] = b;
        }
        if (opcode == 0x9) send(0xA, data, n);
        continue;
      }
      remaining = length;
    }
    remaining--;
    return raw();
  }

  // For the scanner in F1LiveState: the current message, then end of input
  static int nextChar(void *self) { return ((WsStream *)self)->read(); }

  int read() {
    if (atEnd) return -1;
    if (++count % 1024 == 0) vTaskDelay(1);
    int c = payload();
    if (c == RS) { atEnd = true; return -1; }
    return c;
  }

  size_t readBytes(char *to, size_t length) {
    size_t n = 0;
    for (; n < length; n++) {
      int c = read();
      if (c < 0) break;
      to[n] = c;
    }
    return n;
  }

  // Past the end of the current message, and ready for the next
  void nextMessage() {
    while (!atEnd && !failed) {
      int c = payload();
      if (c < 0 || c == RS) break;
    }
    atEnd = false;
  }
};

// One connection, from the handshake until it ends or is no longer wanted
void F1Live::run() {
  char token[96], cookie[200];
  WiFiClientSecure client;
  bool mdnsOff = false;
  struct MdnsBack { bool &off; ~MdnsBack() { if (off && f1PauseMdns) f1PauseMdns(false); } } mdnsBack{mdnsOff};
  {
    std::lock_guard<std::mutex> net(f1NetLock);  // the ticker is idle meanwhile: its downloads take heap the handshake needs
    f1Park = true;  // the ticker hands its stack back, see F1Live.h
    struct Unpark { ~Unpark() { f1Park = false; } } unpark;
    for (int i = 0; i < 30 && f1TaskUp; i++) vTaskDelay(pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(300));  // the idle task frees a deleted task's memory
    // A connection that fails for lack of memory is tried once more with mDNS stopped: its task and records cost several KB
    for (int attempt = 0;; attempt++) {
      bool ok = false;
      if (dataFree() < TLS_MIN_FREE || dataBlock() < TLS_MIN_BLOCK) {
        liveLog("[F1 live] not enough memory to connect (heap free %u, largest block %u)\n", (unsigned)dataFree(), (unsigned)dataBlock());
      } else {
        std::lock_guard<std::mutex> tls(f1TlsLock);  // negotiate and connect each do a handshake, one after the other
        f1At("live negotiate");
        if (negotiate(token, sizeof(token), cookie, sizeof(cookie))) {
          f1At("live connect");
          client.setInsecure();
          client.setTimeout(10);
          ok = client.connect(HOST, 443);
          if (!ok) liveLog("[F1 live] connect failed (heap free %u, largest block %u)\n", (unsigned)dataFree(), (unsigned)dataBlock());
        }
      }
      if (ok) break;
      bool memory = dataFree() < TLS_MIN_FREE || dataBlock() < TLS_MIN_BLOCK;
      if (attempt > 0 || !memory || !f1PauseMdns) return;
      liveLog("[F1 live] short of memory: stopping mDNS and trying again\n");
      f1PauseMdns(true);
      mdnsOff = true;
      vTaskDelay(pdMS_TO_TICKS(300));  // its task frees its stack
    }
  }

  f1At("live upgrade");
  // The WebSocket upgrade; the token is in the address
  String path = "/signalrcore?id=";
  for (const char *p = token; *p; p++) {
    if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.') path += *p;
    else path += String("%") + String((uint8_t)*p < 16 ? "0" : "") + String((uint8_t)*p, HEX);
  }
  client.print("GET " + path + " HTTP/1.1\r\nHost: " + HOST + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n" +
               (cookie[0] ? String("Cookie: ") + cookie + "\r\n" : String()) +
               "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\nUser-Agent: BestHTTP\r\n\r\n");
  String status = client.readStringUntil('\n');
  if (!status.startsWith("HTTP/1.1 101")) {
    liveLog("[F1 live] upgrade: %s\n", status.c_str());
    return;
  }
  while (client.connected()) {  // the rest of the headers
    String header = client.readStringUntil('\n');
    if (header.length() <= 1) break;
  }

  _connected = true;
  f1At("live subscribe");
  WsStream stream(client);
  stream.sendText("{\"protocol\":\"json\",\"version\":1}\x1e");
  stream.nextMessage();  // the empty answer, {}
  stream.sendText("{\"type\":1,\"target\":\"Subscribe\",\"invocationId\":\"1\",\"arguments\":[[\"DriverList\",\"TimingData\","
                  "\"LapCount\",\"TrackStatus\",\"SessionStatus\",\"ExtrapolatedClock\"]]}\x1e");
  liveLog("[F1 live] subscribed (heap free %u, largest block %u, stack left %u)\n", (unsigned)dataFree(), (unsigned)dataBlock(),
          (unsigned)uxTaskGetStackHighWaterMark(nullptr));
  {
    std::lock_guard<std::mutex> guard(_lock);
    _state.reset();
  }

  unsigned long unwantedSince = 0;
  unsigned clockSeen = 0;
  uint32_t shownHash = 0;
  while (!stream.failed) {
    f1At("live parse");
    bool data = _state.applyStream(WsStream::nextChar, &stream, &_lock);
    stream.nextMessage();
    if (stream.failed) break;
    _lastData = millis();  // any message, the server's pings too: a quiet stretch in a session isn't a stale feed
    if (data) {
      std::lock_guard<std::mutex> guard(_lock);
      uint32_t shown = _state.displayHash();  // most messages are sector times and the like: no redraw for those
      if (shown != shownHash) {
        shownHash = shown;
        _updates++;
      }
      if (_state.clockMessages() != clockSeen) {
        clockSeen = _state.clockMessages();
        liveLog("[F1 live] session clock: %d s left, %s\n", _state.clockSecs((long)ezt::now()), _state.clockRunning() ? "running" : "stopped");
      }
    }
    if (firmwareUpdating) break;  // hand the connection's memory to the update
    if (dataFree() < LOW_HEAP) {  // the clock itself is running out of memory
      liveLog("[F1 live] dropped, heap free %u, largest block %u\n", (unsigned)dataFree(), (unsigned)dataBlock());
      break;
    }
    if (_wanted) {
      unwantedSince = 0;
    } else if (!unwantedSince) {
      unwantedSince = millis();
    } else if (millis() - unwantedSince > UNWANTED_MS) {
      break;  // the session is over
    }
  }
  _connected = false;
  liveLog("[F1 live] disconnected (stack left %u)\n", (unsigned)uxTaskGetStackHighWaterMark(nullptr));
  client.stop();
}

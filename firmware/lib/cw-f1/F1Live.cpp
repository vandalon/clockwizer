#include "F1Live.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <TelnetStream.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

extern volatile bool firmwareUpdating;  // main.cpp

static const char HOST[] = "livetiming.formula1.com";
static const unsigned long PING_MS = 15 * 1000UL;          // SignalR drops a client that stays quiet
static const unsigned long SILENCE_MS = 40 * 1000UL;       // nothing at all from the server: reconnect
static const unsigned long FRESH_MS = 45 * 1000UL;         // data older than this isn't shown
static const unsigned long UNWANTED_MS = 3 * 60 * 1000UL;  // how long after the session the connection stays
static const unsigned long RETRY_MS = 15 * 1000UL;
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
    // Core 0, like the other downloads. The TLS handshake needs a deep stack.
    xTaskCreatePinnedToCore(task, "f1live", 16384, this, 1, nullptr, 0);
  }
}

bool F1Live::fresh() {
  std::lock_guard<std::mutex> guard(_lock);
  return _lastData && millis() - _lastData <= FRESH_MS && _state.hasOrder();
}

bool F1Live::get(char tag, F1LiveState::Row *rows, int &count, int &lap, int &totalLaps, char &flag) {
  std::lock_guard<std::mutex> guard(_lock);
  if (!_lastData || millis() - _lastData > FRESH_MS || !_state.hasOrder()) return false;
  count = _state.top(tag, rows);
  lap = _state.lap();
  totalLaps = _state.totalLaps();
  flag = _state.flag();
  return count > 0;
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
bool F1Live::negotiate(char *token, size_t size) {
  WiFiClientSecure client;
  client.setInsecure();  // public data, nothing to protect
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, "https://livetiming.formula1.com/signalrcore/negotiate?negotiateVersion=1")) return false;
  http.setUserAgent("BestHTTP");
  int code = http.POST("");
  if (code != HTTP_CODE_OK) {
    liveLog("[F1 live] negotiate: HTTP %d\n", code);
    http.end();
    return false;
  }
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

// A WebSocket's messages as one stream of text for ArduinoJson: frames come and go underneath, a SignalR
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
    uint8_t frame[160];  // control frames and our few messages are short; the server needs them masked
    if (length > 120) return;
    uint32_t mask = esp_random();
    frame[0] = 0x80 | opcode;
    frame[1] = 0x80 | length;
    memcpy(frame + 2, &mask, 4);
    for (size_t i = 0; i < length; i++) frame[6 + i] = data[i] ^ ((uint8_t *)&mask)[i % 4];
    client.write(frame, 6 + length);
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

  // For ArduinoJson: the current message, then end of input
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
  char token[96];
  if (!negotiate(token, sizeof(token))) return;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10);
  if (!client.connect(HOST, 443)) {
    liveLog("[F1 live] connect failed\n");
    return;
  }

  // The WebSocket upgrade; the token is in the address
  String path = "/signalrcore?id=";
  for (const char *p = token; *p; p++) {
    if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.') path += *p;
    else path += String("%") + String((uint8_t)*p < 16 ? "0" : "") + String((uint8_t)*p, HEX);
  }
  client.print("GET " + path + " HTTP/1.1\r\nHost: " + HOST + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
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

  // The parser's memory is only held while connected
  DynamicJsonDocument *doc = new DynamicJsonDocument(20480);
  if (!doc || !doc->capacity()) {
    liveLog("[F1 live] no memory for the parser (heap %u)\n", ESP.getFreeHeap());
    delete doc;
    return;
  }
  DynamicJsonDocument filter(1536);
  F1LiveState::buildFilter(filter);

  WsStream stream(client);
  stream.sendText("{\"protocol\":\"json\",\"version\":1}\x1e");
  stream.nextMessage();  // the empty answer, {}
  stream.sendText("{\"type\":1,\"target\":\"Subscribe\",\"invocationId\":\"1\",\"arguments\":[[\"DriverList\",\"TimingData\","
                  "\"LapCount\",\"TrackStatus\"]]}\x1e");
  liveLog("[F1 live] subscribed (heap free %u)\n", ESP.getFreeHeap());
  {
    std::lock_guard<std::mutex> guard(_lock);
    _state.reset();
  }

  unsigned long unwantedSince = 0;
  while (!stream.failed) {
    doc->clear();
    DeserializationError err = deserializeJson(*doc, stream, DeserializationOption::Filter(filter),
                                               DeserializationOption::NestingLimit(20));
    stream.nextMessage();
    if (stream.failed) break;
    if (!err && !doc->overflowed()) {
      std::lock_guard<std::mutex> guard(_lock);
      if (_state.apply(*doc)) {
        _lastData = millis();
        _updates++;
      }
    } else if (err == DeserializationError::NoMemory || doc->overflowed()) {
      liveLog("[F1 live] message too big for the parser\n");
    }

    if (_wanted) {
      unwantedSince = 0;
    } else if (!unwantedSince) {
      unwantedSince = millis();
    } else if (millis() - unwantedSince > UNWANTED_MS) {
      break;  // the session is over
    }
  }
  liveLog("[F1 live] disconnected\n");
  client.stop();
  delete doc;
}

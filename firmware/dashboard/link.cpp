#include "link.h"
#include <ArduinoJson.h>

namespace pclink {
namespace {

constexpr size_t RX_LINE_MAX = 16384;
constexpr uint32_t TIMEOUT_MS = 4000;
constexpr uint32_t HELLO_EVERY_MS = 3000;
constexpr const char *FW_VERSION = "0.1.0";

char *line = nullptr;
size_t len = 0;
bool overflow = false;
uint32_t lastRx = 0;
uint32_t lastHello = 0;

void copy(char *dst, size_t n, const char *src) {
  strlcpy(dst, src ? src : "", n);
}

bool handle(Model &m) {
  JsonDocument doc;
  if (deserializeJson(doc, line, len)) return false;
  const char *t = doc["t"] | "";
  if (strcmp(t, "state") != 0) return false;

  copy(m.clock, sizeof(m.clock), doc["clock"] | "");
  copy(m.date, sizeof(m.date), doc["date"] | "");
  JsonArray arr = doc["sessions"].as<JsonArray>();
  int n = 0;
  for (JsonObject o : arr) {
    if (n >= MAX_SESSIONS) break;
    Session &s = m.sessions[n++];
    copy(s.id, sizeof(s.id), o["id"] | "");
    copy(s.chat, sizeof(s.chat), o["chat"] | "");
    copy(s.project, sizeof(s.project), o["project"] | "");
    copy(s.state, sizeof(s.state), o["state"] | "idle");
    copy(s.detail, sizeof(s.detail), o["detail"] | "");
    s.age = o["age"] | 0;
    s.agents = o["agents"] | 0;
  }
  m.count = n;
  return true;
}

}  // namespace

void begin() {
  Serial.setRxBufferSize(RX_LINE_MAX);
  Serial.setTxTimeoutMs(0);  // nicht blockieren, wenn kein PC zuhört
  Serial.begin(115200);
  line = (char *)heap_caps_malloc(RX_LINE_MAX, MALLOC_CAP_SPIRAM);
}

bool poll(Model &m) {
  bool updated = false;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      if (!overflow && len > 0) {
        lastRx = millis();
        if (handle(m)) updated = true;
      }
      len = 0;
      overflow = false;
    } else if (c != '\r') {
      if (len < RX_LINE_MAX - 1) line[len++] = c;
      else overflow = true;
    }
  }
  if (!connected() && millis() - lastHello > HELLO_EVERY_MS) {
    lastHello = millis();
    Serial.printf("{\"t\":\"hello\",\"fw\":\"%s\"}\n", FW_VERSION);
  }
  return updated;
}

bool connected() {
  return lastRx != 0 && millis() - lastRx < TIMEOUT_MS;
}

void sendOpen(const char *id) {
  JsonDocument doc;
  doc["t"] = "open";
  doc["id"] = id;
  serializeJson(doc, Serial);
  Serial.print('\n');
}

}  // namespace pclink

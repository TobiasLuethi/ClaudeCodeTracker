#include "link.h"
#include <ArduinoJson.h>
#include <mbedtls/md.h>
#include "clock.h"
#include "net.h"

namespace pclink {
namespace {

constexpr size_t RX_LINE_MAX = 16384;
constexpr uint32_t TIMEOUT_MS = 4000;
constexpr uint32_t HELLO_EVERY_MS = 3000;
constexpr const char *FW_VERSION = "0.2.0";

struct Rx {
  char *buf = nullptr;
  size_t len = 0;
  bool overflow = false;
};

Rx rxUsb, rxTcp;
uint32_t lastUsb = 0, lastTcp = 0, lastHello = 0;
Event pending = Event::None;

void copy(char *dst, size_t n, const char *src) { strlcpy(dst, src ? src : "", n); }

void sendTo(Print &out, JsonDocument &doc) {
  serializeJson(doc, out);
  out.print('\n');
}

void hmacHex(const char *key, const char *msg, char *out /* 65 */) {
  uint8_t mac[32];
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(info, (const uint8_t *)key, strlen(key), (const uint8_t *)msg, strlen(msg), mac);
  for (int i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", mac[i]);
  out[64] = 0;
}

bool readState(JsonDocument &doc, Model &m) {
  uint32_t ts = doc["ts"] | 0;
  if (ts) clock_::syncFromPc(ts);
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

// Liefert true, wenn ein neuer Zustand angekommen ist.
bool handle(Rx &rx, bool fromUsb, Model &m) {
  JsonDocument doc;
  if (deserializeJson(doc, rx.buf, rx.len)) return false;
  const char *t = doc["t"] | "";
  if (!strcmp(t, "state")) {
    (fromUsb ? lastUsb : lastTcp) = millis();
    return readState(doc, m);
  }
  if (fromUsb && !strcmp(t, "paired")) {
    const char *tk = doc["token"] | "";
    if (strlen(tk) >= 32) {
      net::setPaired(tk, doc["pc"] | "PC");
      pending = Event::Paired;
    }
    return false;
  }
  if (!fromUsb && !strcmp(t, "challenge") && net::paired()) {
    char mac[65];
    hmacHex(net::token(), doc["nonce"] | "", mac);
    JsonDocument a;
    a["t"] = "auth";
    a["gid"] = net::gadgetId();
    a["mac"] = mac;
    sendTo(net::client(), a);
  }
  return false;
}

bool pump(Stream &in, Rx &rx, bool fromUsb, Model &m) {
  bool updated = false;
  while (in.available()) {
    char c = (char)in.read();
    if (c == '\n') {
      if (!rx.overflow && rx.len > 0 && handle(rx, fromUsb, m)) updated = true;
      rx.len = 0;
      rx.overflow = false;
    } else if (c != '\r') {
      if (rx.len < RX_LINE_MAX - 1) rx.buf[rx.len++] = c;
      else rx.overflow = true;
    }
  }
  return updated;
}

}  // namespace

void begin() {
  Serial.setRxBufferSize(4096);  // Ringpuffer liegt im internen RAM
  Serial.setTxTimeoutMs(0);  // nicht blockieren, wenn kein PC zuhört
  Serial.begin(115200);
  rxUsb.buf = (char *)heap_caps_malloc(RX_LINE_MAX, MALLOC_CAP_SPIRAM);
  rxTcp.buf = (char *)heap_caps_malloc(RX_LINE_MAX, MALLOC_CAP_SPIRAM);
}

bool poll(Model &m) {
  bool updated = pump(Serial, rxUsb, true, m);
  WiFiClient &c = net::client();
  if (c.connected()) updated |= pump(c, rxTcp, false, m);
  else rxTcp.len = 0;

  if (!usbActive() && millis() - lastHello > HELLO_EVERY_MS) {
    lastHello = millis();
    JsonDocument d;
    d["t"] = "hello";
    d["fw"] = FW_VERSION;
    d["gid"] = net::gadgetId();
    sendTo(Serial, d);
  }
  return updated;
}

bool usbActive() { return lastUsb && millis() - lastUsb < TIMEOUT_MS; }
bool connected() { return usbActive() || (net::clientConnected() && lastTcp && millis() - lastTcp < TIMEOUT_MS); }

Via via() {
  if (usbActive()) return Via::Usb;
  if (connected()) return Via::Wlan;
  return Via::None;
}

Event takeEvent() {
  Event e = pending;
  pending = Event::None;
  return e;
}

void sendOpen(const char *id) {
  JsonDocument doc;
  doc["t"] = "open";
  doc["id"] = id;
  if (usbActive()) sendTo(Serial, doc);
  else if (net::clientConnected()) sendTo(net::client(), doc);
}

bool requestPair() {
  if (!usbActive()) return false;
  JsonDocument doc;
  doc["t"] = "pair";
  doc["gid"] = net::gadgetId();
  sendTo(Serial, doc);
  return true;
}

}  // namespace pclink

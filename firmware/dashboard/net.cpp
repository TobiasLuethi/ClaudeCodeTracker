#include "net.h"
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include "clock.h"

namespace net {
namespace {

Preferences prefs;
char gid[16];
String wifiSsid, wifiPass;
char tok[72] = "";
char pc[48] = "";
bool ntpStarted = false;

// Hotspot
bool portal = false;
bool portalSaved = false;
uint32_t portalStopAt = 0;
char apSsid[32], apPass[12];
DNSServer dns;
WebServer web(80);
String scanOptions;

// Brücke
WiFiUDP udp;
WiFiClient tcp;
uint32_t lastDiscover = 0;
bool udpOpen = false;

// Protokollmeldung an die Brücke (erscheint dort als "Gadget (COM3): ...")
void logf(const char *fmt, ...) {
  char msg[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  JsonDocument d;
  d["t"] = "log";
  d["msg"] = msg;
  serializeJson(d, Serial);
  Serial.print('\n');
}

int lastStations = -1;
constexpr uint32_t PORTAL_TIMEOUT_MS = 5 * 60 * 1000;
uint32_t portalStartedAt = 0;

// WLAN-Ereignisse des Hotspots protokollieren (Verbinden, IP vergeben, Trennen mit Grund)
void onWifiEvent(arduino_event_id_t ev, arduino_event_info_t info) {
  if (!portal) return;
  if (ev == ARDUINO_EVENT_WIFI_AP_STACONNECTED) logf("Portal: Gerät angemeldet");
  else if (ev == ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED)
    logf("Portal: IP vergeben %s", IPAddress(info.wifi_ap_staipassigned.ip.addr).toString().c_str());
  else if (ev == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED)
    logf("Portal: Gerät getrennt, Grund %d", info.wifi_ap_stadisconnected.reason);
}

String htmlEscape(const String &s) {
  String o;
  for (char c : s) {
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

const char PAGE_HEAD[] PROGMEM = R"(<!doctype html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Claude Gadget</title>
<style>body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:24px;max-width:420px}
h1{font-size:22px;color:#d97757}label{display:block;margin:16px 0 6px}input,select{width:100%;box-sizing:border-box;
padding:12px;font-size:16px;border-radius:10px;border:1px solid #444;background:#222;color:#eee}
button{margin-top:22px;width:100%;padding:14px;font-size:17px;border:0;border-radius:10px;background:#d97757;color:#fff}
p{color:#aaa;line-height:1.4}</style></head><body><h1>Claude Gadget</h1>)";

void handleRoot() {
  logf("Portal: Seite / von %s", web.client().remoteIP().toString().c_str());
  String h = FPSTR(PAGE_HEAD);
  h += F("<p>WLAN auswählen, mit dem sich das Gadget verbinden soll. Es muss dasselbe Netz sein wie der PC.</p>"
         "<form method=post action=/save><label>Netzwerk</label><input name=ssid list=nets autocomplete=off required>"
         "<datalist id=nets>");
  h += scanOptions;
  h += F("</datalist><label>Passwort</label><input name=pass type=password autocomplete=off>"
         "<button type=submit>Speichern und verbinden</button></form></body></html>");
  web.send(200, "text/html; charset=utf-8", h);
}

void handleSave() {
  String s = web.arg("ssid"), p = web.arg("pass");
  s.trim();
  if (s.isEmpty() || s.length() > 32 || p.length() > 64) {
    web.send(400, "text/plain; charset=utf-8", "Ungültige Eingabe");
    return;
  }
  prefs.putString("ssid", s);
  prefs.putString("pass", p);
  wifiSsid = s;
  wifiPass = p;
  String h = FPSTR(PAGE_HEAD);
  h += "<p>Gespeichert. Das Gadget verbindet sich jetzt mit <b>" + htmlEscape(s) +
       "</b>.</p><p>Du kannst dieses Fenster schliessen und wieder in dein normales WLAN wechseln.</p></body></html>";
  web.send(200, "text/html; charset=utf-8", h);
  portalSaved = true;
  portalStopAt = millis() + 1500;  // Antwort noch ausliefern
}

void handleRedirect() {
  logf("Portal: %s %s -> Umleitung", web.hostHeader().c_str(), web.uri().c_str());
  web.sendHeader("Location", "http://192.168.4.1/", true);
  web.send(302, "text/plain", "");
}

void connectWifi() {
  if (wifiSsid.isEmpty()) return;
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
}

void discover() {
  if (!udpOpen) udpOpen = udp.begin(DISCOVERY_PORT + 2);  // Antwort-Port
  JsonDocument d;
  d["t"] = "discover";
  d["gid"] = gid;
  char buf[96];
  size_t n = serializeJson(d, buf, sizeof(buf));
  udp.beginPacket(IPAddress(255, 255, 255, 255), DISCOVERY_PORT);
  udp.write((const uint8_t *)buf, n);
  udp.endPacket();
}

void readDiscoveryReply() {
  int len = udp.parsePacket();
  if (len <= 0) return;
  char buf[192];
  len = udp.read(buf, sizeof(buf) - 1);
  if (len <= 0) return;
  buf[len] = 0;
  JsonDocument d;
  if (deserializeJson(d, buf)) return;
  if (strcmp(d["t"] | "", "here") != 0) return;
  uint16_t port = d["port"] | 0;
  if (!port || tcp.connected()) return;
  IPAddress ip = udp.remoteIP();
  tcp.setTimeout(2);
  if (tcp.connect(ip, port, 1500)) tcp.setNoDelay(true);
}

}  // namespace

void begin() {
  uint64_t mac = ESP.getEfuseMac();
  snprintf(gid, sizeof(gid), "%06llx", (unsigned long long)(mac >> 24) & 0xFFFFFF);
  prefs.begin("net", false);
  wifiSsid = prefs.getString("ssid", "");
  wifiPass = prefs.getString("pass", "");
  strlcpy(tok, prefs.getString("token", "").c_str(), sizeof(tok));
  strlcpy(pc, prefs.getString("pc", "").c_str(), sizeof(pc));
  WiFi.persistent(false);
  WiFi.onEvent(onWifiEvent);
  connectWifi();
}

void loop(bool usbActive) {
  if (portal) {
    dns.processNextRequest();
    web.handleClient();
    int st = WiFi.softAPgetStationNum();
    if (st != lastStations) {
      lastStations = st;
      logf("Portal: %d Gerät(e) verbunden", st);
    }
    if (portalStopAt && millis() > portalStopAt) {
      portalStopAt = 0;
      stopPortal();
    } else if (millis() - portalStartedAt > PORTAL_TIMEOUT_MS) {
      logf("Portal: Zeit abgelaufen");
      stopPortal();
    }
    return;
  }

  bool up = wifiConnected();
  if (up && !ntpStarted) {
    clock_::startNtp();
    ntpStarted = true;
  }
  if (!up || !paired() || usbActive) {
    if (tcp.connected()) tcp.stop();
    return;
  }
  if (!tcp.connected()) {
    readDiscoveryReply();
    if (millis() - lastDiscover > 3000) {
      lastDiscover = millis();
      discover();
    }
  }
}

const char *gadgetId() { return gid; }
bool configured() { return !wifiSsid.isEmpty(); }
bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }
String ssid() { return wifiSsid; }
String ip() { return wifiConnected() ? WiFi.localIP().toString() : String(); }

void startPortal() {
  if (portal) return;
  if (tcp.connected()) tcp.stop();
  // Netzwerke suchen (blockiert ein paar Sekunden)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  int n = WiFi.scanNetworks();
  scanOptions = "";
  for (int i = 0; i < n && i < 20; i++) {
    String s = WiFi.SSID(i);
    if (s.isEmpty() || scanOptions.indexOf("\"" + htmlEscape(s) + "\"") >= 0) continue;
    scanOptions += "<option value=\"" + htmlEscape(s) + "\">";
  }
  WiFi.scanDelete();

  // Offener Hotspot nur für die Einrichtung; endet nach dem Speichern oder nach PORTAL_TIMEOUT_MS
  snprintf(apSsid, sizeof(apSsid), "Claude-Gadget-%s", gid + 2);
  apPass[0] = 0;

  // wie das CaptivePortal-Beispiel des Cores; softAPConfig() vor softAP() legt den DHCP-Dienst lahm
  WiFi.mode(WIFI_AP);
  bool apOk = WiFi.softAP(apSsid);
  portalStartedAt = millis();
  delay(100);
  bool dnsOk = dns.start(53, "*", WiFi.softAPIP());
  web.on("/", HTTP_GET, handleRoot);
  web.on("/save", HTTP_POST, handleSave);
  web.onNotFound(handleRedirect);
  web.begin();
  portal = true;
  lastStations = -1;
  logf("Portal: Hotspot %s %s, IP %s, DNS %s, %d Netze gefunden", apSsid, apOk ? "ok" : "FEHLER",
       WiFi.softAPIP().toString().c_str(), dnsOk ? "ok" : "FEHLER", n);
  logf("Speicher intern: frei %u, groesster Block %u, PSRAM frei %u",
       (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
       (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
       (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  portalSaved = false;
}

void stopPortal() {
  if (!portal) return;
  web.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  portal = false;
  connectWifi();
}

bool portalActive() { return portal; }
const char *portalSsid() { return apSsid; }
const char *portalPass() { return apPass; }

bool takePortalSaved() {
  bool s = portalSaved && !portal;
  if (s) portalSaved = false;
  return s;
}

bool paired() { return tok[0] != 0; }
const char *pcName() { return pc; }
const char *token() { return tok; }

void setPaired(const char *t, const char *p) {
  strlcpy(tok, t, sizeof(tok));
  strlcpy(pc, p, sizeof(pc));
  prefs.putString("token", tok);
  prefs.putString("pc", pc);
}

void clearPairing() {
  tok[0] = pc[0] = 0;
  prefs.remove("token");
  prefs.remove("pc");
  if (tcp.connected()) tcp.stop();
}

WiFiClient &client() { return tcp; }
bool clientConnected() { return tcp.connected(); }

}  // namespace net

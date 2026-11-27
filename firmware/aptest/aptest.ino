// Minimaltest Hotspot: offener Hotspot "Gadget-Test" + Webseite unter http://192.168.4.1
// Gleiche Abfolge wie im Dashboard (erst Netzsuche im STA-Modus, dann Hotspot).
// Meldet über USB-Seriell: angemeldete Geräte, vergebene IPs, Trennungen, Seitenaufrufe.
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

WebServer web(80);
DNSServer dns;

void onEvent(arduino_event_id_t ev, arduino_event_info_t info) {
  if (ev == ARDUINO_EVENT_WIFI_AP_STACONNECTED) Serial.println("AP: Gerät angemeldet");
  else if (ev == ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED)
    Serial.printf("AP: IP vergeben %s\n", IPAddress(info.wifi_ap_staipassigned.ip.addr).toString().c_str());
  else if (ev == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED)
    Serial.printf("AP: Gerät getrennt, Grund %d\n", info.wifi_ap_stadisconnected.reason);
  else if (ev == ARDUINO_EVENT_WIFI_AP_START) Serial.println("AP: gestartet");
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  WiFi.onEvent(onEvent);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  int n = WiFi.scanNetworks();
  Serial.printf("Scan: %d Netze\n", n);
  WiFi.scanDelete();

  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP("Gadget-Test");
  Serial.printf("softAP %s, IP %s\n", ok ? "ok" : "FEHLER", WiFi.softAPIP().toString().c_str());
  dns.start(53, "*", WiFi.softAPIP());
  web.on("/", [] {
    Serial.println("Web: Seite / abgerufen");
    web.send(200, "text/html; charset=utf-8", "<h1>Gadget-Test funktioniert</h1>");
  });
  web.onNotFound([] {
    web.sendHeader("Location", "http://192.168.4.1/", true);
    web.send(302, "text/plain", "");
  });
  web.begin();
}

void loop() {
  dns.processNextRequest();
  web.handleClient();
  delay(2);
}

// WLAN: Zugangsdaten (NVS), Einrichtung über eigenen Hotspot mit Webseite, Kopplung mit dem PC,
// Suche nach der Brücke per UDP-Broadcast und TCP-Verbindung zu ihr.
#pragma once
#include <Arduino.h>
#include <WiFi.h>

namespace net {

constexpr uint16_t DISCOVERY_PORT = 47390;

void begin();
// usbActive: Brücke ist per USB verbunden -> keine WLAN-Verbindung zur Brücke aufbauen
void loop(bool usbActive);

const char *gadgetId();

// WLAN
bool configured();
bool wifiConnected();
String ssid();
String ip();

// Einrichtung per Hotspot
void startPortal();
void stopPortal();
bool portalActive();
const char *portalSsid();
const char *portalPass();
bool takePortalSaved();  // true einmalig, nachdem neue Zugangsdaten gespeichert wurden

// Kopplung mit dem PC
bool paired();
const char *pcName();
const char *token();
void setPaired(const char *token, const char *pc);
void clearPairing();

// TCP-Verbindung zur Brücke
WiFiClient &client();
bool clientConnected();

}  // namespace net

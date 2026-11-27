// Uhrzeit: Systemzeit (UTC) mit Schweizer Zeitzone, gesichert in der RTC PCF85063 (I2C 0x51).
// Quellen: RTC beim Start, Zeitstempel der Brücke, NTP sobald WLAN verbunden ist.
#pragma once
#include <Arduino.h>

namespace clock_ {

void begin();          // nach Wire.begin(): Zeitzone setzen, Zeit aus der RTC lesen
void loop();           // schreibt die Zeit regelmässig zurück in die RTC
bool valid();          // true, sobald eine plausible Zeit bekannt ist
void syncFromPc(uint32_t unixSeconds);
void startNtp();       // nach WLAN-Verbindung
void clockText(char *buf, size_t n);  // "14:05"
void dateText(char *buf, size_t n);   // "Fr 27.11."

}  // namespace clock_

#include "clock.h"
#include <Wire.h>
#include <sys/time.h>
#include <time.h>

namespace clock_ {
namespace {

constexpr uint8_t ADDR_RTC = 0x51;
constexpr const char *TZ_CH = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr time_t MIN_VALID = 1767225600;  // 2026-01-01
uint32_t lastRtcWrite = 0;

uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
uint8_t dec2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

// PCF85063: Register 0x04..0x0A = Sekunden, Minuten, Stunden, Tag, Wochentag, Monat, Jahr (BCD, UTC)
bool readRtc(time_t &out) {
  Wire.beginTransmission(ADDR_RTC);
  Wire.write(0x04);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(ADDR_RTC, (uint8_t)7) != 7) return false;
  uint8_t r[7];
  for (auto &b : r) b = Wire.read();
  if (r[0] & 0x80) return false;  // Oszillator war gestoppt: Zeit ungültig
  struct tm t = {};
  t.tm_sec = bcd2dec(r[0] & 0x7F);
  t.tm_min = bcd2dec(r[1] & 0x7F);
  t.tm_hour = bcd2dec(r[2] & 0x3F);
  t.tm_mday = bcd2dec(r[3] & 0x3F);
  t.tm_mon = bcd2dec(r[5] & 0x1F) - 1;
  t.tm_year = bcd2dec(r[6]) + 100;
  // mktime rechnet in lokaler Zeit; für UTC kurz auf UTC umstellen
  setenv("TZ", "UTC0", 1);
  tzset();
  out = mktime(&t);
  setenv("TZ", TZ_CH, 1);
  tzset();
  return out >= MIN_VALID;
}

void writeRtc(time_t now) {
  struct tm t;
  gmtime_r(&now, &t);
  Wire.beginTransmission(ADDR_RTC);
  Wire.write(0x04);
  Wire.write(dec2bcd(t.tm_sec));
  Wire.write(dec2bcd(t.tm_min));
  Wire.write(dec2bcd(t.tm_hour));
  Wire.write(dec2bcd(t.tm_mday));
  Wire.write(t.tm_wday);
  Wire.write(dec2bcd(t.tm_mon + 1));
  Wire.write(dec2bcd(t.tm_year % 100));
  Wire.endTransmission();
  lastRtcWrite = millis();
}

void setSystem(time_t t) {
  struct timeval tv = {t, 0};
  settimeofday(&tv, nullptr);
}

}  // namespace

void begin() {
  setenv("TZ", TZ_CH, 1);
  tzset();
  time_t t;
  if (readRtc(t)) setSystem(t);
}

bool valid() { return time(nullptr) >= MIN_VALID; }

void syncFromPc(uint32_t unixSeconds) {
  if (unixSeconds < MIN_VALID) return;
  time_t now = time(nullptr);
  if (labs((long)(now - (time_t)unixSeconds)) > 2) {
    setSystem(unixSeconds);
    writeRtc(unixSeconds);
  }
}

void startNtp() {
  configTzTime(TZ_CH, "pool.ntp.org", "time.google.com");
}

void loop() {
  // stündlich zurückschreiben (deckt auch NTP-Korrekturen ab)
  if (valid() && (lastRtcWrite == 0 || millis() - lastRtcWrite > 3600UL * 1000)) writeRtc(time(nullptr));
}

void clockText(char *buf, size_t n) {
  if (!valid()) { strlcpy(buf, "--:--", n); return; }
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  snprintf(buf, n, "%02d:%02d", t.tm_hour, t.tm_min);
}

void dateText(char *buf, size_t n) {
  static const char *WD[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
  if (!valid()) { strlcpy(buf, "", n); return; }
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  snprintf(buf, n, "%s %02d.%02d.", WD[t.tm_wday], t.tm_mday, t.tm_mon + 1);
}

}  // namespace clock_

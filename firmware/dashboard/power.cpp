#include "power.h"
#include <Wire.h>
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>

namespace power {
namespace {

XPowersPMU pmu;
bool ok = false;
bool bat = false, chg = false, vbus = true;
int pct = -1;
uint32_t lastRead = 0;

void read() {
  bat = pmu.isBatteryConnect();
  pct = bat ? pmu.getBatteryPercent() : -1;
  chg = bat && pmu.isCharging();
  vbus = pmu.isVbusIn();
  lastRead = millis();
}

}  // namespace

bool begin() {
  ok = pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, 15, 14);
  if (!ok) return false;
  pmu.enableBattDetection();
  pmu.enableBattVoltageMeasure();
  pmu.enableVbusVoltageMeasure();
  pmu.enableSystemVoltageMeasure();
  read();
  return true;
}

void loop() {
  if (ok && millis() - lastRead > 5000) read();
}

bool hasBattery() { return bat; }
int percent() { return pct; }
bool charging() { return chg; }
bool usbPower() { return !ok || vbus; }

}  // namespace power

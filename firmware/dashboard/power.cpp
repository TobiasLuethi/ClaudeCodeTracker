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
uint32_t lastRead = 0, lastKeyPoll = 0;
Key pendingKey = Key::None;

void read() {
  bat = pmu.isBatteryConnect();
  pct = bat ? pmu.getBatteryPercent() : -1;
  chg = bat && pmu.isCharging();
  vbus = pmu.isVbusIn();
  lastRead = millis();
}

void pollKey() {
  pmu.getIrqStatus();
  if (pmu.isPekeyLongPressIrq()) pendingKey = Key::Long;
  else if (pmu.isPekeyShortPressIrq()) pendingKey = Key::Short;
  pmu.clearIrqStatus();
  lastKeyPoll = millis();
}

}  // namespace

bool begin() {
  ok = pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, 15, 14);
  if (!ok) return false;
  pmu.enableBattDetection();
  pmu.enableBattVoltageMeasure();
  pmu.enableVbusVoltageMeasure();
  pmu.enableSystemVoltageMeasure();

  // PWR-Taste: Einschalten nach 0.5 s, "langer Druck" ab 1.5 s (Software schaltet aus),
  // Hardware-Notabschaltung nach 6 s, falls die Firmware nicht reagiert
  pmu.setPowerKeyPressOnTime(XPOWERS_POWERON_512MS);
  pmu.setIrqLevelTime(XPOWERS_AXP2101_IRQ_TIME_1S5);
  pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_6S);
  pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
  pmu.clearIrqStatus();
  pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ);

  read();
  return true;
}

void loop() {
  if (!ok) return;
  if (millis() - lastKeyPoll > 50) pollKey();
  if (millis() - lastRead > 5000) read();
}

bool hasBattery() { return bat; }
int percent() { return pct; }
bool charging() { return chg; }
bool usbPower() { return !ok || vbus; }

Key takeKey() {
  Key k = pendingKey;
  pendingKey = Key::None;
  return k;
}

void shutdown() {
  if (ok) pmu.shutdown();
}

}  // namespace power

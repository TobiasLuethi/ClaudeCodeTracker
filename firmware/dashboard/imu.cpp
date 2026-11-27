#include "imu.h"
#include <Wire.h>

namespace imu {
namespace {

constexpr uint8_t ADDR = 0x6B;
constexpr uint8_t REG_WHO_AM_I = 0x00, REG_CTRL1 = 0x02, REG_CTRL2 = 0x03, REG_CTRL7 = 0x08;
constexpr uint8_t REG_AX_L = 0x35, REG_RESET = 0x60;
constexpr float LSB_PER_G = 16384.0f;  // Bereich ±2 g
// Vorzeichen von Z: am Board gemessen az ≈ -1.1 g, wenn das Display nach oben zeigt
constexpr float UP_SIGN = -1.0f;
constexpr float DOWN_THRESHOLD = -0.75f;  // g, in Richtung "Display nach unten"
constexpr float UP_THRESHOLD = -0.3f;     // Hysterese zurück
constexpr uint32_t STABLE_MS = 1000;

bool ok = false;
bool down = false;
float lastZ = 0;
uint32_t lastRead = 0, candidateSince = 0;

bool wr(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool rd(uint8_t reg, uint8_t *buf, size_t n) {
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(ADDR, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

}  // namespace

bool begin() {
  uint8_t id = 0;
  if (!rd(REG_WHO_AM_I, &id, 1) || id != 0x05) return false;
  wr(REG_RESET, 0xB0);
  delay(20);
  ok = wr(REG_CTRL1, 0x40)      // Adressen automatisch hochzählen, little endian
       && wr(REG_CTRL2, 0x06)   // Beschleunigung ±2 g, 125 Hz
       && wr(REG_CTRL7, 0x01);  // nur Beschleunigung an
  delay(20);
  return ok;
}

void loop() {
  if (!ok || millis() - lastRead < 100) return;
  lastRead = millis();
  uint8_t b[6];
  if (!rd(REG_AX_L, b, 6)) return;
  int16_t rawZ = (int16_t)(b[4] | (b[5] << 8));
  lastZ = rawZ / LSB_PER_G * UP_SIGN;

  bool want = down ? lastZ < UP_THRESHOLD : lastZ < DOWN_THRESHOLD;
  if (want == down) {
    candidateSince = 0;
  } else if (!candidateSince) {
    candidateSince = millis();
  } else if (millis() - candidateSince > STABLE_MS) {
    down = want;
    candidateSince = 0;
  }
}

bool faceDown() { return down; }
float z() { return lastZ; }

}  // namespace imu

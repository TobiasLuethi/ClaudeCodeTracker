// Hardwaretest für das Waveshare ESP32-S3-Touch-AMOLED-1.8:
// sucht alle Geräte auf dem I2C-Bus und gibt sie über USB-Seriell aus.
// Erwartet (laut Waveshare-BSP und Datenblättern, noch unbestätigt):
//   0x15 CST816S / 0x38 FT3168 (Touch), 0x18 ES8311 (Audio), 0x20 TCA9554 (IO-Expander),
//   0x34 AXP2101 (Power), 0x51 PCF85063 (RTC), 0x6A/0x6B QMI8658 (IMU)
#include <Wire.h>

static constexpr int PIN_SDA = 15;
static constexpr int PIN_SCL = 14;

struct Known { uint8_t addr; const char *name; };
static const Known KNOWN[] = {
  {0x15, "CST816S Touch"}, {0x38, "FT3168/FT5x06 Touch"}, {0x18, "ES8311 Audio"},
  {0x20, "TCA9554 IO-Expander"}, {0x34, "AXP2101 Power"}, {0x51, "PCF85063 RTC"},
  {0x6A, "QMI8658 IMU"}, {0x6B, "QMI8658 IMU"},
};

static const char *nameOf(uint8_t a) {
  for (auto &k : KNOWN) if (k.addr == a) return k.name;
  return "?";
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
}

void loop() {
  Serial.println("--- I2C-Scan (SDA 15, SCL 14) ---");
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("0x%02X  %s\n", a, nameOf(a));
      n++;
    }
  }
  Serial.printf("%d Geraete gefunden\n\n", n);
  delay(5000);
}

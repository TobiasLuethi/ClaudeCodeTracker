// Akku und Stromversorgung über AXP2101 (I2C 0x34). Ändert keine Spannungsschienen.
#pragma once
#include <Arduino.h>

namespace power {

bool begin();          // nach Wire.begin()
void loop();           // liest alle paar Sekunden neu
bool hasBattery();
int percent();         // 0..100, -1 ohne Akku
bool charging();
bool usbPower();       // USB-Spannung vorhanden

}  // namespace power

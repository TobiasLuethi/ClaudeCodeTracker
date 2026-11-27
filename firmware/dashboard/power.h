// Akku, Stromversorgung und Einschalttaste (PWR) über AXP2101 (I2C 0x34). Ändert keine Spannungsschienen.
#pragma once
#include <Arduino.h>

namespace power {

enum class Key { None, Short, Long };

bool begin();          // nach Wire.begin()
void loop();           // Akku alle paar Sekunden, Taste laufend
bool hasBattery();
int percent();         // 0..100, -1 ohne Akku
bool charging();
bool usbPower();       // USB-Spannung vorhanden
Key takeKey();         // letzter Druck auf PWR (kurz < 1.5 s, lang >= 1.5 s)
void shutdown();       // schaltet das Gerät aus; einschalten mit PWR

}  // namespace power

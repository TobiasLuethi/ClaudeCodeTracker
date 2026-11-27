// Lagesensor QMI8658 (I2C 0x6B), nur Beschleunigung: erkennt, ob das Gadget mit dem Display nach unten liegt.
#pragma once
#include <Arduino.h>

namespace imu {

bool begin();      // nach Wire.begin()
void loop();       // liest mit 10 Hz
bool faceDown();   // entprellt: Display liegt seit ~1 s nach unten
float z();         // letzte Z-Beschleunigung in g (für Diagnose)

}  // namespace imu

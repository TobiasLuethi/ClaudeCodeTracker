// Ton über ES8311 (I2C 0x18) und I2S, Verstärker an GPIO 46.
#pragma once
#include <Arduino.h>

namespace audio {

// Initialisiert Codec und I2S; muss nach Wire.begin() laufen.
bool begin();
// Spielt den Hinweiston im Hintergrund ab (blockiert nicht).
void chime();

}  // namespace audio

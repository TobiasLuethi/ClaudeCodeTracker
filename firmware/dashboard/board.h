// Hardware des Waveshare ESP32-S3-Touch-AMOLED-1.8 (Werte am Board geprüft, siehe README)
#pragma once
#include <Arduino.h>

namespace board {

constexpr int W = 368, H = 448;

// Initialisiert I2C, setzt Display und Touch über den IO-Expander zurück und startet das Display.
bool begin();

// Zeichnet RGB565-Pixel (native Byte-Reihenfolge); Bereich muss gerade x/y/Breite/Höhe haben.
// Blockiert, bis die Übertragung fertig ist.
void draw(int x, int y, int w, int h, const uint16_t *pixels);

// Liest den Touch-Controller; true, wenn ein Finger aufliegt.
bool readTouch(int &x, int &y);

// Helligkeit 0..255 (CO5300 Befehl 0x51)
void setBrightness(uint8_t level);

}  // namespace board

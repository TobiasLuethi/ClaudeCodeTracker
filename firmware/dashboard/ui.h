// Oberfläche (LVGL): Kopfzeile, Session-Kacheln, Uhr ohne Sessions bzw. ohne PC,
// Einstellungen (Tipp auf den Titel) und WLAN-Einrichtung mit QR-Code.
#pragma once
#include "link.h"

namespace ui {

void begin();
void update(const pclink::Model &model, bool connected);
// einmal pro Sekunde: Uhr, Akku, Verbindung, Statustexte
void tick();
// Ton per Lautsprecher-Symbol in der Kopfzeile stummgeschaltet (bleibt nach Neustart erhalten)
bool muted();
void onPaired();

}  // namespace ui

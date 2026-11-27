// Oberfläche (LVGL): Kopfzeile, Session-Kacheln, leere Ansicht mit Uhr, Offline-Ansicht.
#pragma once
#include "link.h"

namespace ui {

void begin();
void update(const pclink::Model &model, bool connected);
// Ton per Lautsprecher-Symbol in der Kopfzeile stummgeschaltet (bleibt nach Neustart erhalten)
bool muted();

}  // namespace ui

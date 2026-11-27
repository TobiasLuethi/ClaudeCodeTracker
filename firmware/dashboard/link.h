// Verbindung zum PC über USB-Seriell: eine JSON-Nachricht pro Zeile.
// PC -> Gerät: {"t":"state","clock":"14:05","date":"Fr 27.11.","sessions":[{"id","chat","project","state","age","detail","agents"}]}
// Gerät -> PC: {"t":"hello","fw":"..."} und {"t":"open","id":"..."}
#pragma once
#include <Arduino.h>

namespace pclink {

constexpr int MAX_SESSIONS = 12;

struct Session {
  char id[64];
  // Grössen in Bytes (UTF-8); die Brücke kürzt vorher sauber an Zeichengrenzen
  char chat[160];
  char project[64];
  char state[12];  // working | idle | input | approval
  char detail[260];
  uint32_t age;    // Sekunden im aktuellen Zustand
  uint8_t agents;
};

struct Model {
  Session sessions[MAX_SESSIONS];
  int count = 0;
  char clock[8] = "";
  char date[20] = "";
};

void begin();
// Liest eingehende Daten; true, wenn ein neuer Zustand angekommen ist.
bool poll(Model &model);
// true, wenn in den letzten Sekunden Daten vom PC kamen.
bool connected();
void sendOpen(const char *id);

}  // namespace pclink

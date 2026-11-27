// Verbindung zur Brücke, per USB-Seriell oder WLAN (TCP): eine JSON-Nachricht pro Zeile.
// Brücke -> Gerät: {"t":"state","ts":<unix>,"sessions":[{"id","chat","project","state","age","detail","agents"}]}
//                  {"t":"paired","token":"<hex>","pc":"<name>"}   (nur USB, Antwort auf "pair")
//                  {"t":"challenge","nonce":"<hex>"}              (nur WLAN, vor dem ersten Zustand)
// Gerät -> Brücke: {"t":"hello","fw","gid"}  {"t":"open","id"}  {"t":"pair","gid"}  {"t":"auth","gid","mac"}
//   mac = HMAC-SHA256(token, nonce) als Hex; der Token wird nur beim Koppeln über USB übertragen.
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
};

enum class Via { None, Usb, Wlan };
enum class Event { None, Paired };

void begin();
// Liest eingehende Daten; true, wenn ein neuer Zustand angekommen ist.
bool poll(Model &model);
bool connected();
bool usbActive();
Via via();
Event takeEvent();
void sendOpen(const char *id);
// Koppeln anfordern (nur über USB möglich)
bool requestPair();

}  // namespace pclink

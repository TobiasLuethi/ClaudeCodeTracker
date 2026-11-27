#include "ui.h"
#include <lvgl.h>
#include <Preferences.h>
#include "audio.h"
#include "board.h"
#include "clock.h"
#include "net.h"
#include "power.h"

extern const uint8_t montserrat_ttf[];
extern const size_t montserrat_ttf_size;

namespace ui {
namespace {

constexpr uint32_t COL_BG = 0x000000;
constexpr uint32_t COL_CARD = 0x1C1C1E;
constexpr uint32_t COL_CARD_PRESSED = 0x2C2C2E;
constexpr uint32_t COL_TEXT = 0xFFFFFF;
constexpr uint32_t COL_MUTED = 0x8E8E93;
constexpr uint32_t COL_DIM = 0x5A5A5E;
constexpr uint32_t COL_ACCENT = 0xD97757;
constexpr uint32_t COL_WARN = 0xFFB020;
constexpr uint32_t COL_BAD = 0xFF5A36;
constexpr int HEADER_H = 56;

struct StateInfo {
  const char *label;
  uint32_t color;
  int rank;
};

StateInfo stateInfo(const char *s) {
  if (!strcmp(s, "approval")) return {"FREIGABE", COL_BAD, 0};
  if (!strcmp(s, "input")) return {"FRAGE", COL_WARN, 1};
  if (!strcmp(s, "working")) return {"ARBEITET", 0x4DA3FF, 2};
  return {"BEREIT", 0x34C759, 3};
}

struct Card {
  lv_obj_t *root;
  lv_obj_t *meta;
  lv_obj_t *title;
  lv_obj_t *detail;
  char id[64];
};

// Schriften: Montserrat über tiny_ttf (mit Umlauten), Symbole aus den eingebauten Schriften
const lv_font_t *F14, *F16, *F20;

Card cards[pclink::MAX_SESSIONS];
lv_obj_t *hTitle, *hClock, *hLink, *hBattery, *hSound;
lv_obj_t *list;
lv_obj_t *emptyPanel, *emptyClock, *emptyDate;
lv_obj_t *offlinePanel, *offClock, *offDate, *offHint;
lv_obj_t *settings, *sWifi, *sPc, *sBattery, *sPairBtn, *sUnpairBtn;
lv_obj_t *portalScr, *pQr, *pInfo;
lv_obj_t *toastLbl;
lv_timer_t *toastTimer = nullptr;
bool isConnected = false;
int attentionCount = 0;
bool isMuted = false;
bool portalShown = false;
Preferences prefs;

const lv_font_t *makeFont(int size, const lv_font_t *symbols) {
  lv_font_t *f = lv_tiny_ttf_create_data(montserrat_ttf, montserrat_ttf_size, size);
  if (!f) return symbols;
  f->fallback = symbols;
  return f;
}

void show(lv_obj_t *o, bool v) {
  if (v) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void fmtAge(char *buf, size_t n, uint32_t s) {
  if (s < 60) snprintf(buf, n, "gerade eben");
  else if (s < 3600) snprintf(buf, n, "%u min", (unsigned)(s / 60));
  else snprintf(buf, n, "%u h %02u", (unsigned)(s / 3600), (unsigned)((s % 3600) / 60));
}

void hideToast(lv_timer_t *) {
  lv_obj_add_flag(toastLbl, LV_OBJ_FLAG_HIDDEN);
  toastTimer = nullptr;
}

void showToast(const char *text, uint32_t ms = 1800) {
  lv_label_set_text(toastLbl, text);
  lv_obj_remove_flag(toastLbl, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(toastLbl);
  if (toastTimer) lv_timer_delete(toastTimer);
  toastTimer = lv_timer_create(hideToast, ms, nullptr);
  lv_timer_set_repeat_count(toastTimer, 1);
}

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  return l;
}

lv_obj_t *wrapLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = label(parent, font, color);
  lv_obj_set_width(l, lv_pct(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
  return l;
}

lv_obj_t *button(lv_obj_t *parent, const char *text, bool primary, lv_event_cb_t cb) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_set_width(b, lv_pct(100));
  lv_obj_set_height(b, 48);
  lv_obj_set_style_radius(b, 14, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(primary ? COL_ACCENT : 0x2C2C2E), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(primary ? 0xB8604A : 0x3A3A3C), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(b, LV_OPA_40, LV_STATE_DISABLED);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *l = label(b, F16, COL_TEXT);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  return b;
}

lv_obj_t *section(lv_obj_t *parent, const char *title) {
  lv_obj_t *t = label(parent, F14, COL_MUTED);
  lv_label_set_text(t, title);
  lv_obj_set_style_pad_top(t, 10, 0);
  return t;
}

lv_obj_t *fullPanel(lv_obj_t *parent) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_set_size(p, board::W, board::H);
  lv_obj_set_pos(p, 0, 0);
  lv_obj_set_style_bg_color(p, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(p, 0, 0);
  lv_obj_set_style_radius(p, 0, 0);
  lv_obj_set_style_pad_hor(p, 22, 0);
  lv_obj_set_style_pad_top(p, 22, 0);
  lv_obj_set_style_pad_bottom(p, 30, 0);
  lv_obj_set_style_pad_row(p, 8, 0);
  lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
  return p;
}

lv_obj_t *centerPanel(lv_obj_t *parent) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_set_size(p, lv_pct(100), board::H - HEADER_H);
  lv_obj_align(p, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(p, 0, 0);
  lv_obj_set_style_pad_hor(p, 24, 0);
  lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(p, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(p, 8, 0);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  return p;
}

// --- Ereignisse ------------------------------------------------------------

void showSound() {
  lv_label_set_text(hSound, isMuted ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX);
  lv_obj_set_style_text_color(hSound, lv_color_hex(isMuted ? COL_DIM : COL_MUTED), 0);
}

void onSoundClick(lv_event_t *) {
  isMuted = !isMuted;
  prefs.putBool("muted", isMuted);
  showSound();
  if (isMuted) showToast(LV_SYMBOL_MUTE "  Ton aus");
  else {
    showToast(LV_SYMBOL_VOLUME_MAX "  Ton an");
    audio::chime();
  }
}

void onCardClick(lv_event_t *e) {
  Card *c = (Card *)lv_event_get_user_data(e);
  if (!c->id[0]) return;
  pclink::sendOpen(c->id);
  showToast(LV_SYMBOL_OK "  Chat wird geöffnet");
}

void refreshSettings();

void onOpenSettings(lv_event_t *) {
  refreshSettings();
  show(settings, true);
  lv_obj_move_foreground(settings);
  lv_obj_scroll_to_y(settings, 0, LV_ANIM_OFF);
}

void onCloseSettings(lv_event_t *) { show(settings, false); }

void onWifiSetup(lv_event_t *) {
  showToast("Suche WLAN-Netze ...", 4000);
  lv_refr_now(nullptr);
  net::startPortal();
  char qr[96];
  snprintf(qr, sizeof(qr), "WIFI:T:nopass;S:%s;;", net::portalSsid());
  lv_qrcode_update(pQr, qr, strlen(qr));
  lv_label_set_text_fmt(pInfo, "Netz:  %s\n(ohne Passwort)", net::portalSsid());
  show(settings, false);
  show(portalScr, true);
  lv_obj_move_foreground(portalScr);
  portalShown = true;
  hideToast(nullptr);
}

void onPortalCancel(lv_event_t *) {
  net::stopPortal();
  show(portalScr, false);
  portalShown = false;
  onOpenSettings(nullptr);
}

void onPair(lv_event_t *) {
  if (pclink::requestPair()) showToast("Kopplung angefragt ...");
  else showToast("Dazu per USB mit dem PC verbinden", 2500);
}

void onUnpair(lv_event_t *) {
  net::clearPairing();
  refreshSettings();
  showToast("Kopplung aufgehoben");
}

// --- Kacheln ---------------------------------------------------------------

void makeCard(Card &c) {
  lv_obj_t *r = lv_obj_create(list);
  c.root = r;
  c.id[0] = 0;
  lv_obj_set_width(r, lv_pct(100));
  lv_obj_set_height(r, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(r, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_bg_color(r, lv_color_hex(COL_CARD_PRESSED), LV_STATE_PRESSED);
  lv_obj_set_style_radius(r, 18, 0);
  lv_obj_set_style_border_side(r, LV_BORDER_SIDE_LEFT, 0);
  lv_obj_set_style_border_width(r, 6, 0);
  lv_obj_set_style_pad_all(r, 12, 0);
  lv_obj_set_style_pad_left(r, 16, 0);
  lv_obj_set_style_pad_row(r, 4, 0);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(r, onCardClick, LV_EVENT_CLICKED, &c);

  c.meta = label(r, F14, COL_MUTED);
  lv_obj_set_width(c.meta, lv_pct(100));
  lv_label_set_long_mode(c.meta, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_height(c.meta, lv_font_get_line_height(F14));

  c.title = label(r, F20, COL_TEXT);
  lv_obj_set_width(c.title, lv_pct(100));
  lv_label_set_long_mode(c.title, LV_LABEL_LONG_MODE_DOTS);

  c.detail = label(r, F16, 0xFFD08A);
  lv_obj_set_width(c.detail, lv_pct(100));
  lv_label_set_long_mode(c.detail, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN);
}

// Höhe eines Labels auf höchstens n Zeilen begrenzen (Rest mit "...").
void clampLines(lv_obj_t *l, const lv_font_t *font, int n) {
  lv_obj_set_height(l, LV_SIZE_CONTENT);
  lv_obj_update_layout(l);
  int maxH = lv_font_get_line_height(font) * n;
  if (lv_obj_get_height(l) > maxH) lv_obj_set_height(l, maxH);
}

void fillCard(Card &c, const pclink::Session &s) {
  StateInfo si = stateInfo(s.state);
  strlcpy(c.id, s.id, sizeof(c.id));

  char age[24], meta[200];
  fmtAge(age, sizeof(age), s.age);
  int n = snprintf(meta, sizeof(meta), "%s - %s", si.label, age);
  if (s.project[0]) n += snprintf(meta + n, sizeof(meta) - n, " - %s", s.project);
  if (s.agents) snprintf(meta + n, sizeof(meta) - n, " - %u Agent%s", s.agents, s.agents > 1 ? "s" : "");
  lv_label_set_text(c.meta, meta);
  lv_obj_set_style_text_color(c.meta, lv_color_hex(si.color), 0);
  lv_obj_set_style_border_color(c.root, lv_color_hex(si.color), 0);

  lv_label_set_text(c.title, s.chat[0] ? s.chat : "Neue Session");
  clampLines(c.title, F20, 2);

  bool showDetail = s.detail[0] && si.rank <= 1;
  if (showDetail) {
    lv_label_set_text(c.detail, s.detail);
    lv_obj_set_style_text_color(c.detail, lv_color_hex(si.rank == 0 ? 0xFFB4A2 : 0xFFD08A), 0);
    lv_obj_remove_flag(c.detail, LV_OBJ_FLAG_HIDDEN);
    clampLines(c.detail, F16, 3);
  } else {
    lv_obj_add_flag(c.detail, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_remove_flag(c.root, LV_OBJ_FLAG_HIDDEN);
}

// --- Statustexte -----------------------------------------------------------

const char *batterySymbol(int pct) {
  if (pct >= 90) return LV_SYMBOL_BATTERY_FULL;
  if (pct >= 65) return LV_SYMBOL_BATTERY_3;
  if (pct >= 40) return LV_SYMBOL_BATTERY_2;
  if (pct >= 15) return LV_SYMBOL_BATTERY_1;
  return LV_SYMBOL_BATTERY_EMPTY;
}

void refreshHeader() {
  char buf[16];
  clock_::clockText(buf, sizeof(buf));
  lv_label_set_text(hClock, clock_::valid() ? buf : "");

  switch (pclink::via()) {
    case pclink::Via::Usb:
      lv_label_set_text(hLink, LV_SYMBOL_USB);
      lv_obj_set_style_text_color(hLink, lv_color_hex(COL_MUTED), 0);
      break;
    case pclink::Via::Wlan:
      lv_label_set_text(hLink, LV_SYMBOL_WIFI);
      lv_obj_set_style_text_color(hLink, lv_color_hex(COL_MUTED), 0);
      break;
    default:
      lv_label_set_text(hLink, net::wifiConnected() ? LV_SYMBOL_WIFI : "");
      lv_obj_set_style_text_color(hLink, lv_color_hex(COL_DIM), 0);
  }

  if (power::hasBattery()) {
    int p = power::percent();
    char b[24];
    snprintf(b, sizeof(b), "%s%s", power::charging() ? LV_SYMBOL_CHARGE " " : "", batterySymbol(p));
    lv_label_set_text(hBattery, b);
    lv_obj_set_style_text_color(hBattery, lv_color_hex(p < 15 && !power::charging() ? COL_BAD : COL_MUTED), 0);
    show(hBattery, true);
  } else {
    show(hBattery, false);
  }
}

void refreshOffline() {
  char buf[24];
  clock_::clockText(buf, sizeof(buf));
  lv_label_set_text(offClock, buf);
  clock_::dateText(buf, sizeof(buf));
  lv_label_set_text(offDate, buf);

  char hint[160];
  if (net::wifiConnected() && net::paired())
    snprintf(hint, sizeof(hint), "Suche %s im WLAN %s ...", net::pcName(), net::ssid().c_str());
  else if (net::wifiConnected())
    snprintf(hint, sizeof(hint), "WLAN verbunden. Für WLAN-Betrieb einmal per USB mit dem PC koppeln (Titel antippen).");
  else if (net::configured())
    snprintf(hint, sizeof(hint), "WLAN %s nicht erreichbar. Per USB geht es trotzdem.", net::ssid().c_str());
  else
    snprintf(hint, sizeof(hint), "Per USB mit dem PC verbinden. Für WLAN den Titel antippen.");
  lv_label_set_text(offHint, hint);
}

void refreshSettings() {
  char buf[200];
  if (net::wifiConnected())
    snprintf(buf, sizeof(buf), "Verbunden mit %s\nIP %s", net::ssid().c_str(), net::ip().c_str());
  else if (net::configured())
    snprintf(buf, sizeof(buf), "%s nicht erreichbar", net::ssid().c_str());
  else
    snprintf(buf, sizeof(buf), "Nicht eingerichtet");
  lv_label_set_text(sWifi, buf);

  if (net::paired())
    snprintf(buf, sizeof(buf), "Gekoppelt mit %s%s", net::pcName(),
             pclink::via() == pclink::Via::Wlan ? " (über WLAN verbunden)" : "");
  else
    snprintf(buf, sizeof(buf), "Nicht gekoppelt. Zum Koppeln per USB verbinden.");
  lv_label_set_text(sPc, buf);
  show(sUnpairBtn, net::paired());
  if (pclink::usbActive()) lv_obj_remove_state(sPairBtn, LV_STATE_DISABLED);
  else lv_obj_add_state(sPairBtn, LV_STATE_DISABLED);

  if (power::hasBattery())
    snprintf(buf, sizeof(buf), "%d %%%s", power::percent(),
             power::charging() ? " - lädt" : (power::usbPower() ? " - am USB" : ""));
  else
    snprintf(buf, sizeof(buf), "Kein Akku angeschlossen");
  lv_label_set_text(sBattery, buf);
}

void buildSettings(lv_obj_t *scr) {
  settings = fullPanel(scr);
  lv_obj_t *t = label(settings, F20, COL_ACCENT);
  lv_label_set_text(t, "Einstellungen");

  section(settings, "WLAN");
  sWifi = wrapLabel(settings, F16, COL_TEXT);
  button(settings, LV_SYMBOL_WIFI "  WLAN einrichten", false, onWifiSetup);

  section(settings, "PC");
  sPc = wrapLabel(settings, F16, COL_TEXT);
  lv_obj_t *pcHelp = wrapLabel(settings, F14, COL_MUTED);
  lv_label_set_text(pcHelp, "Nur für den Betrieb ohne Kabel nötig: Einmal per USB koppeln, "
                            "danach findet das Gadget diesen PC im WLAN.");
  sPairBtn = button(settings, LV_SYMBOL_USB "  Mit diesem PC koppeln", false, onPair);
  sUnpairBtn = button(settings, "Kopplung aufheben", false, onUnpair);

  section(settings, "Akku");
  sBattery = wrapLabel(settings, F16, COL_TEXT);

  lv_obj_t *id = label(settings, F14, COL_DIM);
  lv_label_set_text_fmt(id, "Gadget %s", net::gadgetId());
  lv_obj_set_style_pad_top(id, 10, 0);

  button(settings, "Fertig", true, onCloseSettings);
}

void buildPortal(lv_obj_t *scr) {
  portalScr = fullPanel(scr);
  lv_obj_set_flex_align(portalScr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *t = label(portalScr, F20, COL_ACCENT);
  lv_label_set_text(t, "WLAN einrichten");
  lv_obj_t *l = wrapLabel(portalScr, F14, COL_MUTED);
  lv_label_set_text(l, "QR-Code mit dem Handy scannen oder mit dem Netz unten verbinden. "
                       "Mobile Daten kurz ausschalten, dann http://192.168.4.1 öffnen.");
  pQr = lv_qrcode_create(portalScr);
  lv_qrcode_set_size(pQr, 170);
  lv_qrcode_set_dark_color(pQr, lv_color_hex(0x000000));
  lv_qrcode_set_light_color(pQr, lv_color_hex(0xFFFFFF));
  lv_obj_set_style_border_color(pQr, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(pQr, 6, 0);
  pInfo = wrapLabel(portalScr, F16, COL_TEXT);
  lv_obj_set_style_text_align(pInfo, LV_TEXT_ALIGN_CENTER, 0);
  button(portalScr, "Abbrechen", false, onPortalCancel);
}

}  // namespace

bool muted() { return isMuted; }

void setMuted(bool m) {
  if (m == isMuted) return;
  isMuted = m;
  prefs.putBool("muted", isMuted);
  showSound();
}

void toast(const char *text, uint32_t ms) {
  showToast(text, ms);
  lv_refr_now(nullptr);
}

void begin() {
  prefs.begin("gadget", false);
  isMuted = prefs.getBool("muted", false);

  F14 = makeFont(14, &lv_font_montserrat_14);
  F16 = makeFont(16, &lv_font_montserrat_16);
  F20 = makeFont(20, &lv_font_montserrat_20);

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // Kopfzeile: Titel links (Tipp = Einstellungen), rechts Uhr, Verbindung, Akku, Ton
  lv_obj_t *header = lv_obj_create(scr);
  lv_obj_set_size(header, lv_pct(100), HEADER_H);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_pad_hor(header, 18, 0);
  lv_obj_set_style_pad_top(header, 8, 0);
  lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
  hTitle = label(header, F20, COL_ACCENT);
  lv_obj_align(hTitle, LV_ALIGN_LEFT_MID, 0, 0);
  lv_label_set_text(hTitle, "Claude Code");
  lv_obj_add_flag(hTitle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(hTitle, 14);
  lv_obj_add_event_cb(hTitle, onOpenSettings, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *right = lv_obj_create(header);
  lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(right, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(right, 0, 0);
  lv_obj_set_style_pad_all(right, 0, 0);
  lv_obj_set_style_pad_column(right, 10, 0);
  lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(right, LV_OBJ_FLAG_SCROLLABLE);
  hClock = label(right, F16, COL_MUTED);
  hLink = label(right, F16, COL_MUTED);
  hBattery = label(right, F16, COL_MUTED);
  hSound = label(right, F16, COL_MUTED);
  lv_obj_add_flag(hSound, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(hSound, 16);
  lv_obj_add_event_cb(hSound, onSoundClick, LV_EVENT_CLICKED, nullptr);
  showSound();

  // Liste der Sessions
  list = lv_obj_create(scr);
  lv_obj_set_size(list, lv_pct(100), board::H - HEADER_H);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_hor(list, 10, 0);
  lv_obj_set_style_pad_top(list, 4, 0);
  lv_obj_set_style_pad_bottom(list, 24, 0);
  lv_obj_set_style_pad_row(list, 10, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
  for (auto &c : cards) makeCard(c);

  // Verbunden, keine Sessions: grosse Uhr
  emptyPanel = centerPanel(scr);
  emptyClock = label(emptyPanel, &lv_font_montserrat_48, COL_TEXT);
  emptyDate = label(emptyPanel, F20, COL_MUTED);
  lv_obj_t *e = label(emptyPanel, F16, COL_MUTED);
  lv_label_set_text(e, "Keine aktiven Sessions");
  lv_obj_set_style_pad_top(e, 24, 0);

  // Keine Verbindung zum PC: Uhr und Hinweis
  offlinePanel = centerPanel(scr);
  offClock = label(offlinePanel, &lv_font_montserrat_48, COL_TEXT);
  offDate = label(offlinePanel, F20, COL_MUTED);
  lv_obj_t *w = label(offlinePanel, F20, COL_TEXT);
  lv_label_set_text(w, "Warte auf PC");
  lv_obj_set_style_pad_top(w, 28, 0);
  offHint = wrapLabel(offlinePanel, F14, COL_MUTED);
  lv_obj_set_style_text_align(offHint, LV_TEXT_ALIGN_CENTER, 0);

  buildSettings(scr);
  buildPortal(scr);

  // Kurzmeldung unten
  toastLbl = label(scr, F16, COL_TEXT);
  lv_obj_set_style_bg_color(toastLbl, lv_color_hex(0x3A3A3C), 0);
  lv_obj_set_style_bg_opa(toastLbl, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(toastLbl, 16, 0);
  lv_obj_set_style_pad_hor(toastLbl, 16, 0);
  lv_obj_set_style_pad_ver(toastLbl, 8, 0);
  lv_obj_align(toastLbl, LV_ALIGN_BOTTOM_MID, 0, -16);
  lv_obj_add_flag(toastLbl, LV_OBJ_FLAG_HIDDEN);

  static pclink::Model empty;  // ~7 KB, nicht auf den Stack
  update(empty, false);
}

void update(const pclink::Model &m, bool connected) {
  isConnected = connected;
  show(offlinePanel, !connected);
  show(emptyPanel, connected && m.count == 0);
  show(list, connected && m.count > 0);

  attentionCount = 0;
  for (int i = 0; i < m.count; i++) attentionCount += stateInfo(m.sessions[i].state).rank <= 1;
  if (connected && attentionCount) {
    char t[40];
    snprintf(t, sizeof(t), "%d %s auf dich", attentionCount, attentionCount == 1 ? "wartet" : "warten");
    lv_label_set_text(hTitle, t);
    lv_obj_set_style_text_color(hTitle, lv_color_hex(COL_WARN), 0);
  } else {
    lv_label_set_text(hTitle, "Claude Code");
    lv_obj_set_style_text_color(hTitle, lv_color_hex(COL_ACCENT), 0);
  }

  for (int i = 0; i < pclink::MAX_SESSIONS; i++) {
    if (i < m.count) fillCard(cards[i], m.sessions[i]);
    else {
      cards[i].id[0] = 0;
      lv_obj_add_flag(cards[i].root, LV_OBJ_FLAG_HIDDEN);
    }
  }
  tick();
}

void tick() {
  refreshHeader();
  char buf[24];
  clock_::clockText(buf, sizeof(buf));
  lv_label_set_text(emptyClock, buf);
  clock_::dateText(buf, sizeof(buf));
  lv_label_set_text(emptyDate, buf);
  if (!isConnected) refreshOffline();
  if (!lv_obj_has_flag(settings, LV_OBJ_FLAG_HIDDEN)) refreshSettings();

  // Einrichtung abgeschlossen (neue Zugangsdaten gespeichert, Hotspot beendet)
  if (portalShown && !net::portalActive()) {
    portalShown = false;
    show(portalScr, false);
    if (net::takePortalSaved()) showToast(LV_SYMBOL_WIFI "  WLAN gespeichert, verbinde ...", 3000);
  }
}

void onDnd(bool on) {
  if (!on) showToast(LV_SYMBOL_BELL "  Nicht stören aus", 2000);
}

void onPaired() {
  refreshSettings();
  showToast(LV_SYMBOL_OK "  Mit PC gekoppelt", 2500);
}

}  // namespace ui

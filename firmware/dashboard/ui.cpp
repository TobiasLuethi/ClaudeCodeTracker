#include "ui.h"
#include <lvgl.h>
#include <Preferences.h>
#include "audio.h"
#include "board.h"

extern const uint8_t montserrat_ttf[];
extern const size_t montserrat_ttf_size;

namespace ui {
namespace {

constexpr uint32_t COL_BG = 0x000000;
constexpr uint32_t COL_CARD = 0x1C1C1E;
constexpr uint32_t COL_CARD_PRESSED = 0x2C2C2E;
constexpr uint32_t COL_TEXT = 0xFFFFFF;
constexpr uint32_t COL_MUTED = 0x8E8E93;
constexpr uint32_t COL_ACCENT = 0xD97757;
constexpr int HEADER_H = 56;

struct StateInfo {
  const char *label;
  uint32_t color;
  int rank;
};

StateInfo stateInfo(const char *s) {
  if (!strcmp(s, "approval")) return {"FREIGABE", 0xFF5A36, 0};
  if (!strcmp(s, "input")) return {"FRAGE", 0xFFB020, 1};
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
lv_obj_t *hTitle, *hClock, *hSound, *list, *emptyPanel, *emptyClock, *emptyDate, *offlinePanel, *toast;
lv_timer_t *toastTimer = nullptr;
bool isMuted = false;
Preferences prefs;

const lv_font_t *makeFont(int size, const lv_font_t *symbols) {
  lv_font_t *f = lv_tiny_ttf_create_data(montserrat_ttf, montserrat_ttf_size, size);
  if (!f) return symbols;
  f->fallback = symbols;
  return f;
}

void fmtAge(char *buf, size_t n, uint32_t s) {
  if (s < 60) snprintf(buf, n, "gerade eben");
  else if (s < 3600) snprintf(buf, n, "%u min", (unsigned)(s / 60));
  else snprintf(buf, n, "%u h %02u", (unsigned)(s / 3600), (unsigned)((s % 3600) / 60));
}

void hideToast(lv_timer_t *) {
  lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);
  toastTimer = nullptr;
}

void showToast(const char *text) {
  lv_label_set_text(toast, text);
  lv_obj_remove_flag(toast, LV_OBJ_FLAG_HIDDEN);
  if (toastTimer) lv_timer_delete(toastTimer);
  toastTimer = lv_timer_create(hideToast, 1800, nullptr);
  lv_timer_set_repeat_count(toastTimer, 1);
}

void showSound() {
  lv_label_set_text(hSound, isMuted ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX);
  lv_obj_set_style_text_color(hSound, lv_color_hex(isMuted ? 0x5A5A5E : COL_MUTED), 0);
}

void onSoundClick(lv_event_t *) {
  isMuted = !isMuted;
  prefs.putBool("muted", isMuted);
  showSound();
  if (isMuted) {
    showToast(LV_SYMBOL_MUTE "  Ton aus");
  } else {
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

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  return l;
}

lv_obj_t *panel(lv_obj_t *parent) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_set_size(p, lv_pct(100), board::H - HEADER_H);
  lv_obj_align(p, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(p, 0, 0);
  lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(p, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(p, 10, 0);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  return p;
}

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

}  // namespace

bool muted() { return isMuted; }

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

  // Kopfzeile: Titel links, Uhr und Lautsprecher rechts
  lv_obj_t *header = lv_obj_create(scr);
  lv_obj_set_size(header, lv_pct(100), HEADER_H);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_pad_hor(header, 20, 0);
  lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
  hTitle = label(header, F20, COL_ACCENT);
  lv_obj_align(hTitle, LV_ALIGN_LEFT_MID, 0, 4);
  lv_label_set_text(hTitle, "Claude Code");

  hSound = label(header, F20, COL_MUTED);
  lv_obj_align(hSound, LV_ALIGN_RIGHT_MID, 0, 4);
  lv_obj_add_flag(hSound, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(hSound, 16);
  lv_obj_add_event_cb(hSound, onSoundClick, LV_EVENT_CLICKED, nullptr);
  showSound();

  hClock = label(header, F20, COL_MUTED);
  lv_obj_align_to(hClock, hSound, LV_ALIGN_OUT_LEFT_MID, -14, 0);
  lv_label_set_text(hClock, "");

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

  // Keine Sessions: grosse Uhr
  emptyPanel = panel(scr);
  emptyClock = label(emptyPanel, &lv_font_montserrat_48, COL_TEXT);
  emptyDate = label(emptyPanel, F20, COL_MUTED);
  lv_obj_t *e = label(emptyPanel, F16, COL_MUTED);
  lv_label_set_text(e, "Keine aktiven Sessions");
  lv_obj_set_style_pad_top(e, 24, 0);

  // Keine Verbindung zum PC
  offlinePanel = panel(scr);
  lv_obj_t *sym = label(offlinePanel, &lv_font_montserrat_48, COL_MUTED);
  lv_label_set_text(sym, LV_SYMBOL_USB);
  lv_obj_t *t1 = label(offlinePanel, F20, COL_TEXT);
  lv_label_set_text(t1, "Warte auf PC");
  lv_obj_t *t2 = label(offlinePanel, F14, COL_MUTED);
  lv_label_set_text(t2, "USB-Brücke auf dem PC starten");

  // Kurzmeldung unten
  toast = label(scr, F16, COL_TEXT);
  lv_obj_set_style_bg_color(toast, lv_color_hex(0x3A3A3C), 0);
  lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(toast, 16, 0);
  lv_obj_set_style_pad_hor(toast, 16, 0);
  lv_obj_set_style_pad_ver(toast, 8, 0);
  lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -16);
  lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);

  static pclink::Model empty;  // ~7 KB, nicht auf den Stack
  update(empty, false);
}

void update(const pclink::Model &m, bool connected) {
  auto show = [](lv_obj_t *o, bool v) {
    if (v) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  };

  show(offlinePanel, !connected);
  show(emptyPanel, connected && m.count == 0);
  show(list, connected && m.count > 0);

  lv_label_set_text(hClock, connected ? m.clock : "");
  lv_obj_align_to(hClock, hSound, LV_ALIGN_OUT_LEFT_MID, -14, 0);

  int attention = 0;
  for (int i = 0; i < m.count; i++) attention += stateInfo(m.sessions[i].state).rank <= 1;
  if (connected && attention) {
    char t[40];
    snprintf(t, sizeof(t), "%d %s auf dich", attention, attention == 1 ? "wartet" : "warten");
    lv_label_set_text(hTitle, t);
    lv_obj_set_style_text_color(hTitle, lv_color_hex(0xFFB020), 0);
  } else {
    lv_label_set_text(hTitle, "Claude Code");
    lv_obj_set_style_text_color(hTitle, lv_color_hex(COL_ACCENT), 0);
  }

  lv_label_set_text(emptyClock, m.clock);
  lv_label_set_text(emptyDate, m.date);

  for (int i = 0; i < pclink::MAX_SESSIONS; i++) {
    if (i < m.count) fillCard(cards[i], m.sessions[i]);
    else {
      cards[i].id[0] = 0;
      lv_obj_add_flag(cards[i].root, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

}  // namespace ui

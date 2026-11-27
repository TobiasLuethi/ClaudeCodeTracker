// Claude Touch Gadget: zeigt den Status der Claude-Code-Sessions an, die die Brücke
// (pc/bridge.mjs) per USB oder WLAN schickt. Tipp auf eine Kachel öffnet den Chat am PC.
// Bauen: firmware\build.ps1 dashboard [-Upload]
#include <lvgl.h>
#include "audio.h"
#include "board.h"
#include "clock.h"
#include "link.h"
#include "net.h"
#include "power.h"
#include "ui.h"

// LVGL und tiny_ttf brauchen mehr als die üblichen 8 KB Stack
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

constexpr uint32_t DIM_AFTER_MS = 2 * 60 * 1000;  // im Akkubetrieb
constexpr uint8_t BRIGHT_USB = 255, BRIGHT_BATTERY = 190, BRIGHT_DIM = 20;

static pclink::Model model;
static bool lastConnected = false;
static uint32_t lastActivity = 0;
static uint32_t lastTick = 0;
static uint8_t brightness = BRIGHT_USB;

// Sessions, die beim letzten Stand eine Freigabe oder Antwort brauchten
static char waiting[pclink::MAX_SESSIONS][64];
static int waitingCount = 0;
static bool haveBaseline = false;

static bool needsUser(const pclink::Session &s) {
  return !strcmp(s.state, "approval") || !strcmp(s.state, "input");
}

// Ton, wenn eine Session neu auf den Benutzer wartet (nicht beim ersten Stand nach dem Verbinden)
static void checkAttention(const pclink::Model &m) {
  bool fresh = false;
  for (int i = 0; i < m.count; i++) {
    if (!needsUser(m.sessions[i])) continue;
    bool known = false;
    for (int j = 0; j < waitingCount; j++) known |= !strcmp(waiting[j], m.sessions[i].id);
    fresh |= !known;
  }
  if (fresh && haveBaseline) {
    lastActivity = millis();
    if (!ui::muted()) audio::chime();
  }
  waitingCount = 0;
  for (int i = 0; i < m.count; i++)
    if (needsUser(m.sessions[i])) strlcpy(waiting[waitingCount++], m.sessions[i].id, 64);
  haveBaseline = true;
}

// Akkubetrieb: nach einiger Zeit ohne Berührung abdunkeln
static void updateBrightness() {
  uint8_t want = BRIGHT_USB;
  if (!power::usbPower()) want = millis() - lastActivity > DIM_AFTER_MS ? BRIGHT_DIM : BRIGHT_BATTERY;
  if (want != brightness) {
    brightness = want;
    board::setBrightness(want);
  }
}

static uint32_t tickCb() { return millis(); }

static void flushCb(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  board::draw(a->x1, a->y1, lv_area_get_width(a), lv_area_get_height(a), (const uint16_t *)px);
  lv_display_flush_ready(d);
}

// CO5300 verlangt gerade Koordinaten und Grössen
static void rounderCb(lv_event_t *e) {
  lv_area_t *a = (lv_area_t *)lv_event_get_param(e);
  a->x1 &= ~1;
  a->y1 &= ~1;
  a->x2 |= 1;
  a->y2 |= 1;
}

static void touchCb(lv_indev_t *, lv_indev_data_t *data) {
  static int lx = 0, ly = 0;
  int x, y;
  if (board::readTouch(x, y)) {
    lx = x;
    ly = y;
    data->state = LV_INDEV_STATE_PRESSED;
    lastActivity = millis();
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
  data->point.x = lx;
  data->point.y = ly;
}

static void logLine(const char *msg) {
  Serial.printf("{\"t\":\"log\",\"msg\":\"%s\"}\n", msg);
}

void setup() {
  pclink::begin();
  if (!board::begin()) logLine("Display-Start fehlgeschlagen");
  if (!power::begin()) logLine("AXP2101 nicht gefunden");
  clock_::begin();
  if (!audio::begin()) logLine("Audio-Start fehlgeschlagen");
  net::begin();

  lv_init();
  lv_tick_set_cb(tickCb);

  lv_display_t *disp = lv_display_create(board::W, board::H);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  // Ein Puffer in Bildschirmgrösse (PSRAM): LVGL teilt Bereiche dann nie in ungerade Streifen
  size_t fbSize = board::W * board::H * 2;
  void *fb = heap_caps_malloc(fbSize, MALLOC_CAP_SPIRAM);
  lv_display_set_buffers(disp, fb, nullptr, fbSize, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flushCb);
  lv_display_add_event_cb(disp, rounderCb, LV_EVENT_INVALIDATE_AREA, nullptr);

  lv_indev_t *touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, touchCb);

  ui::begin();
  lastActivity = millis();
  Serial.printf("{\"t\":\"log\",\"msg\":\"Start: interner RAM frei %u, groesster Block %u\"}\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

void loop() {
  power::loop();
  clock_::loop();
  net::loop(pclink::usbActive());

  bool updated = pclink::poll(model);
  bool conn = pclink::connected();
  if (conn && !lastConnected) haveBaseline = false;
  if (updated && conn) checkAttention(model);
  if (updated || conn != lastConnected) {
    ui::update(model, conn);
    lastConnected = conn;
  }
  if (pclink::takeEvent() == pclink::Event::Paired) ui::onPaired();

  if (millis() - lastTick > 1000) {
    lastTick = millis();
    ui::tick();
    updateBrightness();
  }
  lv_timer_handler();
  delay(5);
}

// Display- und Touch-Test für das Waveshare ESP32-S3-Touch-AMOLED-1.8
// - Display CO5300 über QSPI (Bibliothek ESP32_Display_Panel), Init-Befehle aus dem Waveshare-BSP
// - Touch CST816-Familie (0x15) und IO-Expander TCA9554 (0x20) direkt über Wire,
//   damit nur ein I2C-Treiber im Spiel ist
// Ablauf: Farbbalken -> Farbflächen -> Malfläche (Tippen zeichnet Punkte, Koordinaten über Seriell)
#include <Arduino.h>
#include <Wire.h>
#include <esp_display_panel.hpp>

using namespace esp_panel::drivers;

static constexpr int W = 368, H = 448;
static constexpr int PIN_CS = 12, PIN_SCK = 11, PIN_D0 = 4, PIN_D1 = 5, PIN_D2 = 6, PIN_D3 = 7;
static constexpr int PIN_SDA = 15, PIN_SCL = 14, PIN_TOUCH_INT = 21;
static constexpr uint8_t ADDR_TCA9554 = 0x20, ADDR_TOUCH = 0x15;
static constexpr uint16_t X_GAP = 0x10;  // laut BSP bei CST816-Touch

// Init-Sequenz aus waveshare/esp32_s3_touch_amoled_1_8 (BSP)
static const esp_panel_lcd_vendor_init_cmd_t INIT_CMDS[] = {
  ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xFE, {0x00}),
  ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0xC4, {0x80}),
  ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0x3A, {0x55}),
  ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0x35, {0x00}),
  ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0x53, {0x20}),
  ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0x51, {0xFF}),
  ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0x63, {0xFF}),
  ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0x2A, {0x00, 0x00, 0x01, 0x6F}),
  ESP_PANEL_LCD_CMD_WITH_8BIT_PARAM(0, 0x2B, {0x00, 0x00, 0x01, 0xBF}),
  ESP_PANEL_LCD_CMD_WITH_NONE_PARAM(100, 0x11),
  ESP_PANEL_LCD_CMD_WITH_NONE_PARAM(0, 0x29),
};

static LCD *lcd = nullptr;
static uint16_t *line = nullptr;  // eine Zeile RGB565, Bytes getauscht (QSPI sendet MSB zuerst)

static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  return (c >> 8) | (c << 8);
}

static constexpr int BUF_ROWS = 16;  // gerade, CO5300 verlangt 2er-Ausrichtung

// Füllt ein Rechteck; Kanten werden auf gerade Pixel ausgerichtet (CO5300).
static void fillRect(int x, int y, int w, int h, uint16_t c) {
  int x0 = max(0, x) & ~1, y0 = max(0, y) & ~1;
  int x1 = min(W, x + w), y1 = min(H, y + h);
  x1 = min(W, (x1 + 1) & ~1);
  y1 = min(H, (y1 + 1) & ~1);
  int ww = x1 - x0;
  if (ww <= 0 || y1 <= y0) return;
  for (int i = 0; i < ww * BUF_ROWS; i++) line[i] = c;
  for (int r = y0; r < y1; r += BUF_ROWS) {
    int hh = min(BUF_ROWS, y1 - r);
    lcd->drawBitmap(x0, r, ww, hh, (const uint8_t *)line);
  }
}

static bool i2cWrite(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool i2cRead(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

// TCA9554: Pins 0..2 als Ausgänge, kurz low (Reset von Display/Touch), dann high.
static void resetViaExpander() {
  i2cWrite(ADDR_TCA9554, 0x03, 0xF8);  // Config: P0..P2 Ausgang
  i2cWrite(ADDR_TCA9554, 0x01, 0x00);  // Output low
  delay(50);
  i2cWrite(ADDR_TCA9554, 0x01, 0x07);  // Output high
  delay(150);
}

// CST816: Reg 0x02 Anzahl Finger, 0x03..0x06 X/Y (je 12 Bit)
static bool readTouch(int &x, int &y) {
  uint8_t d[5];
  if (!i2cRead(ADDR_TOUCH, 0x02, d, 5)) return false;
  if ((d[0] & 0x0F) == 0) return false;
  x = ((d[1] & 0x0F) << 8) | d[2];
  y = ((d[3] & 0x0F) << 8) | d[4];
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("displaytest: start");

  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  resetViaExpander();
  pinMode(PIN_TOUCH_INT, INPUT_PULLUP);

  auto *bus = new BusQSPI(PIN_CS, PIN_SCK, PIN_D0, PIN_D1, PIN_D2, PIN_D3);
  bus->configQSPI_FreqHz(40 * 1000 * 1000);
  lcd = new LCD_CO5300(bus, W, H, 16, -1);
  lcd->configVendorCommands(INIT_CMDS, sizeof(INIT_CMDS) / sizeof(INIT_CMDS[0]));
  if (!lcd->begin()) {
    Serial.println("LCD begin fehlgeschlagen");
    return;
  }
  lcd->setGapX(X_GAP);
  lcd->displayOn();
  Serial.println("LCD bereit");

  line = (uint16_t *)heap_caps_malloc(W * BUF_ROWS * sizeof(uint16_t), MALLOC_CAP_DMA);

  lcd->colorBarTest();
  Serial.println("Farbbalken (Erwartung: B - G - R von oben nach unten)");
  delay(2500);

  const uint16_t cols[] = {rgb(255, 0, 0), rgb(0, 255, 0), rgb(0, 0, 255), rgb(255, 255, 255)};
  const char *names[] = {"rot", "gruen", "blau", "weiss"};
  for (int i = 0; i < 4; i++) {
    fillRect(0, 0, W, H, cols[i]);
    Serial.printf("Vollbild %s\n", names[i]);
    delay(1200);
  }

  // Randlineal: von jedem Rand nach innen je 4 px Orange, Weiss, Blau, Gruen, Rot
  fillRect(0, 0, W, H, rgb(0, 0, 0));
  const uint16_t band[] = {rgb(255, 140, 0), rgb(255, 255, 255), rgb(0, 0, 255), rgb(0, 255, 0), rgb(255, 0, 0)};
  for (int i = 4; i >= 0; i--) {
    int d = i * 4;  // Abstand vom Rand
    // innen nach aussen zeichnen, damit jeder Streifen genau 4 px breit bleibt
    fillRect(d, d, W - 2 * d, 4, band[i]);          // oben
    fillRect(d, H - d - 4, W - 2 * d, 4, band[i]);  // unten
    fillRect(d, d, 4, H - 2 * d, band[i]);          // links
    fillRect(W - d - 4, d, 4, H - 2 * d, band[i]);  // rechts
  }
  Serial.println("Randlineal: je 4 px Orange, Weiss, Blau, Gruen, Rot von aussen nach innen. Tippen malt.");
}

void loop() {
  static uint32_t lastLog = 0;
  int x, y;
  if (lcd && line && readTouch(x, y)) {
    fillRect(x - 6, y - 6, 12, 12, rgb(80, 200, 255));
    if (millis() - lastLog > 100) {
      Serial.printf("touch x=%d y=%d\n", x, y);
      lastLog = millis();
    }
  }
  delay(10);
}

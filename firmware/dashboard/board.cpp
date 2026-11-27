#include "board.h"
#include <Wire.h>
#include <esp_display_panel.hpp>

using namespace esp_panel::drivers;

namespace board {
namespace {

constexpr int PIN_CS = 12, PIN_SCK = 11, PIN_D0 = 4, PIN_D1 = 5, PIN_D2 = 6, PIN_D3 = 7;
constexpr int PIN_SDA = 15, PIN_SCL = 14;
constexpr uint8_t ADDR_TCA9554 = 0x20, ADDR_TOUCH = 0x15;
constexpr uint16_t X_GAP = 0x10;
constexpr int CHUNK_ROWS = 16;  // gerade; klein halten, liegt im knappen internen RAM

// Init-Sequenz aus dem Waveshare-BSP (esp32_s3_touch_amoled_1_8)
const esp_panel_lcd_vendor_init_cmd_t INIT_CMDS[] = {
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

LCD *lcd = nullptr;
uint16_t *dma = nullptr;  // Zwischenpuffer im internen RAM, Bytes getauscht

bool i2cWrite(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool i2cRead(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

}  // namespace

bool begin() {
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  // TCA9554 P0..P2: Reset von Display und Touch
  i2cWrite(ADDR_TCA9554, 0x03, 0xF8);
  i2cWrite(ADDR_TCA9554, 0x01, 0x00);
  delay(50);
  i2cWrite(ADDR_TCA9554, 0x01, 0x07);
  delay(150);

  auto *bus = new BusQSPI(PIN_CS, PIN_SCK, PIN_D0, PIN_D1, PIN_D2, PIN_D3);
  bus->configQSPI_FreqHz(40 * 1000 * 1000);
  lcd = new LCD_CO5300(bus, W, H, 16, -1);
  lcd->configVendorCommands(INIT_CMDS, sizeof(INIT_CMDS) / sizeof(INIT_CMDS[0]));
  if (!lcd->begin()) return false;
  lcd->setGapX(X_GAP);
  lcd->displayOn();

  dma = (uint16_t *)heap_caps_malloc(W * CHUNK_ROWS * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  return dma != nullptr;
}

void draw(int x, int y, int w, int h, const uint16_t *pixels) {
  for (int r = 0; r < h; r += CHUNK_ROWS) {
    int rows = min(CHUNK_ROWS, h - r);
    int n = w * rows;
    const uint16_t *s = pixels + r * w;
    for (int i = 0; i < n; i++) {
      uint16_t c = s[i];
      dma[i] = (c >> 8) | (c << 8);
    }
    lcd->drawBitmap(x, y + r, w, rows, (const uint8_t *)dma, -1);
  }
}

bool readTouch(int &x, int &y) {
  uint8_t d[5];
  if (!i2cRead(ADDR_TOUCH, 0x02, d, 5)) return false;
  if ((d[0] & 0x0F) == 0) return false;
  x = ((d[1] & 0x0F) << 8) | d[2];
  y = ((d[3] & 0x0F) << 8) | d[4];
  if (x >= W) x = W - 1;
  if (y >= H) y = H - 1;
  return true;
}

void setBrightness(uint8_t level) {
  if (!lcd) return;
  // QSPI-Befehle gehen beim CO5300 als Opcode 0x02 mit Adresse (cmd << 8)
  uint32_t addr = (0x02u << 24) | (0x51u << 8);
  lcd->getBus()->writeRegisterData(addr, &level, 1);
}

}  // namespace board

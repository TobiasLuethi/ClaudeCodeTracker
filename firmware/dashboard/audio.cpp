#include "audio.h"
#include <Wire.h>
#include <ESP_I2S.h>

namespace audio {
namespace {

constexpr uint8_t ADDR_ES8311 = 0x18;
constexpr int PIN_MCLK = 16, PIN_BCLK = 9, PIN_WS = 45, PIN_DOUT = 8, PIN_DIN = 10, PIN_PA = 46;
constexpr uint32_t RATE = 16000;  // MCLK = 256 * 16 kHz = 4.096 MHz
constexpr uint8_t VOLUME = 0xB0;  // DAC-Lautstärke, 0xBF = 0 dB

I2SClass i2s;
TaskHandle_t task = nullptr;
bool ready = false;

bool wr(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ADDR_ES8311);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

// Init nach dem ES8311-Treiber von Espressif (Slave, I2S 16 bit, MCLK vom Pin, 4.096 MHz / 16 kHz)
bool initCodec() {
  bool ok = true;
  ok &= wr(0x45, 0x00);
  ok &= wr(0x01, 0x30);
  ok &= wr(0x02, 0x00);
  ok &= wr(0x03, 0x10);
  ok &= wr(0x16, 0x24);
  ok &= wr(0x04, 0x10);
  ok &= wr(0x05, 0x00);
  ok &= wr(0x0B, 0x00);
  ok &= wr(0x0C, 0x00);
  ok &= wr(0x10, 0x1F);
  ok &= wr(0x11, 0x7F);
  ok &= wr(0x00, 0x80);  // Power on, Slave-Modus
  ok &= wr(0x01, 0x3F);  // alle Takte an, MCLK vom Pin
  // Taktteiler für 4.096 MHz / 16 kHz: pre_div 1, x1, adc/dac_div 1, OSR 0x10, BCLK/4, LRCK = MCLK/256
  ok &= wr(0x02, 0x00);
  ok &= wr(0x05, 0x00);
  ok &= wr(0x03, 0x10);
  ok &= wr(0x04, 0x10);
  ok &= wr(0x07, 0x00);
  ok &= wr(0x08, 0xFF);
  ok &= wr(0x06, 0x03);
  ok &= wr(0x13, 0x10);
  ok &= wr(0x1B, 0x0A);
  ok &= wr(0x1C, 0x6A);
  // Start: Formate I2S 16 bit, DAC einschalten
  ok &= wr(0x09, 0x0C);
  ok &= wr(0x0A, 0x0C);
  ok &= wr(0x17, 0xBF);
  ok &= wr(0x0E, 0x02);
  ok &= wr(0x12, 0x00);
  ok &= wr(0x14, 0x1A);
  ok &= wr(0x0D, 0x01);
  ok &= wr(0x15, 0x40);
  ok &= wr(0x37, 0x48);
  ok &= wr(0x45, 0x00);
  ok &= wr(0x32, VOLUME);
  ok &= wr(0x31, 0x00);  // nicht stumm
  return ok;
}

// Zwei weiche Töne (E6, dann A6) mit Ein- und Ausblenden
void playChime() {
  struct Note { float hz; int ms; };
  const Note notes[] = {{1318.5f, 110}, {0, 30}, {1760.0f, 190}};
  int16_t buf[256 * 2];
  digitalWrite(PIN_PA, HIGH);
  delay(5);
  for (const Note &n : notes) {
    int total = RATE * n.ms / 1000;
    int fade = RATE * 12 / 1000;
    float phase = 0, step = 2.0f * PI * n.hz / RATE;
    for (int i = 0; i < total;) {
      int k = 0;
      for (; k < 256 && i < total; k++, i++) {
        float env = 1.0f;
        if (i < fade) env = (float)i / fade;
        else if (i > total - fade) env = (float)(total - i) / fade;
        int16_t v = n.hz > 0 ? (int16_t)(sinf(phase) * 9000.0f * env) : 0;
        phase += step;
        if (phase > 2.0f * PI) phase -= 2.0f * PI;
        buf[2 * k] = v;
        buf[2 * k + 1] = v;
      }
      i2s.write((uint8_t *)buf, k * 2 * sizeof(int16_t));
    }
  }
  // Stille nachschieben, damit der DMA-Puffer sauber ausläuft
  memset(buf, 0, sizeof(buf));
  for (int i = 0; i < 8; i++) i2s.write((uint8_t *)buf, sizeof(buf));
  digitalWrite(PIN_PA, LOW);
}

void audioTask(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    playChime();
  }
}

}  // namespace

bool begin() {
  pinMode(PIN_PA, OUTPUT);
  digitalWrite(PIN_PA, LOW);
  i2s.setPins(PIN_BCLK, PIN_WS, PIN_DOUT, PIN_DIN, PIN_MCLK);
  if (!i2s.begin(I2S_MODE_STD, RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) return false;
  delay(10);  // MCLK muss laufen, bevor der Codec konfiguriert wird
  if (!initCodec()) return false;
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 2, &task, 0);
  ready = true;
  return true;
}

void chime() {
  if (ready && task) xTaskNotifyGive(task);
}

}  // namespace audio

# Claude Touch Gadget

Firmware und PC-Teil für ein Waveshare ESP32-S3-Touch-AMOLED-1.8, das den Status der
Claude-Code-Sessions anzeigt (Vorbild: Touch-Dashboard-Mod unter `~/.claude/touch-dashboard`).

## Ziel
- Kacheln pro Session (Chatname, Zustand: arbeitet / wartet / Frage / Freigabe), Tipp öffnet den Chat auf dem PC
- Ton und Hinweis bei Freigabe oder Frage
- Verbindung per USB (COM-Port) oder WLAN mit Akku; USB hat Vorrang
- Ohne PC-Verbindung: Uhr mit Wetter
- Board umdrehen: Nicht stören, Display aus
- Freigaben werden nie am Gerät erteilt, nur in der Claude-App

## Hardware (geprüft / Quelle)
| Teil | Chip | Pins / Adresse | Stand |
|---|---|---|---|
| MCU | ESP32-S3 rev v0.2, 8 MB OPI-PSRAM, 16 MB Flash | – | geprüft mit esptool |
| Display 368×448 AMOLED | CO5300, QSPI | CS 12, CLK 11, D0–D3 4/5/6/7, Reset über TCA9554 | CO5300 in Originalfirmware gefunden; Pins aus Waveshare-BSP |
| Touch | CST816S oder FT3168 (FT5x06) | I2C, INT 21 | BSP prüft beide |
| I2C-Bus | – | SDA 15, SCL 14 | Waveshare-BSP |
| Audio | ES8311 | I2S MCLK 16, SCLK 9, LRCK 45, DOUT 8, DIN 10, Verstärker 46 | Waveshare-BSP |
| SD-Karte | 1-Bit SDMMC | CLK 2, CMD 1, D0 3 | Waveshare-BSP |
| IO-Expander | TCA9554 | I2C | Waveshare-BSP |
| Power | AXP2101 | I2C | Produktseite |
| IMU / RTC | QMI8658 / PCF85063 | I2C | Produktseite |

### I2C-Adressen (am Board gemessen mit `firmware/hwtest`, 2026-11-27)
| Adresse | Chip |
|---|---|
| 0x15 | Touch, CST816-Familie (Produktseite: CST820). Laut BSP gilt dann ein Display-X-Gap von 0x10 |
| 0x18 | ES8311 Audio |
| 0x20 | TCA9554 IO-Expander |
| 0x34 | AXP2101 Power |
| 0x51 | PCF85063 RTC |
| 0x6B | QMI8658 IMU |
| 0x7E | antwortet ebenfalls. 0x7C–0x7F sind laut I2C-Spezifikation reserviert (Device-ID), also kein eigener Baustein; wird ignoriert |

### Display und Touch (am Board bestätigt mit `firmware/displaytest`, 2026-11-27)
- CO5300 über `LCD_CO5300` aus ESP32_Display_Panel, Init-Befehle aus dem Waveshare-BSP, QSPI 40 MHz, kein Reset-Pin
- Reset von Display und Touch über TCA9554 P0–P2 (low 50 ms, dann high)
- `setGapX(16)` nötig; Farben, Ränder und Ausrichtung damit korrekt
- RGB565 mit getauschten Bytes (MSB zuerst)
- **Alle Zeichenbereiche brauchen gerade x, y, Breite und Höhe**, sonst verwirft der Treiber sie
- Touch CST816 direkt über Wire: Reg 0x02 Fingerzahl, 0x03–0x06 X/Y; Koordinaten passen ohne Spiegelung zum Display

## Betrieb (Stand 0.1: nur USB)
1. Mod `touch-dashboard` ist in Claude Code aktiv (schreibt `~/.claude/touch-dashboard/sessions/*.json`)
2. Brücke starten: `pc\start-bridge.ps1` (oder `node pc\bridge.mjs`); findet das Gadget über die Espressif-USB-ID,
   sonst `$env:GADGET_PORT='COM5'`
3. Gadget zeigt je Session eine Kachel: Zustand (Freigabe, Frage, Arbeitet, Bereit), Alter, Projekt, Subagents;
   bei Frage/Freigabe zusätzlich die Frage bzw. den wartenden Tool-Call. Tipp öffnet den Chat in der Claude-App.
4. Ohne Sessions: grosse Uhr. Ohne Brücke: "Warte auf PC".
5. Ton (zwei kurze Töne), wenn eine Session neu auf Freigabe oder Antwort wartet; nicht beim ersten Stand nach
   dem Verbinden. Lautsprecher-Symbol oben rechts schaltet stumm (bleibt im NVS gespeichert).

Protokoll: eine JSON-Zeile pro Nachricht, siehe `firmware/dashboard/link.h`.
Schrift: Montserrat Medium (`font_montserrat_ttf.cpp`, eingebettete TTF) wird zur Laufzeit von LVGL `tiny_ttf`
gerendert: ASCII, Latin-1 und €. Symbole (USB, Lautsprecher, Haken) kommen als Fallback aus den eingebauten
LVGL-Schriften. Die Brücke ersetzt typografische Zeichen ("–", "·", "…") durch ASCII und entfernt Emojis;
Texte werden an UTF-8-Zeichengrenzen auf die Puffergrössen des Gadgets gekürzt.

Ton: ES8311 per Wire konfiguriert (Init nach Espressif-Treiber, Slave, I2S 16 bit, 16 kHz, MCLK 4.096 MHz),
I2S über `ESP_I2S`, Verstärker an GPIO 46; Wiedergabe in eigenem Task (`audio.cpp`).

Hinweis Stack: Hauptprogramm läuft mit 16 KB Stack (`SET_LOOP_TASK_STACK_SIZE`). Grosse Strukturen wie
`pclink::Model` (~7 KB) nie lokal anlegen, sonst Neustart-Schleife mit "Double exception".

**Zum Flashen die Brücke beenden**, sonst ist der COM-Port belegt.

## Build
- Toolchain: Arduino IDE 2.3.11 (enthält `arduino-cli` 1.6.0), ESP32-Core 3.3.12
- Bibliotheken: lvgl 9.6.0, ESP32_Display_Panel 2.0.0, ESP32_IO_Expander 2.0.0, esp-lib-utils 0.2.3,
  ArduinoJson 7.4.3, XPowersLib 0.3.3, SensorLib 0.5.2
- FQBN: siehe `firmware/FQBN.txt`

```powershell
.\firmware\build.ps1 dashboard            # kompilieren
.\firmware\build.ps1 dashboard -Upload    # kompilieren und flashen (COM3, sonst -Port)
```
`build.ps1` setzt den Include-Pfad für `lv_conf.h` (liegt im Sketch-Ordner). In der Arduino IDE baut der
Sketch deshalb nicht ohne Weiteres; das CLI-Skript ist der vorgesehene Weg.

## Ordner
- `backup/` – Originalfirmware (XiaoZhi) und Anleitung zum Zurückspielen
- `firmware/hwtest` – I2C-Scan
- `firmware/displaytest` – Farben, Randlineal, Malen per Touch
- `firmware/dashboard` – eigentliche Firmware (LVGL 9)
- `pc/` – USB-Brücke (`bridge.mjs`, Node 18+, Paket `serialport`)

## Offen
- WLAN-Betrieb mit Kopplung per USB, Uhr/Wetter offline, Akku, Lagesensor (umdrehen = Nicht stören)
- Installer für den Kollegen (Mod, Brücke, Autostart)

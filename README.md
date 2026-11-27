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

## Betrieb (Stand 0.2: USB und WLAN)
1. Mod `touch-dashboard` ist in Claude Code aktiv (schreibt `~/.claude/touch-dashboard/sessions/*.json`)
2. Brücke starten: `pc\start-bridge.ps1` (oder `node pc\bridge.mjs`); findet das Gadget über die Espressif-USB-ID,
   sonst `$env:GADGET_PORT='COM5'`
3. Gadget zeigt je Session eine Kachel: Zustand (Freigabe, Frage, Arbeitet, Bereit), Alter, Projekt, Subagents;
   bei Frage/Freigabe zusätzlich die Frage bzw. den wartenden Tool-Call. Tipp öffnet den Chat in der Claude-App.
4. Ohne Sessions: grosse Uhr. Ohne Brücke: "Warte auf PC".
5. Ton (zwei kurze Töne), wenn eine Session neu auf Freigabe oder Antwort wartet; nicht beim ersten Stand nach
   dem Verbinden. Lautsprecher-Symbol oben rechts schaltet stumm (bleibt im NVS gespeichert).
6. Kopfzeile rechts: Uhr, Verbindung (USB/WLAN), Akku, Ton. Tipp auf den Titel öffnet die Einstellungen.
7. **Nicht stören:** Gadget mit dem Display nach unten hinlegen (QMI8658, ~1 s entprellt) schaltet Display,
   Ton und Touch aus. Zurückdrehen weckt es ("Nicht stören aus"). Achse am Board gemessen: Display oben
   ergibt az ≈ -1.1 g (`UP_SIGN` in `imu.cpp`).
8. **Tasten:**
   - **BOOT** (GPIO 0): jeder Klick wechselt Normal → Ohne Ton → Nicht stören → Normal. "Nicht stören" zeigt die
     Meldung 1.5 s, dann geht das Display aus; der nächste Klick weckt.
   - **PWR** (am AXP2101): kurz = Hinweis, 1.5 s halten = Ausschalten (`pmu.shutdown()`), aus dem
     ausgeschalteten Zustand 0.5 s drücken = Einschalten. 6 s halten = Hardware-Notabschaltung des AXP2101.

### WLAN-Betrieb
1. **WLAN einrichten** (Einstellungen): Gadget öffnet den offenen Hotspot `Claude-Gadget-xxxx` (endet nach dem
   Speichern oder nach 5 min) und zeigt einen QR-Code. Am Handy verbinden, mobile Daten aus,
   `http://192.168.4.1` öffnen, WLAN wählen, Passwort eingeben. Zugangsdaten liegen im NVS des Gadgets.
2. **Mit diesem PC koppeln** (Einstellungen, nur am USB): Brücke erzeugt einen 256-bit-Schlüssel, speichert ihn
   in `~/.claude/touch-dashboard/gadgets.json` und schickt ihn per USB ans Gadget. Erst danach öffnet die
   Brücke UDP 47390 (Suche) und TCP 47391 (Verbindung). Windows fragt dabei nach der Firewall-Freigabe für Node.
3. Ohne USB-Datenverbindung sucht das Gadget den PC per UDP-Broadcast, verbindet per TCP und meldet sich mit
   HMAC-SHA256(Schlüssel, Zufallswert der Brücke) an. Nur gekoppelte Gadgets bekommen Antworten und Daten.
   Ist USB aktiv, wird die WLAN-Verbindung zur Brücke getrennt (USB hat Vorrang).
4. Uhrzeit: von der Brücke (`ts`), sonst NTP über WLAN; gesichert in der RTC PCF85063 (Zeitzone CH).
5. Akku (AXP2101): Symbol in der Kopfzeile, Prozent in den Einstellungen. Ohne USB-Strom dunkelt das Display nach
   2 min ohne Berührung ab; Tipp oder neue Freigabe/Frage weckt es.

Speicher: LVGL alloziert über `lv_mem_psram.c` aus dem PSRAM (`LV_STDLIB_CUSTOM`). Vorher war der interne RAM
bis auf ~6 KB belegt, und der DHCP-Dienst des Hotspots vergab keine Adressen. Nach dem Start meldet das Gadget
den freien internen RAM an die Brücke (`Start: interner RAM frei ...`); er sollte deutlich über 100 KB liegen.

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

## Installer (PC des Empfängers)
`installer\Installieren.cmd` → `install.ps1` (nur aktueller Benutzer, keine Admin-Rechte):
- prüft Node.js ≥ 18, bietet sonst `winget install OpenJS.NodeJS.LTS` an
- kopiert die Brücke nach `%LOCALAPPDATA%\ClaudeTouchGadget\bridge` und führt `npm ci` aus
- kopiert den Mod (`mod/touch-dashboard`) und trägt ihn per `pc/settings-helper.mjs` in
  `~/.claude/settings.json` → `env.CLAUDE_CODE_PLUGIN_DIRS` ein (bestehende Einträge bleiben, Sicherung
  `settings.json.bak-touch-gadget`, BOM wird toleriert)
- Autostart: Verknüpfung im Startup-Ordner → `start-bridge.vbs` startet `node bridge.mjs` unsichtbar,
  Protokoll `bridge.log` (ab 1 MB neu)
- die Brücke läuft nur einmal (Sperre auf 127.0.0.1:47393)

`Deinstallieren.cmd` macht alles rückgängig (Kopplung mit `-KeepPairing` behalten). Parameter `-InstallDir`,
`-ClaudeHome`, `-NoAutostart`, `-NoStart` erlauben einen Test in einem Sandbox-Ordner.
Anleitung für den Empfänger: [ANLEITUNG.md](ANLEITUNG.md).

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
- `firmware/aptest` – Minimaltest Hotspot + Webseite (Fehlersuche WLAN)
- `firmware/dashboard` – eigentliche Firmware (LVGL 9)
- `pc/` – Brücke (`bridge.mjs`, Node 18+, Paket `serialport`) und `settings-helper.mjs`
- `mod/touch-dashboard` – der Claude-Code-Mod, der den Status jeder Session schreibt
- `installer/` – Installieren.cmd / Deinstallieren.cmd

## Offen
- Akkubetrieb mit echtem Akku testen
- Installer einmal auf einem frischen PC durchspielen (Autostart-Verknüpfung bisher nur per Code geprüft)

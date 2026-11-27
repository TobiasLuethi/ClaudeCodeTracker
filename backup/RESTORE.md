# Originale Firmware zurückspielen

`original-firmware-16MB.bin` ist das vollständige Abbild (16 MB) des Boards im Auslieferungszustand,
ausgelesen am 2026-11-27. Es enthält den Sprachassistenten **XiaoZhi** von Waveshare.

- Chip: ESP32-S3 (QFN56) rev v0.2, 8 MB PSRAM, 16 MB Flash
- MAC: 44:bd:8d:77:4c:60
- SHA256: `06702A4D6926862473127EFB1C38A9BC8E1D4087E79D4F031CC7B4E9321EA047`
- Mit `esptool verify-flash` gegen das Board geprüft: identisch.

## Zurückspielen (überschreibt alles auf dem Board)

```powershell
& "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py\5.3.1\esptool.exe" --port COM3 write-flash 0 original-firmware-16MB.bin
```

COM-Port bei Bedarf anpassen. Falls das Board nicht verbindet: BOOT gedrückt halten, kurz PWR/Reset, BOOT loslassen.

Hinweis: Beim Auslesen über die eingebaute USB-Schnittstelle brach `read-flash` in einigen Bereichen ab
("Packet content transfer stopped"). Gelesen wurde deshalb in 256-KB-Stücken, einzelne mit `--no-stub`.

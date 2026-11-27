# Claude Touch Gadget – Anleitung

Das Gadget zeigt dir auf einen Blick, was deine Claude-Code-Sessions gerade tun: welche arbeiten, welche
fertig sind und welche auf dich warten. Wartet eine auf eine Freigabe oder eine Antwort, ertönt ein kurzer
Ton. Ein Tipp auf eine Kachel öffnet den passenden Chat in der Claude-App.

Freigaben und Antworten gibst du immer in der Claude-App, nicht am Gadget.

## Was du brauchst
- Windows-PC mit der Claude-App (Claude Code)
- Node.js 18 oder neuer (der Installer bietet an, es zu installieren)
- USB-C-Kabel; für den Betrieb ohne Kabel einen 3.7-V-LiPo-Akku mit MX1.25-Stecker und ein 2,4-GHz-WLAN

## Installation (einmalig, etwa 2 Minuten)
1. Ordner `ClaudeTouchGadget` auf den PC kopieren (oder auf GitHub "Code → Download ZIP" und entpacken).
2. Im Ordner `installer` auf **Installieren.cmd** doppelklicken.
   - Fehlt Node.js, fragt der Installer, ob er es installieren soll.
   - Er richtet die Verbindungssoftware ("Brücke") ein, trägt den Mod in Claude Code ein und startet die
     Brücke automatisch bei jeder Anmeldung. Admin-Rechte sind nicht nötig.
3. **Claude einmal neu starten**, damit der Mod geladen wird.
4. Gadget per USB anschliessen. Nach ein paar Sekunden erscheinen deine Sessions.

## Bedienung
| | |
|---|---|
| **Kachel antippen** | öffnet den Chat in der Claude-App |
| **Titel oben links antippen** | Einstellungen (WLAN, Kopplung, Akku) |
| **Lautsprecher oben rechts** | Ton aus/an |
| **Taste BOOT** | Modus wechseln: Normal → Ohne Ton → Nicht stören → Normal |
| **Taste PWR gedrückt halten** | ausschalten; kurz drücken schaltet wieder ein |
| **Gadget umdrehen** (Display nach unten) | Nicht stören: Display aus, kein Ton |

Farben der Kacheln: **rot** Freigabe nötig · **gelb** Frage offen · **blau** arbeitet · **grün** fertig.
Ohne Sessions oder ohne Verbindung zeigt das Gadget eine grosse Uhr.

## Ohne Kabel (WLAN)
1. Gadget am USB lassen, Titel antippen → **WLAN einrichten**.
2. Mit dem Handy den QR-Code scannen (oder mit dem Netz `Claude-Gadget-…` verbinden, ohne Passwort),
   mobile Daten kurz ausschalten und `http://192.168.4.1` öffnen. Dein WLAN wählen, Passwort eingeben, speichern.
3. Wieder in den Einstellungen: **Mit diesem PC koppeln**. Fragt Windows nach der Firewall, nur
   **"Private Netzwerke"** erlauben.
4. Jetzt kann das Kabel ab (mit Akku oder an einem USB-Netzteil). Das Gadget findet den PC im WLAN selbst.

Gadget und PC müssen im selben WLAN sein. Am Kabel hat USB immer Vorrang.

## Wenn etwas nicht geht
- **"Warte auf PC"**: läuft die Brücke? Abmelden und wieder anmelden startet sie neu. Protokoll:
  `%LOCALAPPDATA%\ClaudeTouchGadget\bridge.log`
- **Keine Sessions sichtbar**: Claude nach der Installation neu gestartet?
- **WLAN-Einrichtungsseite lädt nicht**: mobile Daten aus, Adresse mit `http://` eingeben.
- **Gadget reagiert nicht mehr**: PWR 6 Sekunden halten (Notabschaltung), dann kurz drücken.

## Entfernen
Im Ordner `installer` auf **Deinstallieren.cmd** doppelklicken, danach Claude neu starten.

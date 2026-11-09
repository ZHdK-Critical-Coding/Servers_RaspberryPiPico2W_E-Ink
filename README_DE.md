# Arduino: E-Ink Server

[English](README.md)

Firmware für einen Raspberry Pi Pico 2 W, die bis zu vier Waveshare-4.2"-
E-Paper-Displays ansteuert (V1, 400 × 300, vier Graustufen). Sie macht
dasselbe wie der `screen-websocket`-Server aus
[Servers_RaspberriPi](https://github.com/ZHdK-Critical-Coding/Servers_RaspberriPi),
mit demselben Protokoll. Darum funktioniert
[P5_Output_EInk](https://github.com/ZHdK-Critical-Coding/P5_Output_EInk)
ohne Änderung, bis auf die Adresse.

Der Pico ist über das **USB-Kabel** erreichbar (er erscheint als
Netzwerkadapter) und auf Wunsch über **WLAN**, beides gleichzeitig.

## Protokoll

- WebSocket auf Port 8765. Ein PNG als Binärnachricht schicken. Es wird auf
  400 × 300 skaliert und in vier Graustufen gezeigt, mit denselben
  Graustufen wie der Pi-Server.
- Der Pfad wählt das Display: `/1` … `/4`. `/` ist Display 1, `/all` schickt
  dasselbe Bild an alle Displays.
- Neu: Nach jedem Bild antwortet der Pico mit einer Textnachricht
  (`Image displayed` oder `error: …`). Das p5-Beispiel zeigt sie in seiner
  Statuszeile.

```js
const displayConnection = 'ws://192.168.7.1:8765'      // Pico 1, Display 1, über USB
const displayConnection = 'ws://192.168.7.1:8765/2'    // Pico 1, Display 2
const displayConnection = 'ws://eink-3.local:8765'     // Pico 3, über WLAN
```

Beim Start zeigt jedes Display seine eigenen Adressen.

### Schneller Modus: `?norefresh=true`

Normalerweise ist jedes Bild ein voller Refresh: Das Display flackert und
zeigt vier Graustufen, was einige Sekunden dauert. Vor dem Bild wird das
Display mit einer kräftigen Schwarz-Weiß-Welle weiß geblitzt, damit keine Reste des vorherigen
Bildes bleiben (`CLEAN_FULL_REFRESH` in `PicoEInk.ino`; `false` macht den
vollen Refresh schneller, mit leichten Geisterbildern). Mit `?norefresh=true` am
Ende der Adresse zeichnet der Pico nur die Pixel neu, die sich seit dem
letzten Bild geändert haben (Teil-Refresh), ohne Flackern:

```js
const displayConnection = 'ws://192.168.7.1:8765/1?norefresh=true'
```

- Viel schneller, und je weniger sich ändert, desto schneller: Nur das
  Rechteck um die geänderten Pixel geht an das Display.
- Geänderte Pixel werden nur schwarz oder weiss (dunkelgrau → schwarz,
  hellgrau → weiss). Unveränderte Pixel bleiben, wie sie sind, auch Grau.
- Mit der Zeit bleiben Schatten alter Bilder stehen (Ghosting). Eine
  Verbindung ohne den Schlüssel macht einen vollen Refresh und räumt auf.
- Das erste Bild in diesem Modus ist pro Display noch ein voller Refresh.
  Hat sich nichts geändert, antwortet der Pico sofort mit `Image displayed`.
- Jedes Display, das diesen Modus benutzt hat, braucht 30 KB mehr RAM.

## Mehrere Picos: die ID

Jeder Pico hat eine ID von 1 bis 200. Sie bestimmt:

| ID | USB-Adresse   | Name           |
|----|---------------|----------------|
| 1  | 192.168.7.1   | eink-1.local   |
| 2  | 192.168.8.1   | eink-2.local   |
| N  | 192.168.(6+N).1 | eink-N.local |

**USB-Hub:** USB ist eine Punkt-zu-Punkt-Verbindung. Jeder Pico ist ein
eigener Netzwerkadapter am Computer, mit einem eigenen kleinen Netz, und die
Picos sprechen nie miteinander. Es gibt also keinen Master und keinen Slave.
Jeder Pico ist der DHCP-Server für sein eigenes Kabel und gibt dem Computer
eine Adresse. Die einzige Regel: **Jeder Pico braucht eine andere ID.** Mit
derselben ID hat der Computer zwei Adapter im selben Netz und erreicht nur
einen davon.

**WLAN:** Alle Picos verbinden sich mit demselben Netz und bekommen eine
Adresse vom Router. Der Browser findet sie über den Namen (`eink-N.local`,
mDNS) oder über die IP auf dem Display. Stattdessen lässt sich auch eine
feste IP einstellen (siehe unten).

## Einstellungen

Jeder Pico bekommt dieselbe `PicoEInk.uf2`. Die Einstellungen (ID, WLAN)
liegen im Flash und bleiben bei einem Neustart und beim erneuten Flashen
erhalten.

### Mit config.txt (am einfachsten)

1. Den Pico einstecken und warten, bis das Display seine Adressen zeigt.
2. **Kurz den BOOTSEL-Knopf drücken.** Das Display zeigt "Config mode", und
   am Computer erscheint ein USB-Laufwerk.
3. `config.txt` auf dem Laufwerk öffnen, ändern und speichern:

   ```
   id = 3
   wifi = MeinNetz
   password = geheim123
   ip = dhcp
   ```

4. **Das Laufwerk auswerfen.** Der Pico speichert die Einstellungen und
   startet neu. Falls das Auswerfen nichts bewirkt (z. B. unter Linux mit
   `umount`), nach dem Speichern noch einmal BOOTSEL drücken.

BOOTSEL *beim Einstecken* gedrückt halten startet weiterhin den normalen
Firmware-Lader (das Laufwerk `RP2350` für `.uf2`-Dateien). Der Config-Modus
ist ein kurzer Druck, während die Firmware läuft.

| Schlüssel  | Wert                                                      |
|------------|-----------------------------------------------------------|
| `id`       | Pico-Nummer 1-200                                         |
| `wifi`     | Name des WLANs; leer = kein WLAN                          |
| `password` | WLAN-Passwort; leer = offenes Netz                        |
| `ip`       | `dhcp` (vom Router) oder eine feste Adresse, z. B. `192.168.1.51` |
| `gateway`  | nur bei fester Adresse; Standard x.x.x.1                  |
| `mask`     | nur bei fester Adresse; Standard 255.255.255.0            |

### Über die serielle Schnittstelle

Serieller Monitor mit 115200 Baud, Zeilenende *Neue Zeile*:

| Befehl                    | Bedeutung                                        |
|---------------------------|--------------------------------------------------|
| `show`                    | aktuelle Einstellungen                           |
| `id <1-200>`              | Pico-Nummer                                      |
| `wifi <name>` / `wifi off`| WLAN                                             |
| `pass <passwort>`         | WLAN-Passwort (`pass` ohne etwas dahinter: offenes Netz) |
| `ip dhcp`                 | WLAN-Adresse vom Router (Standard)               |
| `ip <adr> [gw] [mask]`    | feste WLAN-Adresse                               |
| `reboot`                  | mit den neuen Einstellungen neu starten          |

Um stattdessen die Standardwerte im Code festzulegen, `DEFAULTS` oben in
`PicoEInk.ino` ändern.

## Verkabelung: mehrere Displays an einem Pico

Alle Displays teilen sich den SPI-Bus. CLK, DIN, DC, RST, VCC und GND gehen
an jedes Display; jedes Display hat ein eigenes CS und BUSY:

| Display-Pin | Display 1 | Display 2 | Display 3 | Display 4 |
|-------------|-----------|-----------|-----------|-----------|
| CLK         | GPIO 10   | GPIO 10   | GPIO 10   | GPIO 10   |
| DIN         | GPIO 11   | GPIO 11   | GPIO 11   | GPIO 11   |
| DC          | GPIO 8    | GPIO 8    | GPIO 8    | GPIO 8    |
| RST         | GPIO 12   | GPIO 12   | GPIO 12   | GPIO 12   |
| **CS**      | GPIO 9    | GPIO 14   | GPIO 16   | GPIO 18   |
| **BUSY**    | GPIO 13   | GPIO 15   | GPIO 17   | GPIO 19   |
| VCC / GND   | 3V3 / GND | 3V3 / GND | 3V3 / GND | 3V3 / GND |

Display 1 passt zum Waveshare-Board Pico-ePaper-4.2. Nicht angeschlossene
Displays werden beim Start erkannt und übersprungen; jedes fehlende
verlängert den Start um etwa 1,5 s. Für andere Pins oder mehr Displays
`DISPLAY_PINS` in `PicoEInk.ino` ändern.

Mehrere Displays aktualisieren gleichzeitig: Der Pico schickt das Bild an
ein Display (etwa 0,3 s) und macht mit dem nächsten weiter, während das
erste noch aktualisiert. Sie teilen sich einen Bildspeicher, jedes weitere
Display braucht also kein zusätzliches RAM.

## Flashen

**Ohne IDE:** BOOTSEL gedrückt halten, den Pico einstecken und
`PicoEInk.uf2` auf das Laufwerk `RP2350` ziehen.

**Mit der Arduino IDE:**

1. Das Board-Paket hinzufügen: *Einstellungen → Zusätzliche
   Boardverwalter-URLs*:
   `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`,
   dann **Raspberry Pi Pico/RP2040/RP2350** installieren (getestet mit 6.3.2).
2. Board: **Raspberry Pi Pico 2W**. *USB Stack* auf **Pico SDK** lassen.
   *Flash Size*: **4MB (Sketch: 3MB, FS: 1MB)**. Ohne Dateisystem
   funktioniert config.txt nicht, alles andere schon.
3. `PicoEInk/PicoEInk.ino` öffnen und hochladen. Keine Bibliotheken nötig.

## Computer und Netzwerk

- **USB:** macOS, Linux und Windows 11 funktionieren ohne Treiber
  (CDC-NCM). Windows 10 hat keinen NCM-Treiber. Der Pico gibt dem Computer
  keinen Router und kein DNS, der Internetverkehr läuft also weiter über
  die normale Verbindung.
- **Prüfen:** `http://<adresse>:8765` im Browser öffnen. Die Seite listet
  die Adressen und den Zustand jedes Displays.
- **WLAN:** einen eigenen Router oder Hotspot verwenden (WPA2 mit Passwort
  oder offen). Hochschulnetze funktionieren meist nicht: WPA2-Enterprise
  (z. B. eduroam) wird nicht unterstützt, und oft blockieren sie den
  Verkehr zwischen Geräten und `.local`-Namen.
- **`.local`-Namen** funktionieren unter macOS, iOS und Linux und unter
  Windows 10/11 meistens. Sonst die IP vom Display oder eine feste IP
  verwenden.
- Die USB-Netze verwenden 192.168.7.x und höher. Falls das WLAN eines davon
  ebenfalls nutzt, eine andere ID wählen.

## Hinweise

- Ein Refresh dauert einige Sekunden. Ein Bild, das ankommt, während dieses
  Display noch aktualisiert, wartet darauf. Also bei Ereignissen senden,
  nicht in jedem Frame.
- Grenzen: PNG bis 200 KB und bis 800 px breit (eine 400 × 300-Zeichenfläche
  mit `pixelDensity(2)` geht). Interlaced- und 16-Bit-PNGs werden nicht
  unterstützt, Browser erzeugen sie aber nicht. Transparente Bereiche
  erscheinen weiss.
- Bis zu 6 offene Verbindungen. Verbinden sich mehr, wird die älteste
  geschlossen.
- Debug-Ausgabe und Einstellungen: serielle USB-Schnittstelle, 115200 Baud.
- Der Treiber ist aus `epd4in2.py` portiert (V1-Panel, Controller UC8176).
  Ein V2-Panel (SSD1683) braucht einen anderen Treiber.

## Dateien

- `PicoEInk.uf2`: fertig gebaute Firmware (auf das Laufwerk `RP2350` ziehen)
- `PicoEInk/PicoEInk.ino`: Standardwerte, Pins, Setup und Loop, Verbindungen, WLAN
- `PicoEInk/settings.h`: Einstellungen im Flash, serielle Befehle, Format von config.txt
- `PicoEInk/config_mode.h`: BOOTSEL → USB-Laufwerk mit config.txt
- `PicoEInk/epd4in2.h`: Display-Treiber (geteilter Bus, Refresh ohne Warten)
- `PicoEInk/framebuffer.h`: Bildspeicher, Text, PNG → Graustufen
- `PicoEInk/font.h`: Bitmap-Schrift für den Statusbildschirm (Aileron, CC0, generiert)
- `PicoEInk/websocket.h`: WebSocket-Handshake und Frames
- `PicoEInk/dhcp_server.h`: gibt dem Computer seine Adresse, nur über USB
- `PicoEInk/src/PNGdec/`: [PNGdec](https://github.com/bitbank2/PNGdec) 1.1.7
  (Apache-2.0), mit grösserem Zeilenpuffer

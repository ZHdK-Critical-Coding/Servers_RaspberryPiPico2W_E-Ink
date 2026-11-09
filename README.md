# Arduino: E-Ink Server

[Deutsch](README_DE.md)

Firmware for a Raspberry Pi Pico 2 W that drives up to four Waveshare 4.2"
e-paper displays (V1, 400 × 300, four greys). It does the same job as the
`screen-websocket` server from
[Servers_RaspberriPi](https://github.com/ZHdK-Critical-Coding/Servers_RaspberriPi),
using the same protocol, so
[P5_Output_EInk](https://github.com/ZHdK-Critical-Coding/P5_Output_EInk)
works without changes, apart from the address.

The Pico can be reached over the **USB cable** (it shows up as a network
adapter) and optionally over **Wi-Fi**, both at the same time.

## Protocol

- WebSocket on port 8765. Send a PNG as a binary message. It is scaled to
  400 × 300 and shown in four greys, using the same grey levels as the Pi
  server.
- The path picks the display: `/1` … `/4`. `/` is display 1, and `/all` sends
  the same image to every display.
- New: after every image the Pico answers with a text message
  (`Image displayed` or `error: …`). The p5 example shows it in its status
  line.

```js
const displayConnection = 'ws://192.168.7.1:8765'      // Pico 1, display 1, via USB
const displayConnection = 'ws://192.168.7.1:8765/2'    // Pico 1, display 2
const displayConnection = 'ws://eink-3.local:8765'     // Pico 3, via Wi-Fi
```

On startup every display shows its own addresses.

### Fast mode: `?norefresh=true`

By default every image is a full refresh: the panel flashes and shows four
greys, which takes a few seconds. With `?norefresh=true` at the end of the
address, the Pico only redraws the pixels that changed since the last
image (partial refresh), without flashing:

```js
const displayConnection = 'ws://192.168.7.1:8765/1?norefresh=true'
```

- Much faster, and the less changes, the faster: only the rectangle
  around the changed pixels is sent to the panel.
- Changed pixels become black or white only (dark grey → black, light
  grey → white). Unchanged pixels stay as they are, greys included.
- Ghosting builds up over time. A connection without the key does a full
  refresh and cleans the panel.
- The first image in this mode on a display is still a full refresh. If
  nothing changed, the Pico answers `Image displayed` at once.
- Each display that has used this mode uses 30 KB of extra RAM.

## Several Picos: the ID

Every Pico has an ID from 1 to 200. It sets:

| ID | USB address   | Name           |
|----|---------------|----------------|
| 1  | 192.168.7.1   | eink-1.local   |
| 2  | 192.168.8.1   | eink-2.local   |
| N  | 192.168.(6+N).1 | eink-N.local |

**USB hub:** USB is point to point. Every Pico is its own network adapter
on the computer, with its own small network, and the Picos never talk to
each other. So there is no master or slave. Each Pico is the DHCP server
for its own cable and hands the computer one address. The only rule: **every
Pico needs a different ID**. With the same ID, the computer has two adapters
on the same network and reaches only one of them.

**Wi-Fi:** all Picos join the same network and get an address from the
router. The browser finds them by name (`eink-N.local`, mDNS) or by the IP
on the display. A fixed IP can be set instead (see below).

## Settings

Every Pico gets the same `PicoEInk.uf2`. The settings (ID, Wi-Fi) are
stored in flash, and survive restarts and reflashing.

### With config.txt (easiest)

1. Plug in the Pico and wait until the display shows its addresses.
2. **Briefly press the BOOTSEL button.** The display shows "Config mode", and
   a USB drive appears on the computer.
3. Open `config.txt` on the drive, change it, and save:

   ```
   id = 3
   wifi = MyNetwork
   password = secret123
   ip = dhcp
   ```

4. **Eject the drive.** The Pico stores the settings and restarts. If
   ejecting does nothing (e.g. on Linux with `umount`), press BOOTSEL again
   after saving.

Holding BOOTSEL *while plugging in* still starts the normal firmware loader
(the `RP2350` drive for `.uf2` files). Config mode is a short press while
the firmware is running.

| Key        | Value                                                     |
|------------|-----------------------------------------------------------|
| `id`       | Pico number 1-200                                         |
| `wifi`     | Wi-Fi network name; empty = no Wi-Fi                      |
| `password` | Wi-Fi password; empty = open network                      |
| `ip`       | `dhcp` (from the router) or a fixed address, e.g. `192.168.1.51` |
| `gateway`  | only with a fixed address; default x.x.x.1                |
| `mask`     | only with a fixed address; default 255.255.255.0          |

### Over the serial port

Serial monitor at 115200 baud, line ending *Newline*:

| Command                   | Meaning                                          |
|---------------------------|--------------------------------------------------|
| `show`                    | current settings                                 |
| `id <1-200>`              | Pico number                                      |
| `wifi <name>` / `wifi off`| Wi-Fi network                                    |
| `pass <password>`         | Wi-Fi password (`pass` with nothing after it: open network) |
| `ip dhcp`                 | Wi-Fi address from the router (default)          |
| `ip <addr> [gw] [mask]`   | fixed Wi-Fi address                              |
| `reboot`                  | restart with the new settings                    |

To set the defaults in code instead, change `DEFAULTS` at the top of
`PicoEInk.ino`.

## Wiring: several displays on one Pico

All displays share the SPI bus. CLK, DIN, DC, RST, VCC and GND go to every
display; each display has its own CS and BUSY:

| Display pin | Display 1 | Display 2 | Display 3 | Display 4 |
|-------------|-----------|-----------|-----------|-----------|
| CLK         | GPIO 10   | GPIO 10   | GPIO 10   | GPIO 10   |
| DIN         | GPIO 11   | GPIO 11   | GPIO 11   | GPIO 11   |
| DC          | GPIO 8    | GPIO 8    | GPIO 8    | GPIO 8    |
| RST         | GPIO 12   | GPIO 12   | GPIO 12   | GPIO 12   |
| **CS**      | GPIO 9    | GPIO 14   | GPIO 16   | GPIO 18   |
| **BUSY**    | GPIO 13   | GPIO 15   | GPIO 17   | GPIO 19   |
| VCC / GND   | 3V3 / GND | 3V3 / GND | 3V3 / GND | 3V3 / GND |

Display 1 matches the Waveshare Pico-ePaper-4.2 board. Displays that are
not connected are detected at startup and skipped; each missing one adds
about 1.5 s to the start. To use other pins or more displays, change
`DISPLAY_PINS` in `PicoEInk.ino`.

Several displays refresh at the same time: the Pico sends the image to one
display (about 0.3 s) and goes on to the next while the first is still
refreshing. They share one frame buffer, so each extra display costs no RAM.

## Flashing

**Without the IDE:** hold BOOTSEL, plug in the Pico, and drag
`PicoEInk.uf2` onto the `RP2350` drive.

**With the Arduino IDE:**

1. Add the board package: *Settings → Additional boards manager URLs*:
   `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`,
   then install **Raspberry Pi Pico/RP2040/RP2350** (tested with 6.3.2).
2. Board: **Raspberry Pi Pico 2W**. Leave *USB Stack* at **Pico SDK**.
   *Flash Size*: **4MB (Sketch: 3MB, FS: 1MB)**. Without a file system,
   config.txt doesn't work, but everything else does.
3. Open `PicoEInk/PicoEInk.ino` and upload. No libraries to install.

## Computer and network

- **USB:** macOS, Linux and Windows 11 work without drivers (CDC-NCM).
  Windows 10 has no NCM driver. The Pico gives the computer no router and
  no DNS, so internet traffic keeps going over its normal connection.
- **Check:** open `http://<address>:8765` in a browser. It lists the
  addresses and the state of every display.
- **Wi-Fi:** use a router or hotspot of your own (WPA2 with a password, or
  open). University networks usually do not work: WPA2-Enterprise (e.g.
  eduroam) is not supported, and they often block traffic between devices
  and `.local` names.
- **`.local` names** work on macOS, iOS and Linux, and on Windows 10/11 in most
  cases. If not, use the IP shown on the display, or a fixed IP.
- The USB networks use 192.168.7.x and up. If the Wi-Fi network uses
  one of these as well, choose another ID.

## Notes

- A refresh takes a few seconds. An image sent while that display is still
  refreshing waits for it, so send on events, not every frame.
- Limits: PNG up to 200 KB and up to 800 px wide (a 400 × 300 canvas with
  `pixelDensity(2)` is fine). Interlaced and 16-bit PNGs are not supported,
  but browsers do not produce them. Transparent areas show as white.
- Up to 6 open connections. If more connect, the oldest one is closed.
- Debug output and settings: USB serial port, 115200 baud.
- The driver is ported from `epd4in2.py` (V1 panel, UC8176 controller). A
  V2 panel (SSD1683) needs a different driver.

## Files

- `PicoEInk.uf2`: ready-built firmware (drag onto the `RP2350` drive)
- `PicoEInk/PicoEInk.ino`: defaults, pins, setup and loop, connections, Wi-Fi
- `PicoEInk/settings.h`: settings in flash, serial commands, config.txt format
- `PicoEInk/config_mode.h`: BOOTSEL → USB drive with config.txt
- `PicoEInk/epd4in2.h`: display driver (shared bus, non-blocking refresh)
- `PicoEInk/framebuffer.h`: frame buffer, text, PNG → greys
- `PicoEInk/font.h`: bitmap font for the status screen (Aileron, CC0, generated)
- `PicoEInk/websocket.h`: WebSocket handshake and frames
- `PicoEInk/dhcp_server.h`: gives the computer its address, on the USB link only
- `PicoEInk/src/PNGdec/`: [PNGdec](https://github.com/bitbank2/PNGdec) 1.1.7
  (Apache-2.0), with a larger line buffer

// PicoEInk
//
// Raspberry Pi Pico 2 W + up to 4 Waveshare 4.2" e-paper displays (V1,
// 400 x 300, 4 greys). Replacement for the Raspberry Pi "screen-websocket"
// server used by the P5_Output_EInk example: same protocol (a PNG as one
// binary WebSocket message on port 8765).
//
// Connections (both at the same time):
//   USB   The Pico appears as a network adapter (CDC-NCM). Pico number N
//         has the address 192.168.(6+N).1, e.g. N = 1: 192.168.7.1.
//         Every Pico on a hub has its own link, so give each a different
//         number.
//   Wi-Fi Optional. Joins a given network; reachable as eink-N.local or by
//         the IP shown on the display.
//
// Displays: ws://<address>:8765/1, /2, /3, /4. "/" = display 1, "/all" =
// same image on every display.
//
// Settings (number, Wi-Fi): press BOOTSEL while the Pico runs, it appears
// as a USB drive with config.txt (config_mode.h). Or use the serial port
// (settings.h), or set the defaults below.
//
// Arduino IDE: board "Raspberry Pi Pico 2W" (earlephilhower/arduino-pico),
// USB Stack "Pico SDK", Flash Size "4MB (Sketch: 3MB, FS: 1MB)" (for
// config.txt). No extra libraries needed.

#include <NCMEthernetlwIP.h>
#include <WiFi.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <SimpleMDNS.h>
#include "epd4in2.h"
#include "framebuffer.h"
#include "websocket.h"
#include "dhcp_server.h"
#include "settings.h"
#include "config_mode.h"

// ------------------------------------------------------------ settings
// Defaults for a freshly flashed Pico. Values set over the serial port
// are stored in flash and win over these.
const Settings DEFAULTS = {
  .magic = 0,
  .id = 1,       // Pico number 1..200
  .ssid = "",    // Wi-Fi network, "" = no Wi-Fi
  .pass = "",    // Wi-Fi password
  .wifiIp = {0, 0, 0, 0},  // 0.0.0.0 = address from the router
  .wifiGw = {0, 0, 0, 0},
  .wifiMask = {0, 0, 0, 0},
};

const uint16_t WS_PORT = 8765;

// Wiring. CLK, DIN, DC and RST go to every display. Each display has its
// own CS and BUSY. Display 1 matches the Waveshare Pico-ePaper-4.2 board.
// Slots without a display are detected at start and skipped.
const epd::Bus EPD_BUS = {.sck = 10, .mosi = 11, .dc = 8, .rst = 12};
struct DisplayPins {
  uint8_t cs, busy;
};
const DisplayPins DISPLAY_PINS[] = {
  {9, 13},   // display 1
  {14, 15},  // display 2
  {16, 17},  // display 3
  {18, 19},  // display 4
};
#define EPD_SPI SPI1  // GPIO 10/11 belong to SPI1

const size_t MAX_MESSAGE = 200 * 1024;  // largest PNG accepted
const int MAX_CLIENTS = 6;              // open WebSocket connections

// -------------------------------------------------------------- globals
const int NUM_DISPLAYS = sizeof(DISPLAY_PINS) / sizeof(DISPLAY_PINS[0]);

SettingsStore settings;
NCMEthernetlwIP eth;
WiFiServer server(WS_PORT);
TinyDhcpServer dhcp;

struct Panel {
  epd::EPD4in2 *dev;
  bool present;
  bool refreshing;
  bool showsImage;  // false while it shows the status screen
  uint32_t started;
};
Panel panels[NUM_DISPLAYS];

struct Connection {
  WiFiClient client;
  bool used;
  int target;        // 1..NUM_DISPLAYS, 0 = all, -1 = unknown path
  uint32_t pending;  // displays still refreshing for this client (bit mask)
  bool failed;       // one of them timed out
  uint32_t since;
};
Connection conns[MAX_CLIENTS];

static uint8_t message[MAX_MESSAGE];
static IPAddress wifiIp;  // current Wi-Fi address, 0.0.0.0 if none
static uint32_t wifiAttempt;

// -------------------------------------------------------------- helpers
static int presentCount() {
  int n = 0;
  for (auto &p : panels) n += p.present;
  return n;
}

// "ws://host:8765" plus "/k" if there is more than one display
static String url(const String &host, int display) {
  String s = "ws://" + host + ":" + WS_PORT;
  if (presentCount() > 1) s += "/" + String(display);
  return s;
}

static void reply(Connection &c, const char *text) {
  Serial.println(text);
  if (c.used && c.client.connected()) ws::sendText(c.client, text);
}

// Status screen for display d (1-based) into the frame buffer.
static void drawStatus(int d) {
  fb::clear();
  String title = settings.hostname();
  if (presentCount() > 1) title += " / display " + String(d);
  fb::text(title.c_str(), 12, 12, true);
  int y = 70;
  fb::text("USB", 12, y);
  fb::text(url(settings.usbIp().toString(), d).c_str(), 82, y);
  y += 32;
  fb::text("WLAN", 12, y);
  if (!settings.wifiEnabled()) {
    fb::text("off", 82, y);
  } else if (wifiIp.isSet()) {
    fb::text(url(settings.hostname() + ".local", d).c_str(), 82, y);
    y += 26;
    fb::text(url(wifiIp.toString(), d).c_str(), 82, y);
  } else {
    String s = String("connecting to ") + settings.s.ssid + " ...";
    fb::text(s.c_str(), 82, y);
  }

  // How to change the settings (config_mode.h)
  for (int x = 12; x < epd::WIDTH - 12; x++) fb::setPixel(x, 170, 1);
  fb::text("Settings", 12, 180);
  const char *const steps[] = {
    "1. Briefly press the BOOTSEL button",
    "2. Edit config.txt on the USB drive",
    "3. Save it and eject the drive",
  };
  for (int i = 0; i < 3; i++) fb::text(steps[i], 12, 206 + i * 26, false, 1);
}

// A title and some lines of text on every display.
static void showMessage(const char *title, const char *const lines[], int count) {
  fb::clear();
  fb::text(title, 12, 12, true);
  for (int i = 0; i < count; i++) fb::text(lines[i], 12, 70 + i * 28);
  for (auto &p : panels) {
    if (!p.present) continue;
    if (p.refreshing) p.dev->waitIdle();
    p.dev->startDisplay4Gray(fb::buffer);
    p.refreshing = true;
    p.showsImage = true;
    p.started = millis();
  }
}

// Shows the status screen on every display that has no image yet.
static void showStatusScreens() {
  for (int i = 0; i < NUM_DISPLAYS; i++) {
    Panel &p = panels[i];
    if (!p.present || p.showsImage) continue;
    if (p.refreshing && !p.dev->waitIdle()) continue;
    drawStatus(i + 1);
    p.dev->startDisplay4Gray(fb::buffer);
    p.refreshing = true;
    p.started = millis();
  }
}

// ------------------------------------------------------------- displays
static void initDisplays() {
  epd::EPD4in2::beginBus(EPD_SPI, EPD_BUS);
  for (int i = 0; i < NUM_DISPLAYS; i++) {
    Panel &p = panels[i];
    p.dev = new epd::EPD4in2(EPD_SPI, EPD_BUS, DISPLAY_PINS[i].cs, DISPLAY_PINS[i].busy);
    p.dev->beginPins();
  }
  for (int i = 0; i < NUM_DISPLAYS; i++) {
    Panel &p = panels[i];
    p.present = p.dev->init();  // a missing display times out in init()
    Serial.printf("Display %d: %s\n", i + 1, p.present ? "found" : "not connected");
  }
  // Clear all at once, like the Pi server does before the address screen
  for (auto &p : panels)
    if (p.present) p.dev->startClear();
  for (auto &p : panels)
    if (p.present) p.dev->waitIdle();
}

// A refresh ended (ok) or timed out: tell the clients that wait for it.
static void refreshDone(int i, bool ok) {
  panels[i].refreshing = false;
  if (!ok) Serial.printf("Display %d not responding\n", i + 1);
  for (auto &c : conns) {
    if (!c.used || !(c.pending & (1u << i))) continue;
    c.pending &= ~(1u << i);
    if (!ok) c.failed = true;
    if (c.pending == 0) reply(c, c.failed ? "error: display not responding" : "Image displayed");
  }
}

static void pollDisplays() {
  static uint32_t last;
  if (millis() - last < 50) return;
  last = millis();
  for (int i = 0; i < NUM_DISPLAYS; i++) {
    Panel &p = panels[i];
    if (!p.refreshing) continue;
    if (!p.dev->isBusy()) refreshDone(i, true);
    else if (millis() - p.started > 30000) refreshDone(i, false);
  }
}

// Decodes the PNG in message[] and starts the refresh on the target
// display(s). Several displays refresh in parallel.
static void showImage(Connection &c, size_t len) {
  uint32_t t0 = millis();
  const char *err = fb::decodePng(message, len);
  if (err) return reply(c, err);
  Serial.printf("PNG %d x %d, %u bytes, decoded in %lu ms\n", fb::srcWidth, fb::srcHeight,
                (unsigned)len, millis() - t0);

  c.failed = false;
  for (int i = 0; i < NUM_DISPLAYS; i++) {
    if (c.target != 0 && c.target != i + 1) continue;
    Panel &p = panels[i];
    if (!p.present) continue;
    if (p.refreshing) refreshDone(i, p.dev->waitIdle());  // previous image first
    p.dev->startDisplay4Gray(fb::buffer);
    p.refreshing = true;
    p.showsImage = true;
    p.started = millis();
    c.pending |= 1u << i;
  }
  if (c.pending == 0) {
    char e[48];
    snprintf(e, sizeof(e), "error: display %d not connected", c.target);
    reply(c, c.target == 0 ? "error: no display connected" : e);
  }
}

// ------------------------------------------------------------ websocket
static int parseTarget(const char *path) {
  if (!strcmp(path, "/") || !path[0]) return 1;
  if (!strcmp(path, "/all")) return 0;
  int n = atoi(path + 1);
  return (path[0] == '/' && n >= 1 && n <= NUM_DISPLAYS) ? n : -1;
}

static String infoText() {
  String s = settings.hostname() + "\n\nUSB   ws://" + settings.usbIp().toString() + ":" + WS_PORT + "\n";
  if (wifiIp.isSet())
    s += "WLAN  ws://" + settings.hostname() + ".local:" + WS_PORT + "  ws://" + wifiIp.toString() + ":" +
         WS_PORT + "\n";
  s += "\nDisplays (path /1 ... /" + String(NUM_DISPLAYS) + ", /all):\n";
  for (int i = 0; i < NUM_DISPLAYS; i++)
    s += "  /" + String(i + 1) + "  " +
         (panels[i].present ? (panels[i].refreshing ? "refreshing" : "ready") : "not connected") + "\n";
  return s;
}

static void acceptClients() {
  WiFiClient incoming = server.accept();
  if (!incoming) return;
  char path[64];
  if (!ws::handshake(incoming, infoText().c_str(), path, sizeof(path))) {
    incoming.stop();
    return;
  }
  char *q = strchr(path, '?');
  if (q) *q = 0;

  // Free slot, otherwise replace the oldest connection
  Connection *slot = nullptr;
  for (auto &c : conns)
    if (!c.used) { slot = &c; break; }
  if (!slot) {
    slot = &conns[0];
    for (auto &c : conns)
      if (c.since < slot->since) slot = &c;
    slot->client.stop();
  }
  *slot = Connection{incoming, true, parseTarget(path), 0, false, millis()};
  Serial.printf("Client %s connected to %s\n", incoming.remoteIP().toString().c_str(), path);
  if (slot->target < 0) {
    reply(*slot, "error: unknown display, use /1 ... /4 or /all");
  } else if (slot->target > 0 && !panels[slot->target - 1].present) {
    char e[48];
    snprintf(e, sizeof(e), "error: display %d not connected", slot->target);
    reply(*slot, e);
  }
}

static void serveClients() {
  for (auto &c : conns) {
    if (!c.used) continue;
    if (c.client.available() > 0) {
      size_t len;
      switch (ws::readMessage(c.client, message, sizeof(message), len)) {
        case ws::BINARY_MESSAGE:
          if (c.target < 0) reply(c, "error: unknown display, use /1 ... /4 or /all");
          else showImage(c, len);
          break;
        case ws::TEXT_MESSAGE: reply(c, "error: send the PNG as binary data"); break;
        case ws::TOO_BIG: reply(c, "error: image too large"); break;
        case ws::NOTHING: break;
        case ws::CLOSED:
        case ws::FAILED:
          c.client.stop();
          c.used = false;
          break;
      }
    } else if (!c.client.connected()) {
      c.client.stop();
      c.used = false;
    }
  }
}

// ----------------------------------------------------------------- wifi
static void wifiStart() {
  WiFi.setHostname(settings.hostname().c_str());
  if (settings.wifiStatic()) {
    const Settings &s = settings.s;
    IPAddress ip(s.wifiIp), gw(s.wifiGw), mask(s.wifiMask);
    WiFi.config(ip, gw, gw, mask);
  }
  WiFi.beginNoBlock(settings.s.ssid, settings.s.pass[0] ? settings.s.pass : nullptr);
  wifiAttempt = millis();
}

// Notices new / lost Wi-Fi addresses and retries every 30 s.
static void wifiPoll() {
  if (!settings.wifiEnabled()) return;
  IPAddress ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP() : IPAddress();
  if (ip != wifiIp) {
    wifiIp = ip;
    if (ip.isSet()) Serial.printf("Wi-Fi connected: %s (%s.local)\n", ip.toString().c_str(), settings.hostname().c_str());
    else Serial.println("Wi-Fi disconnected");
    showStatusScreens();
  }
  if (!ip.isSet() && millis() - wifiAttempt > 30000) {
    Serial.printf("Wi-Fi: trying %s again\n", settings.s.ssid);
    WiFi.disconnect();
    wifiStart();
  }
}

// ------------------------------------------------------------ setup/loop
void setup() {
  Serial.begin(115200);
  settings.begin(DEFAULTS);

  // USB network: fixed address, no gateway (Wi-Fi keeps the default route)
  eth.config(settings.usbIp(), IPAddress(), IPAddress(255, 255, 255, 0));
  if (!eth.begin()) Serial.println("Could not start USB network");
  dhcp.begin(eth.getNetIf(), settings.usbIp(), settings.usbHostIp(), IPAddress(255, 255, 255, 0));

  if (settings.wifiEnabled()) wifiStart();
  MDNS.begin(settings.hostname().c_str());
  server.begin();

  initDisplays();

  // Give Wi-Fi a few seconds, so the status screen can show its address
  if (settings.wifiEnabled())
    for (uint32_t t0 = millis(); millis() - t0 < 10000 && WiFi.status() != WL_CONNECTED;) delay(100);
  if (settings.wifiEnabled() && WiFi.status() == WL_CONNECTED) wifiIp = WiFi.localIP();
  showStatusScreens();

  settings.print();
  Serial.println("Type 'help' for commands.");
}

// ---------------------------------------------------------- config mode
static void configButton() {
  bool pressed = configmode::buttonPressed();
  if (configmode::active) {
    if (!pressed && !configmode::ejected) return;
    if (configmode::apply(settings)) {
      delay(100);
      rp2040.reboot();
    }
    const char *const lines[] = {"config.txt not found.", "Press BOOTSEL to try again."};
    showMessage("Config mode", lines, 2);
    configmode::active = false;
    return;
  }
  if (!pressed) return;
  const char *err = configmode::start(settings);
  if (err) {
    Serial.println(err);
    const char *const lines[] = {err};
    showMessage("Config mode failed", lines, 1);
    return;
  }
  const char *const lines[] = {
    "The Pico is now a USB drive.",
    "1. Open config.txt and edit it",
    "2. Save it",
    "3. Eject the drive",
    "The Pico then restarts.",
    "(Or press BOOTSEL again when saved.)",
  };
  showMessage("Config mode", lines, 6);
}

void loop() {
  if (settings.pollConsole()) rp2040.reboot();
  configButton();
  wifiPoll();
  acceptClients();
  serveClients();
  pollDisplays();
}

// Settings stored in flash, so the same firmware can go onto every Pico.
// Two ways to change them:
//
// 1. config.txt: press BOOTSEL while the Pico runs. It appears as a USB
//    drive with config.txt (see config_mode.h).
// 2. Serial monitor (115200 baud, line ending "Newline"), e.g.:
//   id 3
//   wifi MyNetwork
//   pass secret123
//   reboot

#pragma once
#include <Arduino.h>
#include <EEPROM.h>

struct Settings {
  uint32_t magic;
  uint8_t id;          // 1..200: USB address 192.168.(6+id).1, name eink-<id>
  char ssid[33];       // empty: Wi-Fi off
  char pass[64];
  uint8_t wifiIp[4];   // 0.0.0.0: address from the router (DHCP)
  uint8_t wifiGw[4];
  uint8_t wifiMask[4];
};

static const uint32_t SETTINGS_MAGIC = 0xE1A40002;

class SettingsStore {
 public:
  Settings s;

  // Defaults come from the sketch; stored values override them.
  void begin(const Settings &defaults) {
    EEPROM.begin(256);
    EEPROM.get(0, s);
    if (s.magic != SETTINGS_MAGIC) {
      s = defaults;
      s.magic = SETTINGS_MAGIC;
    }
    s.ssid[sizeof(s.ssid) - 1] = 0;
    s.pass[sizeof(s.pass) - 1] = 0;
    if (s.id < 1 || s.id > 200) s.id = 1;
  }

  IPAddress usbIp() const { return IPAddress(192, 168, 6 + s.id, 1); }
  IPAddress usbHostIp() const { return IPAddress(192, 168, 6 + s.id, 2); }
  bool wifiEnabled() const { return s.ssid[0] != 0; }
  bool wifiStatic() const { return s.wifiIp[0] != 0; }
  String hostname() const { return String("eink-") + s.id; }

  // Call from loop(). Returns true when the user asked for a reboot.
  bool pollConsole() {
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\r') continue;
      if (c != '\n') {
        if (_len < sizeof(_line) - 1) _line[_len++] = c;
        continue;
      }
      _line[_len] = 0;
      _len = 0;
      if (command(_line)) return true;
    }
    return false;
  }

  void print() {
    Serial.printf("id    %u   (USB: ws://%s:8765, name %s)\n", s.id, usbIp().toString().c_str(),
                  hostname().c_str());
    Serial.printf("wifi  %s\n", wifiEnabled() ? s.ssid : "(off)");
    Serial.printf("pass  %s\n", s.pass[0] ? "********" : "(none)");
    if (wifiStatic())
      Serial.printf("ip    %u.%u.%u.%u  gateway %u.%u.%u.%u  mask %u.%u.%u.%u\n", s.wifiIp[0], s.wifiIp[1],
                    s.wifiIp[2], s.wifiIp[3], s.wifiGw[0], s.wifiGw[1], s.wifiGw[2], s.wifiGw[3],
                    s.wifiMask[0], s.wifiMask[1], s.wifiMask[2], s.wifiMask[3]);
    else
      Serial.println("ip    dhcp");
  }

  void store() {
    EEPROM.put(0, s);
    EEPROM.commit();
  }

  // ------------------------------------------------- config.txt format
  void writeConfigFile(Print &f) const {
    f.print(
        "# PicoEInk settings\r\n"
        "# Edit, save, then eject the drive. The Pico restarts with the new settings.\r\n"
        "\r\n"
        "# Pico number 1-200. USB address 192.168.(6+id).1, name eink-<id>.local\r\n"
        "# Every Pico needs its own number.\r\n");
    f.printf("id = %u\r\n\r\n", s.id);
    f.print("# Wi-Fi network and password. Leave wifi empty for no Wi-Fi.\r\n");
    f.printf("wifi = %s\r\n", s.ssid);
    f.printf("password = %s\r\n\r\n", s.pass);
    f.print("# Wi-Fi address: dhcp (from the router) or a fixed address, e.g. 192.168.1.51\r\n");
    if (wifiStatic()) {
      f.printf("ip = %u.%u.%u.%u\r\n", s.wifiIp[0], s.wifiIp[1], s.wifiIp[2], s.wifiIp[3]);
      f.printf("gateway = %u.%u.%u.%u\r\n", s.wifiGw[0], s.wifiGw[1], s.wifiGw[2], s.wifiGw[3]);
      f.printf("mask = %u.%u.%u.%u\r\n", s.wifiMask[0], s.wifiMask[1], s.wifiMask[2], s.wifiMask[3]);
    } else {
      f.print("ip = dhcp\r\n");
      f.print("# Only for a fixed address (default: x.x.x.1 and 255.255.255.0):\r\n");
      f.print("gateway =\r\nmask =\r\n");
    }
  }

  // Reads all lines of config.txt ("key = value", # comments). Keys that
  // are missing keep their value; wrong values are reported and ignored.
  void readConfigFile(Stream &f) {
    char ip[16] = "", gw[16] = "", mask[16] = "";
    bool haveIp = false;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0 || line[0] == '#') continue;
      int eq = line.indexOf('=');
      if (eq < 0) continue;
      String key = line.substring(0, eq), value = line.substring(eq + 1);
      key.trim();
      key.toLowerCase();
      value.trim();
      if (key == "id") {
        int v = value.toInt();
        if (v >= 1 && v <= 200) s.id = v;
        else Serial.printf("config.txt: id must be 1-200, not '%s'\n", value.c_str());
      } else if (key == "wifi") {
        strlcpy(s.ssid, value.c_str(), sizeof(s.ssid));
      } else if (key == "password") {
        strlcpy(s.pass, value.c_str(), sizeof(s.pass));
      } else if (key == "ip") {
        strlcpy(ip, value.c_str(), sizeof(ip));
        haveIp = true;
      } else if (key == "gateway") {
        strlcpy(gw, value.c_str(), sizeof(gw));
      } else if (key == "mask") {
        strlcpy(mask, value.c_str(), sizeof(mask));
      } else {
        Serial.printf("config.txt: unknown setting '%s'\n", key.c_str());
      }
    }
    if (haveIp) setWifiIp(ip, gw[0] ? gw : nullptr, mask[0] ? mask : nullptr);
  }

 private:
  char _line[160];
  size_t _len = 0;

  void save() {
    store();
    Serial.println("Saved. Type 'reboot' to apply.");
  }

  // ip: "dhcp" / "" or an address; gw and mask optional.
  bool setWifiIp(const char *ip, const char *gwStr, const char *maskStr) {
    if (!ip || !ip[0] || !strcmp(ip, "dhcp")) {
      memset(s.wifiIp, 0, 4);
      return true;
    }
    uint8_t a[4], gw[4], mask[4] = {255, 255, 255, 0};
    if (!parseIp(ip, a)) return Serial.printf("bad address '%s'\n", ip), false;
    memcpy(gw, a, 4);
    gw[3] = 1;  // default gateway: x.x.x.1
    if (gwStr && !parseIp(gwStr, gw)) return Serial.printf("bad gateway '%s'\n", gwStr), false;
    if (maskStr && !parseIp(maskStr, mask)) return Serial.printf("bad mask '%s'\n", maskStr), false;
    memcpy(s.wifiIp, a, 4);
    memcpy(s.wifiGw, gw, 4);
    memcpy(s.wifiMask, mask, 4);
    return true;
  }

  static void help() {
    Serial.println(
        "Commands:\n"
        "  show                     current settings\n"
        "  id <1-200>               display number (USB address, name eink-<id>)\n"
        "  wifi <network name>      join this Wi-Fi network\n"
        "  wifi off                 no Wi-Fi\n"
        "  pass <password>          Wi-Fi password (pass with nothing after it: open network)\n"
        "  ip dhcp                  Wi-Fi address from the router (default)\n"
        "  ip <addr> [gw] [mask]    fixed Wi-Fi address, e.g. ip 192.168.1.51\n"
        "  reboot                   restart with the new settings");
  }

  static bool parseIp(const char *str, uint8_t out[4]) {
    unsigned a, b, c, d;
    if (sscanf(str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 || a > 255 || b > 255 || c > 255 || d > 255)
      return false;
    out[0] = a; out[1] = b; out[2] = c; out[3] = d;
    return true;
  }

  // Returns true for "reboot".
  bool command(char *line) {
    while (*line == ' ') line++;
    char *arg = strchr(line, ' ');
    if (arg) {
      *arg++ = 0;
      while (*arg == ' ') arg++;
    } else {
      arg = line + strlen(line);
    }
    if (!*line) return false;

    if (!strcmp(line, "show")) {
      print();
    } else if (!strcmp(line, "id")) {
      int v = atoi(arg);
      if (v < 1 || v > 200) return Serial.println("id must be 1-200"), false;
      s.id = v;
      save();
    } else if (!strcmp(line, "wifi")) {
      if (!strcmp(arg, "off")) s.ssid[0] = 0;
      else strlcpy(s.ssid, arg, sizeof(s.ssid));
      save();
    } else if (!strcmp(line, "pass")) {
      strlcpy(s.pass, arg, sizeof(s.pass));
      save();
    } else if (!strcmp(line, "ip")) {
      char *a1 = strtok(arg, " "), *a2 = strtok(nullptr, " "), *a3 = strtok(nullptr, " ");
      if (setWifiIp(a1, a2, a3)) save();
    } else if (!strcmp(line, "reboot")) {
      return true;
    } else {
      help();
    }
    return false;
  }
};

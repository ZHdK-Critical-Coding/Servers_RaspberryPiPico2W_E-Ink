// Config mode: press BOOTSEL while the Pico runs. The Pico appears as a
// USB drive with config.txt (the current settings). Edit, save, eject the
// drive: the Pico stores the settings and restarts. Pressing BOOTSEL again
// also applies the file (use it if ejecting does not work).
//
// Needs a file system in flash: Arduino IDE Tools > Flash Size
// "4MB (Sketch: 3MB, FS: 1MB)". The prebuilt PicoEInk.uf2 has it.
//
// (Holding BOOTSEL while plugging in still starts the normal firmware
// loader. Config mode is a short press while the firmware is running.)

#pragma once
#include <Arduino.h>
#include <FatFS.h>
#include <FatFSUSB.h>
#include "settings.h"

namespace configmode {

static volatile bool ejected = false;
static bool active = false;

static void onUnplug(uint32_t) { ejected = true; }

// Writes config.txt and shows the drive. Returns an error text or nullptr.
static const char *start(const SettingsStore &settings) {
  if (!FatFS.begin()) return "No file system: set Flash Size with FS in the Arduino IDE";
  File f = FatFS.open("/config.txt", "w");
  if (!f) {
    FatFS.end();
    return "Could not write config.txt";
  }
  settings.writeConfigFile(f);
  f.close();
  FatFS.end();  // the computer owns the drive now

  ejected = false;
  FatFSUSB.onUnplug(onUnplug);
  FatFSUSB.begin();
  active = true;
  Serial.println("Config mode: edit config.txt on the USB drive, save, eject.");
  return nullptr;
}

// Reads config.txt back into the settings and stores them.
static bool apply(SettingsStore &settings) {
  FatFSUSB.end();
  if (!FatFS.begin()) return false;
  File f = FatFS.open("/config.txt", "r");
  bool found = f;
  if (found) {
    settings.readConfigFile(f);
    f.close();
  }
  FatFS.end();
  if (!found) return false;
  settings.store();
  Serial.println("Settings from config.txt stored, restarting.");
  return true;
}

// Short BOOTSEL press (with debouncing). Checked every 100 ms, because
// reading the button pauses flash access for a moment.
static bool buttonPressed() {
  static uint32_t last;
  static bool wasDown;
  if (millis() - last < 100) return false;
  last = millis();
  bool down = BOOTSEL;
  bool pressed = down && !wasDown;
  wasDown = down;
  return pressed;
}

}  // namespace configmode

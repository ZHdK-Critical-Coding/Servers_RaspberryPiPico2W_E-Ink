// Waveshare 4.2" e-paper (V1, 400 x 300) driver for the Pico.
//
// Ported from waveshare_epd/epd4in2.py as used by the Raspberry Pi
// screen-websocket server (init() + Clear() + display_4Gray()), so the
// panel gets the same commands and LUTs as on the Pi.
//
// Several displays can share one SPI bus: CLK, DIN, DC and RST are shared,
// every display has its own CS and BUSY pin. Refreshes are started without
// waiting, so several displays can refresh at the same time.
//
// Frame buffer format: 2 bits per pixel, 4 pixels per byte, leftmost pixel
// in the two most significant bits.
// Levels: 0 = black, 1 = dark grey, 2 = light grey, 3 = white.

#pragma once
#include <Arduino.h>
#include <SPI.h>

namespace epd {

const int WIDTH = 400;
const int HEIGHT = 300;
const size_t FB_SIZE = WIDTH * HEIGHT / 4;  // 30000 bytes

// Pins shared by all displays
struct Bus {
  uint8_t sck, mosi, dc, rst;
};

// clang-format off
static const uint8_t lut_vcom0[] = {
  0x00, 0x08, 0x08, 0x00, 0x00, 0x02, 0x00, 0x0F, 0x0F, 0x00, 0x00, 0x01,
  0x00, 0x08, 0x08, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t lut_ww[] = {
  0x50, 0x08, 0x08, 0x00, 0x00, 0x02, 0x90, 0x0F, 0x0F, 0x00, 0x00, 0x01,
  0xA0, 0x08, 0x08, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t lut_bw[] = {
  0x50, 0x08, 0x08, 0x00, 0x00, 0x02, 0x90, 0x0F, 0x0F, 0x00, 0x00, 0x01,
  0xA0, 0x08, 0x08, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t lut_wb[] = {
  0xA0, 0x08, 0x08, 0x00, 0x00, 0x02, 0x90, 0x0F, 0x0F, 0x00, 0x00, 0x01,
  0x50, 0x08, 0x08, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t lut_bb[] = {
  0x20, 0x08, 0x08, 0x00, 0x00, 0x02, 0x90, 0x0F, 0x0F, 0x00, 0x00, 0x01,
  0x10, 0x08, 0x08, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static const uint8_t gray_lut_vcom[] = {
  0x00, 0x0A, 0x00, 0x00, 0x00, 0x01, 0x60, 0x14, 0x14, 0x00, 0x00, 0x01,
  0x00, 0x14, 0x00, 0x00, 0x00, 0x01, 0x00, 0x13, 0x0A, 0x01, 0x00, 0x01,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t gray_lut_ww[] = {
  0x40, 0x0A, 0x00, 0x00, 0x00, 0x01, 0x90, 0x14, 0x14, 0x00, 0x00, 0x01,
  0x10, 0x14, 0x0A, 0x00, 0x00, 0x01, 0xA0, 0x13, 0x01, 0x00, 0x00, 0x01,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t gray_lut_bw[] = {
  0x40, 0x0A, 0x00, 0x00, 0x00, 0x01, 0x90, 0x14, 0x14, 0x00, 0x00, 0x01,
  0x00, 0x14, 0x0A, 0x00, 0x00, 0x01, 0x99, 0x0C, 0x01, 0x03, 0x04, 0x01,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t gray_lut_wb[] = {
  0x40, 0x0A, 0x00, 0x00, 0x00, 0x01, 0x90, 0x14, 0x14, 0x00, 0x00, 0x01,
  0x00, 0x14, 0x0A, 0x00, 0x00, 0x01, 0x99, 0x0B, 0x04, 0x04, 0x01, 0x01,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t gray_lut_bb[] = {
  0x80, 0x0A, 0x00, 0x00, 0x00, 0x01, 0x90, 0x14, 0x14, 0x00, 0x00, 0x01,
  0x20, 0x14, 0x0A, 0x00, 0x00, 0x01, 0x50, 0x13, 0x01, 0x00, 0x00, 0x01,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Partial refresh (EPD_4IN2_Partial_lut_*1 from the Waveshare C driver):
// one short phase. Old = new: not driven at all. Changed: driven to
// black or white. The rest of each table is zero.
static const uint8_t partial_lut_vcom[44] = {0x00, 0x19, 0x01, 0x00, 0x00, 0x01};
static const uint8_t partial_lut_ww[42] = {0x00, 0x19, 0x01, 0x00, 0x00, 0x01};
static const uint8_t partial_lut_bw[42] = {0x80, 0x19, 0x01, 0x00, 0x00, 0x01};
static const uint8_t partial_lut_wb[42] = {0x40, 0x19, 0x01, 0x00, 0x00, 0x01};
static const uint8_t partial_lut_bb[42] = {0x00, 0x19, 0x01, 0x00, 0x00, 0x01};
// clang-format on

class EPD4in2 {
 public:
  EPD4in2(SPIClassRP2040 &spi, const Bus &bus, uint8_t cs, uint8_t busy)
      : _spi(spi), _bus(bus), _cs(cs), _busy(busy) {}

  // Once for all displays: shared pins, SPI, and one reset pulse (RST is
  // shared, so the displays are never reset one by one).
  static void beginBus(SPIClassRP2040 &spi, const Bus &bus) {
    pinMode(bus.dc, OUTPUT);
    pinMode(bus.rst, OUTPUT);
    spi.setSCK(bus.sck);
    spi.setTX(bus.mosi);
    spi.begin();
    for (int i = 0; i < 3; i++) {  // reset sequence from epd4in2.py
      digitalWrite(bus.rst, HIGH);
      delay(10);
      digitalWrite(bus.rst, LOW);
      delay(10);
    }
    digitalWrite(bus.rst, HIGH);
    delay(10);
  }

  void beginPins() {
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);
    // Pull-down: a missing display reads as "busy" and is detected by the
    // timeout in init(). A connected display drives the pin itself.
    pinMode(_busy, INPUT_PULLDOWN);
  }

  // Same sequence as EPD.init() in epd4in2.py (without the reset, see
  // beginBus). Returns false if no display answers.
  bool init() {
    cmd(0x01);  // power setting
    data(0x03);
    data(0x00);
    data(0x2b);
    data(0x2b);
    cmd(0x06);  // booster soft start
    data(0x17);
    data(0x17);
    data(0x17);
    cmd(0x04);  // power on
    if (!waitIdle(1500)) return false;  // no display: BUSY stays low
    cmd(0x00);  // panel setting
    data(0xbf);
    cmd(0x30);  // PLL 100 Hz
    data(0x3c);
    cmd(0x61);  // resolution 400 x 300
    data(0x01);
    data(0x90);
    data(0x01);
    data(0x2c);
    cmd(0x82);  // VCOM DC
    data(0x12);
    cmd(0x50);  // VCOM and data interval, white border
    data(0x97);
    setLut();
    return true;
  }

  // Clear() from epd4in2.py, without waiting.
  void startClear() {
    cmd(0x10);
    for (size_t i = 0; i < WIDTH * HEIGHT / 8; i++) data(0xFF);
    cmd(0x13);
    for (size_t i = 0; i < WIDTH * HEIGHT / 8; i++) data(0xFF);
    cmd(0x12);
    delay(1);
  }

  // display_4Gray() from epd4in2.py, without waiting for the end of the
  // refresh. The frame buffer can be reused as soon as this returns.
  void startDisplay4Gray(const uint8_t *fb) {
    cmd(0x92);  // leave partial mode, VCOM settings as in init()
    cmd(0x82);
    data(0x12);
    cmd(0x50);
    data(0x97);
    setLut();
    cmd(0x10);  // old data: white and light grey = 1
    sendPlane(fb, 1);
    cmd(0x13);  // new data: white and dark grey = 1
    sendPlane(fb, 0);
    graySetLut();
    cmd(0x12);
    delay(200);
  }

  // Fast partial refresh, without waiting (EPD_4IN2_PartialDisplay from
  // the Waveshare C driver). prev is what the panel shows now, fb the new
  // image (both 2 bits per pixel). Only the rectangle around the changed
  // pixels is sent, and only changed pixels are driven: to black (levels
  // 0, 1) or white (2, 3). Everything else stays as it is, greys included.
  // No flashing, but ghosting builds up; a full refresh cleans it.
  // Returns false if nothing changed (then no refresh is started).
  bool startPartial(const uint8_t *prev, const uint8_t *fb) {
    const int ROW = WIDTH / 4;  // bytes per row
    int x0 = WIDTH, x1 = -1, y0 = -1, y1 = -1;
    for (int y = 0; y < HEIGHT; y++) {
      const uint8_t *a = prev + y * ROW, *b = fb + y * ROW;
      if (!memcmp(a, b, ROW)) continue;
      if (y0 < 0) y0 = y;
      y1 = y;
      for (int i = 0; i < ROW; i++)
        if (a[i] != b[i]) {
          if (i * 4 < x0) x0 = i * 4;
          if (i * 4 + 3 > x1) x1 = i * 4 + 3;
        }
    }
    if (y0 < 0) return false;
    x0 &= ~7;            // the window is in whole bytes (8 pixels)
    x1 = (x1 | 7) + 1;   // exclusive
    y1 += 1;

    cmd(0x82);  // VCOM DC
    data(0x08);
    cmd(0x50);  // VCOM and data interval: 1 = black
    data(0x47);
    partialSetLut();
    cmd(0x91);  // partial mode
    cmd(0x90);  // window
    data(x0 >> 8);
    data(x0 & 0xff);
    data((x1 - 1) >> 8);
    data((x1 - 1) & 0xff);
    data(y0 >> 8);
    data(y0 & 0xff);
    data((y1 - 1) >> 8);
    data((y1 - 1) & 0xff);
    data(0x28);
    cmd(0x10);  // old data
    sendWindow(prev, fb, x0, x1, y0, y1, true);
    cmd(0x13);  // new data
    sendWindow(prev, fb, x0, x1, y0, y1, false);
    cmd(0x12);
    delay(10);
    return true;
  }

  // BUSY is LOW while the panel works. 0x71 (get status) is sent first,
  // like ReadBusy() in the Python driver.
  bool isBusy() {
    cmd(0x71);
    return digitalRead(_busy) == LOW;
  }

  bool waitIdle(uint32_t timeoutMs = 30000) {
    uint32_t t0 = millis();
    while (isBusy()) {
      if (millis() - t0 > timeoutMs) return false;
      delay(20);
    }
    return true;
  }

 private:
  SPIClassRP2040 &_spi;
  Bus _bus;
  uint8_t _cs, _busy;

  void write(uint8_t b, bool isData) {
    _spi.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    digitalWrite(_bus.dc, isData ? HIGH : LOW);
    digitalWrite(_cs, LOW);
    _spi.transfer(b);
    digitalWrite(_cs, HIGH);
    _spi.endTransaction();
  }
  void cmd(uint8_t c) { write(c, false); }
  void data(uint8_t d) { write(d, true); }
  void dataArray(const uint8_t *a, size_t n) {
    for (size_t i = 0; i < n; i++) data(a[i]);
  }

  // Note: the Python driver writes lut_bb to 0x23 and lut_wb to 0x24;
  // kept as is.
  void setLut() {
    cmd(0x20);
    dataArray(lut_vcom0, 36);
    cmd(0x21);
    dataArray(lut_ww, 36);
    cmd(0x22);
    dataArray(lut_bw, 36);
    cmd(0x23);
    dataArray(lut_bb, 36);
    cmd(0x24);
    dataArray(lut_wb, 36);
  }

  void graySetLut() {
    cmd(0x20);
    dataArray(gray_lut_vcom, 42);
    cmd(0x21);
    dataArray(gray_lut_ww, 42);
    cmd(0x22);
    dataArray(gray_lut_bw, 42);
    cmd(0x23);
    dataArray(gray_lut_wb, 42);
    cmd(0x24);
    dataArray(gray_lut_bb, 42);
    cmd(0x25);
    dataArray(gray_lut_ww, 42);
  }

  void partialSetLut() {
    cmd(0x20);
    dataArray(partial_lut_vcom, 44);
    cmd(0x21);
    dataArray(partial_lut_ww, 42);
    cmd(0x22);
    dataArray(partial_lut_bw, 42);
    cmd(0x23);
    dataArray(partial_lut_wb, 42);
    cmd(0x24);
    dataArray(partial_lut_bb, 42);
  }

  // Window for startPartial, 1 bit per pixel, 1 = black. New data: the
  // new image. Old data: the same, except where the level changed, there
  // it is the opposite, so the panel drives exactly those pixels.
  void sendWindow(const uint8_t *prev, const uint8_t *fb, int x0, int x1, int y0, int y1, bool old) {
    for (int y = y0; y < y1; y++)
      for (int x = x0; x < x1; x += 8) {
        uint8_t out = 0;
        for (int k = 0; k < 8; k++) {
          size_t i = y * WIDTH + x + k;
          uint8_t shift = 6 - 2 * (i & 3);
          uint8_t n = (fb[i >> 2] >> shift) & 3;
          uint8_t black = n < 2;
          if (old && n != ((prev[i >> 2] >> shift) & 3)) black ^= 1;
          out = (out << 1) | black;
        }
        data(out);
      }
  }

  // One bit plane: 8 pixels per byte, bit = (level >> shift) & 1.
  void sendPlane(const uint8_t *fb, int shift) {
    for (size_t i = 0; i < WIDTH * HEIGHT / 8; i++) {
      uint8_t out = 0;
      for (int j = 0; j < 2; j++) {
        uint8_t b = fb[i * 2 + j];
        for (int k = 0; k < 4; k++) {
          uint8_t level = (b >> (6 - 2 * k)) & 0x03;
          out = (out << 1) | ((level >> shift) & 1);
        }
      }
      data(out);
    }
  }
};

}  // namespace epd

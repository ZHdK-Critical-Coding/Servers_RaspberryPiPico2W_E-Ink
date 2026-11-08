// Frame buffer (400 x 300, 2 bits per pixel), text drawing, and PNG
// decoding into it.

#pragma once
#include <Arduino.h>
#include "src/PNGdec/PNGdec.h"
#include "epd4in2.h"
#include "font.h"

namespace fb {

static uint8_t buffer[epd::FB_SIZE];

static void setPixel(int x, int y, uint8_t level) {
  if (x < 0 || y < 0 || x >= epd::WIDTH || y >= epd::HEIGHT) return;
  size_t i = y * epd::WIDTH + x;
  uint8_t shift = 6 - 2 * (i & 3);
  buffer[i >> 2] = (buffer[i >> 2] & ~(3 << shift)) | (level << shift);
}

static void clear() { memset(buffer, 0xFF, sizeof(buffer)); }  // all white

// Draws ASCII text with its top left corner at x, y. Returns the x after
// the last character.
static int text(const char *s, int x, int y, bool large = false, uint8_t level = 0) {
  const uint8_t h = large ? FONT_LARGE_HEIGHT : FONT_SMALL_HEIGHT;
  for (; *s; s++) {
    int c = (uint8_t)*s;
    if (c < 32 || c > 126) c = '?';
    c -= 32;
    for (int row = 0; row < h; row++) {
      uint32_t bits = large ? FONT_LARGE_ROWS[c][row] : FONT_SMALL_ROWS[c][row];
      for (int col = 0; bits; col++, bits <<= 1)
        if (bits & 0x80000000u) setPixel(x + col, y + row, level);
    }
    x += large ? FONT_LARGE_ADVANCE[c] : FONT_SMALL_ADVANCE[c];
  }
  return x;
}

// ------------------------------------------------------------ PNG -> fb
static PNG png;
static int srcWidth, srcHeight;
static uint8_t lineLevels[epd::WIDTH];

// Grey level like the Pi server: PIL convert('L'), then the quirk in
// epd4in2.getbuffer_4Gray (0xC0 -> 0x80, 0x80 -> 0x40), then top 2 bits.
// Transparent pixels are laid over white (what the browser shows).
static uint8_t toLevel(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  if (a != 255) {
    r = (r * a + 255 * (255 - a)) / 255;
    g = (g * a + 255 * (255 - a)) / 255;
    b = (b * a + 255 * (255 - a)) / 255;
  }
  uint8_t L = (r * 19595 + g * 38470 + b * 7471 + 0x8000) >> 16;
  if (L == 0xC0) L = 0x80;
  else if (L == 0x80) L = 0x40;
  return L >> 6;
}

// Value of pixel x in a row with less than 8 bits per pixel.
static uint8_t packedValue(const uint8_t *row, int x, int bits) {
  int perByte = 8 / bits;
  int shift = 8 - bits * (x % perByte + 1);
  return (row[x / perByte] >> shift) & ((1 << bits) - 1);
}

static uint8_t pixelLevel(PNGDRAW *d, int x) {
  const uint8_t *p = d->pPixels;
  switch (d->iPixelType) {
    case PNG_PIXEL_GRAYSCALE: {
      uint8_t v = d->iBpp == 8 ? p[x] : packedValue(p, x, d->iBpp) * 255 / ((1 << d->iBpp) - 1);
      return toLevel(v, v, v, 255);
    }
    case PNG_PIXEL_TRUECOLOR:
      return toLevel(p[x * 3], p[x * 3 + 1], p[x * 3 + 2], 255);
    case PNG_PIXEL_INDEXED: {
      uint8_t i = d->iBpp == 8 ? p[x] : packedValue(p, x, d->iBpp);
      const uint8_t *c = &d->pPalette[i * 3];
      return toLevel(c[0], c[1], c[2], d->iHasAlpha ? d->pPalette[768 + i] : 255);
    }
    case PNG_PIXEL_GRAY_ALPHA:
      return toLevel(p[x * 2], p[x * 2], p[x * 2], p[x * 2 + 1]);
    case PNG_PIXEL_TRUECOLOR_ALPHA:
      return toLevel(p[x * 4], p[x * 4 + 1], p[x * 4 + 2], p[x * 4 + 3]);
  }
  return 3;
}

// Called by PNGdec for every source row. Scales to 400 x 300 (nearest
// neighbour; for a 400 x 300 canvas this is a 1:1 copy).
static int pngDraw(PNGDRAW *d) {
  const int W = epd::WIDTH, H = epd::HEIGHT;
  int sy = d->y;
  int dy = (sy * H + srcHeight - 1) / srcHeight;  // first target row for sy
  if (dy >= H || dy * srcHeight / H != sy) return 1;  // row not used
  for (int dx = 0; dx < W; dx++) lineLevels[dx] = pixelLevel(d, dx * srcWidth / W);
  for (; dy < H && dy * srcHeight / H == sy; dy++)
    for (int dx = 0; dx < W; dx++) setPixel(dx, dy, lineLevels[dx]);
  return 1;
}

// Decodes a PNG into the frame buffer. Returns nullptr on success or an
// error text for the client.
static const char *decodePng(uint8_t *data, size_t len) {
  int rc = png.openRAM(data, len, pngDraw);
  if (rc == PNG_SUCCESS) {
    srcWidth = png.getWidth();
    srcHeight = png.getHeight();
    clear();
    rc = png.decode(nullptr, 0);
    png.close();
  }
  switch (rc) {
    case PNG_SUCCESS: return nullptr;
    case PNG_INVALID_FILE: return "error: not a PNG";
    case PNG_UNSUPPORTED_FEATURE: return "error: PNG not supported (interlaced or 16 bit)";
    case PNG_TOO_BIG: return "error: image too wide (max 800 px)";
    default: return "error: could not decode PNG";
  }
}

}  // namespace fb

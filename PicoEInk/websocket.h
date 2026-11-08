// Minimal WebSocket server side (RFC 6455) on top of WiFiClient.
// Enough for a browser sending binary messages: handshake, masked frames,
// fragmentation, ping/pong, close. No extensions, no TLS.

#pragma once
#include <Arduino.h>
#include <WiFiClient.h>

namespace ws {

// ---------------------------------------------------------------- SHA-1
static inline uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void sha1(const uint8_t *msg, size_t len, uint8_t out[20]) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  size_t total = ((len + 8) / 64 + 1) * 64;  // padded length
  uint64_t bits = (uint64_t)len * 8;
  uint32_t w[80];
  for (size_t off = 0; off < total; off += 64) {
    for (int i = 0; i < 16; i++) {
      uint32_t word = 0;
      for (int j = 0; j < 4; j++) {
        size_t idx = off + i * 4 + j;
        uint8_t b;
        if (idx < len) b = msg[idx];
        else if (idx == len) b = 0x80;
        else if (idx >= total - 8) b = (uint8_t)(bits >> (8 * (total - 1 - idx)));
        else b = 0;
        word = (word << 8) | b;
      }
      w[i] = word;
    }
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }
  for (int i = 0; i < 5; i++) {
    out[i * 4] = h[i] >> 24;
    out[i * 4 + 1] = h[i] >> 16;
    out[i * 4 + 2] = h[i] >> 8;
    out[i * 4 + 3] = h[i];
  }
}

// --------------------------------------------------------------- base64
static void base64(const uint8_t *in, size_t len, char *out) {
  static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < len; i += 3) {
    uint32_t v = in[i] << 16;
    if (i + 1 < len) v |= in[i + 1] << 8;
    if (i + 2 < len) v |= in[i + 2];
    out[o++] = tbl[(v >> 18) & 63];
    out[o++] = tbl[(v >> 12) & 63];
    out[o++] = (i + 1 < len) ? tbl[(v >> 6) & 63] : '=';
    out[o++] = (i + 2 < len) ? tbl[v & 63] : '=';
  }
  out[o] = 0;
}

// ------------------------------------------------------------- reading
// Reads exactly n bytes. Fails on disconnect or when no byte arrives for
// timeoutMs.
static bool readExact(WiFiClient &c, uint8_t *buf, size_t n, uint32_t timeoutMs = 5000) {
  size_t got = 0;
  uint32_t last = millis();
  while (got < n) {
    int avail = c.available();
    if (avail > 0) {
      int r = c.read(buf + got, min((size_t)avail, n - got));
      if (r > 0) {
        got += r;
        last = millis();
        continue;
      }
    }
    if (!c.connected() && c.available() <= 0) return false;
    if (millis() - last > timeoutMs) return false;
    delay(1);
  }
  return true;
}

// Reads n bytes and throws them away.
static bool skip(WiFiClient &c, size_t n) {
  uint8_t tmp[256];
  while (n > 0) {
    size_t chunk = min(n, sizeof(tmp));
    if (!readExact(c, tmp, chunk)) return false;
    n -= chunk;
  }
  return true;
}

// Reads one header line (without CRLF). Too long lines are truncated.
static int readLine(WiFiClient &c, char *buf, size_t size, uint32_t deadline) {
  size_t n = 0;
  while (true) {
    if (c.available() > 0) {
      char ch = c.read();
      if (ch == '\n') break;
      if (ch != '\r' && n < size - 1) buf[n++] = ch;
    } else {
      if (!c.connected() || (int32_t)(millis() - deadline) > 0) return -1;
      delay(1);
    }
  }
  buf[n] = 0;
  return n;
}

// ----------------------------------------------------------- handshake
// Reads the HTTP request. For a WebSocket upgrade it answers 101, copies
// the request path (e.g. "/2") into path and returns true. A plain browser
// request (http://ip:port) gets infoText as a small text page and false.
static bool handshake(WiFiClient &c, const char *infoText, char *path, size_t pathSize) {
  const char *keyHeader = "sec-websocket-key:";
  char line[256];
  char key[64] = "";
  uint32_t deadline = millis() + 3000;
  bool first = true;
  path[0] = 0;
  while (true) {
    int n = readLine(c, line, sizeof(line), deadline);
    if (n < 0) return false;
    if (n == 0) break;  // end of headers
    if (first) {
      first = false;
      if (strncmp(line, "GET ", 4) != 0) return false;
      const char *p = line + 4;
      size_t i = 0;
      while (*p && *p != ' ' && i < pathSize - 1) path[i++] = *p++;
      path[i] = 0;
      continue;
    }
    if (strncasecmp(line, keyHeader, strlen(keyHeader)) == 0) {
      const char *v = line + strlen(keyHeader);
      while (*v == ' ') v++;
      strncpy(key, v, sizeof(key) - 1);
      key[sizeof(key) - 1] = 0;
      for (int i = strlen(key) - 1; i >= 0 && key[i] == ' '; i--) key[i] = 0;
    }
  }

  if (key[0] == 0) {
    c.printf("HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nConnection: close\r\n\r\n%s", infoText);
    c.flush();
    return false;
  }

  char buf[128];
  snprintf(buf, sizeof(buf), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
  uint8_t digest[20];
  sha1((const uint8_t *)buf, strlen(buf), digest);
  char accept[32];
  base64(digest, 20, accept);
  c.printf("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
           "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",
           accept);
  c.flush();
  return true;
}

// -------------------------------------------------------------- frames
enum Opcode : uint8_t { CONT = 0x0, TEXT = 0x1, BINARY = 0x2, CLOSE = 0x8, PING = 0x9, PONG = 0xA };

struct FrameHeader {
  bool fin;
  uint8_t opcode;
  uint64_t length;
  bool masked;
  uint8_t mask[4];
};

static bool readHeader(WiFiClient &c, FrameHeader &h) {
  uint8_t b[8];
  if (!readExact(c, b, 2)) return false;
  h.fin = b[0] & 0x80;
  h.opcode = b[0] & 0x0F;
  h.masked = b[1] & 0x80;
  h.length = b[1] & 0x7F;
  if (h.length == 126) {
    if (!readExact(c, b, 2)) return false;
    h.length = (b[0] << 8) | b[1];
  } else if (h.length == 127) {
    if (!readExact(c, b, 8)) return false;
    h.length = 0;
    for (int i = 0; i < 8; i++) h.length = (h.length << 8) | b[i];
  }
  if (h.masked && !readExact(c, h.mask, 4)) return false;
  return true;
}

// Payload bytes are XORed with the mask, counted from the frame start.
static void unmask(uint8_t *data, size_t len, const FrameHeader &h, size_t offset = 0) {
  if (!h.masked) return;
  for (size_t i = 0; i < len; i++) data[i] ^= h.mask[(offset + i) & 3];
}

// Server frames are never masked.
static void sendFrame(WiFiClient &c, uint8_t opcode, const uint8_t *data, size_t len) {
  uint8_t hdr[4];
  size_t n = 0;
  hdr[n++] = 0x80 | opcode;
  if (len < 126) {
    hdr[n++] = len;
  } else {
    hdr[n++] = 126;
    hdr[n++] = len >> 8;
    hdr[n++] = len & 0xFF;
  }
  c.write(hdr, n);
  if (len) c.write(data, len);
  c.flush();
}

static void sendText(WiFiClient &c, const char *text) {
  sendFrame(c, TEXT, (const uint8_t *)text, strlen(text));
}

// ------------------------------------------------------------ messages
enum Result { NOTHING, BINARY_MESSAGE, TEXT_MESSAGE, TOO_BIG, CLOSED, FAILED };

// Call when data is waiting. Reads frames until one whole data message is
// in buf (answering pings on the way). Returns NOTHING if only control
// frames were waiting.
static Result readMessage(WiFiClient &c, uint8_t *buf, size_t max, size_t &len) {
  uint8_t opcode = 0;  // opcode of the message being assembled
  bool tooBig = false;
  len = 0;
  while (true) {
    FrameHeader h;
    if (!readHeader(c, h)) return FAILED;

    if (h.opcode >= 0x8) {  // control frame
      uint8_t p[125];
      if (h.length > sizeof(p) || !readExact(c, p, h.length)) return FAILED;
      unmask(p, h.length, h);
      if (h.opcode == PING) {
        sendFrame(c, PONG, p, h.length);
      } else if (h.opcode == CLOSE) {
        sendFrame(c, CLOSE, p, h.length >= 2 ? 2 : 0);
        return CLOSED;
      }
      if (opcode == 0) return NOTHING;
      continue;
    }

    if (opcode == 0) {
      if (h.opcode != TEXT && h.opcode != BINARY) return FAILED;
      opcode = h.opcode;
    } else if (h.opcode != CONT) {
      return FAILED;
    }

    if (tooBig || len + h.length > max) {
      tooBig = true;
      if (!skip(c, h.length)) return FAILED;
    } else {
      if (!readExact(c, buf + len, h.length)) return FAILED;
      unmask(buf + len, h.length, h);
      len += h.length;
    }
    if (h.fin) {
      if (tooBig) return TOO_BIG;
      return opcode == BINARY ? BINARY_MESSAGE : TEXT_MESSAGE;
    }
  }
}

}  // namespace ws

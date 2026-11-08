// Tiny DHCP server for the USB network: hands out exactly one address
// (the computer at the other end of the cable).
//
// It is bound to the USB interface only, so it never answers on the Wi-Fi
// network. It sends no router and no DNS option on purpose: the computer
// keeps using its normal connection for everything else.

#pragma once
#include <Arduino.h>
#include <lwip/udp.h>
#include <lwip/pbuf.h>
#include <lwip/netif.h>

class TinyDhcpServer {
 public:
  bool begin(struct netif *netif, IPAddress server, IPAddress client, IPAddress mask) {
    _netif = netif;
    _server = server;
    _client = client;
    _mask = mask;
    _pcb = udp_new();
    if (!_pcb) return false;
    udp_bind_netif(_pcb, netif);
    if (udp_bind(_pcb, IP_ANY_TYPE, 67) != ERR_OK) return false;
    udp_recv(_pcb, &TinyDhcpServer::onPacket, this);
    return true;
  }

 private:
  struct udp_pcb *_pcb = nullptr;
  struct netif *_netif = nullptr;
  IPAddress _server, _client, _mask;

  // Runs inside the network stack.
  static void onPacket(void *arg, struct udp_pcb *, struct pbuf *p, const ip_addr_t *, u16_t) {
    uint8_t in[548];
    int n = pbuf_copy_partial(p, in, sizeof(in), 0);
    pbuf_free(p);
    static_cast<TinyDhcpServer *>(arg)->handle(in, n);
  }

  void handle(const uint8_t *in, int n) {
    if (n < 240 || in[0] != 1 /* BOOTREQUEST */) return;
    if (in[236] != 99 || in[237] != 130 || in[238] != 83 || in[239] != 99) return;  // magic cookie

    uint8_t type = 0;
    bool hasRequested = false, hasServerId = false;
    uint8_t requested[4], serverId[4];
    for (int i = 240; i < n;) {
      uint8_t opt = in[i];
      if (opt == 255) break;
      if (opt == 0) { i++; continue; }
      if (i + 1 >= n) break;
      uint8_t len = in[i + 1];
      if (i + 2 + len > n) break;
      const uint8_t *v = &in[i + 2];
      if (opt == 53 && len >= 1) type = v[0];
      if (opt == 50 && len == 4) { memcpy(requested, v, 4); hasRequested = true; }
      if (opt == 54 && len == 4) { memcpy(serverId, v, 4); hasServerId = true; }
      i += 2 + len;
    }

    uint8_t reply;
    if (type == 1) {  // DISCOVER
      reply = 2;      // OFFER
    } else if (type == 3) {  // REQUEST
      if (hasServerId && !sameIp(serverId, _server)) return;  // for another server
      reply = (hasRequested && !sameIp(requested, _client)) ? 6 /* NAK */ : 5 /* ACK */;
    } else {
      return;  // RELEASE, DECLINE, INFORM: nothing to do
    }

    uint8_t out[300];
    memset(out, 0, sizeof(out));
    out[0] = 2;                     // BOOTREPLY
    out[1] = 1;                     // Ethernet
    out[2] = 6;                     // MAC length
    memcpy(&out[4], &in[4], 4);     // xid
    memcpy(&out[10], &in[10], 2);   // flags
    if (reply != 6) putIp(&out[16], _client);  // yiaddr
    memcpy(&out[24], &in[24], 4);   // giaddr
    memcpy(&out[28], &in[28], 16);  // chaddr
    out[236] = 99; out[237] = 130; out[238] = 83; out[239] = 99;

    int o = 240;
    out[o++] = 53; out[o++] = 1; out[o++] = reply;
    out[o++] = 54; out[o++] = 4; putIp(&out[o], _server); o += 4;
    if (reply != 6) {
      out[o++] = 51; out[o++] = 4;  // lease time: 1 day
      out[o++] = 0x00; out[o++] = 0x01; out[o++] = 0x51; out[o++] = 0x80;
      out[o++] = 1; out[o++] = 4; putIp(&out[o], _mask); o += 4;
    }
    out[o++] = 255;

    struct pbuf *q = pbuf_alloc(PBUF_TRANSPORT, sizeof(out), PBUF_RAM);
    if (!q) return;
    pbuf_take(q, out, sizeof(out));
    ip_addr_t broadcast;
    IP_ADDR4(&broadcast, 255, 255, 255, 255);
    udp_sendto_if(_pcb, q, &broadcast, 68, _netif);
    pbuf_free(q);
  }

  static void putIp(uint8_t *dst, const IPAddress &ip) {
    for (int i = 0; i < 4; i++) dst[i] = ip[i];
  }
  static bool sameIp(const uint8_t *a, const IPAddress &ip) {
    for (int i = 0; i < 4; i++)
      if (a[i] != ip[i]) return false;
    return true;
  }
};

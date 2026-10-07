#pragma once

#include <WiFi.h>
#include <ESPmDNS.h>
#include <vector>
#include <algorithm>

// The other clocks on the network, found over mDNS (service _clockwise._tcp). The search runs in
// the background a little at a time, so the web UI only ever reads the list built up so far. Clocks
// are remembered and only forgotten after missing a day of searches, so one
// missed search doesn't make a clock vanish from the list. The list is sorted by name.
struct ClockPeers
{
  struct Peer
  {
    String name, face, ip, host;
    uint16_t missed = 0;  // searches in a row that did not find it
  };

  static ClockPeers *getInstance()
  {
    static ClockPeers base;
    return &base;
  }

  std::vector<Peer> peers;
  mdns_search_once_t *search = nullptr;
  unsigned long nextSearchAt = 20000;  // first search 20 s after boot, then every minute
  static const unsigned long SEARCH_EVERY_MS = 60 * 1000;
  static const unsigned long SEARCH_TIMEOUT_MS = 3000;
  static const uint16_t FORGET_AFTER_MISSES = 24 * 60;  // about a day of searches

  static String lower(String s) { s.toLowerCase(); return s; }

  void loop()
  {
    if (!search)
    {
      if ((long)(millis() - nextSearchAt) < 0) return;
      search = mdns_query_async_new(NULL, "_clockwise", "_tcp", MDNS_TYPE_PTR, SEARCH_TIMEOUT_MS, 10, NULL);
      nextSearchAt = millis() + SEARCH_EVERY_MS;
      return;
    }

    mdns_result_t *results = nullptr;
    if (!mdns_query_async_get_results(search, 0, &results)) return;  // still searching

    std::vector<Peer> found;
    String self = WiFi.localIP().toString();
    for (mdns_result_t *r = results; r; r = r->next)
    {
      Peer peer;
      for (mdns_ip_addr_t *a = r->addr; a; a = a->next)
        if (a->addr.type == ESP_IPADDR_TYPE_V4)
        {
          peer.ip = IPAddress(a->addr.u_addr.ip4.addr).toString();
          break;
        }
      if (peer.ip.length() == 0 || peer.ip == self) continue;  // no address yet, or this clock itself

      peer.host = r->hostname ? r->hostname : "";
      peer.name = r->instance_name ? r->instance_name : peer.host;
      for (size_t i = 0; i < r->txt_count; i++)
      {
        if (strcmp(r->txt[i].key, "name") == 0) peer.name = r->txt[i].value;
        else if (strcmp(r->txt[i].key, "face") == 0) peer.face = r->txt[i].value;
      }
      found.push_back(peer);
    }
    mdns_query_results_free(results);
    mdns_query_async_delete(search);
    search = nullptr;

    // Merge: update what was found, count a miss for the rest, forget clocks gone for a long time
    for (Peer &old : peers) old.missed++;
    for (const Peer &p : found)
    {
      auto it = std::find_if(peers.begin(), peers.end(), [&](const Peer &o) { return o.ip == p.ip; });
      if (it == peers.end()) peers.push_back(p);
      else *it = p;
    }
    peers.erase(std::remove_if(peers.begin(), peers.end(), [](const Peer &o) { return o.missed >= FORGET_AFTER_MISSES; }), peers.end());
    std::sort(peers.begin(), peers.end(), [](const Peer &x, const Peer &y) {
      int c = lower(x.name.length() ? x.name : x.host).compareTo(lower(y.name.length() ? y.name : y.host));
      return c != 0 ? c < 0 : x.ip < y.ip;
    });
  }
};

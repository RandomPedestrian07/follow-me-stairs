#pragma once
// Shared state for the follow-me light (follow-me.yaml): the per-person
// windows, the WLED watcher and the DDP sender.
//
// A FreeRTOS task talks to the WLED controller over plain blocking lwIP
// sockets, so the main loop never waits on the network. ESPHome's
// http_request component is not used because it blocks the main loop for the
// whole request (and for the full timeout while WLED is unreachable).
//
// The WLED address comes from the stored calibration or the LED preview
// (set_wled_host()); it may be an IPv4 address or a host name. The task:
//   - resolves the address and reads /json/info for the LED count and
//     whether the strip has a white channel, when the address changes and
//     every INFO_MS after that (so DHCP changes and strip edits are picked up);
//   - polls /json/state every POLL_MS. wled_on is true while WLED reports
//     itself on. When pause_when_on() is set, the follow-me loop stops
//     streaming while WLED is on (someone is using the light), and on each
//     off-to-on change the task posts {"live":false} so WLED leaves realtime
//     mode at once. An unreachable WLED reads as off.
// wled_ip is 0 and wled_leds 0 until the controller has answered.
//
// Frames go to WLED over DDP (UDP port 4048): 3 bytes per LED on RGB strips,
// 4 on RGBW, split over as many packets as the strip needs. DDP addresses
// LEDs by their raw index on the controller, like WLED's other realtime
// protocols, independent of WLED's segments.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "esphome/components/json/json_util.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

namespace follow_me {

inline std::atomic<bool> wled_on{false};
inline std::atomic<uint32_t> wled_ip{0};     // resolved IPv4, network byte order; 0 = unknown
inline std::atomic<int> wled_leds{0};        // LED count WLED reports; 0 = unknown
inline std::atomic<bool> wled_white{false};  // strip has a white channel (RGBW)
inline std::atomic<bool> pause_when_on{true};  // mirrors the "Follow-Me Pause While WLED On" switch

static const uint32_t POLL_MS = 500;
static const uint32_t INFO_MS = 30000;
static const uint32_t RELEASE_DELAY_MS = 60;  // lets the loop stop streaming before the release
static const size_t HOST_MAX = 128;

inline char wled_host[HOST_MAX] = "";
inline uint32_t wled_host_gen = 0;  // bumped on every change
inline std::mutex wled_host_mutex;

inline void set_wled_host(const char *host) {
  std::lock_guard<std::mutex> lock(wled_host_mutex);
  if (strncmp(wled_host, host, HOST_MAX) == 0) return;
  strncpy(wled_host, host, HOST_MAX - 1);
  wled_host[HOST_MAX - 1] = '\0';
  wled_host_gen++;
}

// IPv4 address of host (dotted quad or name), network byte order; 0 on failure.
inline uint32_t resolve(const char *host) {
  struct in_addr a;
  if (inet_aton(host, &a)) return a.s_addr;
  struct addrinfo hints = {};
  hints.ai_family = AF_INET;
  struct addrinfo *res = nullptr;
  if (lwip_getaddrinfo(host, nullptr, &hints, &res) != 0 || res == nullptr) return 0;
  const uint32_t ip = reinterpret_cast<struct sockaddr_in *>(res->ai_addr)->sin_addr.s_addr;
  lwip_freeaddrinfo(res);
  return ip;
}

// One HTTP/1.0 exchange with WLED's web server (port 80, plain HTTP; WLED
// serves nothing else). Reads at most len - 1 bytes of the response into buf
// (NUL-terminated) and returns a pointer to the body, or nullptr on failure.
inline const char *http_exchange(uint32_t ip, const char *request, char *buf, size_t len) {
  const int s = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s < 0) return nullptr;
  struct timeval tv = {1, 0};
  lwip_setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  lwip_setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(80);
  addr.sin_addr.s_addr = ip;
  int got = -1;
  const int request_len = strlen(request);
  if (lwip_connect(s, (struct sockaddr *) &addr, sizeof(addr)) == 0 &&
      lwip_send(s, request, request_len, 0) == request_len) {
    got = 0;
    while (got < (int) len - 1) {
      const int n = lwip_recv(s, buf + got, len - 1 - got, 0);
      if (n <= 0) break;
      got += n;
    }
    buf[got] = '\0';
  }
  lwip_close(s);
  if (got <= 0) return nullptr;
  const char *body = strstr(buf, "\r\n\r\n");
  return body ? body + 4 : nullptr;
}

inline void watch_task(void *) {
  static char buf[4096];  // /json/info is about 1.5 kB with its headers
  char host[HOST_MAX] = "";
  char get[HOST_MAX + 64];
  uint32_t gen = 0, ip = 0, last_info = 0;
  bool info_ok = false;
  for (;;) {
    {
      std::lock_guard<std::mutex> lock(wled_host_mutex);
      if (gen != wled_host_gen) {
        gen = wled_host_gen;
        memcpy(host, wled_host, HOST_MAX);
        ip = 0;
        info_ok = false;
        wled_ip = 0;
        wled_leds = 0;
      }
    }
    if (!host[0]) {
      wled_on = false;
      vTaskDelay(pdMS_TO_TICKS(POLL_MS));
      continue;
    }
    const uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (!info_ok || now - last_info >= INFO_MS) {
      last_info = now;
      ip = resolve(host);
      info_ok = false;
      if (ip) {
        snprintf(get, sizeof(get), "GET /json/info HTTP/1.0\r\nHost: %s\r\n\r\n", host);
        if (const char *body = http_exchange(ip, get, buf, sizeof(buf))) {
          esphome::json::parse_json(reinterpret_cast<const uint8_t *>(body), strlen(body), [&](JsonObject o) -> bool {
            const int count = o["leds"]["count"] | 0;
            const int lc = o["leds"]["lc"] | -1;  // light capabilities: bit 0 RGB, bit 1 white
            if (count <= 0 || lc < 0) return false;
            wled_leds = count;
            wled_white = (lc & 2) != 0;
            info_ok = true;
            return true;
          });
        }
      }
      wled_ip = info_ok ? ip : 0;
    }
    bool on = false;
    if (info_ok) {
      snprintf(get, sizeof(get), "GET /json/state HTTP/1.0\r\nHost: %s\r\n\r\n", host);
      if (const char *body = http_exchange(ip, get, buf, sizeof(buf))) {
        esphome::json::parse_json(reinterpret_cast<const uint8_t *>(body), strlen(body), [&](JsonObject o) -> bool {
          on = o["on"] | false;
          return true;
        });
      }
    }
    const bool was_on = wled_on.exchange(on);
    if (on && !was_on && pause_when_on.load()) {
      vTaskDelay(pdMS_TO_TICKS(RELEASE_DELAY_MS));
      char release[HOST_MAX + 128];
      snprintf(release, sizeof(release),
               "POST /json/state HTTP/1.0\r\nHost: %s\r\nContent-Type: application/json\r\n"
               "Content-Length: 14\r\n\r\n{\"live\":false}",
               host);
      http_exchange(ip, release, buf, sizeof(buf));
    }
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
  }
}

// One lit window following one person. Positions are wall inches.
struct Track {
  bool alive = false;      // slot in use (lit, fading, or person lingering off the stairs)
  bool tracking = false;   // window has a filtered position
  bool on_stairs = false;  // the person has been on the stairs
  bool entering = false;   // entrance spring running
  bool found = false;      // matched to a radar target this frame
  int exit_dir = 0;        // left the stairs: 1 past the bottom, -1 past the top
  float target = 0.0f;     // filtered person position
  float est_v = 0.0f;      // estimated speed, inches/s
  float last_z = 0.0f;     // last radar position used
  float centre = 0.0f;     // window centre before speed limit and easing
  float limited = 0.0f;    // speed-limited window centre
  float pos = 0.0f;        // displayed window centre
  float level = 0.0f;      // fade envelope, 0 dark to 1 full
  float exit_far = 0.0f;   // farthest radar distance from the exit end since leaving
  float ent_v = 0.0f;      // entrance spring velocity, inches/s
  uint32_t last_meas = 0;
  uint32_t last_seen = 0;
  uint32_t ent_start = 0;
  // Matched radar target this frame.
  float det_p = 0.0f, det_raw = 0.0f, det_x = 0.0f, det_y = 0.0f;
};

static const int MAX_TRACKS = 3;  // the LD2450 tracks three targets
inline Track tracks[MAX_TRACKS];

// Centre of the brightest window, or NAN when all are dark.
inline float display_pos() {
  float best = 0.0f, pos = NAN;
  for (const Track &t : tracks) {
    if (t.level > best) {
      best = t.level;
      pos = t.pos;
    }
  }
  return pos;
}

// ---------- DDP ----------

static const uint16_t DDP_PORT = 4048;
static const int DDP_MAX_DATA = 1440;  // bytes per packet; keeps each datagram inside one Ethernet frame

// Sends one frame (bpl bytes per LED, 3 or 4) to WLED over DDP, in as many
// packets as needed; the last carries the push flag. Main loop only.
inline void send_ddp(uint32_t ip, const uint8_t *data, int len, int bpl) {
  static int sock = -1;
  static uint8_t seq = 0;
  if (sock < 0) {
    sock = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return;
  }
  struct sockaddr_in dest = {};
  dest.sin_family = AF_INET;
  dest.sin_port = htons(DDP_PORT);
  dest.sin_addr.s_addr = ip;
  seq = seq % 15 + 1;  // 1-15; 0 means "no sequence"
  const int chunk = DDP_MAX_DATA / bpl * bpl;
  static uint8_t pkt[10 + DDP_MAX_DATA];
  for (int off = 0; off < len; off += chunk) {
    const int n = std::min(chunk, len - off);
    const bool last = off + n >= len;
    pkt[0] = 0x40 | (last ? 0x01 : 0x00);  // version 1, push on the last packet
    pkt[1] = seq;
    pkt[2] = bpl == 4 ? 0x1B : 0x0B;       // RGBW or RGB, 8 bits per channel
    pkt[3] = 1;                            // destination: default output device
    pkt[4] = off >> 24;
    pkt[5] = off >> 16;
    pkt[6] = off >> 8;
    pkt[7] = off;
    pkt[8] = n >> 8;
    pkt[9] = n;
    memcpy(pkt + 10, data + off, n);
    if (lwip_sendto(sock, pkt, 10 + n, 0, (struct sockaddr *) &dest, sizeof(dest)) < 0) {
      lwip_close(sock);
      sock = -1;
      return;
    }
  }
}

// Starts the watcher once.
inline void start_watch() {
  static bool started = false;
  if (started) return;
  started = true;
  xTaskCreate(watch_task, "follow_me_wled", 6144, nullptr, 1, nullptr);
}

}  // namespace follow_me

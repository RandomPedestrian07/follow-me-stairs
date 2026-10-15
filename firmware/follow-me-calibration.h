#pragma once
// The stairs calibration made by the setup wizard (wizard/wizard.html), stored
// on the device, and the geometry derived from it.
//
// Storage: one flash preference holding the calibration and the wizard's own
// state (an opaque JSON string the wizard reads back to resume). It survives
// reboots and firmware updates.
//
// HTTP, on the device's web server:
//   GET  /followme              the setup wizard (follow-me-wizard.h, built by
//                               wizard/tools/embed.mjs), gzipped, fonts
//                               inlined; works with no internet access.
//
//   GET  /followme/calibration  {"set":false} or {"set":true, ...calibration}
//   POST /followme/calibration  body: calibration JSON, sent as text/plain so
//                               a page on another origin needs no preflight.
//                               200 {"ok":true} once validated; 400 with
//                               {"ok":false,"error":"..."} otherwise.
//
//   GET  /followme/targets     {"t": ms, "targets": [[x, y] | null, ...]}: the
//                               radar's three targets now, raw (mm, x as
//                               reported), unthrottled. The wizard polls it
//                               for its live view.
//   POST /followme/preview      body {"wled": host, "led": n} as text/plain.
//                               Streams LED n (1-based, raw strip index, as
//                               follow-me addresses it) lit and the rest
//                               dark, to that WLED for
//                               PREVIEW_MS after the last request. led 0 ends
//                               it. Works with or without a calibration; the
//                               setup wizard uses it to match LEDs to spots.
//
// Calibration JSON (lengths in mm, in the LD2450's own frame, x as reported):
//   cl       2 to 4 points [x, y]: the walking path from the lower floor to the
//            upper floor (2 = straight, 3 = L, 4 = U).
//   bottom, top            distance along cl of the front of the bottom step
//                          and the top edge of the top step.
//   landIn, landOut        distance along cl of the landing edges (L, U only).
//   steps    treads per flight, [n] or [lower, upper].
//   width    stair width.
//   wled     WLED controller address (IPv4 or host name). The LED count and
//            whether the strip has a white channel are read from WLED itself.
//   anchors  2 to 8 pairs [d, led]: LED number (1-based) whose centre sits at
//            distance d along cl. LEDs between anchors are interpolated, LEDs
//            beyond the outer anchors extrapolated.
//   wizard   wizard state, stored and returned verbatim.
//
// Threading: the HTTP handler runs on the web server task. It validates into
// a pending copy; the main loop applies it (apply_pending()), saves it, and
// rebuilds the derived geometry. Geometry is read only from the main loop.

#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "follow-me.h"
#include "follow-me-wizard.h"

#include "esphome/components/json/json_util.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

namespace follow_me {

static const char *const CAL_TAG = "follow_me.cal";
static const uint32_t CAL_VERSION = 1;
static const int CAL_MAX_POINTS = 4;
static const int CAL_MAX_ANCHORS = 8;
// Longest strip follow-me drives. LED positions are kept for every LED, so
// this sets the RAM used (4 bytes per LED) and the frame buffer (4 per LED).
static const int MAX_STREAM_LEDS = 1500;
static const int CAL_MAX_LED_NUMBER = MAX_STREAM_LEDS;
static const size_t CAL_WIZARD_MAX = 1536;
static const size_t CAL_BODY_MAX = 4096;

struct Calibration {
  uint32_t version;
  uint8_t points;
  float x[CAL_MAX_POINTS], y[CAL_MAX_POINTS];
  float bottom, top, land_in, land_out;
  uint8_t steps[2];
  float width;
  char wled[HOST_MAX];
  uint8_t anchors;
  float anchor_d[CAL_MAX_ANCHORS], anchor_led[CAL_MAX_ANCHORS];
  char wizard[CAL_WIZARD_MAX];
};

// Geometry used by the follow-me loop. Positions are inches along the stairs
// from the top edge of the top step, increasing down the stairs ("wall
// inches"; effects are laid out in the same units).
static const int MAX_GAPS = 8;
static const float GAP_FACTOR = 3.0f;

struct Geometry {
  bool ready = false;
  int points = 0;
  float x[CAL_MAX_POINTS] = {}, y[CAL_MAX_POINTS] = {};  // path, inches
  float cum[CAL_MAX_POINTS] = {};                        // inches along the path at each point
  float top_along = 0;                                   // inches along the path of the top edge
  float stairs_bottom = 0;                               // wall inches of the front of the bottom step
  float inches_per_step = 0;
  float half_width = 0;
  float edge_wall[MAX_STREAM_LEDS + 1] = {};  // wall inches of LED edge k (edge 0 = outer edge of LED 1)
  // Drawing coordinate: wall inches with the strip's gaps squeezed out. A gap
  // is a span between two pinned LEDs with far fewer LEDs than its length
  // needs (a jumper across a landing, a corner, a doorway): each LED there
  // covers more than GAP_FACTOR times the strip's usual spacing. In drawing
  // coordinates each such LED covers the usual spacing, so a window crossing
  // the gap lights both sides of it.
  int gaps = 0;
  float gap_lo[MAX_GAPS] = {}, gap_hi[MAX_GAPS] = {}, gap_cut[MAX_GAPS] = {};  // wall span and inches removed
  float edge_draw[MAX_STREAM_LEDS + 1] = {};  // drawing coordinate of LED edge k
};

inline Calibration active_cal{};
inline Calibration pending_cal{};
inline bool has_active = false;
inline std::atomic<bool> has_pending{false};
inline std::mutex cal_mutex;
inline Geometry geo;
inline ESPPreferenceObject cal_pref;

inline float path_length(const Calibration &c) {
  float total = 0;
  for (int i = 1; i < c.points; i++) total += hypotf(c.x[i] - c.x[i - 1], c.y[i] - c.y[i - 1]);
  return total;
}

// Empty string if the calibration can drive the light, otherwise why not.
inline std::string validate(const Calibration &c) {
  if (c.points < 2 || c.points > CAL_MAX_POINTS) return "cl needs 2 to 4 points";
  for (int i = 1; i < c.points; i++)
    if (hypotf(c.x[i] - c.x[i - 1], c.y[i] - c.y[i - 1]) < 100) return "cl has points closer than 100 mm";
  const float total = path_length(c);
  if (!(c.bottom >= 0 && c.top > c.bottom + 300 && c.top <= total + 1)) return "bottom and top must lie on cl, top past bottom";
  const int flights = c.points == 2 ? 1 : 2;
  if (flights == 2 && !(c.land_in > c.bottom && c.land_out >= c.land_in && c.top > c.land_out))
    return "landIn and landOut must lie between bottom and top";
  for (int f = 0; f < flights; f++)
    if (c.steps[f] < 1 || c.steps[f] > 40) return "steps must be 1 to 40 per flight";
  if (!(c.width >= 300 && c.width <= 2500)) return "width must be 300 to 2500 mm";
  if (!c.wled[0]) return "wled address is required";
  if (c.anchors < 2 || c.anchors > CAL_MAX_ANCHORS) return "anchors needs 2 to 8 pairs";
  for (int i = 0; i < c.anchors; i++) {
    if (!(c.anchor_led[i] >= 1 && c.anchor_led[i] <= CAL_MAX_LED_NUMBER)) return "anchor LED is past the longest strip follow-me drives";
    for (int j = 0; j < i; j++)
      if (fabsf(c.anchor_led[i] - c.anchor_led[j]) < 0.5f) return "two anchors share an LED";
  }
  return "";
}

// Drawing coordinate of a wall position: the gaps' removed inches before it
// are subtracted; inside a gap the removal is spread evenly.
inline float draw_pos(float wall) {
  float out = wall;
  for (int i = 0; i < geo.gaps; i++) {
    const float lo = geo.gap_lo[i], hi = geo.gap_hi[i], cut = geo.gap_cut[i];
    if (wall >= hi) out -= cut;
    else if (wall > lo) out -= cut * (wall - lo) / (hi - lo);
  }
  return out;
}

// Rebuilds geo from a validated calibration.
inline void derive(const Calibration &c) {
  Geometry &g = geo;
  g.points = c.points;
  for (int i = 0; i < c.points; i++) {
    g.x[i] = c.x[i] / 25.4f;
    g.y[i] = c.y[i] / 25.4f;
    g.cum[i] = i == 0 ? 0 : g.cum[i - 1] + hypotf(g.x[i] - g.x[i - 1], g.y[i] - g.y[i - 1]);
  }
  g.top_along = c.top / 25.4f;
  g.stairs_bottom = (c.top - c.bottom) / 25.4f;
  const float treads = c.points == 2 ? c.top - c.bottom : (c.land_in - c.bottom) + (c.top - c.land_out);
  const int steps = c.steps[0] + (c.points == 2 ? 0 : c.steps[1]);
  g.inches_per_step = treads / steps / 25.4f;
  g.half_width = c.width / 2 / 25.4f;

  // Anchors as (LED edge, wall inches), sorted by edge. LED n's centre is
  // edge n - 0.5.
  float ae[CAL_MAX_ANCHORS], aw[CAL_MAX_ANCHORS];
  const int n = c.anchors;
  for (int i = 0; i < n; i++) {
    ae[i] = c.anchor_led[i] - 0.5f;
    aw[i] = (c.top - c.anchor_d[i]) / 25.4f;
  }
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && ae[j] < ae[j - 1]; j--) {
      std::swap(ae[j], ae[j - 1]);
      std::swap(aw[j], aw[j - 1]);
    }
  // Interpolated between anchors, extrapolated past the outer ones.
  for (int k = 0; k <= MAX_STREAM_LEDS; k++) {
    int j = 0;
    while (j < n - 2 && k > ae[j + 1]) j++;
    g.edge_wall[k] = aw[j] + (k - ae[j]) * (aw[j + 1] - aw[j]) / (ae[j + 1] - ae[j]);
  }
  // Gaps: per-LED spacing within the pinned range, against its median.
  {
    int lo = MAX_STREAM_LEDS, hi = 0;
    for (int i = 0; i < n; i++) {
      lo = std::min(lo, (int) floorf(ae[i]));
      hi = std::max(hi, (int) ceilf(ae[i]));
    }
    lo = std::max(lo, 0);
    hi = std::min(hi, MAX_STREAM_LEDS);
    static float pitch[MAX_STREAM_LEDS];
    int m = 0;
    for (int k = lo; k < hi; k++) pitch[m++] = fabsf(g.edge_wall[k + 1] - g.edge_wall[k]);
    g.gaps = 0;
    if (m > 0) {
      static float sorted[MAX_STREAM_LEDS];
      memcpy(sorted, pitch, m * sizeof(float));
      std::nth_element(sorted, sorted + m / 2, sorted + m);
      const float usual = sorted[m / 2];
      for (int k = lo; k < hi && usual > 0.0f; k++) {
        const float a = fminf(g.edge_wall[k], g.edge_wall[k + 1]), b = fmaxf(g.edge_wall[k], g.edge_wall[k + 1]);
        if (b - a <= GAP_FACTOR * usual) continue;
        if (g.gaps > 0 && fabsf(g.gap_hi[g.gaps - 1] - a) < 0.01f) {  // continues the previous gap
          g.gap_hi[g.gaps - 1] = b;
          g.gap_cut[g.gaps - 1] += b - a - usual;
        } else if (g.gaps > 0 && fabsf(g.gap_lo[g.gaps - 1] - b) < 0.01f) {
          g.gap_lo[g.gaps - 1] = a;
          g.gap_cut[g.gaps - 1] += b - a - usual;
        } else if (g.gaps < MAX_GAPS) {
          g.gap_lo[g.gaps] = a;
          g.gap_hi[g.gaps] = b;
          g.gap_cut[g.gaps] = b - a - usual;
          g.gaps++;
        }
      }
    }
  }
  g.ready = true;
  for (int k = 0; k <= MAX_STREAM_LEDS; k++) g.edge_draw[k] = draw_pos(g.edge_wall[k]);
  for (int i = 0; i < g.gaps; i++)
    ESP_LOGI(CAL_TAG, "Strip gap at wall %.1f-%.1f in, drawn %.1f in shorter", g.gap_lo[i], g.gap_hi[i], g.gap_cut[i]);
  set_wled_host(c.wled);
  ESP_LOGI(CAL_TAG, "Calibration applied: %d-point path, %.1f in per step, stairs %.1f in long, WLED %s", c.points,
           g.inches_per_step, g.stairs_bottom, c.wled);
}

// Nearest point on the path to (x, y) in radar inches. Returns the squared
// distance; *wall is the wall-inch position there, *at_end whether the point
// lies beyond either end of the path.
inline float project(float x, float y, float *wall, bool *at_end) {
  const Geometry &g = geo;
  float best = 1e9f;
  for (int k = 0; k < g.points - 1; k++) {
    const float sx = g.x[k + 1] - g.x[k], sy = g.y[k + 1] - g.y[k];
    float f = ((x - g.x[k]) * sx + (y - g.y[k]) * sy) / (sx * sx + sy * sy);
    bool end = false;
    if (f < 0.0f) {
      f = 0.0f;
      end = k == 0;
    }
    if (f > 1.0f) {
      f = 1.0f;
      end = k == g.points - 2;
    }
    const float ex = x - (g.x[k] + f * sx), ey = y - (g.y[k] + f * sy);
    const float d2 = ex * ex + ey * ey;
    if (d2 < best) {
      best = d2;
      *wall = g.top_along - (g.cum[k] + f * (g.cum[k + 1] - g.cum[k]));
      *at_end = end;
    }
  }
  return best;
}

// Main loop: load the stored calibration once, then pick up new ones.
inline void apply_pending() {
  if (!has_pending.load()) return;
  {
    std::lock_guard<std::mutex> lock(cal_mutex);
    active_cal = pending_cal;
    has_active = true;
    has_pending = false;
  }
  if (cal_pref.save(&active_cal) && global_preferences->sync())
    ESP_LOGI(CAL_TAG, "Calibration saved");
  else
    ESP_LOGW(CAL_TAG, "Calibration could not be saved to flash");
  derive(active_cal);
}

inline void load_calibration() {
  cal_pref = global_preferences->make_preference<Calibration>(fnv1_hash("follow_me_calibration"));
  static Calibration c;
  if (!cal_pref.load(&c) || c.version != CAL_VERSION) {
    ESP_LOGI(CAL_TAG, "No calibration stored; run the setup wizard");
    return;
  }
  c.wizard[CAL_WIZARD_MAX - 1] = '\0';
  c.wled[HOST_MAX - 1] = '\0';
  const std::string err = validate(c);
  if (!err.empty()) {
    ESP_LOGW(CAL_TAG, "Stored calibration rejected: %s", err.c_str());
    return;
  }
  {
    std::lock_guard<std::mutex> lock(cal_mutex);
    active_cal = c;
    has_active = true;
  }
  derive(c);
}

// ---------- JSON ----------

inline std::string to_json(const Calibration &c) {
  std::string out = "{\"set\":true,\"cl\":[";
  char buf[64];
  for (int i = 0; i < c.points; i++) {
    snprintf(buf, sizeof(buf), "%s[%.1f,%.1f]", i ? "," : "", c.x[i], c.y[i]);
    out += buf;
  }
  snprintf(buf, sizeof(buf), "],\"bottom\":%.1f,\"top\":%.1f", c.bottom, c.top);
  out += buf;
  if (c.points > 2) {
    snprintf(buf, sizeof(buf), ",\"landIn\":%.1f,\"landOut\":%.1f", c.land_in, c.land_out);
    out += buf;
  }
  if (c.points > 2)
    snprintf(buf, sizeof(buf), ",\"steps\":[%u,%u]", c.steps[0], c.steps[1]);
  else
    snprintf(buf, sizeof(buf), ",\"steps\":[%u]", c.steps[0]);
  out += buf;
  snprintf(buf, sizeof(buf), ",\"width\":%.1f,\"wled\":\"", c.width);
  out += buf;
  for (const char *p = c.wled; *p; p++)
    if (*p != '"' && *p != '\\') out += *p;  // validated to be a plain host; kept JSON-safe regardless
  out += "\",\"anchors\":[";
  for (int i = 0; i < c.anchors; i++) {
    snprintf(buf, sizeof(buf), "%s[%.1f,%.2f]", i ? "," : "", c.anchor_d[i], c.anchor_led[i]);
    out += buf;
  }
  out += "],\"wizard\":";
  out += c.wizard[0] ? c.wizard : "null";
  out += "}";
  return out;
}

// Parses a POST body into c. Empty string on success, otherwise the error.
inline std::string from_json(const std::string &body, Calibration &c) {
  c = Calibration{};
  c.version = CAL_VERSION;
  std::string err = "body is not a JSON object";
  esphome::json::parse_json(body, [&](JsonObject o) -> bool {
    JsonArray cl = o["cl"].as<JsonArray>(), steps = o["steps"].as<JsonArray>(), an = o["anchors"].as<JsonArray>();
    if (cl.isNull() || steps.isNull() || an.isNull()) {
      err = "cl, steps and anchors are required";
      return false;
    }
    if (cl.size() < 2 || cl.size() > CAL_MAX_POINTS) {
      err = "cl needs 2 to 4 points";
      return false;
    }
    c.points = cl.size();
    for (int i = 0; i < c.points; i++) {
      c.x[i] = cl[i][0] | NAN;
      c.y[i] = cl[i][1] | NAN;
    }
    c.bottom = o["bottom"] | NAN;
    c.top = o["top"] | NAN;
    c.land_in = o["landIn"] | NAN;
    c.land_out = o["landOut"] | NAN;
    if (steps.size() != (c.points == 2 ? 1u : 2u)) {
      err = "steps needs one count per flight";
      return false;
    }
    for (size_t i = 0; i < steps.size(); i++) c.steps[i] = steps[i] | 0;
    c.width = o["width"] | NAN;
    const char *wled = o["wled"] | "";
    if (strlen(wled) >= HOST_MAX) {
      err = "wled address is too long";
      return false;
    }
    for (const char *p = wled; *p; p++)
      if (!(isalnum((unsigned char) *p) || *p == '.' || *p == '-' || *p == '_')) {
        err = "wled must be an IPv4 address or host name";
        return false;
      }
    strcpy(c.wled, wled);
    if (an.size() < 2 || an.size() > CAL_MAX_ANCHORS) {
      err = "anchors needs 2 to 8 pairs";
      return false;
    }
    c.anchors = an.size();
    for (int i = 0; i < c.anchors; i++) {
      c.anchor_d[i] = an[i][0] | NAN;
      c.anchor_led[i] = an[i][1] | NAN;
    }
    JsonVariant wiz = o["wizard"];
    if (!wiz.isNull()) {
      std::string s;
      serializeJson(wiz, s);
      if (s.size() >= CAL_WIZARD_MAX) {
        err = "wizard state is too large";
        return false;
      }
      memcpy(c.wizard, s.c_str(), s.size() + 1);
    }
    for (int i = 0; i < c.points; i++)
      if (std::isnan(c.x[i]) || std::isnan(c.y[i])) {
        err = "cl points must be [x, y] numbers";
        return false;
      }
    err = validate(c);
    return err.empty();
  });
  return err;
}

// The radar's target X/Y sensors, found by the follow-me loop.
inline esphome::sensor::Sensor *radar_x[MAX_TRACKS] = {}, *radar_y[MAX_TRACKS] = {};

inline std::string targets_json() {
  std::string out = "{\"t\":" + std::to_string(millis()) + ",\"targets\":[";
  char buf[48];
  for (int t = 0; t < MAX_TRACKS; t++) {
    if (t) out += ",";
    const float x = radar_x[t] ? radar_x[t]->get_raw_state() : NAN, y = radar_y[t] ? radar_y[t]->get_raw_state() : NAN;
    if (std::isnan(x) || std::isnan(y) || (x == 0 && y == 0)) {
      out += "null";
    } else {
      snprintf(buf, sizeof(buf), "[%.0f,%.0f]", x, y);
      out += buf;
    }
  }
  return out + "]}";
}

// ---------- LED preview ----------

static const uint32_t PREVIEW_MS = 10000;
inline char preview_host[HOST_MAX] = "";
inline int preview_led = 0;          // guarded by cal_mutex
inline uint32_t preview_until = 0;   // millis(); guarded by cal_mutex

// Main loop: the LED to preview now (0 = none). Points the WLED watcher at
// the preview's WLED while previewing and back at the calibration's after.
inline int preview_now(uint32_t now) {
  static bool was = false;
  int led = 0;
  char host[HOST_MAX];
  {
    std::lock_guard<std::mutex> lock(cal_mutex);
    if (preview_led > 0 && (int32_t) (preview_until - now) > 0) {
      led = preview_led;
      memcpy(host, preview_host, HOST_MAX);
    } else {
      preview_led = 0;
    }
  }
  if (led > 0) {
    set_wled_host(host);
    was = true;
  } else if (was) {
    was = false;
    set_wled_host(has_active ? active_cal.wled : "");
  }
  return led;
}

// Fills data (bpl bytes per LED) with the preview of LED led (1-based) for a
// strip of leds LEDs: that one LED bright amber, the rest dark. Returns the
// data length.
inline int preview_frame(uint8_t *data, int leds, int bpl, int led) {
  const int len = leds * bpl;
  memset(data, 0, len);
  static const uint8_t LIT[3] = {255, 110, 0};
  if (led >= 1 && led <= leds)
    for (int ch = 0; ch < 3; ch++) data[(led - 1) * bpl + ch] = LIT[ch];
  return len;
}

inline std::string parse_preview(const std::string &body) {
  std::string err = "body is not a JSON object";
  esphome::json::parse_json(body, [&](JsonObject o) -> bool {
    const char *wled = o["wled"] | "";
    const int led = o["led"] | -1;
    if (led < 0 || led > CAL_MAX_LED_NUMBER) {
      err = "led is past the longest strip follow-me drives";
      return false;
    }
    if (led > 0) {
      if (!wled[0] || strlen(wled) >= HOST_MAX) {
        err = "wled address is required";
        return false;
      }
      for (const char *p = wled; *p; p++)
        if (!(isalnum((unsigned char) *p) || *p == '.' || *p == '-' || *p == '_')) {
          err = "wled must be an IPv4 address or host name";
          return false;
        }
    }
    std::lock_guard<std::mutex> lock(cal_mutex);
    preview_led = led;
    if (led > 0) {
      strcpy(preview_host, wled);
      preview_until = millis() + PREVIEW_MS;
    }
    err = "";
    return true;
  });
  return err;
}

// ---------- HTTP ----------

class CalibrationHandler : public AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest *request) const override {
    char buf[AsyncWebServerRequest::URL_BUF_SIZE];
    const StringRef url = request->url_to(buf);
    const auto m = request->method();
    return ((url == "/followme" || url == "/followme/") && m == HTTP_GET) ||
           (url == "/followme/calibration" && (m == HTTP_GET || m == HTTP_POST)) ||
           (url == "/followme/preview" && m == HTTP_POST) || (url == "/followme/targets" && m == HTTP_GET);
  }

  void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) override {
    if (index == 0) {
      this->body_.clear();
      this->too_big_ = total > CAL_BODY_MAX;
      if (!this->too_big_) this->body_.reserve(total);
    }
    if (!this->too_big_) this->body_.append(reinterpret_cast<const char *>(data), len);
  }

  void handleRequest(AsyncWebServerRequest *request) override {
    char ubuf[AsyncWebServerRequest::URL_BUF_SIZE];
    const StringRef url = request->url_to(ubuf);
    if (url == "/followme" || url == "/followme/") {
      AsyncWebServerResponse *response =
          request->beginResponse(200, "text/html; charset=utf-8", WIZARD_HTML_GZ, WIZARD_HTML_GZ_LEN);
      response->addHeader("Content-Encoding", "gzip");
      response->addHeader("Cache-Control", "no-cache");
      request->send(response);
      return;
    }
    if (url == "/followme/targets") {
      const std::string out = targets_json();
      request->send(200, "application/json", out.c_str());
      return;
    }
    if (request->method() == HTTP_GET) {
      std::string out;
      {
        std::lock_guard<std::mutex> lock(cal_mutex);
        if (has_pending.load())
          out = to_json(pending_cal);
        else if (has_active)
          out = to_json(active_cal);
        else
          out = "{\"set\":false}";
      }
      request->send(200, "application/json", out.c_str());
      return;
    }
    if (url == "/followme/preview") {
      const std::string err = this->too_big_ ? std::string("body is too large") : parse_preview(this->body_);
      this->body_.clear();
      if (!err.empty()) {
        const std::string out = "{\"ok\":false,\"error\":\"" + err + "\"}";
        request->send(400, "application/json", out.c_str());
      } else {
        request->send(200, "application/json", "{\"ok\":true}");
      }
      return;
    }
    // Heap, not stack: the web server task has little stack to spare.
    auto c = std::make_unique<Calibration>();
    std::string err;
    if (this->too_big_)
      err = "body is too large";
    else
      err = from_json(this->body_, *c);
    this->body_.clear();
    this->body_.shrink_to_fit();
    if (!err.empty()) {
      ESP_LOGW(CAL_TAG, "Calibration refused: %s", err.c_str());
      const std::string out = "{\"ok\":false,\"error\":\"" + err + "\"}";
      request->send(400, "application/json", out.c_str());
      return;
    }
    {
      std::lock_guard<std::mutex> lock(cal_mutex);
      pending_cal = *c;
      has_pending = true;
    }
    request->send(200, "application/json", "{\"ok\":true}");
  }

  bool isRequestHandlerTrivial() const override { return false; }

 protected:
  std::string body_;
  bool too_big_ = false;
};

inline bool web_ready = false;      // the device config has a web server; the wizard needs it
inline bool radar_missing = false;  // set by the follow-me loop when the radar target sensors are not found

// Called once at boot, after the web server is set up.
inline void setup_calibration() {
  start_watch();
  load_calibration();
  if (web_server_base::global_web_server_base == nullptr) {
    ESP_LOGE(CAL_TAG, "No web server in this device config; the setup wizard cannot reach the device");
    return;
  }
  web_server_base::global_web_server_base->add_handler(new CalibrationHandler());  // NOLINT
  web_ready = true;
}

}  // namespace follow_me

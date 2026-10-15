#pragma once
// Effects for the follow-me glow (follow-me.yaml).
//
// Each effect paints one LED's colour inside a lit window; the renderer then
// scales it by the Follow-Me Glow brightness and the window's coverage and
// fade, exactly as it scales the plain colour. Patterns are fixed to the
// stairs (wall inches), not to the window, so a person walks through them.
// Wall inches increase down the stairs; "falling" patterns move toward larger
// wall values, "rising" ones toward smaller.
//
// Colours are written as perceptual (sRGB-like) values 0-1 per R, G, B and
// the strip's white channel; paint() squares them (gamma 2) so
// saturated mixes such as orange stay orange on the LEDs.
//
// Nothing here depends on ESPHome, so the file also compiles on a desktop for
// previews. EFFECT_NAMES must match the effect names on the Follow-Me Glow
// light in follow-me.yaml.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace follow_me {
namespace fx {

enum Effect {
  NONE = 0,
  RAINBOW,
  NEW_YEARS,
  VALENTINES,
  ST_PATRICKS,
  EASTER,
  FOURTH_OF_JULY,
  AUTUMN,
  HALLOWEEN,
  HAUNTED,
  HANUKKAH,
  CHRISTMAS,
  CANDY_CANE,
  SNOWFALL,
  FIREPLACE,
  AURORA,
  OCEAN,
  EFFECT_COUNT,
};

static const char *const EFFECT_NAMES[EFFECT_COUNT] = {
    "None",           "Rainbow",   "New Year's", "Valentine's", "St. Patrick's", "Easter",
    "Fourth of July", "Autumn",    "Halloween",  "Haunted",     "Hanukkah",      "Christmas",
    "Candy Cane",     "Snowfall",  "Fireplace",  "Aurora",      "Ocean",
};

// Length of the stairs in wall inches, for patterns that span them
// (Snowfall). Set by the follow-me loop every frame.
inline float extent = 240.0f;
inline void set_extent(float wall_inches) { extent = fmaxf(wall_inches, 24.0f); }

inline Effect from_name(const char *name, size_t len) {
  for (int i = 1; i < EFFECT_COUNT; i++) {
    if (strlen(EFFECT_NAMES[i]) == len && strncmp(EFFECT_NAMES[i], name, len) == 0) return (Effect) i;
  }
  return NONE;
}

struct Rgbw {
  float r, g, b, w;
};

inline Rgbw scale(const Rgbw &c, float k) { return {c.r * k, c.g * k, c.b * k, c.w * k}; }
inline Rgbw mix(const Rgbw &a, const Rgbw &b, float f) {
  return {a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f, a.w + (b.w - a.w) * f};
}
inline Rgbw lighten(const Rgbw &a, const Rgbw &b) {  // per-channel max
  return {fmaxf(a.r, b.r), fmaxf(a.g, b.g), fmaxf(a.b, b.b), fmaxf(a.w, b.w)};
}

inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float smoothstep(float x) {
  x = clamp01(x);
  return x * x * (3.0f - 2.0f * x);
}
inline float fract(float x) { return x - floorf(x); }

inline uint32_t hash(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}
// Deterministic random 0-1 for a (seed, n) pair.
inline float rnd(uint32_t seed, uint32_t n) { return (hash(n * 0x9e3779b1u ^ hash(seed + 0x632be5abu)) >> 8) / 16777216.0f; }

// Smooth 1D value noise, 0-1, one feature per unit of x.
inline float noise(uint32_t seed, float x) {
  const float i = floorf(x);
  const int32_t n = (int32_t) i;
  return rnd(seed, (uint32_t) n) + (rnd(seed, (uint32_t) (n + 1)) - rnd(seed, (uint32_t) n)) * smoothstep(x - i);
}

// Smooth 2D value noise, 0-1, one feature per unit of x and of y.
inline float noise2(uint32_t seed, float x, float y) {
  const float j = floorf(y);
  const int32_t m = (int32_t) j;
  const float a = noise(seed ^ hash((uint32_t) m), x), b = noise(seed ^ hash((uint32_t) (m + 1)), x);
  return a + (b - a) * smoothstep(y - j);
}

// Soft-edged stripes: 1 for the first `duty` of each `period`, 0 for the
// rest, with linear edges `soft` units wide (anti-aliased between LEDs).
inline float stripe(float s, float period, float duty, float soft) {
  const float p = fract(s / period) * period;
  const float on = duty * period;
  const float h = soft / 2.0f;
  const float rise = clamp01((p + h) / soft);  // edge at p = 0, wrapped below
  const float fall = clamp01((on + h - p) / soft);
  const float wrap = clamp01((p - (period - h)) / soft);  // edge at p = period
  return fmaxf(fminf(rise, fall), wrap);
}

// Blocks of `size` units, each a random entry of the palette, blended over the
// last `blend` fraction of each block.
inline Rgbw blocks(float s, float size, uint32_t seed, const Rgbw *pal, int n, float blend) {
  const float u = s / size;
  const float i = floorf(u);
  const int32_t k = (int32_t) i;
  const Rgbw a = pal[hash(seed ^ hash((uint32_t) k)) % n];
  const Rgbw b = pal[hash(seed ^ hash((uint32_t) (k + 1))) % n];
  return mix(a, b, smoothstep((u - i - (1.0f - blend)) / blend));
}

// Twinkle envelope 0-1 for one LED: in each `period` seconds the LED flashes
// with probability `chance` (fast rise, slower fall). *slot_out is the period
// index, for picking the twinkle's colour.
inline float twinkle(int led, float t, float period, float chance, uint32_t seed, uint32_t *slot_out = nullptr) {
  const float u = t / period + rnd(seed, (uint32_t) led);
  const uint32_t slot = (uint32_t) floorf(u);
  if (slot_out) *slot_out = slot;
  if (rnd(seed ^ 0x5bd1e995u, slot * 1031u + (uint32_t) led) > chance) return 0.0f;
  const float f = u - floorf(u);
  const float attack = 0.12f;
  if (f < attack) return f / attack;
  const float d = 1.0f - (f - attack) / (1.0f - attack);
  return d * d;
}

inline Rgbw hsv(float h, float s, float v) {
  h = fract(h) * 6.0f;
  const int i = (int) h;
  const float f = h - i;
  const float p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
  switch (i) {
    case 0: return {v, t, p, 0};
    case 1: return {q, v, p, 0};
    case 2: return {p, v, t, 0};
    case 3: return {p, q, v, 0};
    case 4: return {t, p, v, 0};
    default: return {v, p, q, 0};
  }
}

// Firework burst: every `period` seconds a ring expands from a random point
// inside the window and fades. Returns 0-1; *slot_out picks its colour.
inline float burst(float wall, float t, float period, uint32_t seed, float focus, float span, uint32_t *slot_out) {
  const float u = t / period;
  const uint32_t slot = (uint32_t) floorf(u);
  *slot_out = slot;
  const float f = u - floorf(u);
  const float life = 0.75f;  // fraction of the period the burst is visible
  if (f > life) return 0.0f;
  const float age = f / life;
  const float centre = focus + (rnd(seed, slot) - 0.5f) * span * 0.8f;
  const float radius = 2.0f + 12.0f * sqrtf(age);
  const float d = fabsf(wall - centre);
  if (d > radius) return 0.0f;
  const float ring = 0.35f + 0.65f * clamp01(1.0f - (radius - d) / 3.0f);  // brighter rim
  const float fade = (1.0f - age) * (1.0f - age);
  return ring * fade;
}

// Colour of one LED for an effect. led is the LED index, wall the LED's
// position in wall inches, t seconds (wraps daily), focus the centre of the
// window lighting this LED and span its length in inches.
inline Rgbw paint(Effect e, int led, float wall, float t, float focus, float span) {
  Rgbw c = {0, 0, 0, 0};
  switch (e) {
    case NONE:
    case EFFECT_COUNT:
      break;

    case RAINBOW:
      // One full rainbow per 30 in of stairs, drifting slowly downward.
      c = hsv(wall / 30.0f - t / 12.0f, 1.0f, 1.0f);
      break;

    case NEW_YEARS: {
      // Champagne gold with fast silver and gold confetti.
      static const Rgbw GOLD = {1.0f, 0.62f, 0.12f, 0.35f};
      static const Rgbw CONFETTI[3] = {{0.55f, 0.6f, 0.9f, 1.0f}, {1.0f, 0.75f, 0.2f, 0.6f}, {0, 0, 0, 1.0f}};
      c = scale(GOLD, 0.45f + 0.15f * noise(11, wall / 8.0f + t * 0.6f));
      uint32_t slot;
      const float tw = twinkle(led, t, 0.7f, 0.35f, 12, &slot);
      if (tw > 0.0f) c = mix(c, CONFETTI[hash(slot * 7u + led) % 3], tw);
      break;
    }

    case VALENTINES: {
      // Pink and red drifting together, pulsing with a heartbeat.
      static const Rgbw PINK = {1.0f, 0.3f, 0.55f, 0.1f};
      static const Rgbw RED = {1.0f, 0.02f, 0.12f, 0};
      c = mix(PINK, RED, smoothstep(noise(21, wall / 14.0f - t * 0.25f) * 1.6f - 0.3f));
      const float p = fract(t / 1.4f);
      const float lub = expf(-p * 14.0f), dub = p > 0.22f ? 0.7f * expf(-(p - 0.22f) * 14.0f) : 0.0f;
      c = scale(c, 0.5f + 0.5f * fminf(1.0f, lub + dub));
      break;
    }

    case ST_PATRICKS: {
      // Drifting greens with gold glints.
      static const Rgbw GREENS[3] = {{0, 1.0f, 0.2f, 0}, {0.3f, 1.0f, 0, 0}, {0, 0.85f, 0.45f, 0}};
      static const Rgbw GOLD = {1.0f, 0.65f, 0, 0.4f};
      c = blocks(wall - t * 2.0f, 7.0f, 31, GREENS, 3, 0.4f);
      c = mix(c, GOLD, twinkle(led, t, 1.6f, 0.18f, 32));
      break;
    }

    case EASTER: {
      // Slow pastel blocks.
      static const Rgbw PASTELS[5] = {{1.0f, 0.4f, 0.6f, 0.15f}, {0.35f, 1.0f, 0.55f, 0.1f}, {0.7f, 0.45f, 1.0f, 0.1f},
                                      {1.0f, 0.8f, 0.3f, 0.15f}, {0.4f, 0.7f, 1.0f, 0.1f}};
      c = blocks(wall - t * 1.5f, 9.0f, 41, PASTELS, 5, 0.5f);
      break;
    }

    case FOURTH_OF_JULY: {
      // Night-sky navy with red, white and blue fireworks around the person.
      static const Rgbw SKY = {0, 0.02f, 0.3f, 0};
      static const Rgbw SPARK[3] = {{1.0f, 0.02f, 0.05f, 0}, {0.2f, 0.2f, 0.35f, 1.0f}, {0.15f, 0.35f, 1.0f, 0}};
      c = SKY;
      uint32_t slot;
      float b = burst(wall, t, 1.7f, 51, focus, span, &slot);
      if (b > 0.0f) c = lighten(c, scale(SPARK[slot % 3], b));
      b = burst(wall, t + 0.6f, 2.3f, 52, focus, span, &slot);
      if (b > 0.0f) c = lighten(c, scale(SPARK[(slot + 1) % 3], b));
      const float tw = twinkle(led, t, 0.9f, 0.08f, 53);
      if (tw > 0.0f) c = lighten(c, scale(SPARK[1], tw));
      break;
    }

    case AUTUMN: {
      // Rust, amber, gold and russet leaves drifting down the stairs.
      static const Rgbw LEAVES[4] = {{0.85f, 0.2f, 0, 0}, {1.0f, 0.5f, 0, 0}, {1.0f, 0.75f, 0.08f, 0.1f},
                                     {0.6f, 0.08f, 0.02f, 0}};
      c = blocks(wall - t * 3.0f, 5.0f, 61, LEAVES, 4, 0.35f);
      c = scale(c, 0.8f + 0.2f * noise(62, wall / 3.0f - t * 1.5f));
      break;
    }

    case HALLOWEEN: {
      // Jack-o'-lantern orange with candle flicker; purple patches drift
      // through and the odd LED flashes slime green.
      static const Rgbw ORANGE = {1.0f, 0.38f, 0, 0};
      static const Rgbw PURPLE = {0.6f, 0, 1.0f, 0};
      static const Rgbw SLIME = {0.35f, 1.0f, 0, 0};
      const float m = smoothstep((noise(71, wall / 16.0f - t * 0.2f) - 0.5f) * 4.0f);
      c = mix(ORANGE, PURPLE, m);
      c = scale(c, 0.55f + 0.45f * noise2(72, t * 6.0f, wall / 4.0f));  // flickers in clumps of a few LEDs
      c = mix(c, SLIME, twinkle(led, t, 2.5f, 0.05f, 73));
      break;
    }

    case HAUNTED: {
      // Cold ghost light that breathes, with a slow dark patch wandering
      // through it, and every few seconds the whole thing stutters like a
      // dying bulb.
      static const Rgbw GHOST = {0.12f, 0.45f, 0.75f, 0.55f};
      float v = 0.75f + 0.25f * sinf(t * 1.3f);
      v *= 0.35f + 0.65f * smoothstep((fabsf(wall - (focus + 0.6f * span * sinf(t * 0.4f))) - 2.0f) / 5.0f);
      const float cycle = 6.0f;
      const uint32_t slot = (uint32_t) floorf(t / cycle);
      const float local = t - slot * cycle - rnd(81, slot) * 4.5f;
      if (rnd(82, slot) < 0.65f && local >= 0.0f && local < 1.1f) {
        const uint32_t q = (uint32_t) (local * 18.0f);
        if (rnd(83, slot * 64u + q) < 0.5f) v *= 0.06f;
      }
      c = scale(GHOST, v);
      break;
    }

    case HANUKKAH: {
      // Blue and white blocks with a soft shimmer and white sparkles.
      static const Rgbw BLUE = {0, 0.2f, 1.0f, 0};
      static const Rgbw WHITE = {0.25f, 0.3f, 0.55f, 0.9f};
      c = mix(BLUE, WHITE, stripe(wall - t * 1.2f, 14.0f, 0.4f, 3.0f));
      c = scale(c, 0.75f + 0.25f * noise(91, wall / 4.0f + t * 1.2f));
      c = mix(c, WHITE, twinkle(led, t, 1.8f, 0.15f, 92));
      break;
    }

    case CHRISTMAS: {
      // Red and green bands marching down the stairs with warm-white twinkles.
      static const Rgbw RED = {1.0f, 0.02f, 0.02f, 0};
      static const Rgbw GREEN = {0, 1.0f, 0.12f, 0};
      static const Rgbw TWINKLE = {0.25f, 0.18f, 0.05f, 1.0f};
      c = mix(GREEN, RED, stripe(wall - t * 2.0f, 12.0f, 0.5f, 1.5f));
      c = mix(c, TWINKLE, twinkle(led, t, 1.5f, 0.2f, 101));
      break;
    }

    case CANDY_CANE: {
      // Red and white stripes scrolling down.
      static const Rgbw RED = {1.0f, 0, 0.04f, 0};
      static const Rgbw WHITE = {0.2f, 0.2f, 0.2f, 1.0f};
      c = mix(WHITE, RED, stripe(wall - t * 3.0f, 8.0f, 0.5f, 1.5f));
      break;
    }

    case SNOWFALL: {
      // Icy blue with snowflakes falling down the stairs.
      static const Rgbw ICE = {0, 0.12f, 0.45f, 0.05f};
      static const Rgbw FLAKE = {0.3f, 0.4f, 0.6f, 1.0f};
      // Flakes fall from above the top to below the bottom and reappear at
      // the top; their count keeps one flake per 8.6 in of fall on any stairs.
      static const float MARGIN = 20.0f;
      static const int MAX_FLAKES = 160;
      const float loop = extent + 2.0f * MARGIN;
      const int flakes = std::min(MAX_FLAKES, (int) (loop / 8.6f) + 1);
      // Flake positions, computed once per frame.
      static float flake_t = -1.0f, flake_pos[MAX_FLAKES], flake_v[MAX_FLAKES];
      if (t != flake_t) {
        flake_t = t;
        for (int k = 0; k < flakes; k++) {
          const float speed = 4.0f + 5.0f * rnd(111, k);
          flake_pos[k] = fmodf(rnd(112, k) * loop + speed * t, loop) - MARGIN;
          flake_v[k] = 0.55f + 0.45f * rnd(113, k);
        }
      }
      c = ICE;
      for (int k = 0; k < flakes; k++) {
        const float d = fabsf(wall - flake_pos[k]);
        if (d < 1.6f) c = lighten(c, scale(FLAKE, flake_v[k] * (1.0f - d / 1.6f)));
      }
      break;
    }

    case FIREPLACE: {
      // Flames rising up the stairs: deep red embers to orange to yellow tips.
      static const Rgbw EMBER = {0.45f, 0, 0, 0};
      static const Rgbw FLAME = {1.0f, 0.32f, 0, 0};
      static const Rgbw TIP = {1.0f, 0.72f, 0.1f, 0.35f};
      float heat = 0.6f * noise(121, (wall + t * 10.0f) / 4.0f) + 0.4f * noise(122, (wall + t * 18.0f) / 1.5f);
      heat = clamp01((heat - 0.2f) * 1.7f);
      c = heat < 0.5f ? mix(EMBER, FLAME, heat * 2.0f) : mix(FLAME, TIP, (heat - 0.5f) * 2.0f);
      break;
    }

    case AURORA: {
      // Green, teal and violet curtains drifting slowly with a shimmer.
      static const Rgbw GREEN = {0, 1.0f, 0.35f, 0};
      static const Rgbw TEAL = {0, 0.7f, 0.85f, 0};
      static const Rgbw VIOLET = {0.55f, 0.05f, 1.0f, 0};
      const float h = noise(131, wall / 30.0f - t * 0.08f);
      c = h < 0.5f ? mix(GREEN, TEAL, h * 2.0f) : mix(TEAL, VIOLET, (h - 0.5f) * 2.0f);
      c = scale(c, 0.45f + 0.55f * noise(132, wall / 9.0f + t * 0.35f));
      break;
    }

    case OCEAN: {
      // Deep blue swells rolling down the stairs with the odd white cap.
      static const Rgbw DEEP = {0, 0.06f, 0.6f, 0};
      static const Rgbw SHALLOW = {0, 0.6f, 0.9f, 0};
      static const Rgbw FOAM = {0.3f, 0.5f, 0.7f, 0.8f};
      const float swell = 0.5f + 0.5f * sinf(6.2831853f * (wall / 40.0f - t / 7.0f));
      const float n = noise(141, wall / 10.0f - t * 0.4f);
      const float h = clamp01(0.65f * swell + 0.35f * n);
      c = mix(DEEP, SHALLOW, h);
      c = mix(c, FOAM, smoothstep((h - 0.82f) * 6.0f));
      break;
    }
  }
  // Gamma 2 per channel.
  return {clamp01(c.r) * clamp01(c.r), clamp01(c.g) * clamp01(c.g), clamp01(c.b) * clamp01(c.b),
          clamp01(c.w) * clamp01(c.w)};
}

}  // namespace fx
}  // namespace follow_me

// Block generator: renders the starter library (drums, bass, piano, guitar,
// electronic) as mono float buffers locked to the project tempo. Imported
// audio blocks will later land in the same block_t table.

#include "groove.h"

#define SR  ((float)GR_SAMPLE_RATE)
#define TAU 6.28318530718f

const char *const kCategoryName[CAT_COUNT] = {
  "Drums", "Bass", "Piano", "Guitar", "Electronic",
  "Kicks", "Snares", "Hats", "Claps", "Cymbals",
  "Perc", "Fills", "Scratch", "Organ", "Vocals", "FX",
};

uint32_t category_color(category_t cat) {
  static const uint32_t col[CAT_COUNT] = {
    WEB(0x1aa6f2), WEB(0xa047f2), WEB(0xffb21e), WEB(0xff4f6c), WEB(0x1fd2b2),
    WEB(0xff7a2a), WEB(0xff4fa0), WEB(0xf2d02c), WEB(0xff7f5e), WEB(0x5cc6f8),
    WEB(0xffa12c), WEB(0xe64fd8), WEB(0x7c8cff), WEB(0x36c95c), WEB(0xff63a6), WEB(0x6c5cff),
  };
  return col[cat >= 0 && cat < CAT_COUNT ? cat : 0];
}

int bar_frames_for_bpm(int bpm) {
  bpm = bpm < GR_BPM_MIN ? GR_BPM_MIN : bpm > GR_BPM_MAX ? GR_BPM_MAX : bpm;
  return (int)((double)GR_SAMPLE_RATE * 60.0 * GR_BEATS_BAR / bpm + 0.5) & ~3; // multiple of 4: beats stay integral
}

typedef struct { float *buf; int n; double bar; uint32_t rng; } ctx_t;
typedef enum { I_PIANO, I_BASS, I_SUB, I_PLUCK, I_MUTE, I_ACID, I_PAD, I_ARP } inst_t;

static float rnd(ctx_t *c) { c->rng = c->rng * 1664525u + 1013904223u; return (float)(c->rng >> 8) * (1.0f / 8388608.0f) - 1.0f; }
static void  mix(ctx_t *c, int i, float v) { if (i >= 0 && i < c->n) c->buf[i] += v; }
static float midi_hz(int m) { return 440.0f * powf(2.0f, (float)(m - 69) / 12.0f); }
static float fmin1(float v) { return v < 1.0f ? v : 1.0f; }

// ── Drums ────────────────────────────────────────────────────────────────
static void kick(ctx_t *c, int s, float a) {
  double ph = 0;
  for (int i = 0, n = (int)(0.32f * SR); i < n; i++) {
    float t = i / SR;
    ph += TAU * (44.0f + 120.0f * expf(-t * 28.0f)) / SR;
    mix(c, s + i, a * sinf((float)ph) * expf(-t * 8.5f));
  }
}
static void snare(ctx_t *c, int s, float a) {
  for (int i = 0, n = (int)(0.25f * SR); i < n; i++) {
    float t = i / SR;
    mix(c, s + i, a * (rnd(c) * expf(-t * 20.0f) * 0.55f + sinf(TAU * 190.0f * t) * expf(-t * 30.0f) * 0.6f));
  }
}
static void hat(ctx_t *c, int s, float a, float decay, float len) {
  float prev = 0;
  for (int i = 0, n = (int)(len * SR); i < n; i++) {
    float t = i / SR, x = rnd(c), y = x - prev;
    prev = x;
    mix(c, s + i, a * 0.45f * y * expf(-t * decay));
  }
}
static void hat_c(ctx_t *c, int s, float a) { hat(c, s, a, 70.0f, 0.10f); }
static void hat_o(ctx_t *c, int s, float a) { hat(c, s, a, 10.0f, 0.35f); }
static void clap(ctx_t *c, int s, float a) {
  float prev = 0;
  for (int i = 0, n = (int)(0.2f * SR); i < n; i++) {
    float t = i / SR, e = expf(-t * 22.0f) * 0.6f, x = rnd(c);
    for (int k = 0; k < 3; k++) if (t >= 0.009f * k && t < 0.009f * (k + 1)) e = expf(-(t - 0.009f * k) * 250.0f);
    mix(c, s + i, a * 0.7f * e * (x - prev * 0.6f));
    prev = x;
  }
}

typedef void (*hit_fn)(ctx_t *, int, float);
// Sixteen-step patterns: 'x' full hit, 'o' soft hit, '.' rest.
static void drums(ctx_t *c, int bar, const char *k, const char *sn, const char *hc, const char *ho, const char *cl) {
  const struct { const char *p; hit_fn fn; } lanes[] = { {k, kick}, {sn, snare}, {hc, hat_c}, {ho, hat_o}, {cl, clap} };
  double step = c->bar / 16.0;
  for (int l = 0; l < 5; l++)
    for (int i = 0; lanes[l].p && lanes[l].p[i] && i < 16; i++) {
      char ch = lanes[l].p[i];
      if (ch == 'x' || ch == 'o') lanes[l].fn(c, (int)((bar * 16 + i) * step), ch == 'x' ? 0.9f : 0.45f);
    }
}

// ── Pitched voices ───────────────────────────────────────────────────────
static void v_piano(ctx_t *c, int s, int g, float hz, float a) {
  static const float hamp[5] = { 1.0f, 0.55f, 0.3f, 0.18f, 0.1f };
  int n = g + (int)(0.45f * SR);
  for (int h = 0; h < 5; h++) {
    float f = hz * (float)(h + 1) * (1.0f + 0.0004f * h * h), dec = 2.0f + 1.7f * h;
    double ph = 0;
    if (f > SR / 2.2f) break;
    for (int i = 0; i < n; i++, ph += TAU * f / SR) {
      float t = i / SR, e = expf(-t * dec) * fmin1(i / (0.003f * SR));
      if (i > g) e *= expf(-(i - g) / SR * 9.0f);
      if (e < 1e-4f && i > g) break;
      mix(c, s + i, a * hamp[h] * 0.33f * sinf((float)ph) * e);
    }
  }
}
static void v_bass(ctx_t *c, int s, int g, float hz, float a, bool bright) {
  int n = g + (int)(0.06f * SR);
  double fr = 0; float lp = 0;
  for (int i = 0; i < n; i++, fr += hz / SR) {
    float t = i / SR, f = (float)(fr - floor(fr)), saw = 2.0f * f - 1.0f;
    float e = fmin1(i / (0.004f * SR)) * (0.6f + 0.4f * expf(-t * 6.0f)) * (i < g ? 1.0f : expf(-(i - g) / SR * 60.0f));
    float k = 1.0f - expf(-TAU * (250.0f + 1200.0f * expf(-t * 12.0f)) / SR);
    lp += k * (saw - lp);
    mix(c, s + i, a * e * (bright ? lp * 0.8f + sinf(TAU * f) * 0.6f : sinf(TAU * f) * 0.95f));
  }
}
static void v_pluck(ctx_t *c, int s, int g, float hz, float a, float damp) {
  int N = (int)(SR / hz), n = (int)((damp > 0.99f ? 1.4f : 0.5f) * SR);
  float d[4096], prev = 0;
  if (N < 2) N = 2;
  if (N > 4096) N = 4096;
  for (int i = 0; i < N; i++) { float x = rnd(c); d[i] = 0.5f * (x + prev); prev = x; }
  for (int i = 0, p = 0; i < n; i++) {
    float y = d[p], nx = d[(p + 1) % N];
    d[p] = damp * 0.5f * (y + nx);
    p = (p + 1) % N;
    mix(c, s + i, a * 0.9f * y * (i > g ? expf(-(i - g) / SR * 5.0f) : 1.0f));
  }
}
static void v_acid(ctx_t *c, int s, int g, float hz, float a) {
  int n = g + (int)(0.05f * SR);
  double fr = 0; float low = 0, band = 0;
  for (int i = 0; i < n; i++, fr += hz / SR) {
    float t = i / SR, saw = 2.0f * (float)(fr - floor(fr)) - 1.0f;
    float f = 2.0f * sinf(3.14159265f * (300.0f + 3500.0f * expf(-t * 9.0f)) / SR);
    float hi = saw - low - 0.2f * band;
    band += f * hi;
    low += f * band;
    mix(c, s + i, a * 0.55f * low * fmin1(i / (0.003f * SR)) * (i < g ? 1.0f : expf(-(i - g) / SR * 80.0f)));
  }
}
static void v_pad(ctx_t *c, int s, int g, float hz, float a) {
  int n = g + (int)(0.5f * SR);
  double fr[3] = { 0, 0.3, 0.7 }; float lp = 0, k = 1.0f - expf(-TAU * 1400.0f / SR);
  const float det[3] = { 1.0f, 1.004f, 0.996f };
  for (int i = 0; i < n; i++) {
    float t = i / SR, x = 0;
    for (int o = 0; o < 3; o++) { fr[o] += hz * det[o] / SR; x += 2.0f * (float)(fr[o] - floor(fr[o])) - 1.0f; }
    lp += k * (x / 3.0f - lp);
    mix(c, s + i, a * 0.5f * lp * fmin1(t / 0.3f) * (i < g ? 1.0f : expf(-(i - g) / SR * 5.0f)));
  }
}
static void v_arp(ctx_t *c, int s, float hz, float a) {
  double fr = 0; float lp = 0, k = 1.0f - expf(-TAU * 3200.0f / SR);
  for (int i = 0, n = (int)(0.3f * SR); i < n; i++, fr += hz / SR) {
    float t = i / SR, sq = (fr - floor(fr)) < 0.3 ? 1.0f : -1.0f;
    lp += k * (sq - lp);
    mix(c, s + i, a * 0.4f * lp * expf(-t * 11.0f) * fmin1(i / (0.002f * SR)));
  }
}
static void v_riser(ctx_t *c, int len) {
  float low = 0, band = 0;
  for (int i = 0; i < len; i++) {
    float u = (float)i / len, f = 2.0f * sinf(3.14159265f * (300.0f + 8700.0f * u * u) / SR);
    float hi = rnd(c) - low - 0.35f * band;
    band += f * hi;
    low += f * band;
    mix(c, i, 0.35f * band * u * u);
  }
}

// Dance-library roles the original five families do not cover. Synthesized
// here; the reference clips are not copied. See docs/dance-ejay-pxd.md.
static void kick_len(ctx_t *c, int s, float a, float decay, float len) {
  double ph = 0;
  for (int i = 0, n = (int)(len * SR); i < n; i++) {
    float t = i / SR;
    ph += TAU * (38.0f + 150.0f * expf(-t * 26.0f)) / SR;
    mix(c, s + i, a * sinf((float)ph) * expf(-t * decay));
  }
}
static void tom(ctx_t *c, int s, float hz, float a) {
  double ph = 0;
  for (int i = 0, n = (int)(0.28f * SR); i < n; i++) {
    float t = i / SR;
    ph += TAU * hz * (1.0f + 0.5f * expf(-t * 18.0f)) / SR;
    mix(c, s + i, a * (sinf((float)ph) * expf(-t * 7.0f) + 0.2f * rnd(c) * expf(-t * 35.0f)));
  }
}
static void cymbal_at(ctx_t *c, int s, float a, float decay, float len) {
  float prev = 0, lp = 0, k = 1.0f - expf(-TAU * 5500.0f / SR);
  for (int i = 0, n = (int)(len * SR); i < n; i++) {
    float t = i / SR, x = rnd(c), y = x - prev;
    prev = x;
    lp += k * (y - lp);
    mix(c, s + i, a * (0.75f * lp + 0.12f * sinf(TAU * 3100.0f * t) + 0.08f * sinf(TAU * 5700.0f * t)) * expf(-t * decay));
  }
}
static void organ_note(ctx_t *c, int s, int g, float hz, float a) {
  static const float hamp[4] = { 1.0f, 0.55f, 0.28f, 0.16f };
  int n = g + (int)(0.25f * SR);
  for (int h = 0; h < 4; h++) {
    float f = hz * (float)(h * 2 + 1);
    double ph = 0;
    if (f > SR / 2.2f) break;
    for (int i = 0; i < n; i++, ph += TAU * f / SR) {
      float e = fmin1(i / (0.012f * SR)) * (i < g ? 1.0f : expf(-(i - g) / SR * 5.0f));
      mix(c, s + i, a * hamp[h] * 0.28f * sinf((float)ph) * e);
    }
  }
}
static void vox_ah(ctx_t *c, int s, float hz, float a, float len) {
  double fr = 0; float l1 = 0, b1 = 0, l2 = 0, b2 = 0;
  float f1 = 2.0f * sinf(3.14159265f * 640.0f / SR), f2 = 2.0f * sinf(3.14159265f * 1400.0f / SR);
  for (int i = 0, n = (int)(len * SR); i < n; i++, fr += hz / SR) {
    float t = i / SR, saw = 2.0f * (float)(fr - floor(fr)) - 1.0f;
    float h1 = saw - l1 - 0.18f * b1, h2 = saw - l2 - 0.22f * b2;
    b1 += f1 * h1; l1 += f1 * b1; b2 += f2 * h2; l2 += f2 * b2;
    mix(c, s + i, a * 0.35f * (l1 + 0.6f * l2) * fmin1(i / (0.006f * SR)) * expf(-t * 3.5f));
  }
}
static void scratch_at(ctx_t *c, int s, int n, int up) {
  float prev = 0, lp = 0;
  for (int i = 0; i < n; i++) {
    float u = (float)i / (float)n, fc = up ? 400.0f + 7000.0f * u * u : 7400.0f - 6800.0f * u;
    float k = 1.0f - expf(-TAU * fc / SR), x = rnd(c), y;
    lp += k * (x - lp);
    y = x - lp - 0.3f * prev;
    prev = x - lp;
    mix(c, s + i, 0.55f * y * sinf(3.14159265f * u));
  }
}
static void noise_bed(ctx_t *c, int len, float rise) {
  float low = 0, band = 0;
  for (int i = 0; i < len; i++) {
    float u = (float)i / (float)len, fc = rise > 0 ? 200.0f + 4000.0f * u : 4200.0f - 3800.0f * u;
    float f = 2.0f * sinf(3.14159265f * fc / SR), hi = rnd(c) - low - 0.4f * band;
    band += f * hi; low += f * band;
    mix(c, i, 0.3f * band * (rise > 0 ? u : 0.35f + 0.65f * (1.0f - u)));
  }
}
static void impact_at(ctx_t *c, int s) {
  double ph = 0;
  for (int i = 0, n = (int)(0.6f * SR); i < n; i++) {
    float t = i / SR;
    ph += TAU * (90.0f * expf(-t * 4.0f) + 40.0f) / SR;
    mix(c, s + i, 0.8f * sinf((float)ph) * expf(-t * 3.0f) + 0.45f * rnd(c) * expf(-t * 18.0f));
  }
}

static void voice(ctx_t *c, int s, int g, float hz, float a, inst_t inst, int idx) {
  s += inst == I_PLUCK || inst == I_MUTE ? idx * (int)(0.012f * SR) : inst == I_PIANO ? idx * (int)(0.003f * SR) : 0;
  switch (inst) {
    case I_PIANO: v_piano(c, s, g, hz, a);             break;
    case I_BASS:  v_bass(c, s, g, hz, a, true);        break;
    case I_SUB:   v_bass(c, s, g, hz, a, false);       break;
    case I_PLUCK: v_pluck(c, s, g, hz, a, 0.996f);     break;
    case I_MUTE:  v_pluck(c, s, g, hz, a, 0.93f);      break;
    case I_ACID:  v_acid(c, s, g, hz, a);              break;
    case I_PAD:   v_pad(c, s, g, hz, a);               break;
    case I_ARP:   v_arp(c, s, hz, a);                  break;
  }
}

static int note_midi(const char *s, int *len) {
  static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
  int m = base[(s[0] - 'A') % 7], i = 1;
  if (s[i] == '#') { m++; i++; } else if (s[i] == 'b') { m--; i++; }
  *len = i + 1;
  return m + 12 * (s[i] - '0' + 1);
}

// Space-separated steps: "A1" a note, "A3+C4+E4" a chord, "-" a rest.
// `spb` steps per beat; notes are gated for `hold` steps.
static void seq(ctx_t *c, const char *str, int spb, double hold, inst_t inst, float amp) {
  double step = c->bar / (GR_BEATS_BAR * (double)spb);
  int idx = 0;
  for (const char *p = str; *p; idx++) {
    while (*p == ' ') p++;
    if (!*p) break;
    if (*p == '-') { p++; continue; }
    for (int ci = 0;; ci++) {
      int len, m = note_midi(p, &len);
      p += len;
      voice(c, (int)(idx * step), (int)(hold * step), midi_hz(m), amp, inst, ci);
      if (*p != '+') break;
      p++;
    }
  }
}

// ── Block recipes ────────────────────────────────────────────────────────
#define GEN(fn) static void fn(ctx_t *c)
GEN(g_four_floor) { drums(c, 0, "x...x...x...x...", "", "o.o.o.o.o.o.o.o.", "..x...x...x...x.", "....x.......x..."); }
GEN(g_break_beat) { drums(c, 0, "x.....x...x.....", "....x.......x..x", "x.x.x.x.x.x.x.x.", "", ""); }
GEN(g_hat_groove) { drums(c, 0, "", "", "xooxooxooxooxoox", "", ""); }
GEN(g_snare_fill) { drums(c, 0, "x...............", "o.o.o.o.xoxoxxxx", "", "", ""); }
GEN(g_half_time) {
  drums(c, 0, "x.......x.x.....", "........x.......", "x.x.x.x.x.x.x.x.", "", "");
  drums(c, 1, "x.......x.......", "........x.....xx", "x.x.x.x.x.x.x.x.", "..............x.", "");
}
GEN(g_root_pulse) { seq(c, "A1 A1 A1 A1 A1 A1 A1 A1", 2, 0.8, I_BASS, 0.9f); }
GEN(g_walking)    { seq(c, "A1 C2 D2 E2 G1 E2 D2 C2", 1, 0.9, I_BASS, 0.9f); }
GEN(g_sub_drone)  { seq(c, "A1 - - -", 1, 4.0, I_SUB, 0.9f); }
GEN(g_funk_bass)  { seq(c, "A1 - - A1 - - A2 - A1 - - E2 - G1 - -", 4, 1.5, I_BASS, 0.9f); }
GEN(g_am_f)       { seq(c, "A3+C4+E4 - - - F3+A3+C4 - - -", 1, 3.6, I_PIANO, 0.8f); }
GEN(g_c_g)        { seq(c, "C4+E4+G4 - - - G3+B3+D4 - - -", 1, 3.6, I_PIANO, 0.8f); }
GEN(g_stabs)      { seq(c, "A3+C4+E4 - - A3+C4+E4 - - A3+C4+E4 -", 2, 1.0, I_PIANO, 0.8f); }
GEN(g_arp_am)     { seq(c, "A3 C4 E4 A4 E4 C4 E4 A4 A3 C4 E4 A4 E4 C4 E4 C4", 4, 2.0, I_PIANO, 0.8f); }
GEN(g_melody)     { seq(c, "E5 - - D5 C5 - A4 - - - C5 - D5 - E5 -", 2, 1.6, I_PIANO, 0.85f); }
GEN(g_strum)      { seq(c, "A2+E3+A3+C4+E4 - - A2+E3+A3+C4+E4 - A2+E3+A3+C4+E4 - -", 2, 3.0, I_PLUCK, 0.7f); }
GEN(g_palm_mute)  { seq(c, "A2 A2 A2 A2 A2 A2 A2 A2", 2, 0.8, I_MUTE, 0.9f); }
GEN(g_power_riff) { seq(c, "A2+E3 - - A2+E3 - C3+G3 - D3+A3 - - - E3+B3 - D3+A3 - -", 2, 1.5, I_MUTE, 0.8f); }
GEN(g_pick_arp)   { seq(c, "E3 A3 C4 E4 C4 A3 C4 A3", 2, 1.5, I_PLUCK, 0.8f); }
GEN(g_acid)       { seq(c, "A1 - A2 A1 - A1 C2 - A1 - A2 - G1 - E2 -", 4, 1.4, I_ACID, 0.9f); }
GEN(g_pad_am)     { seq(c, "A3+C4+E4 - - - - - - -", 1, 8.0, I_PAD, 0.8f); }
GEN(g_pad_fg)     { seq(c, "F3+A3+C4 - - - G3+B3+D4 - - -", 1, 4.0, I_PAD, 0.8f); }
GEN(g_pluck_arp)  { seq(c, "A4 C5 E5 C5 A4 C5 E5 C5 G4 B4 D5 B4 G4 B4 D5 B4", 4, 1.0, I_ARP, 0.9f); }
GEN(g_riser)      { v_riser(c, c->n); }
GEN(g_boom_bap) { drums(c, 0, "x.....x...x.....", "....x.......x...", "x.x.x.x.x.x.x.x.", "", ""); }
GEN(g_disco) { drums(c, 0, "x...x...x...x...", "....x.......x...", "x.x.x.x.x.x.x.x.", "..o...o...o...o.", ""); }
GEN(g_trap_hats) { drums(c, 0, "x.....x.....x...", "........x.......", "xoxoxxoxoxxoxoxx", "", ""); }
GEN(g_clap_beat) { drums(c, 0, "x...x...x...x...", "", "..x...x...x...x.", "", "....x.......x..."); }
GEN(g_shuffle) { drums(c, 0, "x.....x.x.....x.", "....x.......x...", "x.oox.oox.oox.oo", "", ""); }
GEN(g_kick_rush) { drums(c, 0, "xoxoxoxoxoxoxoxx", "", "", "", ""); }
GEN(g_sixteenths) { drums(c, 0, "x...x...x...x...", "....x.......x...", "xoxoxoxoxoxoxoxo", "", ""); }
GEN(g_breakdown) {
  drums(c, 0, "x.......x.......", "", "", "", "");
  drums(c, 1, "x...x...x.x.x.xx", "..............xx", "x.x.x.x.x.x.x.x.", "", "");
}
GEN(g_clap_fill) { drums(c, 0, "x.......x.......", "", "", "", "........x.x.xxxx"); }
GEN(g_double_kick) { drums(c, 0, "x.x...x.x.x...x.", "....x.......x...", "x.x.x.x.x.x.x.x.", "", ""); }
GEN(g_oct_jump) { seq(c, "A1 A2 A1 A2 A1 A2 A1 A2", 2, 0.8, I_BASS, 0.9f); }
GEN(g_slow_roots) { seq(c, "A1 - - - F1 - - -", 1, 3.6, I_SUB, 0.9f); }
GEN(g_offbeat) { seq(c, "- A1 - A1 - A1 - A1", 2, 0.8, I_BASS, 0.9f); }
GEN(g_gallop) { seq(c, "A1 A1 - A1 A1 A1 - A1", 2, 0.7, I_BASS, 0.9f); }
GEN(g_climb) { seq(c, "A1 B1 C2 D2 E2 D2 C2 B1", 2, 0.9, I_BASS, 0.9f); }
GEN(g_dub_sub) { seq(c, "A1 - - - E1 - - -", 2, 3.5, I_SUB, 0.9f); }
GEN(g_slap) { seq(c, "A1 - A2 - A1 - A2 A1 - A1 - A2 - G1 - -", 4, 1.2, I_BASS, 0.9f); }
GEN(g_fifths) { seq(c, "A1 E2 A1 E2 G1 D2 G1 D2", 2, 0.9, I_BASS, 0.9f); }
GEN(g_dm_g) { seq(c, "D4+F4+A4 - - - G3+B3+D4 - - -", 1, 3.6, I_PIANO, 0.8f); }
GEN(g_f_c) { seq(c, "F3+A3+C4 - - - C4+E4+G4 - - -", 1, 3.6, I_PIANO, 0.8f); }
GEN(g_comping) { seq(c, "A3+C4+E4 - A3+C4+E4 - F3+A3+C4 - G3+B3+D4 -", 1, 1.8, I_PIANO, 0.8f); }
GEN(g_ballad) { seq(c, "A3 E4 C5 E4 A3 E4 C5 E4", 2, 1.8, I_PIANO, 0.8f); }
GEN(g_octaves) { seq(c, "A3+A4 - A3+A4 - C4+C5 - E4+E5 -", 2, 1.6, I_PIANO, 0.8f); }
GEN(g_rolling) { seq(c, "C4 E4 G4 E4 C4 E4 G4 E4 B3 D4 G4 D4 B3 D4 G4 D4", 4, 1.5, I_PIANO, 0.8f); }
GEN(g_bells) { seq(c, "E5 - B5 - G5 - E5 - D5 - - - A5 - - -", 4, 2.5, I_PIANO, 0.7f); }
GEN(g_low_chords) { seq(c, "A2+E3+A3 - - - F2+C3+F3 - - -", 1, 3.6, I_PIANO, 0.85f); }
GEN(g_gospel) { seq(c, "C4+E4+A4 - - - D4+F4+A4 - - -", 1, 3.6, I_PIANO, 0.8f); }
GEN(g_hook) { seq(c, "A4 - C5 - E5 - C5 - D5 - B4 - G4 - - -", 2, 1.6, I_PIANO, 0.85f); }
GEN(g_em_strum) { seq(c, "E2+B2+E3+G3+B3+E4 - - E2+B2+E3+G3+B3+E4 - E2+B2+E3+G3+B3+E4 - -", 2, 3.0, I_PLUCK, 0.7f); }
GEN(g_g_strum) { seq(c, "G2+D3+G3+B3+D4 - - G2+D3+G3+B3+D4 - G2+D3+G3+B3+D4 - -", 2, 3.0, I_PLUCK, 0.7f); }
GEN(g_chug) { seq(c, "E2 E2 - E2 E2 - E2 - E2 E2 - E2 G2 - E2 -", 4, 0.8, I_MUTE, 0.9f); }
GEN(g_slow_pick) { seq(c, "A2 E3 A3 C4 E4 C4 A3 E3", 2, 2.0, I_PLUCK, 0.8f); }
GEN(g_riff_e) { seq(c, "E2+B2 - - E2+B2 - G2+D3 - A2+E3 - - - B2+F#3 - A2+E3 -", 2, 1.5, I_MUTE, 0.8f); }
GEN(g_chop) { seq(c, "- A3+C4+E4 - A3+C4+E4 - A3+C4+E4 - A3+C4+E4", 2, 0.5, I_MUTE, 0.8f); }
GEN(g_lead_line) { seq(c, "E4 - G4 - A4 - B4 - D5 - B4 - A4 - G4 -", 4, 1.8, I_PLUCK, 0.8f); }
GEN(g_ring_out) { seq(c, "A2+E3+A3+C4+E4 - - - - - - -", 1, 7.0, I_PLUCK, 0.7f); }
GEN(g_acid_two) { seq(c, "E1 - E2 E1 - E1 G1 - E1 - E2 - D2 - B1 -", 4, 1.4, I_ACID, 0.9f); }
GEN(g_pad_dm) { seq(c, "D3+F3+A3 - - - - - - -", 1, 8.0, I_PAD, 0.8f); }
GEN(g_pad_cg) { seq(c, "C3+E3+G3 - - - G3+B3+D4 - - -", 1, 4.0, I_PAD, 0.8f); }
GEN(g_pluck_stabs) { seq(c, "A3+C4+E4 - - A3+C4+E4 - - A3+C4+E4 -", 2, 1.0, I_ARP, 0.9f); }
GEN(g_fast_arp) { seq(c, "A4 E5 A5 E5 A4 E5 A5 E5 C5 G5 C5 G5 C5 G5 C5 E5", 4, 1.0, I_ARP, 0.9f); }
GEN(g_sub_pulse) { seq(c, "A1 A1 A1 A1", 1, 0.8, I_SUB, 0.9f); }
GEN(g_acid_climb) { seq(c, "A1 - B1 - C2 - D2 - E2 - D2 - C2 - B1 -", 4, 1.6, I_ACID, 0.9f); }
GEN(g_arp_down) { seq(c, "E5 C5 A4 E4 E5 C5 A4 E4 D5 B4 G4 D4 D5 B4 G4 D4", 4, 1.0, I_ARP, 0.9f); }
GEN(g_arp_dm) { seq(c, "D4 F4 A4 D5 A4 F4 A4 D5 D4 F4 A4 D5 A4 F4 A4 F4", 4, 1.0, I_ARP, 0.9f); }
GEN(g_sweep) { v_riser(c, c->n); }

GEN(g_dry_floor)  { drums(c, 0, "x...x...x...x...", "", "", "", ""); }
GEN(g_room_pulse) { for (int i = 0; i < 4; i++) kick_len(c, (int)(i * c->bar / 4.0), 0.9f, 4.2f, 0.55f); }
GEN(g_half_kick)  { for (int i = 0; i < 2; i++) kick_len(c, (int)(i * c->bar / 2.0), 0.95f, 3.0f, 0.75f); }
GEN(g_kick_run)   { kick_len(c, 0, 0.9f, 6.0f, 0.4f); for (int i = 8; i < 16; i++) kick_len(c, (int)(i * c->bar / 16.0), 0.7f, 11.0f, 0.11f); }
GEN(g_back_snap)  { drums(c, 0, "", "....x.......x...", "", "", ""); }
GEN(g_rim_tick)   { drums(c, 0, "", "..x...x...x...x.", "", "", ""); }
GEN(g_ghosts)     { drums(c, 0, "", "o.o.x.o.o.o.x.o.", "", "", ""); }
GEN(g_snare_run)  { drums(c, 0, "", "........xoxoxxxx", "", "", ""); }
GEN(g_closed_8)   { drums(c, 0, "", "", "x.x.x.x.x.x.x.x.", "", ""); }
GEN(g_open_off)   { drums(c, 0, "", "", "", "..x...x...x...x.", ""); }
GEN(g_tick_16)    { drums(c, 0, "", "", "xxxxxxxxxxxxxxxx", "", ""); }
GEN(g_shuf_hat)   { drums(c, 0, "", "", "x.oox.oox.oox.oo", "", ""); }
GEN(g_clap_back)  { drums(c, 0, "", "", "", "", "....x.......x..."); }
GEN(g_clap_stack) { drums(c, 0, "", "", "", "", "....x.......x..."); clap(c, (int)(4 * c->bar / 16.0) + 160, 0.55f); clap(c, (int)(12 * c->bar / 16.0) + 160, 0.55f); }
GEN(g_clap_doub)  { drums(c, 0, "", "", "", "", "....xx......xx.."); }
GEN(g_clap_rush2) { drums(c, 0, "", "", "", "", "........xoxoxxxx"); }
GEN(g_ride_8)     { for (int i = 0; i < 8; i++) cymbal_at(c, (int)(i * c->bar / 8.0), i & 1 ? 0.32f : 0.55f, 16.0f, 0.16f); }
GEN(g_crash_one)  { cymbal_at(c, 0, 0.9f, 2.0f, 1.5f); }
GEN(g_bell_pat)   { for (int i = 0; i < 8; i++) cymbal_at(c, (int)(i * c->bar / 8.0), 0.4f, 26.0f, 0.07f); }
GEN(g_splash)     { for (int i = 0; i < 4; i++) cymbal_at(c, (int)(i * c->bar / 4.0), 0.75f, 7.0f, 0.2f); }
GEN(g_shaker_16)  { drums(c, 0, "", "", "xoxoxoxoxoxoxoxo", "", ""); }
GEN(g_tamb_8)     { for (int i = 0; i < 8; i++) hat(c, (int)(i * c->bar / 8.0), 0.75f, 16.0f, 0.14f); }
GEN(g_conga)      { static const float hz[8] = { 180, 180, 230, 180, 200, 180, 230, 150 }; for (int i = 0; i < 8; i++) tom(c, (int)(i * c->bar / 8.0), hz[i], 0.8f); }
GEN(g_wood)       { for (int i = 0; i < 4; i++) cymbal_at(c, (int)(i * c->bar / 4.0), 0.45f, 48.0f, 0.04f); }
GEN(g_tom_down)   { static const float hz[8] = { 240, 210, 180, 150, 130, 110, 92, 74 }; for (int i = 0; i < 8; i++) tom(c, (int)(i * c->bar / 8.0), hz[i], 0.85f); }
GEN(g_snare_build){ drums(c, 0, "x...............", "o.o.o.o.xoxoxxxx", "", "", ""); }
GEN(g_kick_tumble){ kick_len(c, 0, 0.9f, 5.0f, 0.4f); for (int i = 8; i < 16; i++) kick_len(c, (int)(i * c->bar / 16.0), 0.75f, 12.0f, 0.1f); }
GEN(g_hat_lift)   { drums(c, 0, "x...............", "", "x.x.x.x.xxxxxxxx", "", ""); }
GEN(g_zip_up)     { scratch_at(c, 0, (int)(c->bar * 0.5), 1); }
GEN(g_zip_down)   { scratch_at(c, 0, (int)(c->bar * 0.5), 0); }
GEN(g_chop_loop)  { for (int i = 0; i < 4; i++) scratch_at(c, (int)(i * c->bar / 4.0), (int)(c->bar / 4.0 * 0.4), i & 1); }
GEN(g_brake)      { scratch_at(c, 0, (int)(c->bar * 0.85), 0); }
GEN(g_org_stab)   { for (int i = 0; i < 4; i += 2) { int s = (int)(i * c->bar / 4.0), g = (int)(c->bar / 4.0); organ_note(c, s, g, midi_hz(57), 0.7f); organ_note(c, s, g, midi_hz(60), 0.5f); organ_note(c, s, g, midi_hz(64), 0.45f); } }
GEN(g_org_off)    { for (int i = 1; i < 8; i += 2) { int s = (int)(i * c->bar / 8.0), g = (int)(c->bar / 10.0); organ_note(c, s, g, midi_hz(57), 0.65f); organ_note(c, s, g, midi_hz(64), 0.5f); } }
GEN(g_org_hold)   { int g = c->n - (int)(0.05f * SR); organ_note(c, 0, g, midi_hz(57), 0.6f); organ_note(c, 0, g, midi_hz(64), 0.45f); organ_note(c, 0, g, midi_hz(69), 0.4f); }
GEN(g_org_fifth)  { for (int i = 0; i < 4; i++) { int s = (int)(i * c->bar / 4.0), g = (int)(c->bar / 5.0), m = i < 2 ? 57 : 53; organ_note(c, s, g, midi_hz(m), 0.65f); organ_note(c, s, g, midi_hz(m + 7), 0.5f); } }
GEN(g_hey)        { vox_ah(c, 0, midi_hz(60), 0.85f, 0.3f); vox_ah(c, (int)(c->bar / 2.0), midi_hz(64), 0.75f, 0.3f); }
GEN(g_oh_layer)   { vox_ah(c, 0, midi_hz(55), 0.7f, 0.55f); vox_ah(c, 220, midi_hz(67), 0.4f, 0.45f); }
GEN(g_ah_hook)    { static const int m[8] = { 64, 67, 69, 67, 65, 64, 62, 60 }; for (int i = 0; i < 8; i++) vox_ah(c, (int)(i * c->bar / 8.0), midi_hz(m[i]), 0.7f, 0.2f); }
GEN(g_breath)     { for (int i = 0; i < 4; i++) hat(c, (int)(i * c->bar / 4.0), 0.45f, 5.0f, 0.22f); vox_ah(c, (int)(c->bar / 4.0), midi_hz(62), 0.55f, 0.4f); }
GEN(g_noise_up)   { noise_bed(c, c->n, 1); }
GEN(g_noise_down) { noise_bed(c, c->n, 0); }
GEN(g_impact)     { impact_at(c, 0); }
GEN(g_air)        {
  float low = 0, band = 0, f = 2.0f * sinf(3.14159265f * 800.0f / SR);
  for (int i = 0; i < c->n; i++) {
    float u = (float)i / (float)c->n, hi = rnd(c) - low - 0.55f * band;
    band += f * hi; low += f * band;
    mix(c, i, 0.28f * band * sinf(3.14159265f * fmin1(u * 4.0f)));
  }
}

static const struct { const char *name; category_t cat; int bars; void (*gen)(ctx_t *); } kDefs[] = {
  { "Four Floor",  CAT_DRUMS,      1, g_four_floor }, { "Break Beat",  CAT_DRUMS,      1, g_break_beat },
  { "Hat Groove",  CAT_DRUMS,      1, g_hat_groove }, { "Snare Fill",  CAT_DRUMS,      1, g_snare_fill },
  { "Half Time",   CAT_DRUMS,      2, g_half_time  },
  { "Root Pulse",  CAT_BASS,       1, g_root_pulse }, { "Walking",     CAT_BASS,       2, g_walking    },
  { "Sub Drone",   CAT_BASS,       1, g_sub_drone  }, { "Funk Bass",   CAT_BASS,       1, g_funk_bass  },
  { "Am - F",      CAT_PIANO,      2, g_am_f       }, { "C - G",       CAT_PIANO,      2, g_c_g        },
  { "Stabs",       CAT_PIANO,      1, g_stabs      }, { "Arp Am",      CAT_PIANO,      1, g_arp_am     },
  { "Melody",      CAT_PIANO,      2, g_melody     },
  { "Am Strum",    CAT_GUITAR,     1, g_strum      }, { "Palm Mute",   CAT_GUITAR,     1, g_palm_mute  },
  { "Power Riff",  CAT_GUITAR,     2, g_power_riff }, { "Pick Arp",    CAT_GUITAR,     1, g_pick_arp   },
  { "Acid Line",   CAT_ELECTRONIC, 1, g_acid       }, { "Pad Am",      CAT_ELECTRONIC, 2, g_pad_am     },
  { "Pad F - G",   CAT_ELECTRONIC, 2, g_pad_fg     }, { "Pluck Arp",   CAT_ELECTRONIC, 1, g_pluck_arp  },
  { "Riser",       CAT_ELECTRONIC, 2, g_riser      },
  { "Boom Bap",    CAT_DRUMS     , 1, g_boom_bap },   { "Disco",       CAT_DRUMS     , 1, g_disco },
  { "Trap Hats",   CAT_DRUMS     , 1, g_trap_hats },   { "Clap Beat",   CAT_DRUMS     , 1, g_clap_beat },
  { "Shuffle",     CAT_DRUMS     , 1, g_shuffle },   { "Kick Rush",   CAT_DRUMS     , 1, g_kick_rush },
  { "Sixteenths",  CAT_DRUMS     , 1, g_sixteenths },   { "Breakdown",   CAT_DRUMS     , 2, g_breakdown },
  { "Clap Fill",   CAT_DRUMS     , 1, g_clap_fill },   { "Double Kick", CAT_DRUMS     , 1, g_double_kick },
  { "Octave Jump", CAT_BASS      , 1, g_oct_jump },   { "Slow Roots",  CAT_BASS      , 2, g_slow_roots },
  { "Offbeat",     CAT_BASS      , 1, g_offbeat },   { "Gallop",      CAT_BASS      , 1, g_gallop },
  { "Climb",       CAT_BASS      , 1, g_climb },   { "Dub Sub",     CAT_BASS      , 1, g_dub_sub },
  { "Slap",        CAT_BASS      , 1, g_slap },   { "Fifths",      CAT_BASS      , 1, g_fifths },
  { "Dm - G",      CAT_PIANO     , 2, g_dm_g },   { "F - C",       CAT_PIANO     , 2, g_f_c },
  { "Comping",     CAT_PIANO     , 2, g_comping },   { "Ballad",      CAT_PIANO     , 1, g_ballad },
  { "Octaves",     CAT_PIANO     , 1, g_octaves },   { "Rolling",     CAT_PIANO     , 1, g_rolling },
  { "Bells",       CAT_PIANO     , 1, g_bells },   { "Low Chords",  CAT_PIANO     , 2, g_low_chords },
  { "Gospel",      CAT_PIANO     , 2, g_gospel },   { "Hook",        CAT_PIANO     , 2, g_hook },
  { "Em Strum",    CAT_GUITAR    , 1, g_em_strum },   { "G Strum",     CAT_GUITAR    , 1, g_g_strum },
  { "Chug",        CAT_GUITAR    , 1, g_chug },   { "Slow Pick",   CAT_GUITAR    , 1, g_slow_pick },
  { "Riff E",      CAT_GUITAR    , 2, g_riff_e },   { "Chop",        CAT_GUITAR    , 1, g_chop },
  { "Lead Line",   CAT_GUITAR    , 1, g_lead_line },   { "Ring Out",    CAT_GUITAR    , 2, g_ring_out },
  { "Acid Two",    CAT_ELECTRONIC, 1, g_acid_two },   { "Pad Dm",      CAT_ELECTRONIC, 2, g_pad_dm },
  { "Pad C - G",   CAT_ELECTRONIC, 2, g_pad_cg },   { "Pluck Stabs", CAT_ELECTRONIC, 1, g_pluck_stabs },
  { "Fast Arp",    CAT_ELECTRONIC, 1, g_fast_arp },   { "Sub Pulse",   CAT_ELECTRONIC, 1, g_sub_pulse },
  { "Acid Climb",  CAT_ELECTRONIC, 1, g_acid_climb },   { "Arp Down",    CAT_ELECTRONIC, 1, g_arp_down },
  { "Arp Dm",      CAT_ELECTRONIC, 1, g_arp_dm },   { "Sweep",       CAT_ELECTRONIC, 1, g_sweep },
  { "Dry Floor",   CAT_KICK,    1, g_dry_floor },  { "Room Pulse",    CAT_KICK,    1, g_room_pulse },
  { "Half Kick",   CAT_KICK,    1, g_half_kick },  { "Kick Run",      CAT_KICK,    1, g_kick_run },
  { "Back Snap",   CAT_SNARE,   1, g_back_snap },  { "Rim Tick",      CAT_SNARE,   1, g_rim_tick },
  { "Ghost Notes", CAT_SNARE,   1, g_ghosts },     { "Snare Run",     CAT_SNARE,   1, g_snare_run },
  { "Closed 8ths", CAT_HAT,     1, g_closed_8 },   { "Open Offbeat",  CAT_HAT,     1, g_open_off },
  { "Tick 16ths",  CAT_HAT,     1, g_tick_16 },    { "Shuffle Hat",   CAT_HAT,     1, g_shuf_hat },
  { "Clap Back",   CAT_CLAP,    1, g_clap_back },  { "Clap Stack",    CAT_CLAP,    1, g_clap_stack },
  { "Double Clap", CAT_CLAP,    1, g_clap_doub },  { "Clap Rush",     CAT_CLAP,    1, g_clap_rush2 },
  { "Ride 8ths",   CAT_CYMBAL,  1, g_ride_8 },     { "Crash Down",    CAT_CYMBAL,  1, g_crash_one },
  { "Bell Pattern",CAT_CYMBAL,  1, g_bell_pat },   { "Splash",        CAT_CYMBAL,  1, g_splash },
  { "Shaker 16",   CAT_PERC,    1, g_shaker_16 },  { "Tamb 8ths",     CAT_PERC,    1, g_tamb_8 },
  { "Conga Loop",  CAT_PERC,    1, g_conga },      { "Wood Tick",     CAT_PERC,    1, g_wood },
  { "Tom Down",    CAT_FILL,    1, g_tom_down },   { "Snare Build",   CAT_FILL,    1, g_snare_build },
  { "Kick Tumble", CAT_FILL,    1, g_kick_tumble },{ "Hat Lift",      CAT_FILL,    1, g_hat_lift },
  { "Zip Up",      CAT_SCRATCH, 1, g_zip_up },     { "Zip Down",      CAT_SCRATCH, 1, g_zip_down },
  { "Chop Loop",   CAT_SCRATCH, 1, g_chop_loop },  { "Brake",         CAT_SCRATCH, 1, g_brake },
  { "Organ Stab",  CAT_ORGAN,   1, g_org_stab },   { "Offbeat Organ", CAT_ORGAN,   1, g_org_off },
  { "Organ Hold",  CAT_ORGAN,   2, g_org_hold },   { "Fifth Chop",    CAT_ORGAN,   1, g_org_fifth },
  { "Hey Chop",    CAT_VOX,     1, g_hey },        { "Oh Layer",      CAT_VOX,     1, g_oh_layer },
  { "Ah Hook",     CAT_VOX,     1, g_ah_hook },    { "Breath Stack",  CAT_VOX,     1, g_breath },
  { "Noise Up",    CAT_FX,      2, g_noise_up },   { "Noise Down",    CAT_FX,      2, g_noise_down },
  { "Impact",      CAT_FX,      1, g_impact },     { "Air Bed",       CAT_FX,      2, g_air },
};
#define NUM_DEFS ((int)(sizeof(kDefs) / sizeof(kDefs[0])))

static block_t g_blocks[GR_MAX_BLOCKS];
static bool    g_meta;

static void meta(void) {
  if (g_meta) return;
  for (int i = 0; i < NUM_DEFS; i++) { g_blocks[i].name = kDefs[i].name; g_blocks[i].cat = kDefs[i].cat; g_blocks[i].bars = kDefs[i].bars; }
  g_meta = true;
}

int blocks_count(void) { return NUM_DEFS; }

const block_t *block_get(int id) { meta(); return id >= 0 && id < NUM_DEFS ? &g_blocks[id] : NULL; }

int blocks_in_category(category_t cat, int *ids, int max) {
  int n = 0;
  for (int i = 0; i < NUM_DEFS && n < max; i++) if (kDefs[i].cat == cat) ids[n++] = i;
  return n;
}

void blocks_render(int bpm, block_pcm_t out[GR_MAX_BLOCKS]) {
  int bar = bar_frames_for_bpm(bpm);
  memset(out, 0, sizeof(block_pcm_t) * GR_MAX_BLOCKS);
  for (int i = 0; i < NUM_DEFS; i++) {
    ctx_t c = { calloc((size_t)bar * kDefs[i].bars, sizeof(float)), bar * kDefs[i].bars, bar, 0x9e3779b9u + (uint32_t)i * 7919u };
    if (!c.buf) { fprintf(stderr, "[synth] allocation failed block=%d frames=%d\n", i, c.n); fflush(stderr); continue; }
    kDefs[i].gen(&c);
    float peak = 1e-6f;
    for (int k = 0; k < c.n; k++) if (fabsf(c.buf[k]) > peak) peak = fabsf(c.buf[k]);
    for (int k = 0; k < c.n; k++) c.buf[k] *= 0.85f / peak;
    for (int k = 0; k < 128 && k < c.n; k++) c.buf[c.n - 1 - k] *= k / 128.0f; // de-click the cut tail
    out[i].pcm = c.buf;
    out[i].frames = c.n;
    out[i].npeaks = kDefs[i].bars * GR_PEAKS_BAR;
    for (int k = 0; k < out[i].npeaks; k++) {
      int a = (int)((int64_t)c.n * k / out[i].npeaks), b = (int)((int64_t)c.n * (k + 1) / out[i].npeaks);
      float m = 0;
      for (int j = a; j < b; j++) if (fabsf(c.buf[j]) > m) m = fabsf(c.buf[j]);
      out[i].peaks[k] = (uint8_t)(fmin1(m) * 255.0f);
    }
  }
}

void blocks_swap(block_pcm_t io[GR_MAX_BLOCKS]) {
  meta();
  for (int i = 0; i < NUM_DEFS; i++) {
    block_pcm_t old = g_blocks[i].audio;
    g_blocks[i].audio = io[i];
    io[i] = old;
  }
}

void blocks_free(void) {
  for (int i = 0; i < NUM_DEFS; i++) { free(g_blocks[i].audio.pcm); g_blocks[i].audio = (block_pcm_t){0}; }
}

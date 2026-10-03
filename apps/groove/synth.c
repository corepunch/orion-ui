// Block generator: renders the starter library (drums, bass, piano, guitar,
// electronic) as mono float buffers locked to the project tempo. Imported
// audio blocks will later land in the same block_t table.

#include "groove.h"

#define SR  ((float)GR_SAMPLE_RATE)
#define TAU 6.28318530718f

const char *const kCategoryName[CAT_COUNT] = { "Drums", "Bass", "Piano", "Guitar", "Electronic" };

uint32_t category_color(category_t cat) {
  static const uint32_t col[CAT_COUNT] = { WEB(0xd9702a), WEB(0x2f7bd1), WEB(0xb8962a), WEB(0x3a9a55), WEB(0x9a4cc4) };
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

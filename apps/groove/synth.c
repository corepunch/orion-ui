// Synth engine for the non-vocal blocks; vocal samples are decoded separately.
// See docs/sound-synthesis.md.

#include "synth.h"

#define SR  SY_SR
#define TAU SY_TAU

float sy_rnd(sy_ctx_t *c) { c->rng = c->rng * 1664525u + 1013904223u; return (float)(c->rng >> 8) * (1.0f / 8388608.0f) - 1.0f; }
void  sy_mix(sy_ctx_t *c, int i, float v) { if (i >= 0 && i < c->n + SY_TAIL) c->buf[i] += v; }
float sy_midi_hz(int m) { return 440.0f * powf(2.0f, (float)(m - 69) / 12.0f); }
static float fmin1(float v) { return v < 1.0f ? v : 1.0f; }

// ── Oscillators and filters ──────────────────────────────────────────────
// PolyBLEP: rounds the saw and pulse edges so high notes do not alias.
static float blep(float t, float dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
  if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
  return 0.0f;
}
static float osc_saw(float *ph, float dt) {
  float t = *ph, v = 2.0f * t - 1.0f - blep(t, dt);
  *ph += dt; if (*ph >= 1.0f) *ph -= 1.0f;
  return v;
}
static float osc_pulse(float *ph, float dt, float pw) {
  float t = *ph, t2 = t + 1.0f - pw;
  if (t2 >= 1.0f) t2 -= 1.0f;
  float v = (t < pw ? 1.0f : -1.0f) + blep(t, dt) - blep(t2, dt);
  *ph += dt; if (*ph >= 1.0f) *ph -= 1.0f;
  return v;
}
// Triangle harmonics fall fast, so the corners stay in range without a BLEP.
static float osc_tri(float *ph, float dt) {
  float v = 4.0f * fabsf(*ph - 0.5f) - 1.0f;
  *ph += dt; if (*ph >= 1.0f) *ph -= 1.0f;
  return v;
}
// One note, three channels: triangle is the body, pulse the hollow mid, saw the edge.
static float osc_chans(float *tri, float *pul, float *saw, float dt, float pw, float a_tri, float a_pul, float a_saw) {
  return a_tri * osc_tri(tri, dt) + a_pul * osc_pulse(pul, dt, pw) + a_saw * osc_saw(saw, dt * 1.005f);
}
// Fixed-pitch sine as a two-multiply recurrence; far cheaper than sinf per sample.
typedef struct { double s0, s1, k; } sine_t;
static void sine_init(sine_t *o, float hz, float phase) {
  double w = TAU * hz / SR;
  o->s0 = sin(phase); o->s1 = sin(phase + w); o->k = 2.0 * cos(w);
}
static float sine_next(sine_t *o) {
  double v = o->s0, s2 = o->k * o->s1 - o->s0;
  o->s0 = o->s1; o->s1 = s2;
  return (float)v;
}
// State-variable filter (trapezoidal form): stable at any cutoff, so it can sweep.
typedef struct { float a, b, k, a1, a2, a3; } svf_t;
enum { F_LP, F_BP, F_HP };
static void svf_set(svf_t *s, float fc, float q) {
  float g = tanf(SY_PI * CLAMP(fc, 20.0f, 0.45f * SR) / SR);
  s->k = 1.0f / q;
  s->a1 = 1.0f / (1.0f + g * (g + s->k)); s->a2 = g * s->a1; s->a3 = g * s->a2;
}
static float svf_run(svf_t *s, float x, int mode) {
  float v3 = x - s->b, v1 = s->a1 * s->a + s->a2 * v3, v2 = s->b + s->a2 * s->a + s->a3 * v3;
  s->a = 2.0f * v1 - s->a; s->b = 2.0f * v2 - s->b;
  return mode == F_LP ? v2 : mode == F_BP ? s->k * v1 : x - s->k * v1 - v2;
}
// Attack ramp in seconds, exponential release (1/seconds) after the gate.
static float env(int i, int g, float attack, float release) {
  float e = fmin1(i / (attack * SR));
  return i < g ? e : e * expf(-(i - g) / SR * release);
}
static float glide(const sy_note_t *n, float t, float time) {
  return n->from > 0 && t < time ? n->hz * powf(n->from / n->hz, 1.0f - t / time) : n->hz;
}

// ── Starter drums ────────────────────────────────────────────────────────
void sy_kick(sy_ctx_t *c, int s, float a) { sy_kick_len(c, s, a, 8.5f, 0.32f); }
void sy_snare(sy_ctx_t *c, int s, float a) {
  svf_t crack = {0}, bp = {0};
  svf_set(&crack, 2800.0f, 0.6f); svf_set(&bp, 900.0f, 1.1f);
  for (int i = 0, n = (int)(0.28f * SR); i < n; i++) {
    float t = i / SR, nse = sy_rnd(c);
    float body = (sinf(TAU * 190.0f * t) + 0.5f * sinf(TAU * 330.0f * t)) * expf(-t * 24.0f);
    float noise = 0.9f * svf_run(&crack, nse, F_BP) * expf(-t * 16.0f) + 0.55f * svf_run(&bp, sy_rnd(c), F_BP) * expf(-t * 12.0f);
    sy_mix(c, s + i, a * 0.7f * (body + noise));
  }
}
void sy_hat(sy_ctx_t *c, int s, float a, float decay, float len) {
  svf_t hp = {0}, bp = {0}, lp = {0};
  float ph[3] = { 0.1f, 0.45f, 0.8f };
  static const float hz[3] = { 317.0f, 491.0f, 787.0f };
  svf_set(&hp, 4200.0f, 0.7f); svf_set(&bp, 2000.0f, 0.9f); svf_set(&lp, 11500.0f, 0.7f);
  for (int i = 0, n = (int)(len * SR); i < n; i++) {
    float m = 0, nse = sy_rnd(c);
    for (int o = 0; o < 3; o++) m += osc_pulse(&ph[o], hz[o] / SR, 0.5f);
    float y = 0.45f * svf_run(&hp, 0.5f * nse + 0.5f * m / 3.0f, F_HP) + 0.75f * svf_run(&bp, nse, F_BP);
    sy_mix(c, s + i, a * 0.4f * svf_run(&lp, y, F_LP) * expf(-i / SR * decay));
  }
}
void sy_clap(sy_ctx_t *c, int s, float a) {
  float prev = 0;
  for (int i = 0, n = (int)(0.2f * SR); i < n; i++) {
    float t = i / SR, e = expf(-t * 22.0f) * 0.6f, x = sy_rnd(c);
    for (int k = 0; k < 3; k++) if (t >= 0.009f * k && t < 0.009f * (k + 1)) e = expf(-(t - 0.009f * k) * 250.0f);
    sy_mix(c, s + i, a * 0.7f * e * (x - prev * 0.6f));
    prev = x;
  }
}
void sy_kick_len(sy_ctx_t *c, int s, float a, float decay, float len) {
  double ph = 0;
  float prev = 0;
  svf_t bp = {0};
  svf_set(&bp, 1300.0f, 1.15f);
  for (int i = 0, n = (int)(len * SR); i < n; i++) {
    float t = i / SR, nse = sy_rnd(c);
    ph += TAU * (42.0f + 170.0f * expf(-t * 26.0f)) / SR;
    float v = sinf((float)ph) * expf(-t * decay);
    v += 0.5f * svf_run(&bp, nse, F_BP) * expf(-t * 24.0f) + 0.3f * (nse - prev) * expf(-t * 360.0f);
    prev = nse;
    sy_mix(c, s + i, a * tanhf(1.5f * v) / tanhf(1.5f));
  }
}
void sy_tom(sy_ctx_t *c, int s, float hz, float a) {
  double ph = 0;
  for (int i = 0, n = (int)(0.28f * SR); i < n; i++) {
    float t = i / SR;
    ph += TAU * hz * (1.0f + 0.5f * expf(-t * 18.0f)) / SR;
    sy_mix(c, s + i, a * (sinf((float)ph) * expf(-t * 7.0f) + 0.2f * sy_rnd(c) * expf(-t * 35.0f)));
  }
}
void sy_cymbal(sy_ctx_t *c, int s, float a, float decay, float len) {
  svf_t hp = {0}, bp = {0}, lp = {0};
  float ph[4] = { 0.1f, 0.32f, 0.58f, 0.84f };
  static const float hz[4] = { 211.0f, 317.0f, 463.0f, 691.0f };
  svf_set(&hp, 3600.0f, 0.75f); svf_set(&bp, 1600.0f, 0.85f); svf_set(&lp, 10500.0f, 0.7f);
  for (int i = 0, n = (int)(len * SR); i < n; i++) {
    float m = 0, nse = sy_rnd(c);
    for (int o = 0; o < 4; o++) m += osc_pulse(&ph[o], hz[o] / SR, 0.5f);
    float y = 0.45f * svf_run(&hp, 0.45f * nse + 0.55f * m / 4.0f, F_HP) + 0.7f * svf_run(&bp, nse, F_BP);
    sy_mix(c, s + i, a * 0.5f * svf_run(&lp, y, F_LP) * expf(-i / SR * decay));
  }
}

// ── Kits ─────────────────────────────────────────────────────────────────
typedef struct {
  float kf, ksweep, krate, kdecay, klen, kclick, kdrive; // kick: floor Hz, sweep Hz, sweep rate, decay, seconds, click, drive
  float stone, sbody, snoise, sdecay, sbright;           // snare: tone Hz, body, noise, noise decay, crack-band Hz
  float hdecay, odecay, hmetal, hhp;                     // hats: closed and open decay, metal-to-noise mix, high-pass Hz
  float cfc, cdecay;                                     // clap: band centre Hz, tail decay
  float tune;                                            // toms and hand percussion pitch factor
} kit_t;

static const kit_t kKits[KIT_COUNT] = {
  [KIT_CLASSIC] = { 54, 200, 36,  8.5f, 0.38f, 0.55f, 1.8f,  188, 0.55f, 0.70f, 15, 2200,   70,  8.5f, 0.65f, 4600,  1200, 18, 1.00f },
  [KIT_909]     = { 52, 230, 38,  9.0f, 0.40f, 0.55f, 4.2f,  190, 0.55f, 0.70f, 14, 2800,   70,  8.5f, 0.62f, 4000,  1250, 20, 1.00f },
  [KIT_808]     = { 47,  95, 42,  3.0f, 0.90f, 0.12f, 0.0f,  172, 0.70f, 0.45f, 24, 4200,   95, 12.0f, 0.85f, 5000,  1100, 26, 0.90f },
  [KIT_HARD]    = { 58, 420, 30,  5.5f, 0.42f, 0.60f, 6.0f,  215, 0.45f, 0.95f, 12, 1700,   60,  7.0f, 0.55f, 4000,  1450, 15, 1.10f },
  [KIT_TECHNO]  = { 46, 170, 46,  6.5f, 0.50f, 0.35f, 2.4f,  265, 0.40f, 0.60f, 28, 5200,  110, 11.0f, 0.75f, 5400,  1700, 24, 0.85f },
  [KIT_BREAK]   = { 64, 120, 55, 13.0f, 0.30f, 0.90f, 1.6f,  205, 0.60f, 0.85f, 13, 1100,   55,  8.0f, 0.30f, 3800,   950, 16, 1.05f },
  [KIT_LOFI]    = { 56, 105, 36,  8.0f, 0.40f, 0.25f, 3.0f,  182, 0.60f, 0.60f, 18, 1500,   70, 10.0f, 0.35f, 3200,  1000, 20, 0.95f },
};

static float tail_window(int i, int n) { return fmin1((n - i) / (0.004f * SR)); }

static void k_kick(sy_ctx_t *c, const kit_t *k, int s, float a) {
  double ph = 0;
  float norm = k->kdrive > 0 ? 1.0f / tanhf(k->kdrive) : 1.0f, prev = 0;
  svf_t bp = {0};
  svf_set(&bp, 1250.0f, 1.15f);
  for (int i = 0, n = (int)(k->klen * SR); i < n; i++) {
    float t = i / SR, x = sy_rnd(c);
    ph += TAU * (k->kf + k->ksweep * expf(-t * k->krate)) / SR;
    float v = sinf((float)ph) * expf(-t * k->kdecay);
    if (k->kdrive > 0) v = tanhf(v * k->kdrive) * norm;
    v += k->kclick * (0.25f * (x - prev) * expf(-t * 420.0f) + 0.7f * svf_run(&bp, x, F_BP) * expf(-t * 24.0f));
    prev = x;
    sy_mix(c, s + i, a * v * tail_window(i, n));
  }
}
static void k_snare(sy_ctx_t *c, const kit_t *k, int s, float a) {
  svf_t crack = {0}, bp = {0};
  double p1 = 0, p2 = 0;
  svf_set(&crack, k->sbright, 0.6f);
  svf_set(&bp, fmaxf(450.0f, k->sbright * 0.35f), 1.1f);
  for (int i = 0, n = (int)(0.3f * SR); i < n; i++) {
    float t = i / SR, bend = 1.0f + 0.35f * expf(-t * 45.0f), nse = sy_rnd(c);
    p1 += TAU * k->stone * bend / SR; p2 += TAU * k->stone * 1.78f * bend / SR;
    float body = (sinf((float)p1) + 0.55f * sinf((float)p2)) * expf(-t * 14.0f);
    float noise = svf_run(&crack, nse, F_BP) * expf(-t * k->sdecay);
    float mid = svf_run(&bp, sy_rnd(c), F_BP) * expf(-t * k->sdecay * 0.55f);
    sy_mix(c, s + i, a * 0.7f * (k->sbody * body + k->snoise * (1.1f * noise + 0.5f * mid)) * tail_window(i, n));
  }
}
// Six band-limited pulses at the drum-machine cymbal ratios.
static float metal(float ph[6], float pitch) {
  static const float hz[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
  float m = 0;
  for (int o = 0; o < 6; o++) m += osc_pulse(&ph[o], hz[o] * pitch / SR, 0.5f);
  return m / 6.0f;
}
static void k_metal(sy_ctx_t *c, int s, float a, float decay, float len, float pitch, float metal_mix, float hp_hz) {
  svf_t hp = {0}, bp = {0}, lp = {0};
  float ph[6] = { 0.05f, 0.2f, 0.37f, 0.55f, 0.7f, 0.88f };
  svf_set(&hp, hp_hz, 0.8f);
  svf_set(&bp, fmaxf(700.0f, hp_hz * 0.42f), 0.85f);
  svf_set(&lp, fminf(14000.0f, hp_hz * 2.8f), 0.7f);
  for (int i = 0, n = (int)(len * SR); i < n; i++) {
    float nse = sy_rnd(c);
    float x = metal_mix * 2.2f * metal(ph, pitch) + (1.0f - metal_mix) * nse;
    float y = 0.48f * svf_run(&hp, x, F_HP) + 0.8f * svf_run(&bp, nse, F_BP);
    sy_mix(c, s + i, a * 0.45f * svf_run(&lp, y, F_LP) * expf(-i / SR * decay) * fmin1(i / (0.0005f * SR)) * tail_window(i, n));
  }
}
static void k_clap(sy_ctx_t *c, const kit_t *k, int s, float a) {
  svf_t bp = {0};
  svf_set(&bp, k->cfc, 1.6f);
  for (int i = 0, n = (int)(0.32f * SR); i < n; i++) {
    float t = i / SR, e = t < 0.033f ? expf(-fmodf(t, 0.011f) * 200.0f) : expf(-(t - 0.033f) * k->cdecay);
    sy_mix(c, s + i, a * 2.4f * svf_run(&bp, sy_rnd(c), F_BP) * e * tail_window(i, n));
  }
}
// Two decaying sines plus an optional noise snap: rim, clave, conga, cowbell-like knocks.
static void k_knock(sy_ctx_t *c, int s, float a, float hz1, float hz2, float decay, float noise) {
  sine_t o1, o2;
  sine_init(&o1, hz1, 0); sine_init(&o2, hz2, 0);
  for (int i = 0, n = (int)(6.0f / decay * SR); i < n; i++) {
    float t = i / SR;
    sy_mix(c, s + i, a * ((0.6f * sine_next(&o1) + 0.4f * sine_next(&o2)) * expf(-t * decay) + noise * sy_rnd(c) * expf(-t * 300.0f)));
  }
}
static void k_cowbell(sy_ctx_t *c, int s, float a, float tune) {
  svf_t bp = {0};
  float p1 = 0, p2 = 0;
  svf_set(&bp, 2640.0f * tune, 2.5f);
  for (int i = 0, n = (int)(0.3f * SR); i < n; i++) {
    float t = i / SR, x = osc_pulse(&p1, 540.0f * tune / SR, 0.5f) + osc_pulse(&p2, 800.0f * tune / SR, 0.5f);
    sy_mix(c, s + i, a * 0.5f * svf_run(&bp, x, F_BP) * (0.7f * expf(-t * 40.0f) + 0.3f * expf(-t * 11.0f)) * tail_window(i, n));
  }
}
static void k_shaker(sy_ctx_t *c, int s, float a, float hp_hz, float attack, float decay) {
  svf_t hp = {0};
  svf_set(&hp, hp_hz, 0.9f);
  for (int i = 0, n = (int)((attack + 5.0f / decay) * SR); i < n; i++) {
    float t = i / SR, e = t < attack ? t / attack : expf(-(t - attack) * decay);
    sy_mix(c, s + i, a * 0.5f * svf_run(&hp, sy_rnd(c), F_HP) * e);
  }
}

void sy_hit(sy_ctx_t *c, sy_kit_t kit, char lane, int s, float a) {
  const kit_t *k = &kKits[kit < KIT_COUNT ? kit : KIT_909];
  switch (lane) {
    case 'k': k_kick(c, k, s, a);                                                    break;
    case 's': k_snare(c, k, s, a);                                                   break;
    case 'h': k_metal(c, s, a * 1.8f, k->hdecay, 0.12f, 1.0f, k->hmetal, k->hhp);    break;
    case 'o': k_metal(c, s, a * 1.8f, k->odecay, 0.45f, 1.0f, k->hmetal, k->hhp);    break;
    case 'c': k_clap(c, k, s, a);                                                    break;
    case 'r': k_knock(c, s, a * 0.6f, 1750.0f * k->tune, 480.0f * k->tune, 80.0f, 0.5f);       break; // rim
    case 'w': k_knock(c, s, a * 0.55f, 2450.0f * k->tune, 2460.0f * k->tune, 55.0f, 0.1f);      break; // clave / wood
    case 'g': k_knock(c, s, a * 0.7f, 330.0f * k->tune, 345.0f * k->tune, 20.0f, 0.25f);       break; // high conga
    case 'G': k_knock(c, s, a * 0.7f, 215.0f * k->tune, 222.0f * k->tune, 15.0f, 0.2f);        break; // low conga
    case 'T': sy_tom(c, s, 215.0f * k->tune, a * 0.7f);                                                break;
    case 't': sy_tom(c, s, 155.0f * k->tune, a * 0.7f);                                                break;
    case 'l': sy_tom(c, s, 105.0f * k->tune, a * 0.7f);                                                break;
    case 'b': k_cowbell(c, s, a, k->tune);                                                      break;
    case 'y': k_metal(c, s, a * 1.5f, 2.6f, 1.6f, 1.0f, 0.45f, 3400.0f);                        break; // crash
    case 'd': k_metal(c, s, a * 1.1f, 9.0f, 0.5f, 1.45f, 0.9f, 4200.0f);                        break; // ride
    case 'm': k_shaker(c, s, a * 0.9f, 5500.0f, 0.012f, 55.0f);                                 break; // shaker
    case 'a': k_metal(c, s, a * 1.4f, 22.0f, 0.2f, 2.2f, 0.5f, 4600.0f);                        break; // tambourine
    case 'z': sy_sweep_tone(c, s, (int)(0.25f * SR), 3200.0f * k->tune, 110.0f, 0.5f, a * 0.7f, 14.0f); break; // zap
    default:
      fprintf(stderr, "[synth] unknown drum lane '%c'\n", lane);
      fflush(stderr);
  }
}

void sy_drums(sy_ctx_t *c, sy_kit_t kit, const char *lanes) {
  double step = c->bar / 16.0;
  int steps = (int)(c->n / step + 0.5);
  for (const char *p = lanes; p && *p;) {
    while (*p == ' ') p++;
    if (!p[0] || p[1] != '=') break;
    char lane = p[0];
    const char *pat = p + 2;
    int len = (int)strcspn(pat, " ");
    for (int i = 0; len > 0 && i < steps; i++) {
      int s = (int)((i + (i & 1 ? c->swing : 0.0f)) * step), half = (int)(step / 2), third = (int)(step / 3);
      switch (pat[i % len]) {
        case 'x': sy_hit(c, kit, lane, s, 0.9f);  break;
        case 'o': sy_hit(c, kit, lane, s, 0.45f); break;
        case 'X': sy_hit(c, kit, lane, s, 1.1f);  break;
        case 'r': sy_hit(c, kit, lane, s, 0.8f); sy_hit(c, kit, lane, s + half, 0.6f); break;
        case 'R': sy_hit(c, kit, lane, s, 0.8f); sy_hit(c, kit, lane, s + third, 0.6f); sy_hit(c, kit, lane, s + 2 * third, 0.7f); break;
      }
    }
    p = pat + len;
  }
}

// ── Starter pitched voices ───────────────────────────────────────────────
static void v_piano(sy_ctx_t *c, int s, int g, float hz, float a) {
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
      sy_mix(c, s + i, a * hamp[h] * 0.33f * sinf((float)ph) * e);
    }
  }
  float tri = 0.3f, prev = 0;
  for (int i = 0; i < n; i++) {
    float t = i / SR, e = expf(-t * 2.0f) * fmin1(i / (0.003f * SR));
    if (i > g) e *= expf(-(i - g) / SR * 9.0f);
    sy_mix(c, s + i, a * 0.16f * osc_tri(&tri, hz / SR) * e);
  }
  for (int i = 0, len = (int)(0.012f * SR); i < len; i++) { // hammer
    float x = sy_rnd(c), y = x - prev;
    prev = x;
    sy_mix(c, s + i, a * 0.2f * y * (1.0f - (float)i / len));
  }
}
static void v_bass(sy_ctx_t *c, int s, int g, float hz, float a, bool bright) {
  int n = g + (int)(0.06f * SR);
  float tri = 0.15f, pul = 0.4f, saw = 0.72f, sub = 0.05f;
  svf_t lp = {0};
  for (int i = 0; i < n; i++) {
    float t = i / SR;
    float e = fmin1(i / (0.004f * SR)) * (0.6f + 0.4f * expf(-t * 6.0f)) * (i < g ? 1.0f : expf(-(i - g) / SR * 60.0f));
    if (!(i & 15)) svf_set(&lp, bright ? 420.0f + 1500.0f * expf(-t * 6.0f) : 70.0f + 180.0f * expf(-t * 6.0f), 0.85f);
    float x = bright ? osc_chans(&tri, &pul, &saw, hz / SR, 0.4f, 0.55f, 0.4f, 0.5f) + 0.3f * osc_tri(&sub, hz * 0.5f / SR)
                     : osc_tri(&tri, hz / SR);
    sy_mix(c, s + i, a * e * svf_run(&lp, x * 0.55f, F_LP));
  }
}
static void v_pluck(sy_ctx_t *c, int s, int g, float hz, float a, float damp) {
  int N = (int)(SR / hz), n = (int)((damp > 0.99f ? 1.4f : 0.5f) * SR);
  float d[4096], prev = 0;
  if (N < 2) N = 2;
  if (N > 4096) N = 4096;
  for (int i = 0; i < N; i++) { float x = sy_rnd(c); d[i] = 0.5f * (x + prev); prev = x; }
  for (int i = 0, p = 0; i < n; i++) {
    float y = d[p], nx = d[(p + 1) % N];
    d[p] = damp * 0.5f * (y + nx);
    p = (p + 1) % N;
    sy_mix(c, s + i, a * 0.9f * y * (i > g ? expf(-(i - g) / SR * 5.0f) : 1.0f));
  }
}
static void v_acid(sy_ctx_t *c, const sy_note_t *n) {
  int len = n->g + (int)(0.05f * SR);
  float saw = 0.2f, sub = 0.6f;
  svf_t lp = {0};
  for (int i = 0; i < len; i++) {
    float t = i / SR, hz = glide(n, t, 0.06f), top = n->accent ? 4600.0f : 3000.0f;
    if (!(i & 15)) svf_set(&lp, 280.0f + top * expf(-t * 8.0f), n->accent ? 2.8f : 2.0f);
    float y = svf_run(&lp, osc_saw(&saw, hz / SR), F_LP) + 0.5f * osc_tri(&sub, hz * 0.5f / SR);
    sy_mix(c, n->s + i, n->a * 0.5f * y * fmin1(i / (0.003f * SR)) * (i < n->g ? 1.0f : expf(-(i - n->g) / SR * 80.0f)));
  }
}
static void v_pad(sy_ctx_t *c, int s, int g, float hz, float a) {
  int n = g + (int)(0.5f * SR);
  float tri[3] = { 0.1f, 0.4f, 0.75f }, pul[3] = { 0.2f, 0.55f, 0.85f }, saw[3] = { 0.05f, 0.3f, 0.6f };
  const float det[3] = { 1.0f, 1.006f, 0.994f };
  svf_t lp = {0};
  for (int i = 0; i < n; i++) {
    float t = i / SR, x = 0;
    if (!(i & 15)) svf_set(&lp, 1500.0f + 450.0f * sinf(TAU * 0.15f * t), 0.75f);
    for (int o = 0; o < 3; o++) x += osc_chans(&tri[o], &pul[o], &saw[o], hz * det[o] / SR, 0.35f, 0.5f, 0.22f, 0.4f);
    sy_mix(c, s + i, a * 0.22f * svf_run(&lp, x, F_LP) * fmin1(t / 0.3f) * (i < g ? 1.0f : expf(-(i - g) / SR * 5.0f)));
  }
}
static void v_arp(sy_ctx_t *c, int s, float hz, float a) {
  float tri = 0.2f, pul = 0.55f, saw = 0.1f;
  svf_t lp = {0};
  svf_set(&lp, 2400.0f, 0.9f);
  for (int i = 0, n = (int)(0.3f * SR); i < n; i++) {
    float x = osc_chans(&tri, &pul, &saw, hz / SR, 0.3f, 0.45f, 0.7f, 0.25f);
    sy_mix(c, s + i, a * 0.4f * svf_run(&lp, x, F_LP) * expf(-i / SR * 11.0f) * fmin1(i / (0.002f * SR)));
  }
}
void sy_riser(sy_ctx_t *c, int len) {
  float low = 0, band = 0;
  for (int i = 0; i < len; i++) {
    float u = (float)i / len, f = 2.0f * sinf(SY_PI * (300.0f + 8700.0f * u * u) / SR);
    float hi = sy_rnd(c) - low - 0.35f * band;
    band += f * hi;
    low += f * band;
    sy_mix(c, i, 0.35f * band * u * u);
  }
}
void sy_organ_note(sy_ctx_t *c, int s, int g, float hz, float a) {
  static const float hamp[4] = { 1.0f, 0.55f, 0.28f, 0.16f };
  int n = g + (int)(0.25f * SR);
  for (int h = 0; h < 4; h++) {
    float f = hz * (float)(h * 2 + 1);
    double ph = 0;
    if (f > SR / 2.2f) break;
    for (int i = 0; i < n; i++, ph += TAU * f / SR) {
      float e = fmin1(i / (0.012f * SR)) * (i < g ? 1.0f : expf(-(i - g) / SR * 5.0f));
      sy_mix(c, s + i, a * hamp[h] * 0.28f * sinf((float)ph) * e);
    }
  }
}
void sy_vox_ah(sy_ctx_t *c, int s, float hz, float a, float len) {
  double fr = 0; float l1 = 0, b1 = 0, l2 = 0, b2 = 0;
  float f1 = 2.0f * sinf(SY_PI * 640.0f / SR), f2 = 2.0f * sinf(SY_PI * 1400.0f / SR);
  for (int i = 0, n = (int)(len * SR); i < n; i++, fr += hz / SR) {
    float t = i / SR, saw = 2.0f * (float)(fr - floor(fr)) - 1.0f;
    float h1 = saw - l1 - 0.18f * b1, h2 = saw - l2 - 0.22f * b2;
    b1 += f1 * h1; l1 += f1 * b1; b2 += f2 * h2; l2 += f2 * b2;
    sy_mix(c, s + i, a * 0.35f * (l1 + 0.6f * l2) * fmin1(i / (0.006f * SR)) * expf(-t * 3.5f));
  }
}
void sy_noise_zip(sy_ctx_t *c, int s, int n, int up) {
  float prev = 0, lp = 0;
  for (int i = 0; i < n; i++) {
    float u = (float)i / (float)n, fc = up ? 400.0f + 7000.0f * u * u : 7400.0f - 6800.0f * u;
    float k = 1.0f - expf(-TAU * fc / SR), x = sy_rnd(c), y;
    lp += k * (x - lp);
    y = x - lp - 0.3f * prev;
    prev = x - lp;
    sy_mix(c, s + i, 0.55f * y * sinf(SY_PI * u));
  }
}
void sy_noise_bed(sy_ctx_t *c, int len, float rise) {
  float low = 0, band = 0;
  for (int i = 0; i < len; i++) {
    float u = (float)i / (float)len, fc = rise > 0 ? 200.0f + 4000.0f * u : 4200.0f - 3800.0f * u;
    float f = 2.0f * sinf(SY_PI * fc / SR), hi = sy_rnd(c) - low - 0.4f * band;
    band += f * hi; low += f * band;
    sy_mix(c, i, 0.3f * band * (rise > 0 ? u : 0.35f + 0.65f * (1.0f - u)));
  }
}
void sy_impact(sy_ctx_t *c, int s) {
  double ph = 0;
  for (int i = 0, n = (int)(0.6f * SR); i < n; i++) {
    float t = i / SR;
    ph += TAU * (90.0f * expf(-t * 4.0f) + 40.0f) / SR;
    sy_mix(c, s + i, 0.8f * sinf((float)ph) * expf(-t * 3.0f) + 0.45f * sy_rnd(c) * expf(-t * 18.0f));
  }
}

// ── Keys ─────────────────────────────────────────────────────────────────
// Decaying harmonics: `count` partials at `mult`, level `amp`, decay `dec` (1/s).
static void v_partials(sy_ctx_t *c, const sy_note_t *n, int count, const float *mult, const float *amp, const float *dec,
                       float attack, float release, float tail, float trem_hz, float trem) {
  int len = n->g + (int)(tail * SR);
  sine_t lfo;
  for (int h = 0; h < count; h++) {
    float f = n->hz * mult[h], e = n->a * amp[h], ek = expf(-dec[h] / SR), rel = expf(-release / SR);
    sine_t o;
    if (f > SR / 2.3f) break;
    sine_init(&o, f, 0.3f * h);
    sine_init(&lfo, trem_hz, 0);
    for (int i = 0; i < len && e > 1e-5f; i++) {
      sy_mix(c, n->s + i, sine_next(&o) * e * fmin1(i / (attack * SR)) * (1.0f + trem * sine_next(&lfo)));
      e *= i < n->g ? ek : ek * rel;
    }
  }
}
static void v_hpiano(sy_ctx_t *c, const sy_note_t *n) {
  static const float mult[7] = { 1.0f, 2.001f, 3.003f, 4.006f, 5.01f, 6.015f, 7.02f };
  static const float amp[7]  = { 0.42f, 0.25f, 0.18f, 0.15f, 0.12f, 0.10f, 0.08f };
  static const float dec[7]  = { 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f };
  float tri = 0.25f, prev = 0;
  v_partials(c, n, 7, mult, amp, dec, 0.002f, 11.0f, 0.45f, 1.0f, 0.0f);
  for (int i = 0, len = n->g + (int)(0.4f * SR); i < len; i++) {
    float e = expf(-i / SR * 3.0f) * fmin1(i / (0.002f * SR));
    if (i > n->g) e *= expf(-(i - n->g) / SR * 11.0f);
    sy_mix(c, n->s + i, n->a * 0.14f * osc_tri(&tri, n->hz / SR) * e);
  }
  for (int i = 0, len = (int)(0.01f * SR); i < len; i++) {
    float x = sy_rnd(c), y = x - prev;
    prev = x;
    sy_mix(c, n->s + i, n->a * 0.16f * y * (1.0f - (float)i / len));
  }
}
static void v_organ(sy_ctx_t *c, const sy_note_t *n) {
  static const float mult[6] = { 1, 2, 3, 4, 6, 8 }, amp[6] = { 0.30f, 0.24f, 0.15f, 0.10f, 0.06f, 0.04f }, dec[6] = {0};
  static const float pmult[1] = { 2 }, pamp[1] = { 0.30f }, pdec[1] = { 18.0f }; // percussion stop
  v_partials(c, n, 6, mult, amp, dec, 0.003f, 30.0f, 0.2f, 6.2f, 0.05f);
  v_partials(c, n, 1, pmult, pamp, pdec, 0.001f, 30.0f, 0.2f, 6.2f, 0.0f);
  for (int i = 0, len = (int)(0.01f * SR); i < len; i++) sy_mix(c, n->s + i, n->a * 0.1f * sy_rnd(c) * (1.0f - (float)i / len)); // key click
  float tri = 0.2f, pul = 0.55f, saw = 0.8f;
  int len = n->g + (int)(0.2f * SR);
  for (int i = 0; i < len; i++) {
    float t = i / SR, e = fmin1(i / (0.004f * SR)) * (i < n->g ? 1.0f : expf(-(i - n->g) / SR * 30.0f));
    float x = osc_chans(&tri, &pul, &saw, n->hz * 1.003f / SR, 0.5f, 0.45f, 0.32f, 0.16f);
    sy_mix(c, n->s + i, n->a * 0.22f * x * e * (1.0f + 0.04f * sinf(TAU * 6.1f * t)));
  }
}
static void v_vibes(sy_ctx_t *c, const sy_note_t *n) {
  static const float mult[3] = { 1, 4, 10 }, amp[3] = { 0.5f, 0.18f, 0.05f }, dec[3] = { 2.2f, 9.0f, 25.0f };
  v_partials(c, n, 3, mult, amp, dec, 0.002f, 7.0f, 0.8f, 5.5f, 0.25f);
}
static void v_upright(sy_ctx_t *c, const sy_note_t *n) {
  static const float mult[3] = { 1, 2, 3 }, amp[3] = { 0.6f, 0.25f, 0.12f }, dec[3] = { 4.5f, 9.0f, 14.0f };
  v_partials(c, n, 3, mult, amp, dec, 0.004f, 25.0f, 0.25f, 1.0f, 0.0f);
  for (int i = 0, len = (int)(0.008f * SR); i < len; i++) sy_mix(c, n->s + i, n->a * 0.08f * sy_rnd(c) * (1.0f - (float)i / len)); // finger
}
static void v_bleep(sy_ctx_t *c, const sy_note_t *n) {
  static const float mult[2] = { 1, 2 }, amp[2] = { 0.6f, 0.15f }, dec[2] = { 12.0f, 20.0f };
  v_partials(c, n, 2, mult, amp, dec, 0.001f, 40.0f, 0.1f, 1.0f, 0.0f);
}
// Two-operator FM. The index envelope sets the brightness; adec 0 sustains.
static void v_fm(sy_ctx_t *c, const sy_note_t *n, float ratio, float index, float idec, float ifloor,
                 float adec, float release, float tail, float trem) {
  int len = n->g + (int)(tail * SR);
  sine_t mod, lfo;
  double ph = 0, inc = TAU * n->hz / SR;
  float ie = index * (n->accent ? 1.5f : 1.0f), iek = expf(-idec / SR), e = 1, ek = expf(-adec / SR), rel = expf(-release / SR);
  sine_init(&mod, n->hz * ratio, 0);
  sine_init(&lfo, 4.6f, 0);
  for (int i = 0; i < len && e > 1e-4f; i++, ph += inc) {
    sy_mix(c, n->s + i, n->a * 0.6f * sinf((float)ph + (ie + ifloor) * sine_next(&mod)) * e * fmin1(i / (0.002f * SR)) * (1.0f + trem * sine_next(&lfo)));
    ie *= iek;
    e *= i < n->g ? ek : ek * rel;
  }
}

// ── Basses, leads, layers ────────────────────────────────────────────────
static void v_reese(sy_ctx_t *c, const sy_note_t *n) {
  int len = n->g + (int)(0.12f * SR);
  float p1 = 0, p2 = 0.37f, pul = 0.2f, tri = 0.55f;
  svf_t lp = {0};
  for (int i = 0; i < len; i++) {
    float t = i / SR;
    if (!(i & 15)) svf_set(&lp, 560.0f + 420.0f * sinf(TAU * 0.35f * t + n->step) + (n->accent ? 650.0f : 0.0f), 1.05f);
    float dt = n->hz / SR;
    float x = osc_saw(&p1, dt * 0.994f) + osc_saw(&p2, dt * 1.006f) + 0.4f * osc_pulse(&pul, dt, 0.45f);
    float y = svf_run(&lp, x * 0.45f, F_LP) + 0.4f * osc_tri(&tri, dt);
    sy_mix(c, n->s + i, n->a * y * env(i, n->g, 0.008f, 35.0f));
  }
}
// One filtered oscillator pair with an envelope on the cutoff: the workhorse
// behind the mono basses, leads, stabs and brass.
typedef struct {
  float det, oct, pulse;        // second saw detune ratio, octave-up saw level, pulse level
  float fc, fenv, frate, q;     // cutoff floor, envelope depth, envelope rate (negative = rising), resonance
  float attack, adec, release;  // amp attack seconds, decay rate, release rate
  float vib, drive, glide;
} mono_t;
static void v_mono(sy_ctx_t *c, const sy_note_t *n, const mono_t *m) {
  int len = n->g + (int)(5.0f / m->release * SR);
  float p0 = 0.12f, p1 = 0, p2 = 0.41f, p3 = 0.17f, p4 = 0.63f, e = 1, ek = expf(-m->adec / SR), vib = 1, pw = 0.5f;
  float depth = m->fenv * (n->accent ? 1.5f : 1.0f);
  svf_t lp = {0};
  for (int i = 0; i < len; i++) {
    float t = i / SR;
    if (!(i & 15)) {
      float fe = m->frate >= 0 ? expf(-t * m->frate) : 1.0f - expf(t * m->frate);
      svf_set(&lp, m->fc + depth * fe, m->q);
      vib = 1.0f + (t > 0.18f ? m->vib * sinf(TAU * 5.5f * t) : 0.0f);
      pw = 0.5f + 0.3f * sinf(TAU * 0.9f * t);
    }
    float dt = glide(n, t, m->glide) * vib / SR;
    float x = osc_saw(&p1, dt) + 0.5f * osc_tri(&p0, dt);
    x += osc_saw(&p2, dt * (m->det > 0 ? 1.0f + m->det : 1.007f)) * (m->det > 0 ? 1.0f : 0.35f);
    if (m->oct > 0) x += m->oct * osc_saw(&p3, dt * 2.0f);
    x += (m->pulse > 0 ? m->pulse : 0.28f) * osc_pulse(&p4, dt, pw);
    x += 0.03f * sy_rnd(c);
    x = svf_run(&lp, x * 0.4f, F_LP);
    if (m->drive > 0) x = tanhf(x * m->drive);
    sy_mix(c, n->s + i, n->a * 0.8f * x * e * env(i, n->g, m->attack, m->release));
    e *= ek;
  }
}
static const mono_t kMoog   = { 0,       0,    0.5f,  160, 1900, 9.0f,  2.2f, 0.003f, 0,    45, 0,      0,    0.05f };
static const mono_t kSquare = { 0,       0,    1.0f,  700, 3200, 6.0f,  1.0f, 0.002f, 0.6f, 45, 0,      0,    0.05f };
static const mono_t kSaw    = { 0.006f,  0,    0,    1200, 2600, 4.0f,  1.3f, 0.004f, 0,    30, 0.005f, 0,    0.06f };
static const mono_t kStab   = { 0.004f,  0.5f, 0.7f,  500, 5200, 16.0f, 1.7f, 0.001f, 6.5f, 35, 0,      0,    0    };
static const mono_t kBrass  = { 0.005f,  0,    0,     350, 3000, -28.f, 1.1f, 0.020f, 0.5f, 20, 0.004f, 0,    0    };

static void v_stack(sy_ctx_t *c, const sy_note_t *n, int count, const float *det, float fc, float fenv, float frate,
                    float attack, float release, float gain) {
  int len = n->g + (int)(5.0f / release * SR);
  float ph[7], sub = 0.23f;
  svf_t lp = {0};
  for (int o = 0; o < count; o++) ph[o] = 0.5f + 0.5f * sy_rnd(c) * 0.999f;
  for (int i = 0; i < len; i++) {
    float x = 0.45f * osc_tri(&sub, n->hz / SR);
    if (!(i & 15)) svf_set(&lp, fc + fenv * expf(-i / SR * frate), 0.9f);
    for (int o = 0; o < count; o++) x += osc_saw(&ph[o], n->hz * (1.0f + det[o]) / SR);
    sy_mix(c, n->s + i, n->a * gain * svf_run(&lp, x, F_LP) * env(i, n->g, attack, release));
  }
}
static const float kSuperDet[7]  = { 0, 0.0035f, -0.0035f, 0.0085f, -0.0085f, 0.014f, -0.014f };
static const float kStringDet[4] = { -0.007f, -0.002f, 0.003f, 0.008f };

// Detuned saws an octave apart with a pitch scoop into every note.
static void v_hoover(sy_ctx_t *c, const sy_note_t *n) {
  int len = n->g + (int)(0.5f * SR);
  float pa = 0, pb = 0.3f, pc = 0.6f, bend = 1, wob = 0, pw = 0.5f;
  svf_t lp = {0};
  svf_set(&lp, 5200.0f, 1.1f);
  for (int i = 0; i < len; i++) {
    float t = i / SR;
    if (!(i & 15)) {
      bend = powf(2.0f, -0.25f * expf(-t * 14.0f));
      wob = 0.011f * sinf(TAU * 5.7f * t);
      pw = 0.5f + 0.3f * sinf(TAU * 1.3f * t);
    }
    float f = glide(n, t, 0.08f) * bend / SR;
    float x = osc_saw(&pa, f * (1.0f + wob)) + 0.8f * osc_saw(&pb, f * 2.0f * (1.0f - wob)) + 0.7f * osc_pulse(&pc, f * 0.5f, pw);
    sy_mix(c, n->s + i, n->a * 0.7f * tanhf(1.6f * svf_run(&lp, x * 0.4f, F_LP)) * env(i, n->g, 0.006f, 9.0f));
  }
}
static void v_whistle(sy_ctx_t *c, const sy_note_t *n) {
  int len = n->g + (int)(0.25f * SR);
  double ph = 0;
  svf_t bp = {0};
  for (int i = 0; i < len; i++) {
    float t = i / SR, f = glide(n, t, 0.07f) * (1.0f + (t > 0.1f ? 0.006f * sinf(TAU * 5.5f * t) : 0.0f));
    if (!(i & 15)) svf_set(&bp, f, 10.0f);
    ph += TAU * f / SR;
    float v = sinf((float)ph) + 0.12f * sinf(2.0f * (float)ph) + 0.6f * svf_run(&bp, sy_rnd(c), F_BP);
    sy_mix(c, n->s + i, n->a * 0.6f * v * env(i, n->g, 0.025f, 20.0f));
  }
}

// ── Strings (Karplus-Strong) ─────────────────────────────────────────────
enum { POST_NONE, POST_WAH, POST_DIST };
static void v_string(sy_ctx_t *c, const sy_note_t *n, float damp, float seconds, bool bright, float release, int post) {
  int N = CLAMP((int)(SR / n->hz), 2, 4096), len = (int)(seconds * SR);
  float d[4096], prev = 0, lp1 = 0, lp2 = 0, e = 1, rel = expf(-release / SR);
  svf_t wah = {0};
  for (int i = 0; i < N; i++) { float x = sy_rnd(c); d[i] = bright ? x : 0.5f * (x + prev); prev = x; }
  for (int i = 0, p = 0; i < len && e > 1e-4f; i++) {
    float y = d[p], nx = d[(p + 1) % N];
    d[p] = damp * 0.5f * (y + nx);
    p = (p + 1) % N;
    if (post == POST_WAH) {
      if (!(i & 15)) svf_set(&wah, 450.0f + 1900.0f * sinf(SY_PI * fmin1(i / (0.2f * SR))), 3.5f);
      y = 3.0f * svf_run(&wah, y, F_BP);
    } else if (post == POST_DIST) {
      y = tanhf(y * 7.0f);
      lp1 += 0.36f * (y - lp1); lp2 += 0.36f * (lp1 - lp2);
      y = lp2 * 0.6f;
    }
    sy_mix(c, n->s + i, n->a * 0.9f * y * e);
    if (i > n->g) e *= rel;
  }
}

// ── Formant voices ───────────────────────────────────────────────────────
// First three formants of a vowel. Words are vowel strings ("iea" reads as
// "yeah"); a leading h adds a breath.
static const float *vowel(char ch) {
  static const float f[6][3] = {
    { 800, 1150, 2900 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 450, 800, 2830 }, { 325, 700, 2530 }, { 640, 1190, 2390 },
  };
  const char *keys = "aeiou", *p = ch ? strchr(keys, ch) : NULL;
  return f[p ? p - keys : 5];
}
static void v_vox(sy_ctx_t *c, const sy_note_t *n, bool robot) {
  const char *w = n->arg && n->arg[0] ? n->arg : "a";
  int words = 1, len = n->g + (int)(0.09f * SR);
  for (const char *p = w; *p; p++) words += *p == ' ';
  for (int k = n->step % words; k > 0; w++) if (*w == ' ') k--;
  bool breath = *w == 'h';
  if (breath) w++;
  int vowels = MAX(1, (int)strcspn(w, " "));
  float ph = 0, F[3] = {0};
  svf_t f[3] = {{0}};
  static const float q[3] = { 9, 11, 12 }, level[3] = { 1.0f, 0.6f, 0.3f };
  for (int i = 0; i < len; i++) {
    float t = i / SR;
    if (!(i & 15)) {
      float pos = fmin1((float)i / (float)MAX(1, n->g)) * (vowels - 1);
      int k = MIN((int)pos, vowels - 1), k1 = MIN(k + 1, vowels - 1);
      float mix = robot ? 0.0f : pos - k;
      for (int j = 0; j < 3; j++) {
        F[j] = vowel(w[k])[j] + (vowel(w[k1])[j] - vowel(w[k])[j]) * mix;
        svf_set(&f[j], F[j], q[j]);
      }
    }
    float hz = robot ? n->hz : n->hz * (1.0f + (t > 0.12f ? 0.012f * sinf(TAU * 5.5f * t) : 0.0f)) * (1.0f - 0.01f * t);
    float src = robot ? osc_pulse(&ph, hz / SR, 0.2f) : osc_saw(&ph, hz / SR);
    float x = src * (breath ? fmin1(t / 0.05f) : 1.0f) + sy_rnd(c) * (breath ? 0.9f * expf(-t * 30.0f) : 0.03f), y = 0;
    for (int j = 0; j < 3; j++) y += level[j] * q[j] * svf_run(&f[j], x, F_BP);
    sy_mix(c, n->s + i, n->a * 0.12f * y * env(i, n->g, 0.012f, 30.0f));
  }
}
static void v_choir(sy_ctx_t *c, const sy_note_t *n) {
  const float *F = vowel(n->arg && n->arg[0] ? n->arg[0] : 'a');
  int len = n->g + (int)(0.9f * SR);
  float ph[3] = { 0, 0.33f, 0.71f };
  static const float det[3] = { 0, 0.005f, -0.004f };
  svf_t f[3] = {{0}};
  for (int j = 0; j < 3; j++) svf_set(&f[j], F[j], 5.0f + j);
  for (int i = 0; i < len; i++) {
    float x = 0.04f * sy_rnd(c), y = 0;
    for (int o = 0; o < 3; o++) x += osc_saw(&ph[o], n->hz * (1.0f + det[o]) / SR) / 3.0f;
    for (int j = 0; j < 3; j++) y += (5.0f + j) * svf_run(&f[j], x, F_BP) / (1 + j);
    sy_mix(c, n->s + i, n->a * 0.2f * y * env(i, n->g, 0.14f, 5.0f));
  }
}

void sy_voice(sy_ctx_t *c, sy_inst_t inst, const sy_note_t *note) {
  sy_note_t n = *note;
  int strum = inst == I_PLUCK || inst == I_MUTE || inst == I_CLEAN || inst == I_WAH ? (int)(0.012f * SR)
            : inst == I_PIANO || inst == I_HPIANO || inst == I_EPIANO || inst == I_DIST ? (int)(0.003f * SR) : 0;
  n.s += n.idx * strum;
  switch (inst) {
    case I_PIANO:    v_piano(c, n.s, n.g, n.hz, n.a);                                       break;
    case I_BASS:     v_bass(c, n.s, n.g, n.hz, n.a, true);                                  break;
    case I_SUB:      v_bass(c, n.s, n.g, n.hz, n.a, false);                                 break;
    case I_PLUCK:    v_pluck(c, n.s, n.g, n.hz, n.a, 0.996f);                               break;
    case I_MUTE:     v_pluck(c, n.s, n.g, n.hz, n.a, 0.93f);                                break;
    case I_ACID:     v_acid(c, &n);                                                         break;
    case I_PAD:      v_pad(c, n.s, n.g, n.hz, n.a);                                         break;
    case I_ARP:      v_arp(c, n.s, n.hz, n.a);                                              break;
    case I_HPIANO:   v_hpiano(c, &n);                                                       break;
    case I_EPIANO:   v_fm(c, &n, 1.0f, 1.2f, 5.0f, 0.2f, 1.6f, 12.0f, 0.4f, 0.07f);         break;
    case I_ORGAN:    v_organ(c, &n);                                                        break;
    case I_VIBES:    v_vibes(c, &n);                                                        break;
    case I_BELL:     v_fm(c, &n, 3.5f, 2.6f, 3.2f, 0.25f, 2.4f, 5.0f, 1.0f, 0.0f);          break;
    case I_FMBASS:   v_fm(c, &n, 1.0f, 3.2f, 10.0f, 1.3f, 0.8f, 60.0f, 0.08f, 0.0f);        break;
    case I_DONK:     v_fm(c, &n, 2.0f, 4.5f, 28.0f, 0.7f, 6.0f, 70.0f, 0.08f, 0.0f);        break;
    case I_FMSEQ:    v_fm(c, &n, 1.414f, 3.0f, 12.0f, 0.0f, 8.0f, 40.0f, 0.1f, 0.0f);       break;
    case I_REESE:    v_reese(c, &n);                                                        break;
    case I_MOOG:     v_mono(c, &n, &kMoog);                                                 break;
    case I_SQUARE:   v_mono(c, &n, &kSquare);                                               break;
    case I_UPRIGHT:  v_upright(c, &n);                                                      break;
    case I_SAW:      v_mono(c, &n, &kSaw);                                                  break;
    case I_STAB:     v_mono(c, &n, &kStab);                                                 break;
    case I_BRASS:    v_mono(c, &n, &kBrass);                                                break;
    case I_SUPERSAW: v_stack(c, &n, 7, kSuperDet, 1300.0f, 5200.0f, 5.0f, 0.003f, 9.0f, 0.12f);   break;
    case I_STRINGS:  v_stack(c, &n, 4, kStringDet, 2600.0f, 0.0f, 1.0f, 0.16f, 5.5f, 0.16f);      break;
    case I_HOOVER:   v_hoover(c, &n);                                                       break;
    case I_CHOIR:    v_choir(c, &n);                                                        break;
    case I_WHISTLE:  v_whistle(c, &n);                                                      break;
    case I_PIZZ:     v_string(c, &n, 0.975f, 0.3f, false, 30.0f, POST_NONE);                break;
    case I_BLEEP:    v_bleep(c, &n);                                                        break;
    case I_WAH:      v_string(c, &n, 0.992f, 0.5f, true, 25.0f, POST_WAH);                  break;
    case I_DIST:     v_string(c, &n, 0.996f, 1.2f, false, 25.0f, POST_DIST);                break;
    case I_CLEAN:    v_string(c, &n, 0.990f, 0.6f, true, 12.0f, POST_NONE);                 break;
    case I_VOX:      v_vox(c, &n, false);                                                   break;
    case I_ROBOT:    v_vox(c, &n, true);                                                    break;
  }
}

static int note_midi(const char *s, int *len) {
  static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
  int m = base[(s[0] - 'A') % 7], i = 1;
  if (s[i] == '#') { m++; i++; } else if (s[i] == 'b') { m--; i++; }
  *len = i + 1;
  return m + 12 * (s[i] - '0' + 1);
}

#define SEQ_MAX_STEPS 256

void sy_seq(sy_ctx_t *c, const char *str, int spb, double hold, sy_inst_t inst, const char *arg) {
  const char *tok[SEQ_MAX_STEPS];
  double step = c->bar / (GR_BEATS_BAR * (double)spb);
  int count = 0, total = (int)(c->n / step + 0.5), sounded = 0;
  float last = 0;
  for (const char *p = str; p && *p && count < SEQ_MAX_STEPS;) {
    while (*p == ' ') p++;
    if (!*p) break;
    tok[count++] = p;
    p += strcspn(p, " ");
  }
  for (int k = 0; count > 0 && k < total; k++) {
    const char *p = tok[k % count], *end = p + strcspn(p, " ");
    int ties = 0;
    if (*p == '-' || *p == '_') continue;
    if (*p < 'A' || *p > 'G') {
      fprintf(stderr, "[synth] bad note step '%.*s' in \"%s\"\n", (int)(end - p), p, str);
      fflush(stderr);
      continue;
    }
    while (k + 1 + ties < total && *tok[(k + 1 + ties) % count] == '_') ties++;
    bool accent = memchr(p, '!', (size_t)(end - p)) != NULL, slide = memchr(p, '~', (size_t)(end - p)) != NULL;
    float first = 0;
    for (int ci = 0;; ci++) {
      int len, m = note_midi(p, &len);
      sy_note_t n = { (int)((k + (spb == 4 && (k & 1) ? c->swing : 0.0f)) * step), (int)((hold + ties) * step), ci, sounded,
                      sy_midi_hz(m), slide ? last : 0.0f, accent ? 1.0f : 0.8f, accent, arg };
      p += len;
      if (!ci) first = n.hz;
      sy_voice(c, inst, &n);
      if (*p != '+') break;
      p++;
    }
    last = first;
    sounded++;
  }
}

// ── Effects primitives for recipes ───────────────────────────────────────
void sy_sweep_tone(sy_ctx_t *c, int s, int len, float hz0, float hz1, float curve, float a, float decay) {
  double ph = 0;
  for (int i = 0; i < len; i++) {
    float u = (float)i / (float)len;
    ph += TAU * hz0 * powf(hz1 / hz0, powf(u, curve)) / SR;
    sy_mix(c, s + i, a * sinf((float)ph) * expf(-i / SR * decay) * fmin1(i / (0.002f * SR)) * tail_window(i, len));
  }
}
void sy_noise_sweep(sy_ctx_t *c, int s, int len, float hz0, float hz1, float q, float a, float swell) {
  svf_t bp = {0};
  for (int i = 0; i < len; i++) {
    float u = (float)i / (float)len, e = swell > 0 ? powf(u, swell) : swell < 0 ? powf(1.0f - u, -swell) : 1.0f;
    if (!(i & 15)) svf_set(&bp, hz0 * powf(hz1 / hz0, u), q);
    sy_mix(c, s + i, a * q * svf_run(&bp, sy_rnd(c), F_BP) * e * fmin1(i / (0.004f * SR)) * tail_window(i, len));
  }
}
void sy_scratch(sy_ctx_t *c, int s, int len, float turns, float speed) {
  enum { SRC = GR_SAMPLE_RATE / 2 };
  float *src = malloc(SRC * sizeof(float)), ph = 0, prev = 0, gain = 0;
  svf_t f1 = {0}, f2 = {0};
  if (!src) { fprintf(stderr, "[synth] scratch source allocation failed frames=%d\n", SRC); fflush(stderr); return; }
  svf_set(&f1, 760.0f, 6.0f); svf_set(&f2, 1250.0f, 7.0f);
  for (int i = 0; i < SRC; i++) { // the "record": a held vowel with a breathy onset
    float x = osc_saw(&ph, 185.0f / SR) + 0.25f * sy_rnd(c);
    src[i] = 2.4f * svf_run(&f1, x, F_BP) + 1.7f * svf_run(&f2, x, F_BP) + 0.3f * sy_rnd(c) * expf(-i / SR * 60.0f);
  }
  for (int i = 0; i < len; i++) {
    float u = (float)i / (float)len, pos;
    if (turns > 0)       pos = speed * len / (2.0f * turns) * 0.5f * (1.0f - cosf(TAU * turns * u)); // back and forth
    else if (turns == 0) pos = speed * len * (u - 0.5f * u * u);                                      // forward, slowing to a stop
    else                 pos = speed * len * 0.5f * (1.0f - u) * (1.0f - u);                          // spin back
    float rate = fabsf(pos - prev), wrapped = fmodf(pos, (float)(SRC - 1));
    int k = (int)wrapped;
    prev = pos;
    gain += 0.02f * (fmin1(rate * 2.0f) - gain); // a still record is silent
    sy_mix(c, s + i, 0.6f * (src[k] + (src[k + 1] - src[k]) * (wrapped - k)) * gain * tail_window(i, len));
  }
  free(src);
}

// ── Post effects ─────────────────────────────────────────────────────────
static float buffer_peak(const float *b, int n) {
  float peak = 1e-6f;
  for (int i = 0; i < n; i++) if (fabsf(b[i]) > peak) peak = fabsf(b[i]);
  return peak;
}
static float *buffer_copy(const float *b, int n) {
  float *copy = malloc((size_t)n * sizeof(float));
  if (!copy) { fprintf(stderr, "[synth] effect buffer allocation failed frames=%d\n", n); fflush(stderr); return NULL; }
  memcpy(copy, b, (size_t)n * sizeof(float));
  return copy;
}
static void fx_drive(float *b, int n) {
  float peak = buffer_peak(b, n);
  for (int i = 0; i < n; i++) b[i] = tanhf(2.5f * b[i] / peak) * peak;
}
static void fx_crush(float *b, int n) {
  float peak = buffer_peak(b, n), held = 0, lp = 0;
  for (int i = 0; i < n; i++) {
    if (!(i & 1)) held = floorf(b[i] / peak * 48.0f + 0.5f) / 48.0f * peak;
    lp += 0.6f * (held - lp);
    b[i] = lp;
  }
}
static void fx_gate(float *b, int n, double bar) {
  double step = bar / 16.0;
  for (int i = 0; i < n; i++) {
    float u = (float)(fmod(i, step) / step);
    b[i] *= fmin1(u / 0.04f) * fmin1(fmaxf(0.0f, 0.6f - u) / 0.04f);
  }
}
static void fx_chorus(float *b, int n) {
  float *dry = buffer_copy(b, n);
  if (!dry) return;
  for (int i = 0; i < n; i++) {
    float t = i / SR, d1 = (0.012f + 0.004f * sinf(TAU * 0.6f * t)) * SR, d2 = (0.017f + 0.005f * sinf(TAU * 0.83f * t + 1.0f)) * SR;
    int a = i - (int)d1, k = i - (int)d2;
    b[i] = 0.7f * dry[i] + 0.4f * (a > 0 ? dry[a - 1] + (dry[a] - dry[a - 1]) * (1.0f - (d1 - (int)d1)) : 0.0f)
                         + 0.4f * (k > 0 ? dry[k - 1] + (dry[k] - dry[k - 1]) * (1.0f - (d2 - (int)d2)) : 0.0f);
  }
  free(dry);
}
static void fx_echo(float *b, int n, double bar) {
  int d = (int)(bar * 3.0 / 16.0);
  float *wet = calloc((size_t)n, sizeof(float)), lp = 0;
  if (!wet) { fprintf(stderr, "[synth] echo allocation failed frames=%d\n", n); fflush(stderr); return; }
  for (int i = d; i < n; i++) {
    lp += 0.35f * (wet[i - d] - lp); // each repeat is duller
    wet[i] = b[i - d] + 0.45f * lp;
  }
  for (int i = 0; i < n; i++) b[i] += 0.32f * wet[i];
  free(wet);
}
// Four damped comb filters into two all-pass stages (a small Schroeder reverb).
static void fx_reverb(float *b, int n, float seconds, float mix) {
  static const int comb[4] = { 1116, 1188, 1277, 1356 }, pass[2] = { 556, 441 };
  float *line[6], store[4] = {0}, fb[4];
  int pos[6] = {0};
  for (int k = 0; k < 6; k++) {
    line[k] = calloc((size_t)(k < 4 ? comb[k] : pass[k - 4]), sizeof(float));
    if (!line[k]) {
      fprintf(stderr, "[synth] reverb allocation failed line=%d\n", k);
      fflush(stderr);
      while (k-- > 0) free(line[k]);
      return;
    }
  }
  for (int k = 0; k < 4; k++) fb[k] = powf(10.0f, -3.0f * comb[k] / (seconds * SR));
  for (int i = 0; i < n; i++) {
    float x = b[i], wet = 0;
    for (int k = 0; k < 4; k++) {
      float y = line[k][pos[k]];
      store[k] = y * 0.7f + store[k] * 0.3f;
      line[k][pos[k]] = x + store[k] * fb[k];
      pos[k] = (pos[k] + 1) % comb[k];
      wet += y;
    }
    for (int k = 4; k < 6; k++) {
      float y = line[k][pos[k]];
      line[k][pos[k]] = wet + y * 0.5f;
      pos[k] = (pos[k] + 1) % pass[k - 4];
      wet = y - wet;
    }
    b[i] = x + mix * 0.25f * wet;
  }
  for (int k = 0; k < 6; k++) free(line[k]);
}
static void fx_sweep(float *b, int n, int loop, bool up) {
  svf_t lp = {0};
  for (int i = 0; i < n; i++) {
    float u = fmin1((float)i / (float)loop);
    if (!(i & 15)) svf_set(&lp, 250.0f * powf(40.0f, up ? u : 1.0f - u), 1.4f);
    b[i] = svf_run(&lp, b[i], F_LP);
  }
}
static void fx_pump(float *b, int n, double bar) {
  double beat = bar / 4.0;
  for (int i = 0; i < n; i++) {
    float u = fmin1((float)(fmod(i, beat) / beat) / 0.55f);
    b[i] *= 0.15f + 0.85f * u * sqrtf(u);
  }
}

void sy_fx(sy_ctx_t *c, uint32_t fx) {
  int n = c->n + SY_TAIL;
  if (fx & X_DRIVE)  fx_drive(c->buf, n);
  if (fx & X_CRUSH)  fx_crush(c->buf, n);
  if (fx & X_GATE)   fx_gate(c->buf, n, c->bar);
  if (fx & X_CHORUS) fx_chorus(c->buf, n);
  if (fx & X_ECHO)   fx_echo(c->buf, n, c->bar);
  if (fx & X_ROOM)   fx_reverb(c->buf, n, 0.45f, 0.3f);
  if (fx & X_HALL)   fx_reverb(c->buf, n, 1.9f, 0.28f);
  if (fx & (X_UP | X_DOWN)) fx_sweep(c->buf, n, c->n, (fx & X_UP) != 0);
  if (fx & X_PUMP)   fx_pump(c->buf, n, c->bar);
}

#undef SR
#undef TAU

#ifndef __GROOVE_SAMPLE_LEVELS_H__
#define __GROOVE_SAMPLE_LEVELS_H__

#include "groove.h"

typedef struct { int gain_db; float peak, active_rms, window_rms; } level_profile_t;
typedef struct { float peak, rms, active_rms, window_rms; } level_measure_t;

static const level_profile_t kSampleLevels[CAT_COUNT] = {
  [CAT_DRUMS]   = {  0, .85f, .205f, .30f },
  [CAT_KICK]    = {  0, .85f, .205f, .30f },
  [CAT_SNARE]   = { -2, .65f, .100f, .18f },
  [CAT_HAT]     = { -2, .34f, .040f, .06f },
  [CAT_CLAP]    = { -2, .65f, .090f, .16f },
  [CAT_CYMBAL]  = { -4, .54f, .070f, .14f },
  [CAT_PERC]    = { -3, .60f, .100f, .16f },
  [CAT_FILL]    = {  0, .85f, .205f, .30f },
  [CAT_BASS]    = {  0, .85f, .220f, .30f },
  [CAT_KEYS]    = { -5, .48f, .120f, .18f },
  [CAT_ORGAN]   = { -5, .48f, .115f, .16f },
  [CAT_GUITAR]  = { -2, .68f, .170f, .23f },
  [CAT_SYNTH]   = { -5, .48f, .130f, .19f },
  [CAT_PAD]     = { -4, .54f, .140f, .20f },
  [CAT_STAB]    = { -4, .54f, .115f, .18f },
  [CAT_VOX]     = { -2, .68f, .180f, .25f },
  [CAT_SCRATCH] = { -4, .54f, .130f, .20f },
  [CAT_FX]      = { -3, .61f, .160f, .22f },
};

static inline level_measure_t sample_measure(const float *pcm, int frames) {
  level_measure_t m = {0};
  if (frames < 1) return m;
  int window = MIN(frames, GR_SAMPLE_RATE * 4 / 10), hop = GR_SAMPLE_RATE / 10, count = 0;
  double power = 0, rolling = 0, max_power = 0, hop_power = 0, max_hop = 0, active = 0;
  for (int i = 0; i < frames; i++) {
    double x = pcm[i];
    m.peak = fmaxf(m.peak, fabsf(pcm[i]));
    power += x * x; rolling += x * x; hop_power += x * x;
    if (i >= window) { double y = pcm[i - window]; rolling -= y * y; }
    if (i >= window - 1) max_power = fmax(max_power, rolling / window);
    if ((i + 1) % hop == 0 || i == frames - 1) {
      max_hop = fmax(max_hop, hop_power / (i % hop + 1)); hop_power = 0;
    }
  }
  for (int at = 0; at < frames; at += hop) {
    int n = MIN(hop, frames - at);
    double energy = 0;
    for (int i = 0; i < n; i++) { double x = pcm[at + i]; energy += x * x; }
    if (energy / n >= max_hop * .01) { active += energy; count += n; }
  }
  m.rms = sqrt(power / frames);
  m.active_rms = count ? sqrt(active / count) : 0;
  m.window_rms = sqrt(max_power);
  return m;
}

// One constant trim preserves attacks, releases and the balance inside a loop.
static inline int sample_balance(float *pcm, int frames, category_t cat) {
  const level_profile_t *p = &kSampleLevels[cat];
  level_measure_t m = sample_measure(pcm, frames);
  int db = p->gain_db;
  float limit = 1;
  if (m.peak > 0)       limit = fminf(limit, p->peak / m.peak);
  if (m.active_rms > 0) limit = fminf(limit, p->active_rms / m.active_rms);
  if (m.window_rms > 0) limit = fminf(limit, p->window_rms / m.window_rms);
  if (limit > 0) db = MIN(db, (int)floorf(20 * log10f(limit) + .00001f));
  float gain = powf(10, db * .05f);
  for (int i = 0; i < frames; i++) pcm[i] *= gain;
  return db;
}

#endif

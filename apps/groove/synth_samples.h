#ifndef __GROOVE_SYNTH_SAMPLES_H__
#define __GROOVE_SYNTH_SAMPLES_H__

#include <stddef.h>
#define TSF_STATIC
#define TSF_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "third_party/tsf.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static tsf *g_instrument_samples;

static bool instrument_samples_load(void) {
  if (g_instrument_samples) return true;
  char path[1024];
  int n = snprintf(path, sizeof(path), "%s/../share/groove/instruments/acoustic.sf2", ui_get_exe_dir());
  if (n > 0 && (size_t)n < sizeof(path)) g_instrument_samples = tsf_load_filename(path);
  if (!g_instrument_samples) {
    snprintf(path, sizeof(path), "apps/groove/share/instruments/acoustic.sf2");
    g_instrument_samples = tsf_load_filename(path);
  }
  if (!g_instrument_samples) {
    fprintf(stderr, "[synth] instrument samples unavailable path=%s\n", path);
    fflush(stderr);
    return false;
  }
  tsf_set_output(g_instrument_samples, TSF_MONO, GR_SAMPLE_RATE, -3.0f);
  return true;
}

static void sampled_instrument(sy_ctx_t *c, const sy_note_t *n, sy_inst_t inst, int program) {
  if (c->failed) return;
  if (n->s < 0 || n->s >= c->n + SY_TAIL || n->g < 0 || !isfinite(n->hz) || n->hz <= 0 ||
      !isfinite(n->a) || n->a < 0 || !isfinite(n->from) || n->from < 0) {
    fprintf(stderr, "[synth] instrument note rejected inst=%d start=%d gate=%d hz=%g gain=%g from=%g\n",
            inst, n->s, n->g, n->hz, n->a, n->from);
    fflush(stderr);
    c->failed = true;
    return;
  }
  if (!instrument_samples_load()) { c->failed = true; return; }
  int preset = tsf_get_presetindex(g_instrument_samples, 0, program);
  tsf *voice = preset >= 0 ? tsf_copy(g_instrument_samples) : NULL;
  float pitch = 69.0f + 12.0f * log2f(n->hz / 440.0f);
  int key = (int)roundf(pitch);
  if (!voice || key < 0 || key > 127 || !tsf_channel_set_presetindex(voice, 0, preset) ||
      !tsf_channel_set_tuning(voice, 0, pitch - key) ||
      !tsf_channel_note_on(voice, 0, key, CLAMP(n->a, 0.01f, 1.0f)) || !tsf_active_voice_count(voice)) {
    fprintf(stderr, "[synth] instrument note failed inst=%d program=%d preset=%d key=%d\n", inst, program, preset, key);
    fflush(stderr);
    tsf_close(voice);
    c->failed = true;
    return;
  }
  bool guitar = inst == I_PLUCK || inst == I_CLEAN || inst == I_MUTE || inst == I_WAH || inst == I_DIST;
  int tail = (int)((guitar ? 0.35f : inst == I_BASS ? 0.15f : 1.0f) * SY_SR);
  int total = MIN(c->n + SY_TAIL - n->s, n->g + tail);
  float pcm[128], low = 0, band = 0;
  bool released = false;
  for (int at = 0; at < total && tsf_active_voice_count(voice);) {
    int frames = MIN((int)ARRAY_LEN(pcm), total - at);
    if (!released && at >= n->g) { tsf_channel_note_off(voice, 0, key); released = true; }
    if (!released) frames = MIN(frames, n->g - at);
    if (n->from > 0) {
      float slide = fmaxf(0, 1.0f - at / (0.065f * SY_SR));
      tsf_channel_set_tuning(voice, 0, pitch - key + 12.0f * log2f(n->from / n->hz) * slide);
    }
    tsf_render_float(voice, pcm, frames, 0);
    float wah = inst == I_WAH ? 2.0f * sinf(SY_PI * (550.0f + 1750.0f * expf(-at / (0.09f * SY_SR))) / SY_SR) : 0;
    for (int i = 0; i < frames; i++) {
      float x = pcm[i];
      if (inst == I_MUTE) x *= expf(-(at + i) / SY_SR * 22.0f);
      if (inst == I_WAH) {
        float high = x - low - 0.7f * band;
        band += wah * high; low += wah * band;
        x = 0.35f * x + 1.3f * band;
      }
      float fade = fminf(1, (total - at - i - 1) / (0.003f * SY_SR));
      c->buf[n->s + at + i] += x * fade;
    }
    at += frames;
  }
  tsf_close(voice);
}

void sy_samples_free(void) {
  tsf_close(g_instrument_samples);
  g_instrument_samples = NULL;
}

#endif

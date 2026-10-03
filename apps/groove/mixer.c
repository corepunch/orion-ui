// Song model and offline mixer. song_render() is a pure function over a
// song_t and the block table, so it runs identically in the audio callback
// and in tests without a device.

#include "groove.h"

#define CLIP_GAIN 0.5f

void song_init(song_t *s) {
  memset(s, 0, sizeof(*s));
  s->bpm = GR_BPM_DEFAULT;
  s->loop = true;
  s->preview_block = -1;
}

static int clip_bars(const clip_t *c) {
  const block_t *b = block_get(c->block);
  return b ? b->bars : 1;
}

int song_length_bars(const song_t *s) {
  int n = 0;
  for (int i = 0; i < s->nclips; i++) n = MAX(n, s->clips[i].bar + clip_bars(&s->clips[i]));
  return n;
}

int song_clip_at(const song_t *s, int track, int bar) {
  for (int i = 0; i < s->nclips; i++)
    if (s->clips[i].track == track && bar >= s->clips[i].bar && bar < s->clips[i].bar + clip_bars(&s->clips[i])) return i;
  return -1;
}

bool song_can_place(const song_t *s, int track, int bar, int bars, int ignore_clip) {
  if (track < 0 || track >= GR_TRACKS || bar < 0 || bars < 1 || bar + bars > GR_BARS) return false;
  for (int i = 0; i < s->nclips; i++) {
    const clip_t *c = &s->clips[i];
    if (i == ignore_clip || c->track != track) continue;
    if (bar < c->bar + clip_bars(c) && c->bar < bar + bars) return false;
  }
  return true;
}

int song_add_clip(song_t *s, int block, int track, int bar) {
  const block_t *b = block_get(block);
  if (!b || s->nclips >= GR_MAX_CLIPS || !song_can_place(s, track, bar, b->bars, -1)) {
    fprintf(stderr, "[mx] add_clip rejected block=%d track=%d bar=%d nclips=%d\n", block, track, bar, s->nclips);
    fflush(stderr);
    return -1;
  }
  s->clips[s->nclips] = (clip_t){ block, track, bar };
  return s->nclips++;
}

void song_remove_clip(song_t *s, int idx) {
  if (idx < 0 || idx >= s->nclips) {
    fprintf(stderr, "[mx] remove_clip rejected idx=%d nclips=%d\n", idx, s->nclips);
    fflush(stderr);
    return;
  }
  memmove(&s->clips[idx], &s->clips[idx + 1], sizeof(clip_t) * (size_t)(s->nclips - idx - 1));
  s->nclips--;
}

static bool track_audible(const song_t *s, int t) {
  bool any_solo = false;
  for (int i = 0; i < GR_TRACKS; i++) any_solo |= s->solo[i];
  return any_solo ? s->solo[t] : !s->mute[t];
}

// Adds src[off .. off+n) to the interleaved stereo buffer.
static void add_mono(float *lr, const float *src, int n, float gain) {
  for (int i = 0; i < n; i++) { float v = src[i] * gain; lr[i * 2] += v; lr[i * 2 + 1] += v; }
}

void song_render(song_t *s, float *lr, int frames) {
  int bar = bar_frames_for_bpm(s->bpm), done = 0;
  memset(lr, 0, sizeof(float) * 2 * (size_t)frames);
  int end = s->playing ? song_length_bars(s) * bar : 0;
  if (s->playing && end == 0) s->playing = false;
  while (s->playing && done < frames) {
    if (s->pos >= end) {
      if (!s->loop) { s->playing = false; s->pos = 0; break; }
      s->pos = 0;
    }
    int chunk = (int)MIN((int64_t)(frames - done), (int64_t)end - s->pos);
    for (int i = 0; i < s->nclips; i++) {
      const clip_t *c = &s->clips[i];
      const block_t *b = block_get(c->block);
      if (!b || !b->audio.pcm || !track_audible(s, c->track)) continue;
      int64_t from = MAX(s->pos, (int64_t)c->bar * bar), to = MIN(s->pos + chunk, (int64_t)c->bar * bar + b->audio.frames);
      if (to > from) add_mono(lr + 2 * (done + (int)(from - s->pos)), b->audio.pcm + (from - (int64_t)c->bar * bar), (int)(to - from), CLIP_GAIN);
    }
    s->pos += chunk;
    done += chunk;
  }
  if (s->preview_block >= 0) {
    const block_t *b = block_get(s->preview_block);
    int n = b && b->audio.pcm ? (int)MIN((int64_t)frames, (int64_t)b->audio.frames - s->preview_pos) : 0;
    if (n > 0) { add_mono(lr, b->audio.pcm + s->preview_pos, n, 0.7f); s->preview_pos += n; }
    if (n <= 0 || s->preview_pos >= b->audio.frames) s->preview_block = -1;
  }
}

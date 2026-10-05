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

static int clip_ticks(const clip_t *c) {
  const block_t *b = block_get(c->block);
  return (b ? b->bars : 1) * GR_TICKS_BAR;
}

int64_t position_frames_for_bpm(int position, int bpm) {
  return (int64_t)position * bar_frames_for_bpm(bpm) / GR_TICKS_BAR;
}

int song_clip_end(const song_t *s, int idx) {
  if (idx < 0 || idx >= s->nclips) {
    fprintf(stderr, "[mx] clip_end rejected idx=%d nclips=%d\n", idx, s->nclips);
    fflush(stderr);
    return 0;
  }
  const clip_t *c = &s->clips[idx];
  int end = c->position + clip_ticks(c);
  for (int i = 0; i < s->nclips; i++) {
    const clip_t *next = &s->clips[i];
    if (i == idx || next->track != c->track) continue;
    if (next->position == c->position && next->order > c->order) return c->position;
    if (next->position > c->position) end = MIN(end, next->position);
  }
  return end;
}

int song_length_ticks(const song_t *s) {
  int n = 0;
  for (int i = 0; i < s->nclips; i++) n = MAX(n, song_clip_end(s, i));
  return n;
}

int song_clip_at(const song_t *s, int track, int position) {
  int idx = -1;
  for (int i = 0; i < s->nclips; i++) {
    const clip_t *c = &s->clips[i];
    if (c->track != track || c->position > position) continue;
    if (idx < 0 || c->position > s->clips[idx].position ||
        (c->position == s->clips[idx].position && c->order > s->clips[idx].order)) idx = i;
  }
  return idx >= 0 && position < s->clips[idx].position + clip_ticks(&s->clips[idx]) ? idx : -1;
}

bool song_can_place(const song_t *s, int track, int position, int ticks, int ignore_clip) {
  if (track < 0 || track >= GR_TRACKS || position < 0 || ticks < 1 || ticks > GR_BARS * GR_TICKS_BAR || position > GR_BARS * GR_TICKS_BAR - ticks) return false;
  return ignore_clip >= 0 ? ignore_clip < s->nclips : s->nclips < GR_MAX_CLIPS;
}

int song_add_clip(song_t *s, int block, int track, int position) {
  const block_t *b = block_get(block);
  if (!b || s->nclips >= GR_MAX_CLIPS || !song_can_place(s, track, position, b->bars * GR_TICKS_BAR, -1)) {
    fprintf(stderr, "[mx] add_clip rejected block=%d track=%d position=%d nclips=%d\n", block, track, position, s->nclips);
    fflush(stderr);
    return -1;
  }
  s->clips[s->nclips] = (clip_t){ block, track, position, ++s->clip_order };
  return s->nclips++;
}

bool song_move_clip(song_t *s, int idx, int track, int position) {
  if (idx < 0 || idx >= s->nclips || !song_can_place(s, track, position, clip_ticks(&s->clips[idx]), idx)) {
    fprintf(stderr, "[mx] move_clip rejected idx=%d track=%d position=%d nclips=%d\n", idx, track, position, s->nclips);
    fflush(stderr);
    return false;
  }
  s->clips[idx].track = track;
  s->clips[idx].position = position;
  s->clips[idx].order = ++s->clip_order;
  return true;
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
  int done = 0;
  memset(lr, 0, sizeof(float) * 2 * (size_t)frames);
  int64_t clip_ends[GR_MAX_CLIPS], end = 0;
  if (s->playing) for (int i = 0; i < s->nclips; i++) {
    clip_ends[i] = position_frames_for_bpm(song_clip_end(s, i), s->bpm);
    end = MAX(end, clip_ends[i]);
  }
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
      int64_t start = position_frames_for_bpm(c->position, s->bpm);
      int64_t from = MAX(s->pos, start), to = MIN(s->pos + chunk, MIN(clip_ends[i], start + b->audio.frames));
      if (to > from) add_mono(lr + 2 * (done + (int)(from - s->pos)), b->audio.pcm + (from - start), (int)(to - from), CLIP_GAIN);
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

// CONTROLLER: owns the song, the audio device and every state change.
// Edits touch the song under the audio lock; the audio callback only reads it.

#include "groove.h"

groove_t *g_app;

void app_lock(void)   { if (g_app && g_app->audio_dev) axAudioLock(g_app->audio_dev); }
void app_unlock(void) { if (g_app && g_app->audio_dev) axAudioUnlock(g_app->audio_dev); }

static void audio_cb(void *userdata, uint8_t *stream, int len) {
  static float tmp[2 * 1024];
  groove_t *app = userdata;
  int16_t *out = (int16_t *)stream;
  for (int frames = len / 4; frames > 0;) {
    int n = MIN(frames, 1024);
    song_render(&app->song, tmp, n);
    for (int i = 0; i < n * 2; i++) out[i] = (int16_t)(tanhf(tmp[i]) * 32000.0f);
    out += n * 2;
    frames -= n;
  }
}

static int find_block(const char *name) {
  for (int i = 0; i < blocks_count(); i++) if (strcmp(block_get(i)->name, name) == 0) return i;
  fprintf(stderr, "[gr] seed: unknown block '%s'\n", name);
  fflush(stderr);
  return -1;
}

static void seed(song_t *s, const char *name, int track, int bar, int count) {
  int b = find_block(name);
  if (b < 0) return;
  for (int i = 0; i < count; i++) song_add_clip(s, b, track, bar + i * block_get(b)->bars);
}

static void seed_demo(song_t *s) {
  seed(s, "Four Floor", 0, 0, 8);
  seed(s, "Hat Groove", 1, 2, 5);  seed(s, "Snare Fill", 1, 7, 1);
  seed(s, "Root Pulse", 2, 0, 4);  seed(s, "Funk Bass", 2, 4, 4);
  seed(s, "Am - F", 3, 0, 1);      seed(s, "C - G", 3, 2, 1);      seed(s, "Am - F", 3, 4, 1);  seed(s, "C - G", 3, 6, 1);
  seed(s, "Pad Am", 4, 0, 1);      seed(s, "Pad F - G", 4, 2, 1);  seed(s, "Pad Am", 4, 4, 1);  seed(s, "Pad F - G", 4, 6, 1);
  seed(s, "Am Strum", 5, 4, 4);
  seed(s, "Pluck Arp", 6, 4, 4);
}

groove_t *app_init(void) {
  groove_t *app = calloc(1, sizeof(*app));
  block_pcm_t pcm[GR_MAX_BLOCKS];
  if (!app) { fprintf(stderr, "[gr] app_init: allocation failed\n"); fflush(stderr); return NULL; }
  song_init(&app->song);
  app->selected_clip = -1;
  app->drag.track = -1;
  blocks_render(app->song.bpm, pcm);
  blocks_swap(pcm);
  seed_demo(&app->song);
  if (axAudioInit()) {
    AXaudiospec want = { GR_SAMPLE_RATE, AX_AUDIO_S16, 2, 1024, audio_cb, app }, got;
    app->audio_dev = axAudioOpen(&want, &got);
    if (app->audio_dev) {
      GR_TRACE("audio open dev=%d freq=%d channels=%d period=%d", app->audio_dev, got.freq, got.channels, got.samples);
      axAudioPause(app->audio_dev, FALSE);
    } else GR_TRACE("audio open failed: %s", axAudioGetError() ? axAudioGetError() : "unknown");
  } else GR_TRACE("audio init failed: %s", axAudioGetError() ? axAudioGetError() : "unknown");
  g_app = app;
  return app;
}

void app_shutdown(groove_t *app) {
  if (!app) return;
  if (app->timer) axCancelTimer(app->timer);
  if (app->audio_dev) axAudioClose(app->audio_dev);
  axAudioShutdown();
  blocks_free();
  if (g_app == app) g_app = NULL;
  free(app);
}

static int bar_of(const song_t *s) { return (int)(s->pos / bar_frames_for_bpm(s->bpm)); }

void app_update_status(void) {
  char buf[64];
  const song_t *s = &g_app->song;
  int bar = bar_frames_for_bpm(s->bpm), beat = (int)((s->pos % bar) / (bar / GR_BEATS_BAR));
  snprintf(buf, sizeof(buf), "%d BPM   %02d.%d   %s%s", s->bpm, bar_of(s) + 1, beat + 1,
           s->playing ? "Playing" : "Stopped", s->loop ? "   Loop" : "");
  send_message(g_app->win, evStatusBar, 0, buf);
}

void app_set_playing(bool playing) {
  GR_TRACE("play=%d pos=%lld", playing, (long long)g_app->song.pos);
  app_lock();
  g_app->song.playing = playing;
  app_unlock();
  toolbar_refresh(g_app->win);
  app_update_status();
}

void app_seek_bar(int bar) {
  GR_TRACE("seek bar=%d", bar);
  app_lock();
  g_app->song.pos = (int64_t)CLAMP(bar, 0, GR_BARS - 1) * bar_frames_for_bpm(g_app->song.bpm);
  app_unlock();
  invalidate_window(g_app->sheet);
  app_update_status();
}

void app_set_bpm(int bpm) {
  song_t *s = &g_app->song;
  block_pcm_t pcm[GR_MAX_BLOCKS];
  bpm = CLAMP(bpm, GR_BPM_MIN, GR_BPM_MAX);
  if (bpm == s->bpm) return;
  GR_TRACE("bpm %d -> %d", s->bpm, bpm);
  blocks_render(bpm, pcm);
  app_lock();
  s->pos = s->pos * bar_frames_for_bpm(bpm) / bar_frames_for_bpm(s->bpm);
  s->bpm = bpm;
  s->preview_block = -1;
  blocks_swap(pcm);
  app_unlock();
  for (int i = 0; i < GR_MAX_BLOCKS; i++) free(pcm[i].pcm);
  app_update_status();
}

void app_preview(int block) {
  GR_TRACE("preview block=%d", block);
  app_lock();
  g_app->song.preview_block = block;
  g_app->song.preview_pos = 0;
  app_unlock();
}

void app_select_clip(int idx) {
  GR_TRACE("select clip=%d", idx);
  g_app->selected_clip = idx;
  invalidate_window(g_app->sheet);
}

bool app_drop(const drag_t *d) {
  song_t *s = &g_app->song;
  bool ok = false;
  app_lock();
  if (d->from_clip >= 0 && d->from_clip < s->nclips) {
    int bars = block_get(s->clips[d->from_clip].block)->bars;
    if ((ok = song_can_place(s, d->track, d->bar, bars, d->from_clip))) {
      s->clips[d->from_clip].track = d->track;
      s->clips[d->from_clip].bar = d->bar;
      g_app->selected_clip = d->from_clip;
    }
  } else if (d->from_clip < 0) {
    int idx = song_add_clip(s, d->block, d->track, d->bar);
    if ((ok = idx >= 0)) g_app->selected_clip = idx;
  }
  app_unlock();
  GR_TRACE("drop block=%d from=%d track=%d bar=%d ok=%d", d->block, d->from_clip, d->track, d->bar, ok);
  invalidate_window(g_app->sheet);
  return ok;
}

void app_command(uint16_t id) {
  song_t *s = &g_app->song;
  GR_TRACE("command id=%u", (unsigned)id);
  switch (id) {
    case ID_PLAY:     app_set_playing(!s->playing); break;
    case ID_STOP:     app_set_playing(false); app_seek_bar(0); break;
    case ID_REWIND:   app_seek_bar(0); break;
    case ID_LOOP:     app_lock(); s->loop = !s->loop; app_unlock(); toolbar_refresh(g_app->win); app_update_status(); break;
    case ID_BPM_UP:   app_set_bpm(s->bpm + 5); break;
    case ID_BPM_DOWN: app_set_bpm(s->bpm - 5); break;
    case ID_DELETE:
      if (g_app->selected_clip < 0 || g_app->selected_clip >= s->nclips) break;
      app_lock();
      song_remove_clip(s, g_app->selected_clip);
      g_app->selected_clip = -1;
      app_unlock();
      invalidate_window(g_app->sheet);
      break;
  }
}

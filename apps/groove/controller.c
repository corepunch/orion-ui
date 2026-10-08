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
  if (b < 0 || !app_block_audio(b)) return;
  app_lock();
  for (int i = 0; i < count; i++) song_add_clip(s, b, track, (bar + i * block_get(b)->bars) * GR_TICKS_BAR);
  app_unlock();
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

static bool block_in_song(int id) {
  for (int i = 0; i < g_app->song.nclips; i++) if (g_app->song.clips[i].block == id) return true;
  return false;
}

bool app_block_audio(int id) {
  const block_t *b = block_get(id);
  int bpm = g_app->song.bpm;
  block_pcm_t pcm;
  if (!b) { fprintf(stderr, "[gr] block audio rejected block=%d count=%d\n", id, blocks_count()); fflush(stderr); return false; }
  if (b->audio.pcm && b->audio_bpm == bpm) return true;
  if (!block_render(id, bpm, &pcm)) return false;
  app_lock();
  block_install(id, bpm, &pcm);
  app_unlock();
  free(pcm.pcm);
  return true;
}

bool app_block_peaks(int id) {
  const block_t *b = block_get(id);
  int bpm = g_app->song.bpm;
  block_pcm_t pcm;
  if (!b) { fprintf(stderr, "[gr] block peaks rejected block=%d count=%d\n", id, blocks_count()); fflush(stderr); return false; }
  if (b->audio_bpm == bpm && b->audio.npeaks) return true;
  if (g_app->peak_credit <= 0) { g_app->peaks_pending = true; return false; }
  g_app->peak_credit--;
  if (!block_render(id, bpm, &pcm)) return false;
  free(pcm.pcm); // only the overview is needed; the audio is rendered again if the block is used
  pcm.pcm = NULL;
  pcm.frames = 0;
  app_lock();
  block_install(id, bpm, &pcm);
  app_unlock();
  free(pcm.pcm);
  return true;
}

// Detaches every loaded buffer under the lock and frees them after it.
static void release_all_audio(void) {
  float **stale = calloc((size_t)blocks_count(), sizeof(*stale));
  if (!stale) { fprintf(stderr, "[gr] release allocation failed count=%d\n", blocks_count()); fflush(stderr); return; }
  app_lock();
  g_app->song.preview_block = -1;
  for (int i = 0; i < blocks_count(); i++) stale[i] = block_release(i);
  app_unlock();
  for (int i = 0; i < blocks_count(); i++) free(stale[i]);
  free(stale);
  g_app->auditioned = -1;
}

void app_new_song(void) {
  app_lock();
  g_app->song.nclips = 0;
  g_app->song.pos = 0;
  g_app->selected_clip = -1;
  app_unlock();
  release_all_audio();
  invalidate_window(g_app->sheet);
}

void app_load_demo(void) {
  seed_demo(&g_app->song);
}

groove_t *app_init(void) {
  groove_t *app = calloc(1, sizeof(*app));
  if (!app) { fprintf(stderr, "[gr] app_init: allocation failed\n"); fflush(stderr); return NULL; }
  if (!block_pictograms_load(app)) { free(app); return NULL; }
  song_init(&app->song);
  app->selected_clip = -1;
  app->drag.track = -1;
  app->auditioned = -1;
  app->category = CAT_DRUMS;
  app->peak_credit = GR_PEAKS_PER_TICK;
  if (axAudioInit()) {
    AXaudiospec want = { GR_SAMPLE_RATE, AX_AUDIO_S16, 2, 1024, audio_cb, app }, got;
    app->audio_dev = axAudioOpen(&want, &got);
    if (app->audio_dev) {
      axAudioPause(app->audio_dev, FALSE);
    }
  }
  g_app = app;
  return app;
}

void app_shutdown(groove_t *app) {
  if (!app) return;
  if (app->timer) axCancelTimer(app->timer);
  if (app->audio_dev) axAudioClose(app->audio_dev);
  axAudioShutdown();
  waveform_cache_free(app);
  if (app->pictograms) R_DeleteTexture(app->pictograms);
  blocks_free();
  if (g_app == app) g_app = NULL;
  free(app);
}

void app_set_playing(bool playing) {
  app_lock();
  g_app->song.playing = playing;
  app_unlock();
  transport_refresh();
}

void app_seek_position(int position) {
  app_lock();
  g_app->song.pos = position_frames_for_bpm(CLAMP(position, 0, GR_BARS * GR_TICKS_BAR - GR_SNAP_TICKS), g_app->song.bpm);
  app_unlock();
  invalidate_window(g_app->sheet);
}

// The blocks the song plays are re-rendered before the lock is taken and
// swapped in with the tempo. Everything else is dropped and renders again
// when it is next shown or used.
void app_set_bpm(int bpm) {
  song_t *s = &g_app->song;
  int ids[GR_MAX_CLIPS], n = 0;
  bpm = CLAMP(bpm, GR_BPM_MIN, GR_BPM_MAX);
  if (bpm == s->bpm) return;
  for (int i = 0; i < s->nclips; i++) {
    int k = 0;
    while (k < n && ids[k] != s->clips[i].block) k++;
    if (k == n) ids[n++] = s->clips[i].block;
  }
  block_pcm_t *pcm = calloc((size_t)MAX(n, 1), sizeof(*pcm));
  float **stale = calloc((size_t)blocks_count(), sizeof(*stale));
  if (!pcm || !stale) {
    fprintf(stderr, "[gr] tempo change allocation failed bpm=%d blocks=%d\n", bpm, n);
    fflush(stderr);
    free(pcm); free(stale);
    return;
  }
  for (int i = 0; i < n; i++) block_render(ids[i], bpm, &pcm[i]);
  app_lock();
  s->pos = s->pos * bar_frames_for_bpm(bpm) / bar_frames_for_bpm(s->bpm);
  s->bpm = bpm;
  s->preview_block = -1;
  for (int i = 0; i < blocks_count(); i++) stale[i] = block_release(i);
  for (int i = 0; i < n; i++) block_install(ids[i], bpm, &pcm[i]);
  app_unlock();
  for (int i = 0; i < blocks_count(); i++) free(stale[i]);
  free(stale);
  free(pcm);
  g_app->auditioned = -1;
  invalidate_window(g_app->sheet);
  if (g_app->library) invalidate_window(g_app->library);
}

void app_preview(int block) {
  int old = g_app->auditioned;
  float *stale = NULL;
  if (!app_block_audio(block)) return;
  app_lock();
  g_app->song.preview_block = block;
  g_app->song.preview_pos = 0;
  if (old >= 0 && old != block && !block_in_song(old)) stale = block_release(old);
  app_unlock();
  free(stale);
  g_app->auditioned = block;
}

static void library_refilter(void) {
  if (g_app->bin) send_message(g_app->bin, binFilter, 0, NULL);
}

void app_set_category(int category) {
  if (category < 0 || category >= CAT_COUNT) {
    fprintf(stderr, "[gr] family filter rejected value=%d count=%d\n", category, CAT_COUNT);
    fflush(stderr);
    return;
  }
  if (category == g_app->category) return;
  g_app->category = category;
  library_refilter();
  transport_refresh();
}

void app_set_genre(uint8_t genre) {
  if (genre & (genre - 1) || genre & ~GENRE_ANY) {
    fprintf(stderr, "[gr] genre filter rejected value=0x%x\n", genre);
    fflush(stderr);
    return;
  }
  if (genre == g_app->genre) return;
  g_app->genre = genre;
  library_refilter();
}

bool block_visible(int id) {
  const block_t *b = block_get(id);
  return b && (int)b->cat == g_app->category && (!g_app->genre || (b->genres & g_app->genre));
}

void app_select_clip(int idx) {
  g_app->selected_clip = idx;
  invalidate_window(g_app->sheet);
}

bool app_drop(const drag_t *d) {
  song_t *s = &g_app->song;
  bool ok = false, audio = d->from_clip < 0 && app_block_audio(d->block); // render before taking the lock
  app_lock();
  if (d->from_clip >= 0 && d->from_clip < s->nclips) {
    if ((ok = song_move_clip(s, d->from_clip, d->track, d->position))) g_app->selected_clip = d->from_clip;
  } else if (d->from_clip < 0 && audio) {
    int idx = song_add_clip(s, d->block, d->track, d->position);
    if ((ok = idx >= 0)) g_app->selected_clip = idx;
  }
  app_unlock();
  invalidate_window(g_app->sheet);
  return ok;
}

void app_command(uint16_t id) {
  song_t *s = &g_app->song;
  switch (id) {
    case ID_PLAY:     app_set_playing(!s->playing); break;
    case ID_STOP:     app_set_playing(false); app_seek_position(0); break;
    case ID_REWIND:   app_seek_position(0); break;
    case ID_LOOP:     app_lock(); s->loop = !s->loop; app_unlock(); transport_refresh(); break;
    case ID_WINDOW_LIBRARY:
      show_window(g_app->library, true);
      dock_collapse(g_app->library, false);
      break;
    case ID_WINDOW_RESET:
      show_window(g_app->library, true);
      dock_collapse(g_app->library, false);
      dock_set_side(g_app->library, DOCK_BOTTOM);
      if (g_app->menubar_win) dock_set_side(g_app->menubar_win, DOCK_TOP);
      break;
    case ID_FILE_NEW:  app_new_song(); break;
    case ID_FILE_DEMO: app_new_song(); app_load_demo(); invalidate_window(g_app->sheet); break;
    case ID_FILE_QUIT: ui_request_quit(); break;
    case ID_DELETE:
      if (g_app->selected_clip < 0 || g_app->selected_clip >= s->nclips) break;
      app_lock();
      song_remove_clip(s, g_app->selected_clip);
      g_app->selected_clip = -1;
      app_unlock();
      invalidate_window(g_app->sheet);
      break;
    default:
      if (id >= ID_FAMILY(0) && id < ID_FAMILY(CAT_COUNT)) app_set_category(id - ID_FAMILY(0));
      break;
  }
}

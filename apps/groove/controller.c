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
    for (int i = 0; i < n && app->sample_preview && app->sample_preview_pos < app->sample_preview_frames; i++) {
      float v = app->sample_preview[app->sample_preview_pos++] / 32768.0f;
      tmp[i * 2] += v; tmp[i * 2 + 1] += v;
    }
    recording_click_mix(tmp, n, app->recording_beat_frames, &app->recording_click_pos);
    groove_master_pcm(tmp, out, n * 2);
    out += n * 2;
    frames -= n;
  }
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
  song_init(&g_app->song);
  g_app->selected_clip = -1;
  app_unlock();
  release_all_audio();
  app_reset_tracks();
  app_select_track(0);
  g_app->filename[0] = 0;
  g_app->song_dirty = false;
  transport_refresh();
  if (g_app->tracks) tracks_sync();
  invalidate_window(g_app->sheet);
}

bool app_load_demo_song(int demo) {
  static const char *const files[GR_DEMO_COUNT] = { "neon-skyline.groove", "afterglow-avenue.groove", "victory-lights.groove" };
  if (!g_app || demo < 0 || demo >= GR_DEMO_COUNT) {
    fprintf(stderr, "[gr] demo rejected index=%d count=%d app=%p\n", demo, GR_DEMO_COUNT, (void *)g_app);
    fflush(stderr);
    return false;
  }
  char path[1024];
  int n = snprintf(path, sizeof(path), "%s/../share/groove/demos/%s", ui_get_exe_dir(), files[demo]);
  FILE *f = n > 0 && (size_t)n < sizeof(path) ? fopen(path, "rb") : NULL;
  if (f) fclose(f);
  else snprintf(path, sizeof(path), "apps/groove/share/demos/%s", files[demo]);
  if (!app_open_song(path)) {
    fprintf(stderr, "[gr] demo load failed index=%d path=%s\n", demo, path);
    fflush(stderr);
    return false;
  }
  g_app->filename[0] = 0;
  if (g_app->sheet) {
    set_scroll_info(g_app->sheet, SB_HORZ, &(scroll_info_t){ .fMask = SIF_POS, .nPos = 0 }, false);
    send_message(g_app->sheet, evHScroll, 0, NULL);
  }
  return true;
}

void app_load_demo(void) {
  app_load_demo_song(GR_DEMO_SKYLINE);
}

groove_t *app_init(void) {
  groove_t *app = calloc(1, sizeof(*app));
  if (!app) { fprintf(stderr, "[gr] app_init: allocation failed\n"); fflush(stderr); return NULL; }
  song_init(&app->song);
  app->selected_clip = -1;
  app->drag.track = -1;
  app->auditioned = -1;
  app->family = FAMILY_LOOP;
  app->peak_credit = GR_PEAKS_PER_TICK;
  const char *sample_settings = axSettingsDirectory();
  char legacy_samples[sizeof(app->samples_dir)];
  int legacy_n = sample_settings && sample_settings[0] ? snprintf(legacy_samples, sizeof(legacy_samples), "%s/Samples", sample_settings) : -1;
  bool legacy_valid = legacy_n > 0 && (size_t)legacy_n < sizeof(legacy_samples);
#ifdef __APPLE__
  char documents[sizeof(app->samples_dir)];
  bool documents_available = ui_media_documents_directory(documents, sizeof(documents)) && axMkDir(documents);
#ifdef AX_PLATFORM_IOS
  int sample_path_n = documents_available ? snprintf(app->samples_dir, sizeof(app->samples_dir), "%s/Samples", documents) : -1;
#else
  char groove_docs[sizeof(app->samples_dir)];
  int docs_n = documents_available ? snprintf(groove_docs, sizeof(groove_docs), "%s/Groove", documents) : -1;
  int sample_path_n = docs_n > 0 && (size_t)docs_n < sizeof(groove_docs) && axMkDir(groove_docs)
    ? snprintf(app->samples_dir, sizeof(app->samples_dir), "%s/Samples", groove_docs) : -1;
#endif
#else
  int sample_path_n = legacy_valid ? snprintf(app->samples_dir, sizeof(app->samples_dir), "%s", legacy_samples) : -1;
#endif
  if (sample_path_n <= 0 || (size_t)sample_path_n >= sizeof(app->samples_dir) || !axMkDir(app->samples_dir)) {
    fprintf(stderr, "[gr] sample storage unavailable path=%s\n", app->samples_dir);
    fflush(stderr);
    app->samples_dir[0] = 0;
  } else samples_load(app->samples_dir);
  if (legacy_valid && strcmp(legacy_samples, app->samples_dir)) samples_load(legacy_samples);
#ifdef AX_PLATFORM_IOS
  // iPad songs stay in the app's private storage, like a reader app's library.
  const char *settings = axSettingsDirectory();
  int n = settings && settings[0] ? snprintf(app->songs_dir, sizeof(app->songs_dir), "%s/Songs", settings) : -1;
  if (n <= 0 || (size_t)n >= sizeof(app->songs_dir) || !axMkDir(settings) || !axMkDir(app->songs_dir)) {
    fprintf(stderr, "[gr] song storage unavailable settings=%s songs=%s\n", settings ? settings : "<null>", app->songs_dir);
    fflush(stderr);
    app->songs_dir[0] = 0;
  }
#endif
  if (axAudioInit()) {
    AXaudiospec want = { GR_SAMPLE_RATE, AX_AUDIO_S16, 2, 1024, audio_cb, app }, got;
    app->audio_dev = axAudioOpen(&want, &got);
    if (app->audio_dev) {
      axAudioPause(app->audio_dev, FALSE);
    }
  }
  g_app = app;
  app_reset_tracks();
  return app;
}

void app_shutdown(groove_t *app) {
  if (!app) return;
  if (app->timer) axCancelTimer(app->timer);
  if (app->audio_dev) axAudioClose(app->audio_dev);
  axAudioShutdown();
  waveform_cache_free(app);
  if (app->library_db) destroy_database(app->library_db);
  if (app->songs_db) destroy_database(app->songs_db);
  app->library_db = app->songs_db = NULL;
  ui_set_database(NULL);
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
  g_app->song.pos = position_frames_for_bpm(CLAMP(position, 0, GR_BARS * GR_TICKS_BAR), g_app->song.bpm);
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
  g_app->song_dirty = true;
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

void app_preview_stop(void) {
  app_lock();
  g_app->song.preview_block = -1;
  app_unlock();
}

static void library_refilter(void) {
  if (g_app->bin) send_message(g_app->bin, binFilter, 0, NULL);
}

void app_set_family(int family) {
  if (family < 0 || family >= FAMILY_COUNT) {
    fprintf(stderr, "[gr] family filter rejected value=%d count=%d\n", family, FAMILY_COUNT);
    fflush(stderr);
    return;
  }
  if ((family_t)family == g_app->family) return;
  g_app->family = (family_t)family;
  library_refilter();
  transport_refresh();
  if (g_app->tracks) tracks_sync();
  if (g_app->sheet) invalidate_window(g_app->sheet);
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
  return b && !sample_is_deleted(id) && b->family == g_app->family && (!g_app->genre || (b->genres & g_app->genre));
}

void app_select_clip(int idx) {
  g_app->selected_clip = idx;
  invalidate_window(g_app->sheet);
}

#ifdef AX_PLATFORM_IOS
// iPad: the library is the default destination; Import and Export use Files.
static void open_song_dialog(void)    { app_show_songs(g_app->win); }
static bool save_song_as_dialog(window_t *owner) { return app_save_song_as(owner); }
#else
static void open_song_dialog(void) {
  char path[512] = {0};
  openfilename_t ofn = {0};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = g_app->win;
  ofn.lpstrFile = path;
  ofn.nMaxFile = sizeof(path);
  ofn.lpstrFilter = "Groove Songs\0*.groove\0All Files\0*.*\0";
  ofn.Flags = OFN_FILEMUSTEXIST;
  if (get_open_filename(&ofn) && app_confirm_replace_song(ofn.hwndOwner) && !app_open_song(path))
    message_box(g_app->win, ui_tr("Could not open this Groove song. The file may be damaged or use an unsupported version."), ui_tr("Open failed"), MB_OK);
}

static bool save_song_as_dialog(window_t *owner) {
  char path[512] = {0};
  snprintf(path, sizeof(path), "%s", g_app->filename[0] ? g_app->filename : "Untitled.groove");
  openfilename_t ofn = {0};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFile = path;
  ofn.nMaxFile = sizeof(path);
  ofn.lpstrFilter = "Groove Song\0*.groove\0";
  ofn.Flags = OFN_OVERWRITEPROMPT;
  if (!get_save_filename(&ofn)) return false;
  if (app_save_song(path)) return true;
  message_box(owner, ui_tr("Could not save the Groove song. Check the filename and available space."), ui_tr("Save failed"), MB_OK);
  return false;
}
#endif

static bool save_current_song(window_t *owner) {
  if (!g_app->filename[0]) return save_song_as_dialog(owner);
#ifdef AX_PLATFORM_IOS
  size_t dir_len = strlen(g_app->songs_dir);
  if (!dir_len || strncmp(g_app->filename, g_app->songs_dir, dir_len) || g_app->filename[dir_len] != '/')
    return save_song_as_dialog(owner);
#endif
  if (app_save_song(g_app->filename)) return true;
  message_box(owner, ui_tr("Could not save the Groove song. Check the available space."), ui_tr("Save failed"), MB_OK);
  return false;
}

bool app_confirm_replace_song(window_t *owner) {
  if (!g_app->song_dirty) return true;
  int choice = message_box(owner, ui_tr("Save changes to this song?"), ui_tr("Save Song"), MB_YESNOCANCEL);
  return choice == IDNO || (choice == IDYES && save_current_song(owner));
}

bool app_resize_clip(int idx, int ticks) {
  if (!g_app) return false;
  app_lock();
  bool ok = song_resize_clip(&g_app->song, idx, ticks);
  app_unlock();
  if (ok) g_app->song_dirty = true;
  if (ok && g_app->sheet) invalidate_window(g_app->sheet);
  return ok;
}

static int track_duplicate_space(int track) {
  if (!g_app || track < 0 || track >= GR_TRACKS) return -1;
  bool occupied[GR_TRACKS] = {0};
  for (int i = 0; i < g_app->song.nclips; i++) occupied[g_app->song.clips[i].track] = true;
  for (int t = GR_TRACKS - 1; t > track; t--) if (!occupied[t]) return t;
  for (int t = 0; t < track; t++) if (!occupied[t]) return t;
  return -1;
}

bool app_can_duplicate_track(int track) { return track_duplicate_space(track) >= 0; }

bool app_duplicate_track(int track) {
  int space = track_duplicate_space(track);
  if (space < 0) return false;
  int family = track_family(track), inserted = space > track ? track + 1 : track;
  song_t *s = &g_app->song;
  app_lock();
  if (space > track) {
    for (int i = 0; i < s->nclips; i++) if (s->clips[i].track > track && s->clips[i].track < space) s->clips[i].track++;
    for (int t = space; t > track + 1; t--) {
      s->track_family[t] = s->track_family[t - 1]; s->mute[t] = s->mute[t - 1]; s->solo[t] = s->solo[t - 1];
    }
  } else {
    for (int i = 0; i < s->nclips; i++) if (s->clips[i].track > space && s->clips[i].track <= track) s->clips[i].track--;
    for (int t = space; t < track; t++) {
      s->track_family[t] = s->track_family[t + 1]; s->mute[t] = s->mute[t + 1]; s->solo[t] = s->solo[t + 1];
    }
  }
  s->track_family[inserted] = (family_t)family;
  s->mute[inserted] = s->solo[inserted] = false;
  app_unlock();
  g_app->song_dirty = true;
  app_select_track(inserted);
  if (g_app->sheet) send_message(g_app->sheet, evResize, 0, NULL);
  return true;
}

bool app_drop(const drag_t *d) {
  if (!d) return false;
  int block = d->from_clip >= 0 && d->from_clip < g_app->song.nclips ? g_app->song.clips[d->from_clip].block : d->block;
  if (!track_accepts_block(d->track, block, d->from_clip)) return false;
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
  if (ok) { g_app->song_dirty = true; app_select_track(d->track); }
  invalidate_window(g_app->sheet);
  return ok;
}

static void project_file_dialog(bool exporting) {
  char path[512] = {0};
  if (exporting) song_name_from_path(g_app->filename, path, sizeof(path));
  if (exporting && !path[0]) snprintf(path, sizeof(path), "Untitled.groove");
  openfilename_t ofn = {0};
  ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = g_app->win;
  ofn.lpstrFile = path; ofn.nMaxFile = sizeof(path);
  ofn.lpstrFilter = "Groove Songs\0*.groove\0";
  ofn.Flags = exporting ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST;
  bool accepted = exporting ? get_save_filename(&ofn) : get_open_filename(&ofn);
  if (!accepted || (!exporting && !app_confirm_replace_song(ofn.hwndOwner))) return;
  bool ok = exporting ? song_write_copy(path) : app_open_song(path);
#ifdef AX_PLATFORM_IOS
  if (ok && !exporting) g_app->song_dirty = true;
#endif
  if (!ok) message_box(g_app->win, ui_tr("Could not read or write the Groove song."), ui_tr("Song unavailable"), MB_OK);
}

void app_command(uint16_t id) {
  song_t *s = &g_app->song;
  switch (id) {
    case ID_FILE_EXPORT_AUDIO: app_export_media(g_app->win, 0); break;
    case ID_FILE_SHARE_AUDIO: app_export_media(g_app->win, 1); break;
    case ID_FILE_EXPORT_VIDEO: app_export_media(g_app->win, 2); break;
    case ID_FILE_RECORD:
    case ID_LIBRARY_WINDOW_RECORD_AUDIO: app_record_sample(g_app->win); break;
    case ID_FILE_MANAGE_SAMPLES: app_manage_samples(g_app->win); break;
    case ID_FILE_IMPORT_PROJECT: project_file_dialog(false); break;
    case ID_FILE_EXPORT_PROJECT: project_file_dialog(true); break;
    case ID_PLAY:     app_set_playing(!s->playing); break;
    case ID_STOP:     app_set_playing(false); app_seek_position(0); break;
    case ID_REWIND:   app_seek_position(0); break;
    case ID_FORWARD:  app_seek_position(song_length_ticks(s)); break;
    case ID_LOOP:     app_lock(); s->loop = !s->loop; app_unlock(); g_app->song_dirty = true; transport_refresh(); break;
    case ID_WINDOW_LIBRARY:
      show_window(g_app->library, true);
      dock_collapse(g_app->library, false);
      break;
    case ID_WINDOW_RESET:
      show_window(g_app->library, true);
      dock_collapse(g_app->library, false);
      dock_set_side(g_app->library, DOCK_BOTTOM);
      break;
    case ID_FILE_NEW:  if (app_confirm_replace_song(g_app->win)) app_new_song(); break;
    case ID_FILE_OPEN: open_song_dialog(); break;
    case ID_FILE_SAVE:   save_current_song(g_app->win); break;
    case ID_FILE_SAVEAS: save_song_as_dialog(g_app->win); break;
    case ID_FILE_DEMO:
    case ID_FILE_DEMO_AFTERGLOW:
    case ID_FILE_DEMO_VICTORY: {
      int demo = id == ID_FILE_DEMO ? GR_DEMO_SKYLINE : id == ID_FILE_DEMO_AFTERGLOW ? GR_DEMO_AFTERGLOW : GR_DEMO_VICTORY;
      if (app_confirm_replace_song(g_app->win) && !app_load_demo_song(demo))
        message_box(g_app->win, ui_tr("Could not load this demo song. Check that Groove's demo files are installed."), ui_tr("Demo unavailable"), MB_OK);
      break;
    }
#ifndef AX_PLATFORM_IOS
    case ID_FILE_QUIT: if (app_confirm_replace_song(g_app->win)) ui_request_quit(); break;
#endif
    case ID_DELETE:
      if (g_app->selected_clip < 0 || g_app->selected_clip >= s->nclips) break;
      app_lock();
      song_remove_clip(s, g_app->selected_clip);
      g_app->selected_clip = -1;
      app_unlock();
      g_app->song_dirty = true;
      if (g_app->tracks) tracks_sync();
      invalidate_window(g_app->sheet);
      break;
    default:
      if (id >= ID_MUTE(0) && id < ID_MUTE(GR_TRACKS)) { app_lock(); s->mute[id - ID_MUTE(0)] = !s->mute[id - ID_MUTE(0)]; app_unlock(); g_app->song_dirty = true; tracks_sync(); }
      else if (id >= ID_SOLO(0) && id < ID_SOLO(GR_TRACKS)) { app_lock(); s->solo[id - ID_SOLO(0)] = !s->solo[id - ID_SOLO(0)]; app_unlock(); g_app->song_dirty = true; tracks_sync(); }
      break;
  }
}

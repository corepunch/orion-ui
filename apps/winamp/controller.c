// CONTROLLER: commands, playback state and the UI tick.

#include "winamp.h"
#include <orion/commdlg/filepicker.h>
#include <unistd.h>
#include <strings.h>
#include <sys/stat.h>

winamp_t *g_app = NULL;

typedef struct { const char *name; float db[WA_BANDS]; } eq_preset_t;

static const eq_preset_t kPresets[] = {
  { "Flat",      {    0,    0,    0,    0,    0,    0,    0,     0,     0,     0 } },
  { "Rock",      {  4.8,  2.9, -3.4, -4.8, -1.9,  2.4,  5.6,   6.9,   6.9,   6.9 } },
  { "Pop",       { -1.0,  2.9,  4.3,  4.8,  3.4,    0, -1.4,  -1.4,  -1.0,  -1.0 } },
  { "Dance",     {  5.8,  4.3,  1.4,    0,    0, -3.4, -4.3,  -4.3,     0,     0 } },
  { "Techno",    {  4.8,  3.4,    0, -3.4, -2.9,    0,  4.8,   5.8,   5.8,   5.3 } },
  { "Club",      {    0,    0,  4.8,  3.4,  3.4,  3.4,  1.9,     0,     0,     0 } },
  { "Classical", {    0,    0,    0,    0,    0,    0, -4.3,  -4.3,  -4.3,  -5.8 } },
  { "Full Bass", {  5.8,  5.8,  5.8,  3.4,  1.0, -2.4, -4.8,  -6.3,  -6.7,  -6.7 } },
};

static wa_engine_t *engine(void) { return &g_app->engine; }

static void audio_lock(void)   { if (g_app->audio_dev) axAudioLock(g_app->audio_dev); }
static void audio_unlock(void) { if (g_app->audio_dev) axAudioUnlock(g_app->audio_dev); }

void app_invalidate_all(void) {
  window_t *wins[] = { g_app->player, g_app->equalizer, g_app->playlist };
  for (int i = 0; i < (int)ARRAY_LEN(wins); i++) if (wins[i]) invalidate_window(wins[i]);
}

static void set_state(wa_state_t state) {
  audio_lock();
  engine()->state = state;
  audio_unlock();
  app_invalidate_all();
}

void app_play_index(int index) {
  wa_playlist_t *pl = &g_app->list;
  if (index < 0 || index >= pl->count) {
    fprintf(stderr, "[wa] play rejected index=%d count=%d\n", index, pl->count);
    fflush(stderr);
    return;
  }
  audio_lock();
  bool ok = engine_open(engine(), pl->items[index].path);
  engine()->state = ok ? WA_PLAYING : WA_STOPPED;
  audio_unlock();
  pl->current = pl->selected = index;
  g_app->marquee_px = 0;
  if (ok) {
    wa_engine_t *e = engine();
    int seconds = e->frames ? (int)(e->frames / (uint64_t)e->hz) : 0;
    pl->items[index].seconds = seconds;
    g_app->kbps = seconds > 0 ? (int)(e->file_size * 8 / (size_t)seconds / 1000) : 0;
    g_app->khz = e->hz / 1000;
    g_app->channels = e->channels;
  }
  app_invalidate_all();
}

static int next_index(int step) {
  wa_playlist_t *pl = &g_app->list;
  if (pl->count == 0) return -1;
  if (g_app->shuffle && pl->count > 1) {
    int i;
    do i = rand() % pl->count; while (i == pl->current);
    return i;
  }
  int i = (pl->current < 0 ? (step > 0 ? -1 : 0) : pl->current) + step;
  if (i >= pl->count) return g_app->repeat ? 0 : -1;
  if (i < 0) return g_app->repeat ? pl->count - 1 : 0;
  return i;
}

static void add_files(void) {
  char path[1024] = {0};
  openfilename_t ofn = { .lStructSize = sizeof(ofn), .hwndOwner = g_app->win, .lpstrFile = path, .nMaxFile = sizeof(path),
                         .lpstrFilter = "MP3 Audio\0*.mp3\0", .nFilterIndex = 1, .Flags = OFN_FILEMUSTEXIST };
  if (!get_open_filename(&ofn)) return;
  wa_playlist_t *pl = &g_app->list;
  if (playlist_add(pl, path)) pl->selected = pl->count - 1;
  app_invalidate_all();
}

static void rescan(void) {
  wa_playlist_t *pl = &g_app->list;
#ifdef AX_PLATFORM_IOS
  playlist_scan(pl, ".");                       // the app's Documents folder
#else
  const char *home = getenv("HOME");
  char music[1024];
  if (home && snprintf(music, sizeof(music), "%s/Music", home) < (int)sizeof(music)) playlist_scan(pl, music);
#endif
  if (pl->selected < 0 && pl->count) pl->selected = 0;
  app_invalidate_all();
}

static void apply_eq(void) {
  audio_lock();
  engine_set_eq(engine(), g_app->eq_db, g_app->preamp_db, engine()->eq_on);
  audio_unlock();
  if (g_app->equalizer) invalidate_window(g_app->equalizer);
}

void app_set_eq(int band, float db) {
  db = MAX(-12.0f, MIN(12.0f, db));
  if (band < 0) g_app->preamp_db = db;
  else if (band < WA_BANDS) g_app->eq_db[band] = db;
  else { fprintf(stderr, "[wa] eq band rejected band=%d\n", band); fflush(stderr); return; }
  apply_eq();
}

void app_set_volume(float v) {
  audio_lock();
  engine()->volume = MAX(0.0f, MIN(1.0f, v));
  audio_unlock();
  if (g_app->player) invalidate_window(g_app->player);
}

void app_set_balance(float b) {
  if (fabsf(b) < 0.08f) b = 0;                  // snap to centre like Winamp
  audio_lock();
  engine()->balance = MAX(-1.0f, MIN(1.0f, b));
  audio_unlock();
  if (g_app->player) invalidate_window(g_app->player);
}

void app_seek(float fraction) {
  wa_engine_t *e = engine();
  if (!e->dec) return;
  audio_lock();
  engine_seek(e, (uint64_t)(MAX(0.0f, MIN(1.0f, fraction)) * (double)e->frames));
  audio_unlock();
  app_invalidate_all();
}

float app_position(void) {
  wa_engine_t *e = engine();
  return e->dec && e->frames ? (float)((double)e->pos / (double)e->frames) : 0;
}

int app_elapsed_seconds(void) { wa_engine_t *e = engine(); return e->dec && e->hz ? (int)(e->pos / (uint64_t)e->hz) : 0; }
int app_track_seconds(void) { wa_engine_t *e = engine(); return e->dec && e->hz ? (int)(e->frames / (uint64_t)e->hz) : 0; }

const char *app_marquee_text(char *buf, size_t n) {
  wa_playlist_t *pl = &g_app->list;
  int i = pl->current >= 0 ? pl->current : pl->selected;
  if (i < 0 || i >= pl->count) { snprintf(buf, n, "WINAMP 2.91"); return buf; }
  const wa_track_t *t = &pl->items[i];
  if (t->seconds >= 0) snprintf(buf, n, "%d. %s (%d:%02d)  ***  ", i + 1, t->title, t->seconds / 60, t->seconds % 60);
  else snprintf(buf, n, "%d. %s  ***  ", i + 1, t->title);
  return buf;
}

void app_command(uint16_t id) {
  wa_playlist_t *pl = &g_app->list;
  wa_state_t state = engine()->state;
  switch (id) {
    case ID_PLAYBACK_PREV:    { int i = next_index(-1); if (i >= 0) app_play_index(i); break; }
    case ID_PLAYBACK_NEXT:    { int i = next_index(1);  if (i >= 0) app_play_index(i); break; }
    case ID_PLAYBACK_PLAY:
      if (state == WA_PAUSED) set_state(WA_PLAYING);
      else if (pl->count) app_play_index(pl->selected >= 0 ? pl->selected : pl->current >= 0 ? pl->current : 0);
      break;
    case ID_PLAYBACK_PAUSE:
      if (state != WA_STOPPED) set_state(state == WA_PAUSED ? WA_PLAYING : WA_PAUSED);
      break;
    case ID_PLAYBACK_STOP:    set_state(WA_STOPPED); app_seek(0); break;
    case ID_PLAYBACK_OPEN:    add_files(); break;
    case ID_OPTIONS_SHUFFLE:  g_app->shuffle = !g_app->shuffle; app_invalidate_all(); break;
    case ID_OPTIONS_REPEAT:   g_app->repeat = !g_app->repeat; app_invalidate_all(); break;
    case ID_OPTIONS_TIME:     g_app->time_remaining = !g_app->time_remaining; app_invalidate_all(); break;
    case ID_WINDOW_EQUALIZER: g_app->show_eq = !g_app->show_eq; app_relayout(); break;
    case ID_WINDOW_PLAYLIST:  g_app->show_pl = !g_app->show_pl; app_relayout(); break;
    case ID_EQ_TOGGLE:
      audio_lock();
      engine_set_eq(engine(), g_app->eq_db, g_app->preamp_db, !engine()->eq_on);
      audio_unlock();
      app_invalidate_all();
      break;
    case ID_EQ_AUTO:          g_app->eq_auto = !g_app->eq_auto; app_invalidate_all(); break;
    case ID_EQ_PRESET:
      g_app->eq_preset = (g_app->eq_preset + 1) % (int)ARRAY_LEN(kPresets);
      memcpy(g_app->eq_db, kPresets[g_app->eq_preset].db, sizeof(g_app->eq_db));
      g_app->preamp_db = 0;
      apply_eq();
      break;
    case ID_LIST_REMOVE:
      if (pl->selected < 0) break;
      if (pl->selected == pl->current) { set_state(WA_STOPPED); audio_lock(); engine_close(engine()); audio_unlock(); }
      playlist_remove(pl, pl->selected);
      app_invalidate_all();
      break;
    case ID_LIST_RESCAN:      rescan(); break;
    default:
      fprintf(stderr, "[wa] unknown command id=%u\n", id);
      fflush(stderr);
      break;
  }
}

void app_tick(void) {
  wa_engine_t *e = engine();
  g_app->tick++;
  if (e->state == WA_PLAYING && e->ended) {
    int i = next_index(1);
    if (i >= 0) app_play_index(i);
    else { set_state(WA_STOPPED); app_seek(0); }
  }
  analyzer_update(e->vis_ring, e->vis_at, g_app->vis_bars, g_app->vis_peaks, g_app->vis_peak_hold, e->state == WA_PLAYING);
  g_app->marquee_px++;
  if (g_app->player) invalidate_window(g_app->player);
  static int last_second = -1;
  int second = app_elapsed_seconds();
  if (second != last_second && g_app->playlist) invalidate_window(g_app->playlist);
  last_second = second;
}

static void audio_cb(void *userdata, uint8_t *stream, int len) {
  winamp_t *app = userdata;
  engine_render(&app->engine, (int16_t *)stream, len / 4);
}

winamp_t *app_init(void) {
  winamp_t *app = calloc(1, sizeof(*app));
  if (!app) { fprintf(stderr, "[wa] app_init: allocation failed\n"); fflush(stderr); return NULL; }
  g_app = app;
  app->list = (wa_playlist_t){ .current = -1, .selected = -1 };
  app->show_eq = app->show_pl = true;
  app->engine.volume = 0.8f;
  app->drop_tick = -WA_DROP_BATCH_TICKS - 1;
  engine_set_eq(&app->engine, app->eq_db, 0, false);
  if (!skin_load_default(&app->skin)) {
    fprintf(stderr, "[wa] default skin incomplete\n");
    fflush(stderr);
  }
  // A user skin in Documents (Skin.wsz or an unpacked Skin folder) wins.
  if (!access("Skin.wsz", R_OK)) skin_load(&app->skin, "Skin.wsz");
  else if (!access("Skin", R_OK)) skin_load(&app->skin, "Skin");
  if (axAudioInit()) {
    AXaudiospec want = { WA_RATE, AX_AUDIO_S16, 2, 1024, audio_cb, app }, got;
    app->audio_dev = axAudioOpen(&want, &got);
    if (app->audio_dev) axAudioPause(app->audio_dev, FALSE);
    else { fprintf(stderr, "[wa] audio device open failed: %s\n", axAudioGetError()); fflush(stderr); }
  }
  rescan();
  return app;
}

void app_add_path(const char *path) {
  if (playlist_scan(&g_app->list, path) == 0) playlist_add(&g_app->list, path);
  if (g_app->list.selected < 0 && g_app->list.count) g_app->list.selected = 0;
}

static bool has_ext(const char *path, const char *ext) {
  size_t n = strlen(path), e = strlen(ext);
  return n > e && !strcasecmp(path + n - e, ext);
}

static bool is_skin(const char *path) {
  if (has_ext(path, ".wsz")) return true;
  char main_bmp[1024];
  struct stat st;
  return !stat(path, &st) && S_ISDIR(st.st_mode) &&
         snprintf(main_bmp, sizeof(main_bmp), "%s/main.bmp", path) < (int)sizeof(main_bmp) && !access(main_bmp, R_OK);
}

void app_set_skin(const char *path) {
  if (!skin_load(&g_app->skin, path)) { fprintf(stderr, "[wa] skin incomplete path=%s\n", path); fflush(stderr); }
  if (g_app->player) player_apply_skin(g_app->player);
  if (g_app->equalizer) eq_apply_skin(g_app->equalizer);
  app_invalidate_all();
}

// A dropped skin is applied; MP3s and folders of MP3s join the playlist at
// `index` (-1 appends). With `play`, the first added track starts, once per
// batch: a multi-file drop arrives as one event per file.
bool app_drop_file(const char *path, int index, bool play) {
  wa_playlist_t *pl = &g_app->list;
  if (is_skin(path)) {
#ifdef AX_PLATFORM_IOS
    // The drop was imported into Documents; keep it as the skin for the next launch.
    if (has_ext(path, ".wsz") && rename(path, "Skin.wsz") == 0) path = "Skin.wsz";
#endif
    app_set_skin(path);
    return true;
  }
  int before = pl->count, first = -1;
  if (playlist_scan(pl, path) == 0 && has_ext(path, ".mp3") && !playlist_add(pl, path))
    for (int i = 0; i < pl->count && first < 0; i++) if (!strcmp(pl->items[i].path, path)) first = i;   // already listed
  int added = pl->count - before;
  if (!added && first < 0) { fprintf(stderr, "[wa] drop ignored: no new MP3 or skin path=%s\n", path); fflush(stderr); return false; }
  if (added) {
    first = index >= 0 && index < before ? index : before;
    for (int i = 0; i < added && first < before; i++) playlist_move(pl, before + i, first + i);
  } else if (index >= 0) {
    playlist_move(pl, first, first < index ? index - 1 : index);
    first = first < index ? index - 1 : index;
  }
  pl->selected = first;
  bool batch = g_app->tick - g_app->drop_tick <= WA_DROP_BATCH_TICKS;
  if (play && !batch) app_play_index(first);
  if (play) g_app->drop_tick = g_app->tick;
  app_invalidate_all();
  return true;
}

void app_shutdown(winamp_t *app) {
  if (!app) return;
  if (app->timer) axCancelTimer(app->timer);
  if (app->audio_dev) axAudioClose(app->audio_dev);
  axAudioShutdown();
  engine_close(&app->engine);
  playlist_clear(&app->list);
  skin_free(&app->skin);
  if (g_app == app) g_app = NULL;
  free(app);
}

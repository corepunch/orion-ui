// Groove tests: block synthesis, the offline mixer, snapping rules, and the
// sheet's drop handling through real windows.

#include "test_framework.h"
#include "test_env.h"
#include "apps/groove/groove.h"
#include "apps/groove/sample_levels.h"
#include <orion/user/toolbar.h>
#include <orion/commctl/appchrome.h>
#include <unistd.h>
#include <utime.h>

static float peak_of(const float *p, int n) { float m = 0; for (int i = 0; i < n; i++) m = fmaxf(m, fabsf(p[i])); return m; }

// The mute/solo button with this id inside the track header column.
static window_t *tracks_find(uint32_t id) {
  for (window_t *row = g_app->tracks ? g_app->tracks->children : NULL; row; row = row->next)
    for (window_t *b = row->children; b; b = b->next)
      if (b->id == id) return b;
  return NULL;
}

static bool same_demo_section(const song_t *s, int first, int second) {
  for (int at = 0; at < 8 * GR_TICKS_BAR; at += GR_SNAP_TICKS) for (int t = 1; t < 7; t++) {
    int a = song_clip_at(s, t, first + at), b = song_clip_at(s, t, second + at);
    if ((a < 0) != (b < 0)) return false;
    if (a >= 0 && (s->clips[a].block != s->clips[b].block ||
        first + at - s->clips[a].position != second + at - s->clips[b].position)) return false;
  }
  return true;
}

static void test_eurodance_demos(void) {
  TEST("three EuroDance demos last two to three minutes, develop their sections, and round-trip as editable songs");
  test_env_init();
  g_app = app_init();
  ASSERT_NOT_NULL(g_app);
  const int commands[] = { ID_FILE_DEMO, ID_FILE_DEMO_AFTERGLOW, ID_FILE_DEMO_VICTORY };
  const int bars[] = { 88, 88, 96 };
  for (int demo = 0; demo < GR_DEMO_COUNT; demo++) {
    app_command(commands[demo]);
    song_t expected = g_app->song;
    ASSERT_EQUAL(expected.bpm, 140);
    ASSERT(expected.nclips > 80, "a full arrangement is loaded");
    ASSERT_EQUAL(song_length_ticks(&expected), bars[demo] * GR_TICKS_BAR);
    int64_t frames = position_frames_for_bpm(song_length_ticks(&expected), expected.bpm);
    ASSERT(frames >= 120 * GR_SAMPLE_RATE && frames <= 180 * GR_SAMPLE_RATE, "the complete song is two to three minutes");
    ASSERT_FALSE(expected.loop);
    ASSERT_FALSE(expected.playing);
    ASSERT_EQUAL(expected.pos, 0);
    ASSERT_EQUAL(expected.preview_block, -1);
    ASSERT_EQUAL(g_app->filename[0], 0);
    for (int i = 0; i < expected.nclips; i++) {
      clip_t *a = &expected.clips[i];
      const block_t *b = block_get(a->block);
      ASSERT(b && b->audio.pcm && b->audio_bpm == 140, "all sources are ready at native tempo");
      ASSERT(b->cat != CAT_HAT, "full loops have no extra hat-only layer");
      int mix_db = a->gain_db + b->audio.balance_db;
      if (b->cat == CAT_KEYS) ASSERT(mix_db <= -3 && mix_db >= -7, "piano retains its approved total arrangement level");
      if (b->cat == CAT_DRUMS || b->cat == CAT_BASS) ASSERT_EQUAL(mix_db, 0);
      ASSERT(b->cat != CAT_KEYS && b->cat != CAT_BASS && b->cat != CAT_GUITAR || a->block >= 500, "tonal parts use the new original scores");
      ASSERT_EQUAL(a->position % GR_TICKS_BAR, 0);
      for (int j = i + 1; j < expected.nclips; j++) {
        clip_t *c = &expected.clips[j];
        ASSERT(a->track != c->track || a->position + song_clip_ticks(&expected, i) <= c->position ||
               c->position + song_clip_ticks(&expected, j) <= a->position, "no hidden overlapping loops, fills or calls");
      }
    }
    ASSERT(song_clip_at(&expected, 0, 48 * GR_TICKS_BAR) < 0, "the breakdown removes the beat");
    ASSERT(song_clip_at(&expected, 0, 72 * GR_TICKS_BAR) >= 0, "the final chorus restores the floor");
    for (int a = 0; a < bars[demo]; a += 8) for (int b = a + 8; b < bars[demo]; b += 8)
      ASSERT_FALSE(same_demo_section(&expected, a * GR_TICKS_BAR, b * GR_TICKS_BAR));
    int bar_frames = bar_frames_for_bpm(140);
    float *lr = malloc((size_t)bar_frames * 2 * sizeof(*lr));
    ASSERT_NOT_NULL(lr);
    song_t playing = expected;
    playing.playing = true;
    for (int bar = 0; bar < bars[demo]; bar++) {
      song_render(&playing, lr, bar_frames);
      double power = 0;
      for (int i = 0; i < bar_frames * 2; i++) { ASSERT_TRUE(isfinite(lr[i])); power += lr[i] * lr[i]; }
      ASSERT(power > 0.001, "every arranged bar contains audio");
    }
    song_render(&playing, lr, 1);
    ASSERT_FALSE(playing.playing);
    ASSERT_EQUAL(playing.pos, 0);
    ASSERT_EQUAL(lr[0], 0);
    free(lr);
    char copy[160];
    snprintf(copy, sizeof(copy), "/tmp/orion-eurodance-%ld-%d.groove", (long)getpid(), demo);
    ASSERT_TRUE(app_save_song(copy));
    ASSERT_TRUE(app_open_song(copy));
    ASSERT_EQUAL(g_app->song.nclips, expected.nclips);
    for (int i = 0; i < expected.nclips; i++) {
      clip_t *a = &g_app->song.clips[i], *b = &expected.clips[i];
      ASSERT(a->block == b->block && a->track == b->track && a->position == b->position && a->length == b->length && a->order == b->order && a->gain_db == b->gain_db,
             "saved projects preserve every editable placement and level");
    }
    remove(copy);
  }
  int clips = g_app->song.nclips;
  ASSERT_FALSE(app_load_demo_song(-1));
  ASSERT_FALSE(app_load_demo_song(GR_DEMO_COUNT));
  ASSERT_EQUAL(g_app->song.nclips, clips);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}

static void test_project_io(void) {
  TEST("Groove projects round-trip arrangement and mixer state, and invalid files leave the song intact");
  test_env_init();
  g_app = app_init();
  window_t *win = create_window("Groove", 0, MAKERECT(0, 0, 800, 600), NULL, main_win_proc, 0, g_app);
  ASSERT_NOT_NULL(win);
  g_app->song.bpm = 128;
  g_app->song.loop = false;
  g_app->song.mute[2] = true;
  g_app->song.solo[5] = true;
  ASSERT(song_add_clip(&g_app->song, 0, 1, 0) == 0, "first clip added");
  ASSERT(song_add_clip(&g_app->song, 1, 1, 0) == 1, "same-position clip added");
  ASSERT(song_add_clip(&g_app->song, 2, 6, 4 * GR_TICKS_BAR) == 2, "later clip added");
  ASSERT(song_add_clip(&g_app->song, 0, 3, 6 * GR_TICKS_BAR) == 3, "a second clip reuses its source");
  int resized = block_get(0)->bars * GR_TICKS_BAR + 2 * GR_FRACTION_TICKS;
  ASSERT_TRUE(song_resize_clip(&g_app->song, 3, resized));
  ASSERT_TRUE(song_set_clip_gain(&g_app->song, 0, -7));
  ASSERT_TRUE(song_set_clip_gain(&g_app->song, 3, -3));
  char path[128];
  snprintf(path, sizeof(path), "/tmp/orion-groove-project-%ld", (long)getpid());
  char saved[140]; snprintf(saved, sizeof(saved), "%s.groove", path);
  char bad[140]; snprintf(bad, sizeof(bad), "%s-bad.groove", path);
  ASSERT_TRUE(app_save_song(path));
  ASSERT(access(saved, F_OK) == 0, "save appends the project extension");
  FILE *f = fopen(saved, "rb"); ASSERT_NOT_NULL(f);
  char line[256]; ASSERT(fgets(line, sizeof(line), f) && strcmp(line, "ORION_GROOVE 4\n") == 0, "saves clip levels with the source table");
  int bpm, loop, assets, clips; uint32_t mute, solo;
  ASSERT(fscanf(f, "%d %d %u %u %d %d", &bpm, &loop, &mute, &solo, &assets, &clips) == 6 && assets == 3 && clips == 4, "writes one table entry per distinct source");
  fclose(f);
  app_new_song();
  ASSERT_TRUE(app_open_song(saved));
  ASSERT(g_app->song.bpm == 128 && !g_app->song.loop && g_app->song.mute[2] && g_app->song.solo[5], "tempo, loop, mute and solo are restored");
  ASSERT(g_app->song.nclips == 4 && g_app->song.clips[0].block == 0 && g_app->song.clips[1].block == 1 && g_app->song.clips[2].track == 6 && g_app->song.clips[3].block == 0, "clip sources and placements are restored");
  ASSERT(g_app->song.clips[0].position == 0 && g_app->song.clips[1].position == 0 && g_app->song.clips[2].position == 4 * GR_TICKS_BAR && g_app->song.clips[3].position == 6 * GR_TICKS_BAR, "clip positions are restored");
  ASSERT(g_app->song.clips[0].order == 1 && g_app->song.clips[1].order == 2 && g_app->song.clip_order == 4, "same-position stacking order is restored");
  ASSERT(g_app->song.clips[0].length == 0 && g_app->song.clips[3].length == resized, "only a resized clip stores a duration");
  ASSERT_EQUAL(song_clip_ticks(&g_app->song, 3), resized);
  ASSERT(g_app->song.clips[0].gain_db == -7 && g_app->song.clips[3].gain_db == -3 && g_app->song.clips[1].gain_db == 0,
         "copies of one source retain independent saved levels");
  ASSERT(block_get(0)->audio.pcm && block_get(0)->audio_bpm == 128, "loaded arrangement audio is ready at its saved tempo");
  f = fopen(bad, "wb"); ASSERT_NOT_NULL(f);
  fprintf(f, "ORION_GROOVE 2\n140 1 0 0 1 1\nB 0\t%s\n0 2 0 9\n", block_get(0)->name);
  fclose(f);
  ASSERT_TRUE(app_open_song(bad));
  ASSERT(g_app->song.nclips == 1 && g_app->song.clips[0].length == 0 && g_app->song.clips[0].track == 2, "a version 2 clip keeps its block length");
  ASSERT_EQUAL(song_clip_ticks(&g_app->song, 0), block_get(0)->bars * GR_TICKS_BAR);
  ASSERT_EQUAL(g_app->song.bpm, 140);
  ASSERT_EQUAL(g_app->song.clips[0].gain_db, 0);
  f = fopen(bad, "wb"); ASSERT_NOT_NULL(f);
  fputs("ORION_GROOVE 3\n140 0 0 0 1 1\nB 512\tCome On\n0 2 0 1 0\n", f);
  fclose(f);
  ASSERT_TRUE(app_open_song(bad));
  ASSERT_STR_EQUAL(block_get(g_app->song.clips[0].block)->name, "Come On");
  ASSERT_EQUAL(g_app->song.clips[0].gain_db, 0);
  for (int db = -61; db <= 7; db += 68) {
    f = fopen(bad, "wb"); ASSERT_NOT_NULL(f);
    fprintf(f, "ORION_GROOVE 4\n140 0 0 0 1 1\nB 0\t%s\n0 2 0 1 0 %d\n", block_get(0)->name, db);
    fclose(f);
    ASSERT_FALSE(app_open_song(bad));
    ASSERT_STR_EQUAL(block_get(g_app->song.clips[0].block)->name, "Come On");
  }
  f = fopen(bad, "wb"); ASSERT_NOT_NULL(f);
  fputs("ORION_GROOVE 3\n140 0 0 0 1 1\nB 527\tCanopy Sprint\n0 0 0 1 0\n", f);
  fclose(f);
  ASSERT_FALSE(app_open_song(bad));
  ASSERT_STR_EQUAL(block_get(g_app->song.clips[0].block)->name, "Come On");
  f = fopen(bad, "wb"); ASSERT_NOT_NULL(f); fputs("ORION_GROOVE 1\n70 1 0 0 1\n99999 0 0 1\n", f); fclose(f);
  int old_clips = g_app->song.nclips;
  ASSERT_FALSE(app_open_song(bad));
  ASSERT_EQUAL(g_app->song.nclips, old_clips);
  ASSERT_EQUAL(g_app->song.bpm, 140);
  f = fopen(bad, "wb"); ASSERT_NOT_NULL(f);
  fprintf(f, "ORION_GROOVE 3\n140 1 0 0 1 1\nB 0\t%s\n0 0 0 1 3\n", block_get(0)->name);
  fclose(f);
  ASSERT_FALSE(app_open_song(bad));
  ASSERT_EQUAL(g_app->song.nclips, old_clips);
  ASSERT_EQUAL(g_app->song.clips[0].track, 2);
  f = fopen(bad, "wb"); ASSERT_NOT_NULL(f); fputs("ORION_GROOVE 1\n120 1 0 0 1\n0 2 512 7\n", f); fclose(f);
  ASSERT_FALSE(app_open_song(bad));
  ASSERT_EQUAL(g_app->song.nclips, old_clips);
  ASSERT_EQUAL(g_app->song.bpm, 140);
  remove(saved); remove(bad);
  destroy_window(win);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}

// Dialog procs live later in this unity build.
static result_t songs_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
static result_t save_song_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

static void set_mtime(const char *path, time_t when) {
  struct utimbuf t = { when, when };
  utime(path, &t);
}

static void test_song_library(void) {
  TEST("iPad songs save into Groove's own storage; My Songs lists them newest first with previews read from the files");
  test_env_init();
  g_app = app_init();
  ASSERT_NOT_NULL(g_app);
#ifdef AX_PLATFORM_IOS
  const char *settings = axSettingsDirectory();
  ASSERT(g_app->songs_dir[0] && !strncmp(g_app->songs_dir, settings, strlen(settings)) && axPathExists(g_app->songs_dir),
         "iPad song storage is a folder inside the app's own settings area, not Files");
#else
  ASSERT(!g_app->songs_dir[0], "desktop keeps ordinary file dialogs and creates no song library");
#endif
  window_t *win = create_window("Groove", 0, MAKERECT(0, 0, 800, 600), NULL, main_win_proc, 0, g_app);
  ASSERT_NOT_NULL(win);
  char dir[128], path[512], other[512], broken[512], notes[512], name[128];
  snprintf(dir, sizeof(dir), "/tmp/orion-groove-songs-%ld", (long)getpid());
  ASSERT_TRUE(axMkDir(dir));
  snprintf(g_app->songs_dir, sizeof(g_app->songs_dir), "%s", dir);
  static const fe_component_desc_t preview_class = { .class_name = "GrooveSongPreview", .proc = win_song_preview };
  register_window_class(&preview_class);
  DB_CLASS(groove_songs_db);
  g_app->songs_db = create_database("songs", "groove_songs_db", dir);
  ASSERT_NOT_NULL(g_app->songs_db);

  const char *rejected[] = { "", "   ", "a/b", "a\\b", "C:song", ".hidden" };
  for (int i = 0; i < ARRAY_LEN(rejected); i++)
    ASSERT(!song_path_for_name(rejected[i], path, sizeof(path)), "empty names and names with path separators are rejected");
  ASSERT_TRUE(song_path_for_name("  Night Drive ", path, sizeof(path)));
  char expected[512]; snprintf(expected, sizeof(expected), "%s/Night Drive.groove", dir);
  ASSERT_STR_EQUAL(path, expected);
  song_name_from_path(path, name, sizeof(name));
  ASSERT_STR_EQUAL(name, "Night Drive");

  g_app->song.bpm = 128;
  ASSERT(song_add_clip(&g_app->song, 0, 1, 0) == 0 && song_add_clip(&g_app->song, 2, 4, 4 * GR_TICKS_BAR) == 1, "two clips placed");
  window_t *dlg = create_window_from_form(&groove_save_song_form, 0, 0, NULL, save_song_proc, 0, "Untitled");
  ASSERT_NOT_NULL(dlg);
  ASSERT_STR_EQUAL(get_window_item(dlg, ID_SAVE_SONG_NAME)->title, "Untitled");
  set_window_item_text(dlg, ID_SAVE_SONG_NAME, "%s", "Night Drive");
  send_message(dlg, evCommand, MAKEDWORD(ID_SAVE_SONG_OK, btnClicked), get_window_item(dlg, ID_SAVE_SONG_OK));
  ASSERT(!is_window(dlg) && !strcmp(g_app->filename, path) && access(path, F_OK) == 0, "Save Song writes <songs>/<name>.groove and closes");

  song_t probe;
  ASSERT_TRUE(song_read(path, &probe));
  ASSERT(probe.bpm == 128 && probe.nclips == 2 && probe.clips[1].track == 4, "song_read parses the arrangement");
  app_new_song();
  ASSERT(g_app->song.nclips == 0 && !g_app->filename[0], "song_read leaves the open song alone");
  g_app->song.bpm = 100;
  ASSERT(song_add_clip(&g_app->song, 1, 0, 0) == 0, "clip placed");
  ASSERT_TRUE(song_path_for_name("Afterglow", other, sizeof(other)));
  ASSERT_TRUE(app_save_song(other));
  dlg = create_window_from_form(&groove_save_song_form, 0, 0, NULL, save_song_proc, 0, "Afterglow");
  set_window_item_text(dlg, ID_SAVE_SONG_NAME, "%s", "Night Drive");
  send_message(dlg, evCommand, MAKEDWORD(ID_SAVE_SONG_OK, btnClicked), get_window_item(dlg, ID_SAVE_SONG_OK));
  ASSERT(is_window(dlg) && song_read(path, &probe) && probe.bpm == 128 && !strcmp(g_app->filename, other),
         "another song's name is only replaced after confirmation");
  send_message(dlg, evCommand, MAKEDWORD(ID_SAVE_SONG_CANCEL, btnClicked), get_window_item(dlg, ID_SAVE_SONG_CANCEL));
  ASSERT_FALSE(is_window(dlg));

  snprintf(broken, sizeof(broken), "%s/Broken.groove", dir);
  snprintf(notes, sizeof(notes), "%s/notes.txt", dir);
  FILE *f = fopen(broken, "wb"); ASSERT_NOT_NULL(f); fputs("not a song\n", f); fclose(f);
  f = fopen(notes, "wb"); ASSERT_NOT_NULL(f); fputs("ignored\n", f); fclose(f);
  time_t now = time(NULL);
  set_mtime(path, now); set_mtime(broken, now - 100); set_mtime(other, now - 200);

  window_t *songs = create_window_from_form(&groove_song_browser_form, 0, 0, NULL, songs_proc, 0, NULL);
  ASSERT_NOT_NULL(songs);
  window_t *grid = get_window_item(songs, ID_SONG_BROWSER_SONGS);
  const char *order[] = { "Night Drive", "Broken", "Afterglow" };
  int tiles = 0;
  for (window_t *card = grid->children; card; card = card->next, tiles++) {
    ASSERT(tiles < 3 && !strcmp(get_window_item(card, ID_SONG_CARD_TITLE)->title, order[tiles]), "cards are the saved songs, newest first");
    window_t *preview = get_window_item(card, ID_SONG_CARD_PREVIEW);
    const song_t *shown = preview ? preview->userdata : NULL;
    ASSERT(tiles == 1 ? !shown : shown && shown->nclips == (tiles ? 1 : 2), "each preview parses its own file; an unreadable one draws none");
  }
  ASSERT_EQUAL(tiles, 3);
  ASSERT(strstr(get_window_item(grid->children, ID_SONG_CARD_DETAIL)->title, "128 BPM") &&
         strstr(get_window_item(grid->children->next, ID_SONG_CARD_DETAIL)->title, "Unreadable"), "details show tempo or the read failure");
  ASSERT(!window_has_state(get_window_item(songs, ID_SONG_BROWSER_EMPTY), WINDOW_STATE_VISIBLE) &&
         send_message(grid, tgGetSelection, 0, NULL) == 0, "the newest song is selected");
  show_window(songs, true);
  window_t *third = get_window_item(grid->children->next->next, ID_SONG_CARD_PREVIEW);
  ui_event_t press = { .message = kEventLeftButtonDown,
    .x = (window_screen_x(third) + third->frame.w / 2) * UI_WINDOW_SCALE, .y = (window_screen_y(third) + third->frame.h / 2) * UI_WINDOW_SCALE };
  dispatch_message(&press);
  press.message = kEventLeftButtonUp;
  dispatch_message(&press);
  ASSERT_EQUAL(send_message(grid, tgGetSelection, 0, NULL), 2);
  send_message(songs, evCommand, MAKEDWORD(2, tgnActivate), grid);
  ASSERT(!is_window(songs) && !strcmp(g_app->filename, other) && g_app->song.bpm == 100 && g_app->song.nclips == 1,
         "activating a card opens that song and closes the screen");

  result_node_t *rows = (result_node_t *)send_db_message(g_app->songs_db, dbFetch, MAKEDWORD(TABLE_SONGS, 0), NULL);
  ASSERT(count_result_list(rows) == 3, "the datasource lists only .groove files");
  for (result_node_t *row = rows; row; row = row->next)
    ASSERT_TRUE(send_db_message(g_app->songs_db, dbDelete, TABLE_SONGS, (void *)(intptr_t)((song_entry_t *)row->data)->id));
  free_result_list(rows);
  ASSERT(access(path, F_OK) != 0 && access(other, F_OK) != 0 && access(broken, F_OK) != 0, "deleting a row removes its file");
  ASSERT_FALSE(send_db_message(g_app->songs_db, dbDelete, TABLE_SONGS, (void *)(intptr_t)1));
  songs = create_window_from_form(&groove_song_browser_form, 0, 0, NULL, songs_proc, 0, NULL);
  ASSERT(!get_window_item(songs, ID_SONG_BROWSER_SONGS)->children && window_has_state(get_window_item(songs, ID_SONG_BROWSER_EMPTY), WINDOW_STATE_VISIBLE) &&
         window_has_state(get_window_item(songs, ID_SONG_BROWSER_OPEN), WINDOW_STATE_DISABLED), "an empty library says how to add songs");
  destroy_window(songs);

  remove(notes); rmdir(dir);
  destroy_window(win);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}

// Renders one block and installs it, as the controller does on demand.
static void load_block(int id, int bpm) {
  block_pcm_t pcm;
  if (!block_render(id, bpm, &pcm)) return;
  block_install(id, bpm, &pcm);
  free(pcm.pcm);
}

static const block_t *named_block(const char *name) {
  for (int i = 0; i < blocks_count(); i++) if (!strcmp(block_get(i)->name, name)) return block_get(i);
  return NULL;
}

static void test_library_levels(void) {
  TEST("library levelling ignores silent padding and preserves each sound's envelope");
  int short_frames = GR_SAMPLE_RATE * 4 / 10, long_frames = GR_SAMPLE_RATE * 4;
  float *short_note = calloc((size_t)short_frames, sizeof(float)), *padded = calloc((size_t)long_frames, sizeof(float));
  ASSERT_NOT_NULL(short_note); ASSERT_NOT_NULL(padded);
  for (int i = 0; i < short_frames; i++) short_note[i] = .8f * sinf(i * 6.28318530718f * 440 / GR_SAMPLE_RATE);
  memcpy(padded, short_note, (size_t)short_frames * sizeof(float));
  int short_db = sample_balance(short_note, short_frames, CAT_KEYS), long_db = sample_balance(padded, long_frames, CAT_KEYS);
  ASSERT_EQUAL(short_db, long_db);
  ASSERT(short_db < kSampleLevels[CAT_KEYS].gain_db, "sustained energy overrides the nominal trim for an outlier");
  ASSERT(memcmp(short_note, padded, (size_t)short_frames * sizeof(float)) == 0, "padding with silence cannot make a note louder");
  float gain = powf(10, short_db * .05f);
  for (int i = 0; i < short_frames; i++) {
    float expected = .8f * sinf(i * 6.28318530718f * 440 / GR_SAMPLE_RATE) * gain;
    ASSERT(short_note[i] == expected, "the trim is constant, with no compression or attack changes");
  }
  memset(padded, 0, (size_t)long_frames * sizeof(float));
  padded[0] = .85f;
  ASSERT_EQUAL(sample_balance(padded, long_frames, CAT_KEYS), kSampleLevels[CAT_KEYS].gain_db);
  ASSERT_EQUAL(padded[0], .85f * powf(10, kSampleLevels[CAT_KEYS].gain_db * .05f));
  memset(padded, 0, (size_t)long_frames * sizeof(float));
  sample_balance(padded, long_frames, CAT_VOX);
  ASSERT_EQUAL(peak_of(padded, long_frames), 0);
  free(short_note); free(padded);
  PASS();
}

static void test_blocks(void) {
  TEST("every block renders finite, non-silent, bar-aligned audio at the slowest, default and fastest tempo");
  ASSERT(blocks_count() > 400 && blocks_count() <= GR_MAX_BLOCKS, "library size");
  const int tempos[] = { GR_BPM_MIN, GR_BPM_DEFAULT, GR_BPM_MAX };
  for (int i = 0; i < blocks_count(); i++) {
    const block_t *b = block_get(i);
    block_pcm_t pcm;
    ASSERT(b->name && b->name[0] && b->cat < CAT_COUNT && b->family < FAMILY_COUNT && (b->bars == 1 || b->bars == 2 || b->bars == 4), "metadata");
    ASSERT(b->display_name && b->display_name[0] && b->variant <= 99, "display label and numeric variant metadata");
    ASSERT(b->genres && !(b->genres & ~GENRE_ANY), "every block carries at least one known genre tag");
    for (int k = 0; k < i; k++) ASSERT(strcmp(block_get(k)->name, b->name) != 0, "names are unique");
    int bpm = tempos[i % 3]; // each block at one tempo keeps the run short; the three tempos cover the engine
    ASSERT(block_render(i, bpm, &pcm), "recipe renders");
    ASSERT(pcm.pcm && pcm.frames == b->bars * bar_frames_for_bpm(bpm), "frame count");
    ASSERT(pcm.npeaks == b->bars * GR_PEAKS_BAR, "overview size");
    for (int k = 0; k < pcm.frames; k++) ASSERT(isfinite(pcm.pcm[k]), "NaN or inf sample");
    const level_profile_t *profile = &kSampleLevels[b->cat];
    level_measure_t m = sample_measure(pcm.pcm, pcm.frames);
    ASSERT(m.peak > .001f && m.peak <= profile->peak + .00001f, "role-specific peak headroom");
    ASSERT(m.active_rms <= profile->active_rms + .00001f && m.window_rms <= profile->window_rms + .00001f, "active sound and loud passages stay within their role's limits");
    ASSERT(pcm.balance_db <= profile->gain_db, "library balance never boosts an outlier");
    ASSERT(fabsf(pcm.pcm[pcm.frames - 1]) < 0.01f, "tail is faded");
    free(pcm.pcm);
  }
  const char *piano_chords[] = { "Am - F", "C - G", "Dm - G", "F - C" };
  for (int i = 0; i < ARRAY_LEN(piano_chords); i++) {
    const block_t *b = named_block(piano_chords[i]);
    ASSERT(b && !strcmp(b->display_name, "Piano Chords") && b->variant == i + 1, "piano chord variants share a name and carry their number");
  }
  const char *pad_progressions[] = { "Pad Am", "Pad F - G", "Pad C - G" };
  for (int i = 0; i < ARRAY_LEN(pad_progressions); i++) {
    const block_t *b = named_block(pad_progressions[i]);
    ASSERT(b && !strcmp(b->display_name, "Pad Progression") && b->variant == i + 1, "pad progression variants share a name and carry their number");
  }
  const char *four_floors[] = { "Four Floor", "Disco", "Clap Beat" };
  for (int i = 0; i < ARRAY_LEN(four_floors); i++) {
    const block_t *b = named_block(four_floors[i]);
    ASSERT(b && !strcmp(b->display_name, "Four-on-Floor") && b->variant == i + 1, "four-on-floor beats share a name and carry their number");
  }
  block_pcm_t none;
  ASSERT(!block_render(-1, GR_BPM_DEFAULT, &none) && !block_render(blocks_count(), GR_BPM_DEFAULT, &none), "out-of-range ids are rejected");
  PASS();
}

static void test_genres(void) {
  TEST("genre tags are flags: every genre covers every core family, and shared blocks carry several");
  const category_t core[] = { CAT_DRUMS, CAT_KICK, CAT_SNARE, CAT_HAT, CAT_CLAP, CAT_CYMBAL, CAT_PERC, CAT_FILL,
                              CAT_BASS, CAT_KEYS, CAT_SYNTH, CAT_PAD, CAT_STAB, CAT_VOX, CAT_FX };
  int shared = 0;
  for (int g = 0; g < GENRE_COUNT; g++) {
    int total = 0;
    ASSERT(kGenreName[g] && kGenreName[g][0], "genre name");
    for (int i = 0; i < blocks_count(); i++) total += (block_get(i)->genres >> g) & 1;
    ASSERT(total >= 150, "each genre offers a full palette");
    for (int c = 0; c < ARRAY_LEN(core); c++) {
      int n = 0;
      for (int i = 0; i < blocks_count(); i++) n += block_get(i)->cat == core[c] && ((block_get(i)->genres >> g) & 1);
      ASSERT(n >= 3, "each genre has at least three blocks in every core family");
    }
  }
  for (int i = 0; i < blocks_count(); i++) shared += (block_get(i)->genres & (block_get(i)->genres - 1)) != 0;
  ASSERT(shared > 100, "many blocks belong to more than one genre");
  int ids[GR_MAX_BLOCKS], sum = 0;
  for (int c = 0; c < FAMILY_COUNT; c++) { int n = blocks_in_family(c, ids, GR_MAX_BLOCKS); ASSERT(n > 0 || c == FAMILY_SAMPLES, "only the user recording family may start empty"); sum += n; }
  ASSERT_EQUAL(sum, blocks_count());
  ASSERT(FAMILY_COUNT == 11 && block_family(CAT_FILL) == FAMILY_DRUM && block_family(CAT_CYMBAL) == FAMILY_LOOP,
         "percussion has a rhythm tab and a fill tab");
  ASSERT(block_family(CAT_KEYS) == FAMILY_SYNTH && block_family(CAT_ORGAN) == FAMILY_SYNTH && block_family(CAT_SYNTH) == FAMILY_SYNTH,
         "keys, organ and synth share a tab while retaining their sample types");
  const char *fills[] = { "Snare Fill", "Kick Rush", "Breakdown", "Clap Fill", "Kick Run", "Snare Run", "Clap Rush",
                          "Crash Down", "Floor Build", "909 Crash", "Reverse Crash", "Crash & Ride", "Boom Tail", "Dusty Crash",
                          "Rave Build", "Kick Roll Up", "Big Crash", "China Hits", "Hat Sweep", "Delay Crash", "Click Train", "Tom Down" };
  for (int i = 0; i < ARRAY_LEN(fills); i++) {
    const block_t *b = named_block(fills[i]);
    ASSERT(b && b->family == FAMILY_DRUM, "a one-shot, rising roll or descending fill belongs to Drum Fills");
  }
  const char *loops[] = { "Four Floor", "Half Kick", "Clap Back", "Ride 8ths", "Bell Pattern", "Splash", "Conga Loop", "Ride Swing" };
  for (int i = 0; i < ARRAY_LEN(loops); i++) {
    const block_t *b = named_block(loops[i]);
    ASSERT(b && b->family == FAMILY_LOOP, "a repeating drum pattern belongs to Drum Loops");
  }
  ASSERT(named_block("Crash Down")->cat == CAT_CYMBAL && named_block("Kick Run")->cat == CAT_KICK,
         "moving a sample between tabs preserves its detailed type and pictogram");
  PASS();
}

static void test_tempo(void) {
  TEST("installing a block for a new tempo resizes it and hands the old buffer back");
  load_block(0, GR_BPM_DEFAULT);
  uint64_t revision = block_get(0)->audio_revision;
  block_pcm_t pcm[1];
  ASSERT_TRUE(block_render(0, 140, &pcm[0]));
  block_install(0, 140, &pcm[0]);
  ASSERT(block_get(0)->audio_revision > revision && block_get(0)->audio_bpm == 140, "tempo changes invalidate waveform textures");
  ASSERT(pcm[0].frames == bar_frames_for_bpm(GR_BPM_DEFAULT) * block_get(0)->bars, "old buffer handed back");
  ASSERT(block_get(0)->audio.frames == bar_frames_for_bpm(140) * block_get(0)->bars, "new length");
  song_t s;
  song_init(&s);
  s.bpm = 140;
  int position = GR_SNAP_TICKS + 1;
  ASSERT(song_add_clip(&s, 0, 0, position) >= 0, "fine position at a new tempo");
  int64_t start = (int64_t)position * bar_frames_for_bpm(140) / GR_TICKS_BAR;
  ASSERT_EQUAL(position_frames_for_bpm(position, 140), start);
  float lr[2 * 512];
  s.playing = true; s.loop = false; s.pos = start - 256;
  song_render(&s, lr, 512);
  ASSERT(peak_of(lr, 2 * 256) == 0, "silence until the fractional start at the new tempo");
  for (int i = 0; i < 256; i++) {
    ASSERT(lr[2 * (256 + i)] == block_get(0)->audio.pcm[i] * 0.5f, "fractional start is sample exact");
    ASSERT(lr[2 * (256 + i) + 1] == lr[2 * (256 + i)], "stereo channels agree");
  }
  int64_t end = start + block_get(0)->audio.frames;
  s.pos = end - 1;
  song_render(&s, lr, 2);
  ASSERT(!s.playing && s.pos == 0 && lr[2] == 0, "fractional endpoint stops without adding a bar");
  s.playing = true; s.loop = true; s.pos = end - 1;
  song_render(&s, lr, 2);
  ASSERT(s.playing && s.pos == 1 && lr[2] == 0, "fractional endpoint loops into the initial silence");
  free(pcm[0].pcm);
  float *released = block_release(0);
  ASSERT(released && !block_get(0)->audio.pcm && block_get(0)->audio.npeaks > 0, "releasing drops the PCM and keeps the overview");
  free(released);
  PASS();
}

static void test_song_rules(void) {
  TEST("clip positions retain tick precision while overlaps crop the earlier clip");
  song_t s;
  song_init(&s);
  int two_bar = -1, one_bar = -1;
  for (int i = 0; i < blocks_count(); i++) { if (block_get(i)->bars == 2 && two_bar < 0) two_bar = i; if (block_get(i)->bars == 1 && one_bar < 0) one_bar = i; }
  int start = 4 * GR_TICKS_BAR + GR_SNAP_TICKS;
  ASSERT(song_add_clip(&s, two_bar, 0, start) == 0, "first clip starts at a quarter bar");
  int overlap = song_add_clip(&s, one_bar, 0, start + GR_TICKS_BAR);
  ASSERT(overlap >= 0 && song_clip_end(&s, 0) == start + GR_TICKS_BAR, "overlap inside a 2-bar clip crops its tail");
  ASSERT(song_add_clip(&s, one_bar, 0, start - GR_TICKS_BAR) >= 0 && song_add_clip(&s, one_bar, 0, start + 2 * GR_TICKS_BAR) >= 0, "adjacent clips allowed");
  ASSERT(song_add_clip(&s, one_bar, 1, start + GR_TICKS_BAR) >= 0, "other track is independent");
  ASSERT(song_add_clip(&s, two_bar, 2, (GR_BARS - 2) * GR_TICKS_BAR + 1) < 0 && song_add_clip(&s, one_bar, GR_TRACKS, 0) < 0, "bounds");
  ASSERT(song_clip_at(&s, 0, start) == 0 && song_clip_at(&s, 0, start + 2 * GR_TICKS_BAR - 1) == overlap, "lookup follows the visible clip at fractional starts");
  ASSERT(song_length_ticks(&s) == 7 * GR_TICKS_BAR + GR_SNAP_TICKS, "length retains fractional tail");
  ASSERT(song_can_place(&s, 0, start, 2 * GR_TICKS_BAR, 0), "moving a clip ignores its own footprint");
  ASSERT(song_can_place(&s, 0, start - 1, 2 * GR_TICKS_BAR, 0) && song_can_place(&s, 0, start + 1, 2 * GR_TICKS_BAR, 0), "single-tick overlaps with neighbors are allowed");
  song_remove_clip(&s, 0);
  ASSERT(song_clip_at(&s, 0, start) < 0 && s.nclips == 4, "removal");
  ASSERT(song_add_clip(&s, one_bar, 2, 1) >= 0, "model retains precision finer than the default snap");
  ASSERT(song_clip_at(&s, 2, 0) < 0 && song_clip_at(&s, 2, 1) >= 0 && song_clip_at(&s, 2, GR_TICKS_BAR + 1) < 0, "fractional clip endpoints");
  ASSERT(song_add_clip(&s, one_bar, 3, (GR_BARS - 1) * GR_TICKS_BAR) >= 0, "clip can end exactly at sheet boundary");
  PASS();
}

static void test_mixer(void) {
  TEST("mixer places a clip at a fractional bar, honours mute/solo, loops and stops");
  song_t s;
  song_init(&s);
  int bar = bar_frames_for_bpm(s.bpm);
  static float lr[2 * 4096];
  int blk = 0;
  load_block(blk, s.bpm);
  int position = GR_TICKS_BAR + GR_SNAP_TICKS;
  int64_t start = position_frames_for_bpm(position, s.bpm), end = start + bar;
  song_add_clip(&s, blk, 0, position);
  s.playing = true;
  s.loop = false;
  s.pos = start - 1000;
  song_render(&s, lr, 2000);
  ASSERT(peak_of(lr, 2 * 1000) == 0.0f, "silence before the clip");
  ASSERT(peak_of(lr + 2 * 1000, 2 * 1000) > 0.0f, "audio starts at the quarter-bar offset");
  ASSERT(lr[2 * 1000] == block_get(blk)->audio.pcm[0] * 0.5f || block_get(blk)->audio.pcm[0] == 0.0f, "offset is sample exact");
  s.pos = start; s.mute[0] = true;
  song_render(&s, lr, 1024);
  ASSERT(peak_of(lr, 2048) == 0.0f, "muted track is silent");
  s.mute[0] = false; s.solo[3] = true;
  s.pos = start;
  song_render(&s, lr, 1024);
  ASSERT(peak_of(lr, 2048) == 0.0f, "soloing another track silences this one");
  s.solo[3] = false; s.pos = end - 100;
  song_render(&s, lr, 400);
  ASSERT(!s.playing && s.pos == 0, "stops and rewinds at the end when not looping");
  s.loop = true; s.playing = true; s.pos = end - 100;
  song_render(&s, lr, 400);
  ASSERT(s.playing && s.pos == 300, "wraps when looping");
  s.preview_block = blk; s.preview_pos = 0; s.playing = false;
  song_render(&s, lr, 512);
  ASSERT(peak_of(lr, 1024) > 0.0f && s.preview_pos == 512, "audition plays while stopped");
  PASS();
}

static void test_clip_gain(void) {
  TEST("clip gain changes only that placement, survives edits, and leaves source audition intact");
  song_t s;
  song_init(&s);
  load_block(0, s.bpm);
  ASSERT_EQUAL(song_add_clip(&s, 0, 0, 0), 0);
  ASSERT_EQUAL(s.clips[0].gain_db, 0);
  float original[2048], mixed[2048];
  s.playing = true;
  song_render(&s, original, 1024);
  ASSERT(peak_of(original, 2048) > 0, "the source is audible at default gain");
  ASSERT_TRUE(song_set_clip_gain(&s, 0, -6));
  s.pos = 0;
  song_render(&s, mixed, 1024);
  float scale = powf(10.0f, -6 * 0.05f);
  for (int i = 0; i < 2048; i++) ASSERT(fabsf(mixed[i] - original[i] * scale) < 0.0000001f, "gain is applied after source levelling");
  ASSERT_EQUAL(song_add_clip(&s, 0, 1, 0), 1);
  ASSERT_EQUAL(s.clips[1].gain_db, 0);
  s.pos = 0;
  song_render(&s, mixed, 1024);
  for (int i = 0; i < 2048; i++) ASSERT(fabsf(mixed[i] - original[i] * (1 + scale)) < 0.0000001f, "another placement of the same source stays at unity");
  ASSERT_TRUE(song_move_clip(&s, 0, 2, GR_TICKS_BAR));
  ASSERT_TRUE(song_resize_clip(&s, 0, 2 * GR_TICKS_BAR));
  ASSERT_EQUAL(s.clips[0].gain_db, -6);
  ASSERT_FALSE(song_set_clip_gain(&s, 0, -61));
  ASSERT_FALSE(song_set_clip_gain(&s, 0, 7));
  ASSERT_FALSE(song_set_clip_gain(&s, -1, 0));
  ASSERT_FALSE(song_set_clip_gain(&s, s.nclips, 0));
  ASSERT_FALSE(song_set_clip_gain(NULL, 0, 0));
  ASSERT_EQUAL(s.clips[0].gain_db, -6);
  ASSERT_TRUE(song_set_clip_gain(&s, 0, -60));
  ASSERT_TRUE(song_set_clip_gain(&s, 0, 6));
  s.playing = false; s.preview_block = 0; s.preview_pos = 0;
  song_render(&s, mixed, 1024);
  for (int i = 0; i < 1024; i++) ASSERT(mixed[i * 2] == block_get(0)->audio.pcm[i] * 0.7f, "audition keeps its existing source level");
  PASS();
}

static void test_sheet_drop(void) {
  TEST("dropping a block snaps to the grid; identical starts cover and restore the earlier clip");
  test_env_init();
  g_app = app_init();
  ASSERT(g_app != NULL, "app_init");
  window_t *win = create_window("Groove", 0, MAKERECT(0, 0, 1100, 700), NULL, main_win_proc, 0, g_app);
  ASSERT(win && g_app->sheet, "main window and sheet");
  show_window(win, true);
  int before = g_app->song.nclips;
  int track = 7, bar = 10;
  int sx = window_screen_x(g_app->sheet) + bar * 88 + 20;
  int row = CLAMP((get_client_rect(g_app->sheet).h - 22) / GR_TRACKS, 26, 64);
  int sy = window_screen_y(g_app->sheet) + 22 + track * row + row / 2;
  g_app->drag = (drag_t){ .active = true, .block = 0, .from_clip = -1, .track = -1 };
  send_message(g_app->sheet, shDragOver, MAKEDWORD(sx, sy), NULL);
  ASSERT(g_app->drag.track == track && g_app->drag.position == bar * GR_TICKS_BAR + GR_SNAP_TICKS && g_app->drag.valid, "ghost snaps to the cell");
  send_message(g_app->sheet, shDrop, MAKEDWORD(sx, sy), NULL);
  ASSERT(g_app->song.nclips == before + 1 && song_clip_at(&g_app->song, track, bar * GR_TICKS_BAR + GR_SNAP_TICKS) >= 0, "clip added");
  ASSERT(!g_app->drag.active, "drag state cleared");
  g_app->drag = (drag_t){ .active = true, .block = 0, .from_clip = -1, .track = -1 };
  send_message(g_app->sheet, shDrop, MAKEDWORD(sx, sy), NULL);
  ASSERT(g_app->song.nclips == before + 2, "second drop on the same cell preserves both clips");
  g_app->selected_clip = song_clip_at(&g_app->song, track, bar * GR_TICKS_BAR + GR_SNAP_TICKS);
  app_command(ID_DELETE);
  ASSERT(g_app->song.nclips == before + 1, "delete restores the covered clip");
  g_app->selected_clip = song_clip_at(&g_app->song, track, bar * GR_TICKS_BAR + GR_SNAP_TICKS);
  app_command(ID_DELETE);
  ASSERT_EQUAL(g_app->song.nclips, before);
  ASSERT_TRUE(app_drop(&(drag_t){ .block = 0, .from_clip = -1, .track = 0, .position = 0 }));
  ASSERT_TRUE(app_block_audio(1));
  app_set_bpm(141);
  ASSERT(g_app->song.bpm == 141 && block_get(0)->audio.frames == bar_frames_for_bpm(141) * block_get(0)->bars, "bpm change re-renders the blocks in the song");
  ASSERT(!block_get(1)->audio.pcm, "blocks the song does not use are dropped until they are needed again");
  ASSERT(app_block_audio(1) && block_get(1)->audio.frames == bar_frames_for_bpm(141) * block_get(1)->bars, "and render at the new tempo on demand");
  destroy_window(win);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}

static void test_overlap_audio(void) {
  TEST("overlaps crop playback sample exactly, retain source timing, and restore after deletion or movement");
  song_t s;
  song_init(&s);
  int long_block = 0;
  while (long_block < blocks_count() && block_get(long_block)->bars < 2) long_block++;
  ASSERT(long_block < blocks_count(), "multi-bar block exists");
  load_block(0, s.bpm);
  load_block(long_block, s.bpm);
  ASSERT_EQUAL(song_add_clip(&s, long_block, 0, 0), 0);
  clip_t original = s.clips[0];
  const block_t *a = block_get(long_block), *b = block_get(0);
  int cut = GR_SNAP_TICKS + 1;
  int overlay = song_add_clip(&s, 0, 0, cut);
  ASSERT_EQUAL(overlay, 1);
  ASSERT_EQUAL(song_clip_end(&s, 0), cut);
  ASSERT_EQUAL(song_length_ticks(&s), cut + GR_TICKS_BAR);
  ASSERT(s.clips[0].block == original.block && s.clips[0].track == original.track &&
         s.clips[0].position == original.position && s.clips[0].order == original.order, "underlying clip is unchanged");
  int64_t start = position_frames_for_bpm(cut, s.bpm);
  float lr[128];
  s.playing = true; s.loop = false; s.pos = start - 32;
  song_render(&s, lr, 64);
  for (int i = 0; i < 64; i++) {
    float expected = (i < 32 ? a->audio.pcm[start - 32 + i] : b->audio.pcm[i - 32]) * 0.5f;
    ASSERT(lr[2 * i] == expected && lr[2 * i + 1] == expected, "only one clip plays across the cutoff, without stretching or summing");
  }
  int end = cut + GR_TICKS_BAR;
  ASSERT_EQUAL(song_clip_at(&s, 0, end), -1);
  s.pos = position_frames_for_bpm(end, s.bpm);
  song_render(&s, lr, 64);
  ASSERT(!s.playing && peak_of(lr, 128) == 0, "cropped tail does not resume after a shorter overlay");
  int next = song_add_clip(&s, 0, 0, 2 * GR_SNAP_TICKS);
  ASSERT_EQUAL(song_clip_end(&s, overlay), 2 * GR_SNAP_TICKS);
  song_remove_clip(&s, next);
  ASSERT_EQUAL(song_clip_end(&s, overlay), end);
  ASSERT_TRUE(song_move_clip(&s, overlay, 1, cut));
  ASSERT_EQUAL(song_clip_end(&s, 0), 2 * GR_TICKS_BAR);
  ASSERT_EQUAL(song_clip_at(&s, 0, cut), 0);
  ASSERT_TRUE(song_move_clip(&s, overlay, 0, cut));
  song_remove_clip(&s, overlay);
  ASSERT_EQUAL(song_clip_end(&s, 0), 2 * GR_TICKS_BAR);
  s.playing = true; s.pos = start;
  song_render(&s, lr, 64);
  for (int i = 0; i < 64; i++) ASSERT(lr[2 * i] == a->audio.pcm[start + i] * 0.5f, "deleted overlay restores the original source offset");
  overlay = song_add_clip(&s, 0, 0, 0);
  ASSERT_EQUAL(song_clip_end(&s, 0), 0);
  ASSERT_EQUAL(song_clip_at(&s, 0, 0), overlay);
  ASSERT_EQUAL(song_length_ticks(&s), GR_TICKS_BAR);
  s.pos = 0;
  song_render(&s, lr, 64);
  for (int i = 0; i < 64; i++) ASSERT(lr[2 * i] == b->audio.pcm[i] * 0.5f, "identical starts play only the latest drop");
  ASSERT_TRUE(song_move_clip(&s, 0, 0, 0));
  ASSERT_EQUAL(song_clip_at(&s, 0, 0), 0);
  ASSERT_EQUAL(song_clip_end(&s, overlay), 0);
  song_remove_clip(&s, 0);
  ASSERT_EQUAL(song_clip_at(&s, 0, 0), 0);
  ASSERT_EQUAL(song_clip_end(&s, 0), GR_TICKS_BAR);
  s.nclips = GR_MAX_CLIPS;
  ASSERT(!song_can_place(&s, 0, 0, GR_TICKS_BAR, -1), "full songs reject new clips");
  ASSERT(song_can_place(&s, 0, 0, GR_TICKS_BAR, 0), "full songs still permit moves");
  PASS();
}

static void test_overlap_sheet(void) {
  TEST("routed drops crop card windows; selection, full-width drags, moves and deletion restore earlier cards");
  test_env_init();
  g_app = app_init();
  window_t *win = create_window("Groove", 0, MAKERECT(0, 0, 1100, 760), NULL, main_win_proc, 0, g_app);
  ASSERT_NOT_NULL(win);
  show_window(win, true);
  window_t *sheet = g_app->sheet, *library = g_app->bin->children;
  int long_block = 0, start = 3 * GR_TICKS_BAR + GR_SNAP_TICKS, cut = start + GR_SNAP_TICKS;
  while (long_block < blocks_count() && block_get(long_block)->bars < 2) long_block++;
  ASSERT(long_block < blocks_count(), "multi-bar block exists");
  ASSERT_EQUAL(song_add_clip(&g_app->song, long_block, 0, start), 0);
  send_message(sheet, evResize, 0, NULL);
  window_t *original = sheet->children;
  ASSERT_EQUAL(original->frame.w, 2 * 88);
  int scroll = 83;
  set_scroll_info(sheet, SB_HORZ, &(scroll_info_t){ .fMask = SIF_POS, .nPos = scroll }, false);
  send_message(sheet, evHScroll, 0, NULL);
  ui_event_t event = {.message = kEventLeftButtonDown,
    .x = (window_screen_x(library) + 4) * UI_WINDOW_SCALE,
    .y = (window_screen_y(library) + 4) * UI_WINDOW_SCALE};
  dispatch_message(&event);
  ASSERT_TRUE(g_ui_runtime.captured == library);
  int x = cut * 88 / GR_TICKS_BAR - scroll;
  event.message = kEventLeftButtonDragged;
  event.x = (window_screen_x(sheet) + x + 4) * UI_WINDOW_SCALE;
  event.y = (window_screen_y(sheet) + 22 + 4) * UI_WINDOW_SCALE;
  dispatch_message(&event);
  ASSERT(g_app->drag.valid && g_app->drag.position == cut, "overlapping target is accepted");
  event.message = kEventLeftButtonUp;
  dispatch_message(&event);
  ASSERT_EQUAL(g_app->song.nclips, 2);
  send_message(sheet, evResize, 0, NULL);
  ASSERT_EQUAL(original->frame.w, 22);
  ASSERT_EQUAL(original->next->frame.w, 88);
  ASSERT_EQUAL(original->frame.x + original->frame.w, original->next->frame.x);
  layout_measure_t measure = {0};
  send_message(original, evMeasure, 0, &measure);
  ASSERT_EQUAL(measure.desired_w, 2 * 88);
  event.message = kEventLeftButtonDown;
  event.x = (window_screen_x(sheet) + original->frame.x + 4) * UI_WINDOW_SCALE;
  dispatch_message(&event);
  ASSERT_EQUAL(g_app->selected_clip, 0);
  event.message = kEventLeftButtonDragged;
  event.x += 6 * UI_WINDOW_SCALE;
  dispatch_message(&event);
  ASSERT_TRUE(window_is_lifted(original));
  ASSERT_EQUAL(original->frame.w, 2 * 88);
  event.message = kEventPointerCancel;
  dispatch_message(&event);
  send_message(sheet, evResize, 0, NULL);
  ASSERT_EQUAL(original->frame.w, 22);
  ASSERT_TRUE(app_drop(&(drag_t){ .from_clip = 1, .track = 1, .position = cut }));
  send_message(sheet, evResize, 0, NULL);
  ASSERT_EQUAL(original->frame.w, 2 * 88);
  ASSERT_TRUE(app_drop(&(drag_t){ .from_clip = 1, .track = 0, .position = start }));
  send_message(sheet, evResize, 0, NULL);
  ASSERT_FALSE(window_has_state(original, WINDOW_STATE_VISIBLE));
  event.message = kEventRightButtonDown;
  event.x = (window_screen_x(sheet) + original->frame.x + 4) * UI_WINDOW_SCALE;
  dispatch_message(&event);
  ASSERT_EQUAL(g_app->song.nclips, 1);
  send_message(sheet, evResize, 0, NULL);
  ASSERT_TRUE(window_has_state(original, WINDOW_STATE_VISIBLE));
  ASSERT_EQUAL(original->frame.w, 2 * 88);
  ASSERT_EQUAL(g_app->song.clips[0].position, start);
  ASSERT_EQUAL(g_app->song.clips[0].block, long_block);
  destroy_window(win);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}

static void test_resized_clip_drag(void) {
  TEST("resized clip drags retain their stored length through layout, scrolling, cancellation and drop");
  test_env_init();
  g_app = app_init();
  window_t *win = create_window("Groove", 0, MAKERECT(0, 0, 1100, 760), NULL, main_win_proc, 0, g_app);
  ASSERT_NOT_NULL(win);
  show_window(win, true);
  window_t *sheet = g_app->sheet;
  int natural = block_get(0)->bars * GR_TICKS_BAR, start = 2 * GR_TICKS_BAR;
  int lengths[] = {2 * natural + GR_SNAP_TICKS, natural - GR_SNAP_TICKS};
  ASSERT_EQUAL(song_add_clip(&g_app->song, 0, 0, start), 0);
  for (int i = 0; i < ARRAY_LEN(lengths); i++) {
    int ticks = lengths[i], width = ticks * 88 / GR_TICKS_BAR;
    ASSERT_TRUE(app_resize_clip(0, ticks));
    send_message(sheet, evResize, 0, NULL);
    window_t *card = sheet->children;
    ASSERT_EQUAL(card->frame.w, width);
    int hx = card->frame.x + 4, hy = card->frame.y + 4;
    send_message(sheet, evLeftButtonDown, MAKEDWORD(hx, hy), NULL);
    send_message(sheet, evMouseMove, MAKEDWORD(hx + 12, hy), NULL);
    ASSERT_TRUE(window_is_lifted(card));
    ASSERT_EQUAL(card->frame.w, width);
    send_message(sheet, evResize, 0, NULL);
    ASSERT_EQUAL(card->frame.w, width);
    int screen_x = window_screen_x(card) + window_lift_delta(card).x;
    set_scroll_info(sheet, SB_HORZ, &(scroll_info_t){ .fMask = SIF_POS, .nPos = 83 }, false);
    send_message(sheet, evHScroll, 0, NULL);
    ASSERT_EQUAL(card->frame.w, width);
    ASSERT_EQUAL(window_screen_x(card) + window_lift_delta(card).x, screen_x);
    send_message(sheet, evPointerCancel, 0, NULL);
    send_message(sheet, evResize, 0, NULL);
    ASSERT_FALSE(window_is_lifted(card));
    ASSERT_EQUAL(card->frame.w, width);
    ASSERT_EQUAL(g_app->song.clips[0].position, start);
    ASSERT_EQUAL(song_clip_ticks(&g_app->song, 0), ticks);
    set_scroll_info(sheet, SB_HORZ, &(scroll_info_t){ .fMask = SIF_POS, .nPos = 0 }, false);
    send_message(sheet, evHScroll, 0, NULL);

    ASSERT_EQUAL(song_add_clip(&g_app->song, 1, 0, start + GR_SNAP_TICKS), 1);
    send_message(sheet, evResize, 0, NULL);
    ASSERT_EQUAL(card->frame.w, 22);
    hx = card->frame.x + 4;
    send_message(sheet, evLeftButtonDown, MAKEDWORD(hx, hy), NULL);
    send_message(sheet, evMouseMove, MAKEDWORD(hx + 12, hy), NULL);
    send_message(sheet, evResize, 0, NULL);
    ASSERT_TRUE(window_is_lifted(card));
    ASSERT_EQUAL(card->frame.w, width);
    send_message(sheet, evLeftButtonUp, MAKEDWORD(hx + 12, hy + sheet_row_h(sheet)), NULL);
    send_message(sheet, evResize, 0, NULL);
    ASSERT_FALSE(window_is_lifted(card));
    ASSERT_EQUAL(card->frame.w, width);
    ASSERT_EQUAL(g_app->song.clips[0].track, 1);
    ASSERT_EQUAL(song_clip_ticks(&g_app->song, 0), ticks);
    song_remove_clip(&g_app->song, 1);
    ASSERT_TRUE(song_move_clip(&g_app->song, 0, 0, start));
  }
  destroy_window(win);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}

static void test_fractional_selection(void) {
  TEST("fractional clips select and delete at their visible edges after scrolling; empty canvas and the ruler seek by quarter bars");
  test_env_init();
  g_app = app_init();
  window_t *win = create_window("Groove", 0, MAKERECT(0, 0, 800, 600), NULL, main_win_proc, 0, g_app);
  ASSERT_NOT_NULL(win);
  show_window(win, true);
  window_t *sheet = g_app->sheet;
  int position = 3 * GR_TICKS_BAR + GR_SNAP_TICKS;
  int clip = song_add_clip(&g_app->song, 0, 0, position);
  ASSERT(clip >= 0, "fractional clip added");
  int scroll = 83, left = 3 * 88 + 22;
  set_scroll_info(sheet, SB_HORZ, &(scroll_info_t){ .fMask = SIF_POS, .nPos = scroll }, false);
  send_message(sheet, evHScroll, 0, NULL);
  ASSERT_EQUAL(sheet->children->frame.x, left - scroll);
  const int offsets[] = {-1, 0, 87, 88};
  int64_t held = g_app->song.pos;
  int snap_w = 88 * GR_SNAP_TICKS / GR_TICKS_BAR;
  for (int i = 0; i < ARRAY_LEN(offsets); i++) {
    ui_event_t event = {.message = kEventLeftButtonDown,
      .x = (window_screen_x(sheet) + left - scroll + offsets[i]) * UI_WINDOW_SCALE,
      .y = (window_screen_y(sheet) + 30) * UI_WINDOW_SCALE};
    dispatch_message(&event);
    bool on_clip = i == 1 || i == 2;
    ASSERT_EQUAL(g_app->selected_clip, on_clip ? clip : -1);
    if (on_clip) {
      ASSERT_EQUAL(g_app->song.pos, held);
    } else {
      int snapped = (3 * 88 + snap_w + offsets[i]) / snap_w * GR_SNAP_TICKS;
      held = position_frames_for_bpm(snapped, g_app->song.bpm);
      ASSERT_EQUAL(g_app->song.pos, held);
    }
    event.message = kEventPointerCancel;
    dispatch_message(&event);
  }
  ui_event_t event = {.message = kEventLeftButtonDown,
    .x = (window_screen_x(sheet) + left - scroll + 23) * UI_WINDOW_SCALE,
    .y = (window_screen_y(sheet) + 10) * UI_WINDOW_SCALE};
  dispatch_message(&event);
  ASSERT_EQUAL(g_app->song.pos, position_frames_for_bpm(3 * GR_TICKS_BAR + 2 * GR_SNAP_TICKS, g_app->song.bpm));
  event.message = kEventLeftButtonUp;
  dispatch_message(&event);
  event.message = kEventRightButtonDown;
  event.x = (window_screen_x(sheet) + left - scroll + 87) * UI_WINDOW_SCALE;
  event.y = (window_screen_y(sheet) + 30) * UI_WINDOW_SCALE;
  dispatch_message(&event);
  ASSERT_EQUAL(g_app->song.nclips, 0);
  app_load_demo();
  ASSERT(g_app->song.nclips > 0 && song_length_ticks(&g_app->song) == 88 * GR_TICKS_BAR, "the complete demo is loaded");
  for (int i = 0; i < g_app->song.nclips; i++) ASSERT_EQUAL(g_app->song.clips[i].position % GR_TICKS_BAR, 0);
  app_command(ID_FORWARD);
  ASSERT(g_app->song.pos == position_frames_for_bpm(88 * GR_TICKS_BAR, g_app->song.bpm), "forward jumps to the end of the song");
  app_command(ID_REWIND);
  ASSERT_EQUAL(g_app->song.pos, 0);
  int late = song_clip_at(&g_app->song, 2, 72 * GR_TICKS_BAR);
  ASSERT(late >= 0, "the final hook lies beyond the former timeline limit");
  set_scroll_info(sheet, SB_HORZ, &(scroll_info_t){ .fMask = SIF_POS, .nPos = 71 * 88 }, false);
  send_message(sheet, evHScroll, 0, NULL);
  ASSERT_EQUAL(get_scroll_pos(sheet, SB_HORZ), 71 * 88);
  event = (ui_event_t){.message = kEventLeftButtonDown,
    .x = (window_screen_x(sheet) + 98) * UI_WINDOW_SCALE,
    .y = (window_screen_y(sheet) + GR_RULER_H + 2 * sheet_row_h(sheet) + 8) * UI_WINDOW_SCALE};
  dispatch_message(&event);
  event.message = kEventLeftButtonUp;
  dispatch_message(&event);
  ASSERT_EQUAL(g_app->selected_clip, late);
  ASSERT_TRUE(app_load_demo_song(GR_DEMO_AFTERGLOW));
  ASSERT_EQUAL(get_scroll_pos(sheet, SB_HORZ), 0);
  ASSERT_EQUAL(g_app->song.pos, 0);
  destroy_window(win);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}

static void test_two_finger_sheet_pan(void) {
  TEST("two-finger canvas pan cancels a clip drag and scrolls through the framework without editing the song");
  test_env_init();
  g_app = app_init();
  window_t *win = create_window("Groove", 0, MAKERECT(0, 0, 800, 600), NULL, main_win_proc, 0, g_app);
  ASSERT_NOT_NULL(win);
  show_window(win, true);
  int clip = song_add_clip(&g_app->song, 0, 0, 0);
  window_t *sheet = g_app->sheet;
  int sx = window_screen_x(sheet) + 32, sy = window_screen_y(sheet) + 40;
  ui_event_t event = {.message = kEventLeftButtonDown, .x = sx * UI_WINDOW_SCALE, .y = sy * UI_WINDOW_SCALE};
  dispatch_message(&event);
  ASSERT_TRUE(g_ui_runtime.captured == sheet);
  event.message = kEventLeftButtonDragged; event.x += 12 * UI_WINDOW_SCALE; event.dx = 12;
  dispatch_message(&event);
  ASSERT_TRUE(g_app->drag.active);
  event.message = kEventPointerCancel;
  dispatch_message(&event);
  ASSERT_FALSE(g_app->drag.active);
  ASSERT_TRUE(g_ui_runtime.captured == NULL);
  event = (ui_event_t){.message = kEventGesture,
    .gesture = {AX_GESTURE_BEGIN, sx * UI_WINDOW_SCALE, sy * UI_WINDOW_SCALE,
                sx * UI_WINDOW_SCALE, sy * UI_WINDOW_SCALE, 1, 0}};
  dispatch_message(&event);
  event.gesture = (ax_gesture_t){AX_GESTURE_UPDATE, (sx - 80) * UI_WINDOW_SCALE, (sy + 20) * UI_WINDOW_SCALE,
                                sx * UI_WINDOW_SCALE, sy * UI_WINDOW_SCALE, 1.2f, 0.1f};
  dispatch_message(&event);
  ASSERT_EQUAL(get_scroll_pos(sheet, SB_HORZ), 80);
  ASSERT_EQUAL(g_app->song.nclips, 1);
  ASSERT_EQUAL(g_app->song.clips[clip].position, 0);
  ASSERT_EQUAL(g_app->song.clips[clip].track, 0);
  ASSERT_EQUAL(g_app->song.preview_block, -1);
  event.gesture.phase = AX_GESTURE_END;
  dispatch_message(&event);
  window_t *mute = tracks_find(ID_MUTE(0)), *solo = tracks_find(ID_SOLO(0));
  ASSERT_TRUE(mute && solo && window_screen_y(mute) >= window_screen_y(sheet) + GR_RULER_H);
  ASSERT_TRUE(window_screen_x(mute) + mute->frame.w <= window_screen_x(sheet));
  event = (ui_event_t){.message = kEventLeftButtonDown,
    .x = (window_screen_x(mute) + mute->frame.w / 2) * UI_WINDOW_SCALE,
    .y = (window_screen_y(mute) + mute->frame.h / 2) * UI_WINDOW_SCALE};
  dispatch_message(&event);
  event.message = kEventLeftButtonUp;
  dispatch_message(&event);
  ASSERT_TRUE(g_app->song.mute[0]);
  invalidate_window(g_app->tracks);
  send_message(g_app->tracks, evPaint, 0, NULL);
  ASSERT_TRUE(mute->value);
  char tooltip[256] = {0};
  ASSERT_TRUE(send_message(mute, evGetTooltipText, 0, tooltip) && strcmp(tooltip, "Mute track") == 0);
  event.message = kEventLeftButtonDown;
  event.x = (window_screen_x(solo) + solo->frame.w / 2) * UI_WINDOW_SCALE;
  event.y = (window_screen_y(solo) + solo->frame.h / 2) * UI_WINDOW_SCALE;
  dispatch_message(&event);
  event.message = kEventLeftButtonUp;
  dispatch_message(&event);
  ASSERT_TRUE(g_app->song.solo[0]);
  ASSERT_EQUAL(g_app->song.nclips, 1);
  ASSERT_EQUAL(g_app->song.preview_block, -1);
  ASSERT_FALSE(g_app->drag.active);
  event = (ui_event_t){.message = kEventLeftButtonDown,
    .x = (window_screen_x(sheet) + 2) * UI_WINDOW_SCALE, .y = sy * UI_WINDOW_SCALE};
  dispatch_message(&event);
  ASSERT_EQUAL(g_app->selected_clip, clip);
  event.message = kEventPointerCancel;
  dispatch_message(&event);
  destroy_window(win);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}

static int visible_tiles(window_t *page) {
  int n = 0;
  for (window_t *c = page->children; c; c = c->next) n += window_has_state(c, WINDOW_STATE_VISIBLE);
  return n;
}

static int library_visible(void) { return visible_tiles(g_app->bin); }

// Index of a toolbar item, by command ident.
static int bar_index(window_t *bar, uint16_t ident) {
  toolbar_state_t *tb = toolbar_get_state(bar);
  for (int i = 0; tb && i < tb->item_count; i++) if (tb->items[i].ident == ident) return i;
  return -1;
}
static int toolbar_index(uint16_t ident) { return bar_index(g_app->library, ident); }

static void click_bar(window_t *bar, uint16_t ident) {
  int i = bar_index(bar, ident);
  ASSERT_TRUE(i >= 0);
  irect16_t r = toolbar_get_state(bar)->item_rects[i];
  ui_event_t event = {.message = kEventLeftButtonDown,
    .x = (window_screen_x(bar) + r.x + r.w / 2) * UI_WINDOW_SCALE,
    .y = (window_screen_y(bar) + r.y + r.h / 2) * UI_WINDOW_SCALE};
  dispatch_message(&event);
  event.message = kEventLeftButtonUp;
  dispatch_message(&event);
}
static void click_toolbar(uint16_t ident) { click_bar(g_app->library, ident); }

static bool bar_checked(window_t *bar, uint16_t ident) {
  int i = bar_index(bar, ident);
  return i >= 0 && (toolbar_get_state(bar)->items[i].state & TBSTATE_CHECKED);
}
static bool toolbar_checked(uint16_t ident) { return bar_checked(g_app->library, ident); }

static void test_library_transport(void) {
  TEST("transport and the family buttons share one toolbar row; nested mouse routing reaches every button");
  test_env_init();
  g_app = app_init();
  window_t *win = create_window("Groove", 0, MAKERECT(0, 0, 1000, 700), NULL, main_win_proc, 0, g_app);
  ASSERT_NOT_NULL(win);
  show_window(win, true);
  window_t *bar = g_app->library;
  ASSERT_TRUE(!(win->flags & WINDOW_TOOLBAR) && win->toolbar == NULL);
  ASSERT_TRUE(bar && (bar->flags & WINDOW_TOOLBAR) && bar->parent == win);
  ASSERT(get_window_item(bar, ID_GENRE) == NULL, "the genre filter is hidden");
  ASSERT((bar->flags & (WINDOW_NOCLOSE | WINDOW_NOCOLLAPSE)) == (WINDOW_NOCLOSE | WINDOW_NOCOLLAPSE) && (bar->dock->flags & DOCK_NOFLOAT),
         "the library stays docked, with no close or collapse button");
  ASSERT_EQUAL(g_app->sheet->frame.y, 0);
  ASSERT(g_app->chrome && g_app->menubar_win && app_chrome_menubar(g_app->chrome) == g_app->menubar_win, "the chrome owns the menu bar");
  ASSERT(g_app->toolbar == app_chrome_toolbar(g_app->chrome) && bar_index(g_app->toolbar, ID_DELETE) == 0 && bar_index(g_app->toolbar, ID_LOOP) == 1,
         "delete and loop live on the compact toolbar");
  ASSERT_EQUAL(bar->frame.y, g_app->sheet->frame.y + g_app->sheet->frame.h + DOCK_SPLITTER);
  ASSERT_EQUAL(bar->frame.h, 280);
  ASSERT_TRUE(g_app->bin->parent == bar);
  ASSERT_EQUAL(g_app->bin->frame.y, 0);
  ASSERT_EQUAL(window_screen_y(g_app->bin), window_screen_y(bar) + titlebar_height(bar));
  toolbar_state_t *tb = toolbar_get_state(bar);
  ASSERT_EQUAL(tb->style, TOOLBAR_STYLE_PLASTIC);
  for (int i = 0; i < tb->item_count; i++) ASSERT(tb->items[i].type != TOOLBAR_ITEM_LABEL, "the merged caption shows no title");
  ASSERT_EQUAL(toolbar_index(ID_REWIND), 0);
  ASSERT(tb->strip.tex == g_app->pictograms && tb->strip.cols == 6 && tb->strip.icon_w == 128 && tb->strip.icon_h == 128,
         "the family toolbar uses the shared pictogram atlas metadata in headless tests");
  ASSERT_TRUE(strcmp(tb->items[toolbar_index(ID_REWIND)].icon, "phosphor-rewind-fill") == 0);
  ASSERT_TRUE(tb->items[toolbar_index(ID_PLAY)].color != 0 && tb->items[toolbar_index(ID_PLAY)].color == tb->items[toolbar_index(ID_FORWARD)].color);
  ASSERT(toolbar_index(ID_PLAY) == toolbar_index(ID_REWIND) + 1 && toolbar_index(ID_FORWARD) == toolbar_index(ID_PLAY) + 1, "rewind, play, forward sit together");
  ASSERT(toolbar_index(ID_STOP) < 0 && toolbar_index(ID_DELETE) < 0, "stop is menu-only and delete moved to the compact toolbar");
  uint32_t original_color = tb->items[toolbar_index(ID_PLAY)].color, color = WEB(0x2277bb);
  ASSERT_TRUE(send_message(bar, tbSetItemColor, ID_PLAY, &color));
  ASSERT_EQUAL(tb->items[toolbar_index(ID_PLAY)].color, color);
  ASSERT_FALSE(send_message(bar, tbSetItemColor, ID_GENRE, &color));
  ASSERT_FALSE(send_message(bar, tbCheckButton, ID_GENRE, (void *)1));
  ASSERT_FALSE(send_message(bar, tbSetItemColor, 0xffff, &color));
  ASSERT_TRUE(send_message(bar, tbSetItemColor, ID_PLAY, NULL));
  ASSERT_EQUAL(tb->items[toolbar_index(ID_PLAY)].color, 0);
  ASSERT_TRUE(send_message(bar, tbSetItemColor, ID_PLAY, &original_color));

#ifdef AX_PLATFORM_IOS
  ASSERT_EQUAL(toolbar_effective_bsz(bar), BUTTON_HEIGHT + 4);
#else
  ASSERT_EQUAL(toolbar_effective_bsz(bar), TB_SPACING);
#endif
  ASSERT_EQUAL(titlebar_height(bar), toolbar_effective_bsz(bar) + 2 * toolbar_effective_padding(bar));
  for (int width = 1000; width >= 720; width -= 280) {
    resize_window(win, width, 700);
    for (int i = 0; i < tb->item_count; i++) {
      ASSERT_TRUE(tb->item_rects[i].x >= 0 && tb->item_rects[i].x + tb->item_rects[i].w <= bar->frame.w);
    }
  }
  irect16_t play = tb->item_rects[toolbar_index(ID_PLAY)], fwd = tb->item_rects[toolbar_index(ID_FORWARD)];
  int inset = get_theme()->toolbar.large_inset, small = theme_control_extent(toolbar_effective_bsz(bar), CONTROL_SIZE_SMALL);
#ifndef AX_PLATFORM_IOS
  ASSERT_EQUAL(small, 32);
#endif
  ASSERT(play.w == play.h && play.y == inset && play.y + play.h == titlebar_height(bar) - inset,
         "the large play button fills the band to the theme inset without growing it");
  ASSERT(fwd.w == small && fwd.h == small && small < toolbar_effective_bsz(bar) && play.y + play.h / 2 == fwd.y + fwd.h / 2,
         "rewind and forward are small and share play's centreline");
  irect16_t r = tb->item_rects[toolbar_index(ID_PLAY)];
  ui_event_t event = {.message = kEventLeftButtonDown,
    .x = (window_screen_x(bar) + r.x + r.w / 2) * UI_WINDOW_SCALE,
    .y = (window_screen_y(bar) + r.y + r.h / 2) * UI_WINDOW_SCALE};
  dispatch_message(&event);
  event.message = kEventLeftButtonUp;
  dispatch_message(&event);
  ASSERT(g_app->song.playing && toolbar_checked(ID_PLAY), "play checks the play button");
  ASSERT_TRUE(strcmp(tb->items[toolbar_index(ID_PLAY)].checked_icon, "phosphor-pause-fill") == 0);
  click_toolbar(ID_PLAY);
  ASSERT(!g_app->song.playing && !toolbar_checked(ID_PLAY), "pause stops playback and shows play again");
  click_toolbar(ID_PLAY);
  g_app->song.playing = false; // the mixer reached the end
  send_message(win, evTimer, 0, NULL);
  ASSERT(!toolbar_checked(ID_PLAY), "the next tick shows play once the mixer stops");
  click_toolbar(ID_PLAY);
  ASSERT(g_app->song.playing && toolbar_checked(ID_PLAY), "so the button starts playback again");
  app_set_playing(false);
  ASSERT(toolbar_index(ID_LOOP) < 0, "loop is not on the library toolbar");
  resize_window(g_app->chrome, 1000, 700); // the headless display reports no screen size
  toolbar_state_t *ct = toolbar_get_state(g_app->toolbar);
  int pitch = ct->item_rects[1].x - ct->item_rects[0].x;
  int last = g_app->toolbar->frame.x + ct->item_rects[1].x + ct->item_rects[1].w / 2;
  irect16_t restore = rect_split_right(get_client_rect(g_app->menubar_win), get_client_rect(g_app->menubar_win).h);
  ASSERT(toolbar_dock_hint(g_app->toolbar) == TOOLBAR_DOCK_MENU && restore.x + restore.w / 2 - last == pitch,
         "compact buttons and the menu bar's restore button share one pitch");
  ASSERT(g_app->song.loop && bar_checked(g_app->toolbar, ID_LOOP), "a new song loops, and the compact button shows it");
  ASSERT_TRUE(strcmp(ct->items[1].checked_icon, "arrow-right-to-line") == 0);
  click_bar(g_app->toolbar, ID_LOOP);
  ASSERT(!g_app->song.loop && !bar_checked(g_app->toolbar, ID_LOOP), "the compact loop button toggles loop off");
  click_bar(g_app->toolbar, ID_LOOP);
  ASSERT(g_app->song.loop && bar_checked(g_app->toolbar, ID_LOOP), "and back on");
  char tooltip[256] = {0};
  resize_window(win, SCREEN_W, 700);
  r = tb->item_rects[toolbar_index(ID_FAMILY(FAMILY_GUITAR))];
  ASSERT(send_message(bar->toolbar, evGetTooltipText, MAKEDWORD(r.x + 4, r.y + 4), tooltip) &&
         strcmp(tooltip, kFamilyName[FAMILY_GUITAR]) == 0, "a family button names its family");
  click_toolbar(ID_FAMILY(FAMILY_GUITAR));
  ASSERT(g_app->family == FAMILY_GUITAR && library_visible() > 0 && library_visible() < blocks_count(), "a routed family click switches the library");
  ASSERT(g_app->song.loop && bar_checked(g_app->toolbar, ID_LOOP) && toolbar_checked(ID_FAMILY(FAMILY_GUITAR)), "the family check leaves Loop checked");
  irect16_t area = rect_offset(dock_content_rect(g_app->chrome), window_screen_x(g_app->chrome), window_screen_y(g_app->chrome));
  ASSERT_TRUE(maximize_window(win));
  ASSERT_EQUAL(win->frame.x, area.x);
  ASSERT_EQUAL(win->frame.y, area.y);
  ASSERT_EQUAL(win->frame.w, area.w);
  ASSERT_EQUAL(win->frame.h, area.h);
  ASSERT_EQUAL(g_app->sheet->frame.y, 0);
  ASSERT_TRUE(restore_window(win));
  destroy_window(win);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}

static void test_shared_block_cards(void) {
  TEST("canvas and library share the same card widget, measure, spacing and library drag behavior");
  test_env_init();
  g_app = app_init();
  window_t *win = create_window("Groove", 0, MAKERECT(0, 0, 1000, 700), NULL, main_win_proc, 0, g_app);
  ASSERT_NOT_NULL(win);
  int clip = song_add_clip(&g_app->song, 0, 0, 0);
  send_message(g_app->sheet, evResize, 0, NULL);
  window_t *canvas = g_app->sheet->children, *page = g_app->bin;
  window_t *library = page->children;
  ASSERT_TRUE(canvas && library && canvas->proc == win_block_card && library->proc == canvas->proc);
  layout_measure_t a = {0}, b = {0};
  send_message(canvas, evMeasure, 0, &a);
  send_message(library, evMeasure, 0, &b);
  ASSERT_EQUAL(a.desired_w, b.desired_w);
  ASSERT_EQUAL(a.desired_h, b.desired_h);
  send_message(page, evResize, 0, NULL);
  ASSERT_EQUAL(library->frame.h, canvas->frame.h);
  ASSERT_EQUAL(library->next->frame.x, library->frame.x + library->frame.w);
  send_message(library, evLeftButtonDown, MAKEDWORD(4, 4), NULL);
  send_message(library, evLeftButtonUp, MAKEDWORD(4, 4), NULL);
  ASSERT_EQUAL(g_app->song.preview_block, 0);
  send_message(library, evLeftButtonDown, MAKEDWORD(4, 4), NULL);
  ASSERT_EQUAL(g_app->song.preview_block, 0);
  send_message(library, evMouseMove, MAKEDWORD(12, 4), NULL);
  ASSERT_TRUE(g_app->drag.active && window_is_lifted(library) && window_lift_is_copy(library));
  ASSERT(g_app->song.preview_block == -1, "starting a library drag stops the audition");
  send_message(library, evPointerCancel, 0, NULL);
  ASSERT_FALSE(g_app->drag.active || window_is_lifted(library));
  ASSERT_TRUE(g_ui_runtime.captured == NULL);
  app_select_clip(clip);
  send_message(g_app->sheet, evResize, 0, NULL);
  ASSERT_EQUAL(g_app->song.nclips, 1);
  destroy_window(win);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}

static window_t *library_card(window_t *page, int block) {
  window_t *card = page->children;
  int ids[GR_MAX_BLOCKS];
  for (int family = 0; family < FAMILY_COUNT; family++) {
    int count = blocks_in_family((family_t)family, ids, ARRAY_LEN(ids));
    for (int i = 0; i < count && card; i++, card = card->next) if (ids[i] == block) return card;
  }
  return NULL;
}

static void test_drag_anchor(void) {
  TEST("library and canvas drops snap the exact card origin, including scrolling and release position; pointer anchoring is optional");
  test_env_init();
  g_app = app_init();
  window_t *win = create_window("Groove", 0, MAKERECT(0, 0, 1000, 700), NULL, main_win_proc, 0, g_app);
  ASSERT_NOT_NULL(win);
  show_window(win, true);
  window_t *sheet = g_app->sheet, *page = g_app->bin;
  int block = 4; // Half Time: a visible two-bar loop
  window_t *library = library_card(page, block);
  ASSERT_NOT_NULL(library);
  send_message(page, evResize, 0, NULL);
  set_scroll_info(page, SB_VERT, &(scroll_info_t){ .fMask = SIF_POS, .nPos = 20 }, false);
  send_message(page, evVScroll, 0, NULL);
  set_scroll_info(sheet, SB_HORZ, &(scroll_info_t){ .fMask = SIF_POS, .nPos = 120 }, false);
  send_message(sheet, evHScroll, 0, NULL);
  int row = CLAMP((get_client_rect(sheet).h - 22) / GR_TRACKS, 26, 64);
  ipoint16_t grab = {160, row - 4};
  int sx = window_screen_x(sheet) + 7 * 88 + 10 + grab.x - get_scroll_pos(sheet, SB_HORZ);
  int sy = window_screen_y(sheet) + 22 + 5 * row + 6 + grab.y;
  send_message(library, evLeftButtonDown, MAKEDWORD(grab.x, grab.y), NULL);
  send_message(library, evMouseMove, MAKEDWORD(sx - window_screen_x(library), sy - window_screen_y(library)), NULL);
  ASSERT_TRUE(g_app->drag.active && window_is_lifted(library));
  ASSERT(g_app->drag.grab.x == grab.x, "library horizontal grab offset");
  ASSERT(g_app->drag.grab.y == grab.y, "library vertical grab offset");
  ASSERT_TRUE(g_app->drag.valid && g_app->drag.position == 7 * GR_TICKS_BAR && g_app->drag.track == 5);
  send_message(library, evLeftButtonUp, MAKEDWORD(sx + 88 - window_screen_x(library), sy - window_screen_y(library)), NULL);
  int clip = g_app->selected_clip;
  ASSERT(clip >= 0, "library drop selects the added clip");
  ASSERT(song_clip_at(&g_app->song, 5, 8 * GR_TICKS_BAR) == clip, "library release snaps to card origin");
  ASSERT(g_app->song.nclips == 1, "library drop adds a clip");
  ASSERT_FALSE(g_app->drag.active || window_is_lifted(library));
  ASSERT_NULL(g_ui_runtime.captured);

  ASSERT_TRUE(send_message(sheet, shSetDropAnchor, GR_DROP_ANCHOR_POINTER, NULL));
  g_app->drag = (drag_t){ .active = true, .block = block, .from_clip = -1, .grab = grab, .track = -1 };
  send_message(sheet, shDragOver, MAKEDWORD(sx, sy), NULL);
  ASSERT_TRUE(g_app->drag.valid && g_app->drag.position == 8 * GR_TICKS_BAR + 3 * GR_SNAP_TICKS && g_app->drag.track == 6);
  send_message(sheet, shDrop, MAKEDWORD(sx, sy), NULL);
  ASSERT_TRUE(song_clip_at(&g_app->song, 6, 8 * GR_TICKS_BAR + 3 * GR_SNAP_TICKS) >= 0);
  ASSERT(g_app->song.nclips == 2, "moves and rejected drops preserve clip count");
  ASSERT_TRUE(send_message(sheet, shSetDropAnchor, GR_DROP_ANCHOR_SAMPLE, NULL));

  send_message(sheet, evResize, 0, NULL);
  int mx = 8 * 88 + grab.x, my = 22 + 5 * row + grab.y;
  send_message(sheet, evLeftButtonDown, MAKEDWORD(mx, my), NULL);
  send_message(sheet, evMouseMove, MAKEDWORD(mx - 6, my), NULL);
  ASSERT(g_app->drag.position == 8 * GR_TICKS_BAR && g_app->drag.track == 5, "small movements keep the clip at its nearest grid origin");
  mx = 6 * 88 + 10 + grab.x;
  my = 22 + 2 * row + 6 + grab.y;
  send_message(sheet, evMouseMove, MAKEDWORD(mx, my), NULL);
  ASSERT_TRUE(g_app->drag.active && g_app->drag.valid);
  ASSERT_TRUE(g_app->drag.position == 6 * GR_TICKS_BAR && g_app->drag.track == 2);
  send_message(sheet, evLeftButtonUp, MAKEDWORD(mx + 20, my), NULL);
  ASSERT_TRUE(g_app->song.clips[clip].position == 6 * GR_TICKS_BAR + GR_SNAP_TICKS && g_app->song.clips[clip].track == 2);
  ASSERT(g_app->song.nclips == 2, "moves and rejected drops preserve clip count");
  ASSERT_FALSE(g_app->drag.active);
  ASSERT_NULL(g_ui_runtime.captured);

  g_app->drag = (drag_t){ .active = true, .block = block, .from_clip = -1, .grab = grab, .track = -1 };
  sx = window_screen_x(sheet) + 2;
  sy = window_screen_y(sheet) + 24;
  send_message(sheet, shDragOver, MAKEDWORD(sx, sy), NULL);
  ASSERT_TRUE(g_app->drag.valid && g_app->drag.position == 0 && g_app->drag.track == 0);
  send_message(sheet, shDrop, MAKEDWORD(sx - 3, sy), NULL);
  ASSERT(g_app->song.nclips == 2, "moves and rejected drops preserve clip count");
  ASSERT_FALSE(g_app->drag.active);
  destroy_window(win);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}

static void test_drag_center_boundaries(void) {
  TEST("real drags use the sample center across half-step boundaries, independent of grab offset and scrolling");
  test_env_init();
  g_app = app_init();
  window_t *win = create_window("Groove", 0, MAKERECT(0, 0, 1100, 760), NULL, main_win_proc, 0, g_app);
  ASSERT_NOT_NULL(win);
  show_window(win, true);
  window_t *sheet = g_app->sheet, *page = g_app->bin;
  send_message(page, evResize, 0, NULL);
  const int percent[] = {20, 49, 51, 80};
  for (int block = 0; block <= 4; block += 4) {
    window_t *library = library_card(page, block);
    ASSERT_NOT_NULL(library);
    ipoint16_t size = clip_cell_size(sheet, block_get(block));
    const ipoint16_t grabs[] = {{5, 5}, {size.x / 2, size.y / 2}, {size.x - 5, size.y - 5}};
    for (int scroll = 0; scroll <= 83; scroll += 83) {
      set_scroll_info(sheet, SB_HORZ, &(scroll_info_t){ .fMask = SIF_POS, .nPos = scroll }, false);
      send_message(sheet, evHScroll, 0, NULL);
      for (int grab = 0; grab < ARRAY_LEN(grabs); grab++) {
        ui_event_t event = {.message = kEventLeftButtonDown,
          .x = (window_screen_x(library) + grabs[grab].x) * UI_WINDOW_SCALE,
          .y = (window_screen_y(library) + grabs[grab].y) * UI_WINDOW_SCALE};
        dispatch_message(&event);
        ASSERT(g_ui_runtime.captured == library, "library receives the routed press");
        for (int x = 0; x < ARRAY_LEN(percent); x++) for (int y = 0; y < ARRAY_LEN(percent); y++) {
          int dx = (22 * percent[x] + (percent[x] > 50 ? 99 : 0)) / 100;
          int dy = (size.y * percent[y] + (percent[y] > 50 ? 99 : 0)) / 100;
          int sx = window_screen_x(sheet) + 3 * 88 + 22 - scroll + dx;
          int sy = window_screen_y(sheet) + 22 + 2 * size.y + dy;
          event.message = kEventLeftButtonDragged;
          event.x = (sx + grabs[grab].x) * UI_WINDOW_SCALE;
          event.y = (sy + grabs[grab].y) * UI_WINDOW_SCALE;
          dispatch_message(&event);
          ASSERT(g_app->drag.active && g_app->drag.valid, "drag preview is valid");
          ASSERT(window_screen_x(library) + window_lift_delta(library).x == sx && window_screen_y(library) + window_lift_delta(library).y == sy, "preview is derived from the actual lifted card");
          ASSERT(g_app->drag.position == 3 * GR_TICKS_BAR + GR_SNAP_TICKS * (1 + (percent[x] > 50)), "column changes after the center crosses halfway");
          ASSERT(g_app->drag.track == 2 + (percent[y] > 50), "row changes after the center crosses halfway");
        }
        event.message = kEventLeftButtonUp;
        dispatch_message(&event);
        ASSERT(g_app->song.nclips == 1 && song_clip_at(&g_app->song, 3, 3 * GR_TICKS_BAR + 2 * GR_SNAP_TICKS) >= 0, "80% diagonal drag commits to the bottom-right placement");
        ASSERT_FALSE(g_app->drag.active || window_is_lifted(library));
        app_new_song();
      }
    }
  }
  destroy_window(win);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}

static void test_clip_resize(void) {
  TEST("resizing a clip keeps one placement, loops the extension, and trims the prefix");
  song_t s;
  song_init(&s);
  load_block(0, s.bpm);
  int natural = block_get(0)->bars * GR_TICKS_BAR;
  ASSERT_EQUAL(song_add_clip(&s, 0, 0, 0), 0);
  ASSERT(song_clip_ticks(&s, 0) == natural && s.clips[0].length == 0, "a new clip uses its block length");
  ASSERT_FALSE(song_resize_clip(&s, 0, natural + 1));
  ASSERT_FALSE(song_resize_clip(&s, -1, natural));
  ASSERT_EQUAL(s.clips[0].length, 0);
  ASSERT_TRUE(song_resize_clip(&s, 0, natural + GR_FRACTION_TICKS));
  ASSERT(s.nclips == 1 && s.clips[0].length == natural + GR_FRACTION_TICKS, "the same clip grows by one fraction");
  const float *pcm = block_get(0)->audio.pcm;
  int64_t at = position_frames_for_bpm(natural, s.bpm);
  ASSERT_EQUAL(at, block_get(0)->audio.frames);
  float lr[8];
  s.playing = true; s.loop = false; s.pos = at;
  song_render(&s, lr, 4);
  for (int i = 0; i < 4; i++) ASSERT(lr[2 * i] == pcm[i] * 0.5f && lr[2 * i + 1] == lr[2 * i], "the extension repeats the sample");
  ASSERT_TRUE(song_move_clip(&s, 0, 1, GR_FRACTION_TICKS));
  ASSERT_EQUAL(s.clips[0].length, natural + GR_FRACTION_TICKS);
  ASSERT_TRUE(song_move_clip(&s, 0, 0, 0));
  ASSERT_TRUE(song_resize_clip(&s, 0, GR_FRACTION_TICKS));
  int64_t cut = position_frames_for_bpm(GR_FRACTION_TICKS, s.bpm);
  s.playing = true; s.loop = false; s.pos = cut - 1;
  song_render(&s, lr, 2);
  ASSERT(lr[0] == pcm[cut - 1] * 0.5f && lr[2] == 0 && !s.playing, "a shortened clip stops at its new end");
  ASSERT_TRUE(song_resize_clip(&s, 0, natural));
  ASSERT_EQUAL(s.clips[0].length, 0);
  ASSERT_EQUAL(song_add_clip(&s, 0, 0, (GR_BARS - block_get(0)->bars) * GR_TICKS_BAR), 1);
  ASSERT_FALSE(song_resize_clip(&s, 1, natural + GR_FRACTION_TICKS));
  ASSERT_EQUAL(s.clips[1].length, 0);
  PASS();
}

static void finger(uint32_t message, int x, int y, uint32_t time) {
  ui_event_t event = {.message = message, .wParam = MAKEDWORD(x * UI_WINDOW_SCALE, y * UI_WINDOW_SCALE),
                      .pointer = {.flags = AX_POINTER_TOUCH, .time = time}};
  dispatch_message(&event);
}

static void test_sheet_gestures(void) {
  TEST("a clip swipe scrolls, a hold moves, a tap plays, and the trailing edge resizes on the positioning grid");
  test_env_init();
  g_app = app_init();
  ASSERT_NOT_NULL(g_app);
  int natural = block_get(0)->bars * GR_TICKS_BAR;
  window_t *win = create_window("Groove", 0, MAKERECT(0, 0, 800, 600), NULL, main_win_proc, 0, g_app);
  ASSERT_NOT_NULL(win);
  show_window(win, true);
  song_add_clip(&g_app->song, 0, 0, 0);
  window_t *sheet = g_app->sheet;
  send_message(sheet, evResize, 0, NULL);
  int sx = window_screen_x(sheet) + 32, sy = window_screen_y(sheet) + 40;
  finger(kEventLeftButtonDown, sx + 48, sy, 1000);
  finger(kEventLeftButtonDragged, sx + 8, sy, 1016);
  ASSERT(get_scroll_pos(sheet, SB_HORZ) > 0, "a swipe over a clip scrolls");
  ASSERT_FALSE(g_app->drag.active);
  ASSERT_EQUAL(g_app->song.nclips, 1);
  ASSERT_EQUAL(g_app->song.clips[0].position, 0);
  ASSERT_EQUAL(g_app->song.clips[0].length, 0);
  ASSERT_EQUAL(g_app->song.preview_block, -1);
  finger(kEventLeftButtonUp, sx + 8, sy, 1100);
  ASSERT_NULL(g_ui_runtime.captured);
  set_scroll_info(sheet, SB_HORZ, &(scroll_info_t){ .fMask = SIF_POS, .nPos = 0 }, false);
  send_message(sheet, evHScroll, 0, NULL);

  finger(kEventLeftButtonDown, sx, sy, 2000);
  finger(kEventLeftButtonDragged, sx + 12, sy, 2000 + TOUCH_LONG_PRESS_MS);
  ASSERT_TRUE(g_app->drag.active && g_ui_runtime.captured == sheet);
  window_t *lifted = sheet->children;
  while (lifted && !window_is_lifted(lifted)) lifted = lifted->next;
  ASSERT_NOT_NULL(lifted);
  finger(kEventPointerCancel, sx + 12, sy, 2000 + TOUCH_LONG_PRESS_MS + 20);
  ASSERT_FALSE(g_app->drag.active);
  ASSERT_NULL(g_ui_runtime.captured);
  ASSERT_EQUAL(g_app->song.clips[0].position, 0);
  ASSERT_EQUAL(g_app->song.preview_block, -1);

  finger(kEventLeftButtonDown, sx, sy, 4000);
  finger(kEventLeftButtonUp, sx, sy, 4100);
  ASSERT_EQUAL(g_app->song.preview_block, 0);
  ASSERT_EQUAL(g_app->song.nclips, 1);
  ASSERT_FALSE(g_app->song.playing);

  window_t *card = sheet->children;
  int hx = card->frame.x + card->frame.w - 2, hy = card->frame.y + 4;
  int outside = card->frame.x + card->frame.w - 12;
  ASSERT_FALSE(send_message(sheet, evLeftButtonDoubleClick, MAKEDWORD(hx, hy), NULL));
  ASSERT(g_app->song.nclips == 1 && g_app->song.clips[0].length == 0, "a double-tap does not copy the sample");

  send_message(sheet, evLeftButtonDown, MAKEDWORD(hx, hy), NULL);
  send_message(sheet, evLeftButtonUp, MAKEDWORD(hx, hy), NULL);
  ASSERT(g_app->song.preview_block == 0 && g_app->song.clips[0].length == 0, "a tap on the end still plays the sample");

  int grown = natural + GR_SNAP_TICKS;
  send_message(sheet, evLeftButtonDown, MAKEDWORD(hx, hy), NULL);
  send_message(sheet, evMouseMove, MAKEDWORD(hx + 8, hy), NULL);
  ASSERT_EQUAL(song_clip_ticks(&g_app->song, 0), natural);
  ASSERT_EQUAL(card->frame.w, natural * 88 / GR_TICKS_BAR);
  send_message(sheet, evMouseMove, MAKEDWORD(hx + 12, hy), NULL);
  ASSERT_EQUAL(song_clip_ticks(&g_app->song, 0), grown);
  send_message(sheet, evMouseMove, MAKEDWORD(hx + 20, hy), NULL);
  ASSERT(g_app->song.nclips == 1 && !g_app->drag.active && g_app->song.clips[0].position == 0, "the end drag resizes in place");
  ASSERT_EQUAL(g_app->song.clips[0].length, grown);
  ASSERT_EQUAL(card->frame.w, grown * 88 / GR_TICKS_BAR);
  ASSERT_EQUAL(g_app->song.preview_block, -1);
  send_message(sheet, evPointerCancel, 0, NULL);
  ASSERT(g_app->song.clips[0].length == 0 && song_clip_ticks(&g_app->song, 0) == natural, "cancel restores the block length");
  ASSERT_EQUAL(card->frame.w, natural * 88 / GR_TICKS_BAR);
  ASSERT_NULL(g_ui_runtime.captured);

  send_message(sheet, evLeftButtonDown, MAKEDWORD(outside, hy), NULL);
  send_message(sheet, evMouseMove, MAKEDWORD(outside + 12, hy), NULL);
  ASSERT(g_app->drag.active && g_app->song.clips[0].length == 0 && g_app->song.nclips == 1, "just before the end, a drag still moves");
  send_message(sheet, evPointerCancel, 0, NULL);
  ASSERT_FALSE(g_app->drag.active);
  ASSERT_EQUAL(g_app->song.clips[0].position, 0);

  send_message(sheet, evLeftButtonDown, MAKEDWORD(hx, hy), NULL);
  send_message(sheet, evMouseMove, MAKEDWORD(hx + 20, hy), NULL);
  send_message(sheet, evLeftButtonUp, MAKEDWORD(hx + 20, hy), NULL);
  ASSERT(g_app->song.clips[0].length == grown && g_app->song.nclips == 1 && !g_app->drag.active, "releasing keeps the new length");
  ASSERT_NULL(g_ui_runtime.captured);

  int shrunk = grown - 2 * GR_SNAP_TICKS;
  hx = card->frame.x + card->frame.w - 2;
  send_message(sheet, evLeftButtonDown, MAKEDWORD(hx, hy), NULL);
  send_message(sheet, evMouseMove, MAKEDWORD(hx - 8, hy), NULL);
  ASSERT_EQUAL(song_clip_ticks(&g_app->song, 0), grown);
  send_message(sheet, evMouseMove, MAKEDWORD(hx - 12, hy), NULL);
  ASSERT_EQUAL(song_clip_ticks(&g_app->song, 0), natural);
  send_message(sheet, evMouseMove, MAKEDWORD(hx - 40, hy), NULL);
  send_message(sheet, evLeftButtonUp, MAKEDWORD(hx - 40, hy), NULL);
  ASSERT_EQUAL(g_app->song.clips[0].length, shrunk);
  ASSERT(shrunk < natural && shrunk % GR_SNAP_TICKS == 0, "dragging the end back trims on the positioning grid");

  hx = card->frame.x + card->frame.w - 2;
  int tx = window_screen_x(sheet) + hx, ty = window_screen_y(sheet) + hy;
  int extended = shrunk + 2 * GR_SNAP_TICKS;
  finger(kEventLeftButtonDown, tx, ty, 6000);
  finger(kEventLeftButtonDragged, tx + 40, ty, 6000 + TOUCH_LONG_PRESS_MS);
  ASSERT(g_app->song.nclips == 1 && !g_app->drag.active && g_app->song.clips[0].position == 0, "a hold on the end resizes instead of moving");
  ASSERT_EQUAL(g_app->song.clips[0].length, extended);
  finger(kEventLeftButtonUp, tx + 40, ty, 6000 + TOUCH_LONG_PRESS_MS + 40);
  ASSERT_EQUAL(g_app->song.clips[0].length, extended);
  ASSERT_NULL(g_ui_runtime.captured);

  ASSERT_EQUAL(song_add_clip(&g_app->song, 1, 0, 2 * GR_TICKS_BAR), 1);
  send_message(sheet, evResize, 0, NULL);
  card = sheet->children;
  hx = card->frame.x + card->frame.w - 2;
  hy = card->frame.y + 4;
  send_message(sheet, evLeftButtonDown, MAKEDWORD(hx, hy), NULL);
  send_message(sheet, evMouseMove, MAKEDWORD(hx + 400, hy), NULL);
  send_message(sheet, evLeftButtonUp, MAKEDWORD(hx + 400, hy), NULL);
  ASSERT(g_app->song.nclips == 2 && g_app->song.clips[0].length == 2 * GR_TICKS_BAR, "the edge stops at the next clip");
  ASSERT_EQUAL(song_clip_end(&g_app->song, 0), 2 * GR_TICKS_BAR);
  ASSERT_EQUAL(card->frame.w, 2 * 88);
  destroy_window(win);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}

static void test_drag_off_sheet_removes(void) {
  TEST("dropping a moved clip outside the grid removes it; dropping inside keeps it");
  test_env_init();
  g_app = app_init();
  window_t *win = create_window("Groove", 0, MAKERECT(0, 0, 800, 600), NULL, main_win_proc, 0, g_app);
  ASSERT_NOT_NULL(win);
  show_window(win, true);
  song_add_clip(&g_app->song, 0, 0, 0);
  song_add_clip(&g_app->song, 0, 1, 0);
  window_t *sheet = g_app->sheet;
  send_message(sheet, evResize, 0, NULL);
  int x = 32, y = 22 + 8;
  app_preview(0);
  ASSERT_EQUAL(g_app->song.preview_block, 0);
  send_message(sheet, evLeftButtonDown, MAKEDWORD(x, y), NULL);
  send_message(sheet, evMouseMove, MAKEDWORD(x + 12, y), NULL);
  ASSERT(g_app->song.preview_block == -1, "starting a clip drag stops the audition");
  send_message(sheet, evLeftButtonUp, MAKEDWORD(x + 12, y), NULL);
  ASSERT_EQUAL(g_app->song.nclips, 2);
  send_message(sheet, evLeftButtonDown, MAKEDWORD(x, y), NULL);
  send_message(sheet, evMouseMove, MAKEDWORD(x, 4), NULL);
  window_t *lifted = sheet->children;
  while (lifted && !window_is_lifted(lifted)) lifted = lifted->next;
  ASSERT_TRUE(g_app->drag.active && lifted && !window_lift_is_copy(lifted));
  send_message(sheet, evLeftButtonUp, MAKEDWORD(x, 4), NULL);
  ASSERT_EQUAL(g_app->song.nclips, 1);
  ASSERT_EQUAL(g_app->song.clips[0].track, 1);
  ASSERT_FALSE(g_app->drag.active);
  ASSERT_EQUAL(g_app->selected_clip, -1);
  ASSERT_TRUE(g_ui_runtime.captured == NULL);
  destroy_window(win);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}

static void test_lazy_audio(void) {
  TEST("blocks load on demand: overviews keep no PCM, auditions swap theirs, drops keep the song's audio");
  test_env_init();
  g_app = app_init();
  ASSERT_NOT_NULL(g_app);
  window_t *win = create_window("Groove", 0, MAKERECT(0, 0, 1000, 700), NULL, main_win_proc, 0, g_app);
  ASSERT_NOT_NULL(win);
  for (int i = 0; i < blocks_count(); i++) ASSERT(!block_get(i)->audio.pcm, "nothing is rendered at start-up");
  int credit = g_app->peak_credit;
  ASSERT_EQUAL(credit, GR_PEAKS_PER_TICK);
  ASSERT(app_block_peaks(2) && block_get(2)->audio.npeaks > 0 && !block_get(2)->audio.pcm, "an overview keeps peaks only");
  ASSERT(g_app->peak_credit == credit - 1 && app_block_peaks(2) && g_app->peak_credit == credit - 1, "a ready overview costs nothing");
  g_app->peak_credit = 0;
  ASSERT(!app_block_peaks(3) && g_app->peaks_pending && !block_get(3)->audio.npeaks, "past the per-tick budget the card waits for the next tick");
  app_preview(5);
  ASSERT(g_app->song.preview_block == 5 && block_get(5)->audio.pcm && block_get(5)->audio.npeaks > 0, "audition loads the block");
  app_preview(6);
  ASSERT(block_get(6)->audio.pcm && !block_get(5)->audio.pcm, "the next audition releases the previous one");
  ASSERT_TRUE(app_drop(&(drag_t){ .block = 6, .from_clip = -1, .track = 0, .position = 0 }));
  app_preview(7);
  ASSERT(block_get(6)->audio.pcm, "a block placed in the song stays loaded");
  static float lr[2 * 512];
  g_app->song.preview_block = -1;
  g_app->song.playing = true;
  song_render(&g_app->song, lr, 512);
  ASSERT(peak_of(lr, 1024) > 0, "the dropped clip is audible");
  app_new_song();
  ASSERT(!block_get(6)->audio.pcm && !block_get(7)->audio.pcm && g_app->song.preview_block == -1, "a new song releases everything");
  ASSERT_FALSE(app_block_audio(-1) || app_block_peaks(blocks_count()));
  destroy_window(win);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}

static void test_genre_filter(void) {
  TEST("the genre filter narrows the family to tagged blocks, and every genre restores it");
  test_env_init();
  g_app = app_init();
  window_t *win = create_window("Groove", 0, MAKERECT(0, 0, 1180, 700), NULL, main_win_proc, 0, g_app);
  ASSERT_NOT_NULL(win);
  show_window(win, true);
  ASSERT_EQUAL(g_app->genre, 0);
  int ids[GR_MAX_BLOCKS], n = blocks_in_family(g_app->family, ids, GR_MAX_BLOCKS);
  for (int g = 0; g < GENRE_COUNT; g++) {
    app_set_genre((uint8_t)(1 << g));
    int expect = 0;
    for (int i = 0; i < n; i++) expect += (block_get(ids[i])->genres >> g) & 1;
    ASSERT(expect > 0 && expect < n && library_visible() == expect, "the library shows exactly the tagged blocks of the family");
  }
  ASSERT_EQUAL(g_app->genre, GENRE_TECHNO);
  app_set_genre(GENRE_DANCE | GENRE_RAVE);
  ASSERT(g_app->genre == GENRE_TECHNO, "the filter holds one genre; a combined mask is rejected");
  app_set_genre(0);
  ASSERT(library_visible() == n, "every genre restores the whole family");
  destroy_window(win);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}

static void test_family_filter(void) {
  TEST("family buttons pick the one family the bin shows, as a radio group, and combine with genre");
  test_env_init();
  g_app = app_init();
  window_t *win = create_window("Groove", 0, MAKERECT(0, 0, 1180, 700), NULL, main_win_proc, 0, g_app);
  ASSERT_NOT_NULL(win);
  show_window(win, true);
  window_t *bar = g_app->library;
  toolbar_state_t *tb = toolbar_get_state(bar);
  int ids[GR_MAX_BLOCKS];
  ASSERT(g_app->family == FAMILY_LOOP && toolbar_checked(ID_FAMILY(FAMILY_LOOP)), "the library opens on drum loops");
  ASSERT_EQUAL(library_visible(), blocks_in_family(FAMILY_LOOP, ids, GR_MAX_BLOCKS));
  int first = toolbar_index(ID_FAMILY(0));
  ASSERT(first >= 0 && toolbar_index(ID_FAMILY(FAMILY_COUNT - 1)) == first + FAMILY_COUNT - 1, "one button per family, in order");
  for (int i = first; i < first + FAMILY_COUNT; i++) {
    irect16_t r = tb->item_rects[i];
    ASSERT(tb->items[i].type == TOOLBAR_ITEM_BUTTON && (tb->items[i].style & TOOLBAR_ITEM_FLAG_ARTWORK), "family buttons draw artwork");
    ASSERT(r.w == toolbar_effective_bsz(bar) && r.h == toolbar_effective_bsz(bar), "family buttons are full toolbar size");
    ASSERT(r.x + r.w <= bar->frame.w, "every button fits one row at the default width");
  }
  ASSERT(strcmp(tb->items[first + FAMILY_GUITAR].icon, "strip:11") == 0, "a family button uses an existing atlas cell");
  for (int c = FAMILY_COUNT - 1; c >= 0; c--) {
    click_toolbar(ID_FAMILY(c));
    ASSERT(g_app->family == (family_t)c && library_visible() == blocks_in_family(c, ids, GR_MAX_BLOCKS), "a routed click shows one family");
    int checked = 0;
    for (int i = first; i < first + FAMILY_COUNT; i++) checked += (tb->items[i].state & TBSTATE_CHECKED) != 0;
    ASSERT(checked == 1 && toolbar_checked(ID_FAMILY(c)), "exactly the chosen family is checked");
  }
  app_set_genre(GENRE_DANCE);
  int n = blocks_in_family(FAMILY_LOOP, ids, GR_MAX_BLOCKS), dance_drums = 0;
  for (int i = 0; i < n; i++) dance_drums += (block_get(ids[i])->genres & GENRE_DANCE) != 0;
  ASSERT(dance_drums > 0 && dance_drums < n && library_visible() == dance_drums, "family and genre combine");
  app_set_family(FAMILY_COUNT);
  app_set_family(-1);
  ASSERT(g_app->family == FAMILY_LOOP, "a value outside the families is rejected");
  destroy_window(win);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}


static void test_portable_recordings(void) {
  TEST("a song embeds microphone samples once and opens without the source library");
  test_env_init();
  g_app = app_init();
  ASSERT_NOT_NULL(g_app);
  char dir[] = "/tmp/groove-recordings-XXXXXX";
  ASSERT_NOT_NULL(mkdtemp(dir));
  // app_init loads the user's sample library. Keep this fixture isolated and
  // route restored recordings into its temporary directory, not that library.
  samples_truncate(0);
  snprintf(g_app->samples_dir, sizeof(g_app->samples_dir), "%s", dir);
  window_t *win = create_window("Groove", 0, MAKERECT(0, 0, 800, 600), NULL, main_win_proc, 0, g_app);
  ASSERT_NOT_NULL(win);
  sample_t sample = { .name = "Recorded voice", .bpm = 140, .bars = 1, .frames = bar_frames_for_bpm(140) };
  sample.pcm = calloc((size_t)sample.frames, sizeof(int16_t));
  ASSERT_NOT_NULL(sample.pcm);
  for (int i = 0; i < sample.frames; i++) sample.pcm[i] = (int16_t)(10000 * sin(i * 0.062));
  int original_count = samples_count();
  int id = sample_add(&sample);
  ASSERT(id >= blocks_builtin_count(), "recording becomes a library block");
  ASSERT_EQUAL(sample_add(&sample), id);
  ASSERT_EQUAL(samples_count(), original_count + 1);
  ASSERT_TRUE(sample_save(dir, &sample));
  char wav[512], song[512], bad[512];
  snprintf(wav, sizeof(wav), "%s/recording-1.wav", dir);
  snprintf(song, sizeof(song), "%s/portable.groove", dir);
  snprintf(bad, sizeof(bad), "%s/bad.groove", dir);
  FILE *f = fopen(wav, "rb"); ASSERT_NOT_NULL(f);
  sample_t loaded = {0};
  ASSERT_TRUE(sample_read(f, &loaded)); fclose(f);
  ASSERT_EQUAL(loaded.frames, sample.frames);
  ASSERT(memcmp(loaded.pcm, sample.pcm, (size_t)sample.frames * 2) == 0, "WAV retains exact PCM");
  free(loaded.pcm);
  samples_load(dir);
  ASSERT_EQUAL(samples_count(), original_count + 1);
  ASSERT_EQUAL(song_add_clip(&g_app->song, id, 0, 0), 0);
  ASSERT_EQUAL(song_add_clip(&g_app->song, id, 2, GR_TICKS_BAR), 1);
  ASSERT_TRUE(app_save_song(song));
  f = fopen(song, "rb"); ASSERT_NOT_NULL(f);
  char line[64]; ASSERT_NOT_NULL(fgets(line, sizeof(line), f));
  ASSERT_STR_EQUAL(line, "ORION_GROOVE 5\n"); fclose(f);
  app_new_song(); samples_truncate(original_count);
  ASSERT(remove(wav) == 0, "remove the standalone recording");
  ASSERT_TRUE(app_open_song(song));
  ASSERT_EQUAL(g_app->song.nclips, 2);
  ASSERT_EQUAL(g_app->song.clips[0].block, g_app->song.clips[1].block);
  const sample_t *restored = sample_get(g_app->song.clips[0].block);
  ASSERT_NOT_NULL(restored);
  ASSERT(memcmp(restored->pcm, sample.pcm, (size_t)sample.frames * 2) == 0, "only the song file is needed to restore the sample");
  ASSERT_EQUAL(samples_count(), original_count + 1);
  ASSERT_TRUE(app_open_song(song));
  ASSERT_EQUAL(samples_count(), original_count + 1);
  g_app->song.playing = true;
  float lr[1024]; song_render(&g_app->song, lr, 512);
  ASSERT(peak_of(lr, 1024) > .01f, "embedded sample is audible");
  g_app->song.playing = false;
  f = fopen(bad, "wb"); ASSERT_NOT_NULL(f);
  fprintf(f, "ORION_GROOVE 5\n140 1 0 0 1 1\nS\n");
  ASSERT_TRUE(sample_write(f, &sample));
  fputs("\n0 0 0 1 0 0\ntrailing garbage", f); fclose(f);
  int before = samples_count();
  ASSERT_FALSE(app_open_song(bad));
  ASSERT_EQUAL(g_app->song.nclips, 2); ASSERT_EQUAL(samples_count(), before);
  f = fopen(bad, "wb"); ASSERT_NOT_NULL(f);
  fputs("ORION_GROOVE 5\n140 1 0 0 1 1\nS\nRIFF", f); fclose(f);
  ASSERT_FALSE(app_open_song(bad));
  ASSERT_EQUAL(g_app->song.nclips, 2); ASSERT_EQUAL(samples_count(), before);
  sample_t invalid = sample; invalid.frames++;
  ASSERT_EQUAL(sample_add(&invalid), -1);
  invalid = sample; invalid.bars = 128;
  ASSERT_EQUAL(sample_add(&invalid), -1);
  free(sample.pcm); remove(wav); remove(song); remove(bad); rmdir(dir);
  destroy_window(win); app_shutdown(g_app); test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("Groove");
  test_library_levels();
  test_blocks();
  test_genres();
  test_tempo();
  test_project_io();
  test_song_library();
  test_eurodance_demos();
  test_song_rules();
  test_mixer();
  test_clip_gain();
  test_overlap_audio();
  test_clip_resize();
  test_sheet_drop();
  test_overlap_sheet();
  test_resized_clip_drag();
  test_fractional_selection();
  test_two_finger_sheet_pan();
  test_genre_filter();
  test_family_filter();
  test_lazy_audio();
  test_library_transport();
  test_shared_block_cards();
  test_drag_anchor();
  test_drag_center_boundaries();
  test_sheet_gestures();
  test_drag_off_sheet_removes();
  test_portable_recordings();
  TEST_END();
}

// Groove tests: block synthesis, the offline mixer, snapping rules, and the
// sheet's drop handling through real windows.

#include "test_framework.h"
#include "test_env.h"
#include "apps/groove/groove.h"
#include <orion/user/toolbar.h>

static float peak_of(const float *p, int n) { float m = 0; for (int i = 0; i < n; i++) m = fmaxf(m, fabsf(p[i])); return m; }

static void test_blocks(void) {
  TEST("every block renders finite, non-silent, bar-aligned audio");
  block_pcm_t pcm[GR_MAX_BLOCKS];
  blocks_render(GR_BPM_DEFAULT, pcm);
  blocks_swap(pcm);
  for (int i = 0; i < blocks_count(); i++) {
    const block_t *b = block_get(i);
    ASSERT(b->audio.pcm && b->audio.frames == b->bars * bar_frames_for_bpm(GR_BPM_DEFAULT), "frame count");
    for (int k = 0; k < b->audio.frames; k++) ASSERT(isfinite(b->audio.pcm[k]), "NaN or inf sample");
    ASSERT(peak_of(b->audio.pcm, b->audio.frames) > 0.5f && peak_of(b->audio.pcm, b->audio.frames) <= 0.86f, "normalized peak");
    ASSERT(fabsf(b->audio.pcm[b->audio.frames - 1]) < 0.01f, "tail is faded");
  }
  PASS();
}

static void test_tempo(void) {
  TEST("re-rendering at a new tempo resizes blocks and returns the old buffers");
  uint64_t revision = block_get(0)->audio_revision;
  block_pcm_t pcm[GR_MAX_BLOCKS];
  blocks_render(140, pcm);
  blocks_swap(pcm);
  ASSERT(block_get(0)->audio_revision > revision, "tempo changes invalidate waveform textures");
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
  for (int i = 0; i < GR_MAX_BLOCKS; i++) free(pcm[i].pcm);
  blocks_render(GR_BPM_DEFAULT, pcm);
  blocks_swap(pcm);
  for (int i = 0; i < GR_MAX_BLOCKS; i++) free(pcm[i].pcm);
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
  int sx = window_screen_x(g_app->sheet) + GR_SHEET_HEADER_W + bar * 88 + 20;
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
  app_set_bpm(140);
  ASSERT(g_app->song.bpm == 140 && block_get(0)->audio.frames == bar_frames_for_bpm(140) * block_get(0)->bars, "bpm change re-renders");
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
  window_t *sheet = g_app->sheet, *library = g_app->tabs->children->children;
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
  int x = GR_SHEET_HEADER_W + cut * 88 / GR_TICKS_BAR - scroll;
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
  ASSERT_TRUE(original->drag_visual);
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

static void test_fractional_selection(void) {
  TEST("fractional clips select and delete at their visible edges after scrolling; ruler seeks by quarter bars");
  test_env_init();
  g_app = app_init();
  window_t *win = create_window("Groove", 0, MAKERECT(0, 0, 800, 600), NULL, main_win_proc, 0, g_app);
  ASSERT_NOT_NULL(win);
  show_window(win, true);
  window_t *sheet = g_app->sheet;
  int position = 3 * GR_TICKS_BAR + GR_SNAP_TICKS;
  int clip = song_add_clip(&g_app->song, 0, 0, position);
  ASSERT(clip >= 0, "fractional clip added");
  int scroll = 83, left = GR_SHEET_HEADER_W + 3 * 88 + 22;
  set_scroll_info(sheet, SB_HORZ, &(scroll_info_t){ .fMask = SIF_POS, .nPos = scroll }, false);
  send_message(sheet, evHScroll, 0, NULL);
  ASSERT_EQUAL(sheet->children->frame.x, left - scroll);
  const int offsets[] = {-1, 0, 87, 88};
  for (int i = 0; i < ARRAY_LEN(offsets); i++) {
    ui_event_t event = {.message = kEventLeftButtonDown,
      .x = (window_screen_x(sheet) + left - scroll + offsets[i]) * UI_WINDOW_SCALE,
      .y = (window_screen_y(sheet) + 30) * UI_WINDOW_SCALE};
    dispatch_message(&event);
    ASSERT_EQUAL(g_app->selected_clip, i == 1 || i == 2 ? clip : -1);
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
  ASSERT(g_app->song.nclips > 0 && song_length_ticks(&g_app->song) == 8 * GR_TICKS_BAR, "demo timing retains its original length");
  for (int i = 0; i < g_app->song.nclips; i++) ASSERT_EQUAL(g_app->song.clips[i].position % GR_TICKS_BAR, 0);
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
  int sx = window_screen_x(sheet) + GR_SHEET_HEADER_W + 32, sy = window_screen_y(sheet) + 40;
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
  int row = CLAMP((get_client_rect(sheet).h - 22) / GR_TRACKS, 26, 64);
  int size = MIN(24, (row - 4) / 2), mute_y = 22 + (row - size * 2 - 2) / 2 + size / 2;
  event = (ui_event_t){.message = kEventLeftButtonDown,
    .x = (window_screen_x(sheet) + GR_SHEET_HEADER_W / 2) * UI_WINDOW_SCALE,
    .y = (window_screen_y(sheet) + mute_y) * UI_WINDOW_SCALE};
  dispatch_message(&event);
  ASSERT_TRUE(g_app->song.mute[0]);
  event.message = kEventLeftButtonUp;
  dispatch_message(&event);
  char tooltip[256] = {0};
  ASSERT_TRUE(send_message(sheet, evGetTooltipText, MAKEDWORD(GR_SHEET_HEADER_W / 2 + get_scroll_pos(sheet, SB_HORZ), mute_y), tooltip));
  ASSERT_TRUE(strcmp(tooltip, "Unmute track 1") == 0);
  event.message = kEventLeftButtonDown;
  event.y += (size + 2) * UI_WINDOW_SCALE;
  dispatch_message(&event);
  ASSERT_TRUE(g_app->song.solo[0]);
  event.message = kEventLeftButtonUp;
  dispatch_message(&event);
  ASSERT_EQUAL(g_app->song.nclips, 1);
  ASSERT_EQUAL(g_app->song.preview_block, -1);
  ASSERT_FALSE(g_app->drag.active);
  event = (ui_event_t){.message = kEventLeftButtonDown,
    .x = (window_screen_x(sheet) + GR_SHEET_HEADER_W + 2) * UI_WINDOW_SCALE, .y = sy * UI_WINDOW_SCALE};
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

static void test_library_search(void) {
  TEST("library search filters every bin page by block and family name");
  test_env_init();
  g_app = app_init();
  window_t *win = create_window("Groove", 0, MAKERECT(0, 0, 1000, 700), NULL, main_win_proc, 0, g_app);
  ASSERT_TRUE(win && g_app->tabs && g_app->library);
  window_t *all = g_app->tabs->children;
  ASSERT_TRUE(all && strcmp(all->title, "All") == 0);
  ASSERT_EQUAL(visible_tiles(all), blocks_count());
  ASSERT_TRUE(block_matches(0, "") && block_matches(0, "FLOOR") && block_matches(0, "drum") && !block_matches(0, "zzz"));

  window_t *search = get_window_item(g_app->library, ID_SEARCH);
  ASSERT_NOT_NULL(search);
  set_focus(search);
  send_message(search, evLeftButtonUp, MAKEDWORD(3, 5), NULL);
  send_message(search, evTextInput, 0, "f");
  send_message(search, evTextInput, 0, "l");
  ASSERT_TRUE(strcmp(g_app->search, "fl") == 0);
  int expect = 0;
  for (int i = 0; i < blocks_count(); i++) expect += block_matches(i, "fl");
  ASSERT_TRUE(expect > 0 && expect < blocks_count());
  ASSERT_EQUAL(visible_tiles(all), expect);
  app_set_playing(true);
  app_command(ID_LOOP);
  ASSERT_TRUE(get_window_item(g_app->library, ID_SEARCH) == search && g_ui_runtime.focused == search);
  char text[64];
  send_message(search, edGetText, sizeof(text), text);
  ASSERT_TRUE(strcmp(text, "fl") == 0);
  ASSERT_EQUAL(visible_tiles(all), expect);
  app_set_playing(false);
  send_message(search, evKeyDown, AX_KEY_BACKSPACE, NULL);
  send_message(search, evKeyDown, AX_KEY_BACKSPACE, NULL);
  ASSERT_EQUAL(visible_tiles(all), blocks_count());
  destroy_window(win);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}

static void test_library_transport(void) {
  TEST("transport and search share the library toolbar; nested mouse routing and resizing preserve search");
  test_env_init();
  g_app = app_init();
  window_t *win = create_window("Groove", 0, MAKERECT(0, 0, 1000, 700), NULL, main_win_proc, 0, g_app);
  ASSERT_NOT_NULL(win);
  show_window(win, true);
  window_t *bar = g_app->library, *search = get_window_item(bar, ID_SEARCH);
  ASSERT_TRUE(!(win->flags & WINDOW_TOOLBAR) && win->toolbar == NULL);
  ASSERT_TRUE(bar && (bar->flags & WINDOW_TOOLBAR) && bar->parent == win);
  ASSERT_TRUE(search && search->parent == bar);
  ASSERT_EQUAL(g_app->sheet->frame.y, g_app->menubar_win->frame.h);
  ASSERT_EQUAL(bar->frame.y, g_app->sheet->frame.y + g_app->sheet->frame.h + DOCK_SPLITTER);
  ASSERT_EQUAL(bar->frame.h, 280);
  ASSERT_TRUE(g_app->tabs->parent == bar);
  ASSERT_EQUAL(g_app->tabs->frame.y, 0);
  ASSERT_EQUAL(window_screen_y(g_app->tabs), window_screen_y(bar) + titlebar_height(bar));
  toolbar_state_t *tb = toolbar_get_state(bar);
  ASSERT_EQUAL(tb->style, TOOLBAR_STYLE_PLASTIC);
  ASSERT_EQUAL(tb->strip.tex, 0);
  ASSERT_TRUE(strcmp(tb->items[1].icon, "phosphor-rewind-fill") == 0);
  ASSERT_TRUE(tb->items[2].color != tb->items[3].color);
  uint32_t original_color = tb->items[2].color, color = WEB(0x2277bb);
  ASSERT_TRUE(send_message(bar, tbSetItemColor, ID_PLAY, &color));
  ASSERT_EQUAL(tb->items[2].color, color);
  ASSERT_TRUE(get_window_item(bar, ID_SEARCH) == search);
  ASSERT_FALSE(send_message(bar, tbSetItemColor, ID_SEARCH, &color));
  ASSERT_FALSE(send_message(bar, tbSetItemColor, 0xffff, &color));
  ASSERT_TRUE(send_message(bar, tbSetItemColor, ID_PLAY, NULL));
  ASSERT_EQUAL(tb->items[2].color, 0);
  ASSERT_TRUE(send_message(bar, tbSetItemColor, ID_PLAY, &original_color));

#ifdef AX_PLATFORM_IOS
  ASSERT_EQUAL(toolbar_effective_bsz(bar), BUTTON_HEIGHT + 4);
  ASSERT_EQUAL(search->frame.h, BUTTON_HEIGHT);
#else
  ASSERT_EQUAL(toolbar_effective_bsz(bar), TB_SPACING);
  ASSERT_EQUAL(search->frame.h, TB_SPACING - 4);
#endif
  ASSERT_EQUAL(titlebar_height(bar), toolbar_effective_bsz(bar) + 2 * toolbar_effective_padding(bar));
  ASSERT_EQUAL(search->frame.y, toolbar_effective_padding(bar) + 2);
  ASSERT_EQUAL(tb->items[11].type, TOOLBAR_ITEM_TEXTEDIT);
  ASSERT_TRUE(strcmp(tb->items[11].icon, "search") == 0);
  for (int width = 1000; width >= 720; width -= 280) {
    resize_window(win, width, 700);
    ASSERT_TRUE(get_window_item(bar, ID_SEARCH) == search);
    ASSERT_TRUE(search->frame.x + search->frame.w < tb->item_rects[tb->item_count - 1].x);
    for (int i = 0; i < tb->item_count; i++) {
      ASSERT_TRUE(tb->item_rects[i].x >= 0 && tb->item_rects[i].x + tb->item_rects[i].w <= bar->frame.w);
    }
  }
  irect16_t r = tb->item_rects[2];
  ui_event_t event = {.message = kEventLeftButtonDown,
    .x = (window_screen_x(bar) + r.x + r.w / 2) * UI_WINDOW_SCALE,
    .y = (window_screen_y(bar) + r.y + r.h / 2) * UI_WINDOW_SCALE};
  dispatch_message(&event);
  event.message = kEventLeftButtonUp;
  dispatch_message(&event);
  ASSERT_TRUE(g_app->song.playing && strcmp(tb->items[2].icon, "phosphor-pause-fill") == 0);
  r = tb->item_rects[4];
  event.message = kEventLeftButtonDown;
  event.x = (window_screen_x(bar) + r.x + r.w / 2) * UI_WINDOW_SCALE;
  dispatch_message(&event);
  event.message = kEventLeftButtonUp;
  dispatch_message(&event);
  ASSERT_FALSE(g_app->song.loop);
  ASSERT_FALSE(tb->items[4].flags & TOOLBAR_BUTTON_FLAG_ACTIVE);
  app_command(ID_LOOP);
  ASSERT_TRUE(tb->items[4].flags & TOOLBAR_BUTTON_FLAG_ACTIVE);
  char tooltip[256] = {0};
  ASSERT_TRUE(send_message(bar->toolbar, evGetTooltipText, MAKEDWORD(r.x + 4, r.y + 4), tooltip));
  ASSERT_TRUE(strcmp(tooltip, "Loop (L)") == 0);
  event.message = kEventLeftButtonDown;
  event.x = (window_screen_x(search) + 12) * UI_WINDOW_SCALE;
  event.y = (window_screen_y(search) + search->frame.h / 2) * UI_WINDOW_SCALE;
  dispatch_message(&event);
  event.message = kEventLeftButtonUp;
  dispatch_message(&event);
  ASSERT(g_ui_runtime.focused == search && window_has_state(search, WINDOW_STATE_EDITING), "routed toolbar search click focuses and edits the field");
  send_message(search, evTextInput, 0, "h");
  send_message(search, evTextInput, 0, "a");
  send_message(search, evTextInput, 0, "t");
  ASSERT(strcmp(g_app->search, "hat") == 0, "toolbar field changes reach the library filter");
  ASSERT(visible_tiles(g_app->tabs->children) > 0 && visible_tiles(g_app->tabs->children) < blocks_count(), "embedded search filters the library");
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
  window_t *canvas = g_app->sheet->children, *page = g_app->tabs->children;
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
  send_message(library, evMouseMove, MAKEDWORD(12, 4), NULL);
  ASSERT_TRUE(g_app->drag.active && library->drag_visual);
  send_message(library, evPointerCancel, 0, NULL);
  ASSERT_FALSE(g_app->drag.active || library->drag_visual);
  ASSERT_TRUE(g_ui_runtime.captured == NULL);
  app_select_clip(clip);
  send_message(g_app->sheet, evResize, 0, NULL);
  ASSERT_EQUAL(g_app->song.nclips, 1);
  destroy_window(win);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}

static void test_drag_anchor(void) {
  TEST("library and canvas drops snap the exact card origin, including scrolling and release position; pointer anchoring is optional");
  test_env_init();
  g_app = app_init();
  window_t *win = create_window("Groove", 0, MAKERECT(0, 0, 1000, 700), NULL, main_win_proc, 0, g_app);
  ASSERT_NOT_NULL(win);
  show_window(win, true);
  window_t *sheet = g_app->sheet, *page = g_app->tabs->children, *library = page->children;
  int block = 0;
  while (library && block_get(block)->bars < 2) { library = library->next; block++; }
  ASSERT_NOT_NULL(library);
  send_message(page, evResize, 0, NULL);
  set_scroll_info(page, SB_VERT, &(scroll_info_t){ .fMask = SIF_POS, .nPos = 20 }, false);
  send_message(page, evVScroll, 0, NULL);
  set_scroll_info(sheet, SB_HORZ, &(scroll_info_t){ .fMask = SIF_POS, .nPos = 120 }, false);
  send_message(sheet, evHScroll, 0, NULL);
  int row = CLAMP((get_client_rect(sheet).h - 22) / GR_TRACKS, 26, 64);
  ipoint16_t grab = {160, row - 4};
  int sx = window_screen_x(sheet) + GR_SHEET_HEADER_W + 7 * 88 + 10 + grab.x - get_scroll_pos(sheet, SB_HORZ);
  int sy = window_screen_y(sheet) + 22 + 5 * row + 6 + grab.y;
  send_message(library, evLeftButtonDown, MAKEDWORD(grab.x, grab.y), NULL);
  send_message(library, evMouseMove, MAKEDWORD(sx - window_screen_x(library), sy - window_screen_y(library)), NULL);
  ASSERT_TRUE(g_app->drag.active && library->drag_visual);
  ASSERT(g_app->drag.grab.x == grab.x, "library horizontal grab offset");
  ASSERT(g_app->drag.grab.y == grab.y, "library vertical grab offset");
  ASSERT_TRUE(g_app->drag.valid && g_app->drag.position == 7 * GR_TICKS_BAR && g_app->drag.track == 5);
  send_message(library, evLeftButtonUp, MAKEDWORD(sx + 88 - window_screen_x(library), sy - window_screen_y(library)), NULL);
  int clip = g_app->selected_clip;
  ASSERT(clip >= 0, "library drop selects the added clip");
  ASSERT(song_clip_at(&g_app->song, 5, 8 * GR_TICKS_BAR) == clip, "library release snaps to card origin");
  ASSERT(g_app->song.nclips == 1, "library drop adds a clip");
  ASSERT_FALSE(g_app->drag.active || library->drag_visual);
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
  int mx = GR_SHEET_HEADER_W + 8 * 88 + grab.x, my = 22 + 5 * row + grab.y;
  send_message(sheet, evLeftButtonDown, MAKEDWORD(mx, my), NULL);
  send_message(sheet, evMouseMove, MAKEDWORD(mx - 6, my), NULL);
  ASSERT(g_app->drag.position == 8 * GR_TICKS_BAR && g_app->drag.track == 5, "small movements keep the clip at its nearest grid origin");
  mx = GR_SHEET_HEADER_W + 6 * 88 + 10 + grab.x;
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
  sx = window_screen_x(sheet) + GR_SHEET_HEADER_W + 2;
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
  window_t *sheet = g_app->sheet, *page = g_app->tabs->children;
  send_message(page, evResize, 0, NULL);
  const int percent[] = {20, 49, 51, 80};
  for (int block = 0; block <= 4; block += 4) {
    window_t *library = page->children;
    for (int i = 0; i < block && library; i++) library = library->next;
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
          int sx = window_screen_x(sheet) + GR_SHEET_HEADER_W + 3 * 88 + 22 - scroll + dx;
          int sy = window_screen_y(sheet) + 22 + 2 * size.y + dy;
          event.message = kEventLeftButtonDragged;
          event.x = (sx + grabs[grab].x) * UI_WINDOW_SCALE;
          event.y = (sy + grabs[grab].y) * UI_WINDOW_SCALE;
          dispatch_message(&event);
          ASSERT(g_app->drag.active && g_app->drag.valid, "drag preview is valid");
          ASSERT(window_screen_x(library) + library->drag_dx == sx && window_screen_y(library) + library->drag_dy == sy, "preview is derived from the actual lifted card");
          ASSERT(g_app->drag.position == 3 * GR_TICKS_BAR + GR_SNAP_TICKS * (1 + (percent[x] > 50)), "column changes after the center crosses halfway");
          ASSERT(g_app->drag.track == 2 + (percent[y] > 50), "row changes after the center crosses halfway");
        }
        event.message = kEventLeftButtonUp;
        dispatch_message(&event);
        ASSERT(g_app->song.nclips == 1 && song_clip_at(&g_app->song, 3, 3 * GR_TICKS_BAR + 2 * GR_SNAP_TICKS) >= 0, "80% diagonal drag commits to the bottom-right placement");
        ASSERT_FALSE(g_app->drag.active || library->drag_visual);
        app_new_song();
      }
    }
  }
  destroy_window(win);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("Groove");
  test_blocks();
  test_tempo();
  test_song_rules();
  test_mixer();
  test_overlap_audio();
  test_sheet_drop();
  test_overlap_sheet();
  test_fractional_selection();
  test_two_finger_sheet_pan();
  test_library_search();
  test_library_transport();
  test_shared_block_cards();
  test_drag_anchor();
  test_drag_center_boundaries();
  TEST_END();
}

// Groove tests: block synthesis, the offline mixer, snapping rules, and the
// sheet's drop handling through real windows.

#include "test_framework.h"
#include "test_env.h"
#include "apps/groove/groove.h"

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
  block_pcm_t pcm[GR_MAX_BLOCKS];
  blocks_render(140, pcm);
  blocks_swap(pcm);
  ASSERT(pcm[0].frames == bar_frames_for_bpm(GR_BPM_DEFAULT) * block_get(0)->bars, "old buffer handed back");
  ASSERT(block_get(0)->audio.frames == bar_frames_for_bpm(140) * block_get(0)->bars, "new length");
  for (int i = 0; i < GR_MAX_BLOCKS; i++) free(pcm[i].pcm);
  blocks_render(GR_BPM_DEFAULT, pcm);
  blocks_swap(pcm);
  for (int i = 0; i < GR_MAX_BLOCKS; i++) free(pcm[i].pcm);
  PASS();
}

static void test_song_rules(void) {
  TEST("clips snap to bars and never overlap on a track");
  song_t s;
  song_init(&s);
  int two_bar = -1, one_bar = -1;
  for (int i = 0; i < blocks_count(); i++) { if (block_get(i)->bars == 2 && two_bar < 0) two_bar = i; if (block_get(i)->bars == 1 && one_bar < 0) one_bar = i; }
  ASSERT(song_add_clip(&s, two_bar, 0, 4) == 0, "first clip");
  ASSERT(song_add_clip(&s, one_bar, 0, 5) < 0, "overlap inside 2-bar clip rejected");
  ASSERT(song_add_clip(&s, one_bar, 0, 3) >= 0 && song_add_clip(&s, one_bar, 0, 6) >= 0, "adjacent clips allowed");
  ASSERT(song_add_clip(&s, one_bar, 1, 5) >= 0, "other track is independent");
  ASSERT(song_add_clip(&s, two_bar, 2, GR_BARS - 1) < 0 && song_add_clip(&s, one_bar, GR_TRACKS, 0) < 0, "bounds");
  ASSERT(song_clip_at(&s, 0, 5) == 0 && song_length_bars(&s) == 7, "lookup and length");
  song_remove_clip(&s, 0);
  ASSERT(song_clip_at(&s, 0, 4) < 0 && s.nclips == 3, "removal");
  PASS();
}

static void test_mixer(void) {
  TEST("mixer places a clip at its bar, honours mute/solo, loops and stops");
  song_t s;
  song_init(&s);
  int bar = bar_frames_for_bpm(s.bpm);
  static float lr[2 * 4096];
  int blk = 0;
  song_add_clip(&s, blk, 0, 1);
  s.playing = true;
  s.loop = false;
  s.pos = (int64_t)bar - 1000;
  song_render(&s, lr, 2000);
  ASSERT(peak_of(lr, 2 * 1000) == 0.0f, "silence before the clip");
  ASSERT(peak_of(lr + 2 * 1000, 2 * 1000) > 0.0f, "audio from the first frame of bar 2");
  ASSERT(lr[2 * 1000] == block_get(blk)->audio.pcm[0] * 0.5f || block_get(blk)->audio.pcm[0] == 0.0f, "offset is sample exact");
  s.pos = bar; s.mute[0] = true;
  song_render(&s, lr, 1024);
  ASSERT(peak_of(lr, 2048) == 0.0f, "muted track is silent");
  s.mute[0] = false; s.solo[3] = true;
  s.pos = bar;
  song_render(&s, lr, 1024);
  ASSERT(peak_of(lr, 2048) == 0.0f, "soloing another track silences this one");
  s.solo[3] = false; s.pos = 2 * (int64_t)bar - 100;
  song_render(&s, lr, 400);
  ASSERT(!s.playing && s.pos == 0, "stops and rewinds at the end when not looping");
  s.loop = true; s.playing = true; s.pos = 2 * (int64_t)bar - 100;
  song_render(&s, lr, 400);
  ASSERT(s.playing && s.pos == 300, "wraps when looping");
  s.preview_block = blk; s.preview_pos = 0; s.playing = false;
  song_render(&s, lr, 512);
  ASSERT(peak_of(lr, 1024) > 0.0f && s.preview_pos == 512, "audition plays while stopped");
  PASS();
}

static void test_sheet_drop(void) {
  TEST("dropping a block on the sheet snaps it to the grid; occupied cells reject it");
  test_env_init();
  g_app = app_init();
  ASSERT(g_app != NULL, "app_init");
  window_t *win = create_window("Groove", WINDOW_TOOLBAR | WINDOW_STATUSBAR, MAKERECT(0, 0, 1100, 700), NULL, main_win_proc, 0, g_app);
  ASSERT(win && g_app->sheet, "main window and sheet");
  show_window(win, true);
  int before = g_app->song.nclips;
  int track = 7, bar = 10;
  int sx = window_screen_x(g_app->sheet) + 78 + bar * 88 + 20;
  int row = CLAMP((get_client_rect(g_app->sheet).h - 22) / GR_TRACKS, 26, 64);
  int sy = window_screen_y(g_app->sheet) + 22 + track * row + row / 2;
  g_app->drag = (drag_t){ .active = true, .block = 0, .from_clip = -1, .track = -1 };
  send_message(g_app->sheet, shDragOver, MAKEDWORD(sx, sy), NULL);
  ASSERT(g_app->drag.track == track && g_app->drag.bar == bar && g_app->drag.valid, "ghost snaps to the cell");
  send_message(g_app->sheet, shDrop, MAKEDWORD(sx, sy), NULL);
  ASSERT(g_app->song.nclips == before + 1 && song_clip_at(&g_app->song, track, bar) >= 0, "clip added");
  ASSERT(!g_app->drag.active, "drag state cleared");
  g_app->drag = (drag_t){ .active = true, .block = 0, .from_clip = -1, .track = -1 };
  send_message(g_app->sheet, shDrop, MAKEDWORD(sx, sy), NULL);
  ASSERT(g_app->song.nclips == before + 1, "second drop on the same cell rejected");
  g_app->selected_clip = song_clip_at(&g_app->song, track, bar);
  app_command(ID_DELETE);
  ASSERT(g_app->song.nclips == before, "delete removes the selected clip");
  app_set_bpm(140);
  ASSERT(g_app->song.bpm == 140 && block_get(0)->audio.frames == bar_frames_for_bpm(140) * block_get(0)->bars, "bpm change re-renders");
  destroy_window(win);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}

static void test_two_finger_sheet_pan(void) {
  TEST("two-finger canvas pan cancels a clip drag and scrolls through the framework without editing the song");
  test_env_init();
  g_app = app_init();
  window_t *win = create_window("Groove", WINDOW_TOOLBAR | WINDOW_STATUSBAR, MAKERECT(0, 0, 800, 600), NULL, main_win_proc, 0, g_app);
  ASSERT_NOT_NULL(win);
  show_window(win, true);
  int clip = song_add_clip(&g_app->song, 0, 0, 0);
  window_t *sheet = g_app->sheet;
  int sx = window_screen_x(sheet) + 110, sy = window_screen_y(sheet) + 40;
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
  ASSERT_EQUAL(g_app->song.clips[clip].bar, 0);
  ASSERT_EQUAL(g_app->song.clips[clip].track, 0);
  ASSERT_EQUAL(g_app->song.preview_block, -1);
  event.gesture.phase = AX_GESTURE_END;
  dispatch_message(&event);
  event = (ui_event_t){.message = kEventLeftButtonDown,
    .x = (window_screen_x(sheet) + 38) * UI_WINDOW_SCALE, .y = sy * UI_WINDOW_SCALE};
  dispatch_message(&event);
  ASSERT_TRUE(g_app->song.mute[0]);
  event.message = kEventLeftButtonUp;
  dispatch_message(&event);
  char tooltip[256] = {0};
  ASSERT_TRUE(send_message(sheet, evGetTooltipText, MAKEDWORD(38 + get_scroll_pos(sheet, SB_HORZ), 40), tooltip));
  ASSERT_TRUE(strcmp(tooltip, "Unmute track 1") == 0);
  event.message = kEventLeftButtonDown;
  event.x = (window_screen_x(sheet) + 64) * UI_WINDOW_SCALE;
  dispatch_message(&event);
  ASSERT_TRUE(g_app->song.solo[0]);
  event.message = kEventLeftButtonUp;
  dispatch_message(&event);
  ASSERT_EQUAL(g_app->song.nclips, 1);
  ASSERT_EQUAL(g_app->song.preview_block, -1);
  ASSERT_FALSE(g_app->drag.active);
  event = (ui_event_t){.message = kEventLeftButtonDown,
    .x = (window_screen_x(sheet) + 78 + 2) * UI_WINDOW_SCALE, .y = sy * UI_WINDOW_SCALE};
  dispatch_message(&event);
  ASSERT_EQUAL(g_app->selected_clip, clip);
  event.message = kEventPointerCancel;
  dispatch_message(&event);
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
  test_sheet_drop();
  test_two_finger_sheet_pan();
  TEST_END();
}

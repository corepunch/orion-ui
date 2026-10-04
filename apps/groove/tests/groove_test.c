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
  uint64_t revision = block_get(0)->audio_revision;
  block_pcm_t pcm[GR_MAX_BLOCKS];
  blocks_render(140, pcm);
  blocks_swap(pcm);
  ASSERT(block_get(0)->audio_revision > revision, "tempo changes invalidate waveform textures");
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
  int sx = window_screen_x(g_app->sheet) + GR_SHEET_HEADER_W + bar * 88 + 20;
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
  ASSERT_EQUAL(g_app->song.clips[clip].bar, 0);
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
  window_t *win = create_window("Groove", WINDOW_TOOLBAR | WINDOW_STATUSBAR, MAKERECT(0, 0, 1000, 700), NULL, main_win_proc, 0, g_app);
  ASSERT_TRUE(win && g_app->tabs && g_app->library);
  window_t *all = g_app->tabs->children;
  ASSERT_TRUE(all && strcmp(all->title, "All") == 0);
  ASSERT_EQUAL(visible_tiles(all), blocks_count());
  ASSERT_TRUE(block_matches(0, "") && block_matches(0, "FLOOR") && block_matches(0, "drum") && !block_matches(0, "zzz"));

  window_t *search = NULL;
  for (window_t *c = g_app->library->children; c; c = c->next) if (c->id == ID_SEARCH) search = c;
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
  send_message(search, evKeyDown, AX_KEY_BACKSPACE, NULL);
  send_message(search, evKeyDown, AX_KEY_BACKSPACE, NULL);
  ASSERT_EQUAL(visible_tiles(all), blocks_count());
  destroy_window(win);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}

static void test_shared_block_cards(void) {
  TEST("canvas and library share the same card widget, measure, spacing and library drag behavior");
  test_env_init();
  g_app = app_init();
  window_t *win = create_window("Groove", WINDOW_TOOLBAR | WINDOW_STATUSBAR, MAKERECT(0, 0, 1000, 700), NULL, main_win_proc, 0, g_app);
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
  window_t *win = create_window("Groove", WINDOW_TOOLBAR | WINDOW_STATUSBAR, MAKERECT(0, 0, 1000, 700), NULL, main_win_proc, 0, g_app);
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
  int sx = window_screen_x(sheet) + GR_SHEET_HEADER_W + 7 * 88 + 40 + grab.x - get_scroll_pos(sheet, SB_HORZ);
  int sy = window_screen_y(sheet) + 22 + 5 * row + 6 + grab.y;
  send_message(library, evLeftButtonDown, MAKEDWORD(grab.x, grab.y), NULL);
  send_message(library, evMouseMove, MAKEDWORD(sx - window_screen_x(library), sy - window_screen_y(library)), NULL);
  ASSERT_TRUE(g_app->drag.active && library->drag_visual);
  ASSERT(g_app->drag.grab.x == grab.x, "library horizontal grab offset");
  ASSERT(g_app->drag.grab.y == grab.y, "library vertical grab offset");
  ASSERT_TRUE(g_app->drag.valid && g_app->drag.bar == 7 && g_app->drag.track == 5);
  send_message(library, evLeftButtonUp, MAKEDWORD(sx + 88 - window_screen_x(library), sy - window_screen_y(library)), NULL);
  int clip = g_app->selected_clip;
  ASSERT(clip >= 0, "library drop selects the added clip");
  ASSERT(song_clip_at(&g_app->song, 5, 8) == clip, "library release snaps to card origin");
  ASSERT(g_app->song.nclips == 1, "library drop adds a clip");
  ASSERT_FALSE(g_app->drag.active || library->drag_visual);
  ASSERT_NULL(g_ui_runtime.captured);

  ASSERT_TRUE(send_message(sheet, shSetDropAnchor, GR_DROP_ANCHOR_POINTER, NULL));
  g_app->drag = (drag_t){ .active = true, .block = block, .from_clip = -1, .grab = grab, .track = -1 };
  send_message(sheet, shDragOver, MAKEDWORD(sx, sy), NULL);
  ASSERT_TRUE(g_app->drag.valid && g_app->drag.bar == 9 && g_app->drag.track == 6);
  send_message(sheet, shDrop, MAKEDWORD(sx, sy), NULL);
  ASSERT_TRUE(song_clip_at(&g_app->song, 6, 9) >= 0);
  ASSERT(g_app->song.nclips == 2, "moves and rejected drops preserve clip count");
  ASSERT_TRUE(send_message(sheet, shSetDropAnchor, GR_DROP_ANCHOR_SAMPLE, NULL));

  send_message(sheet, evResize, 0, NULL);
  int mx = GR_SHEET_HEADER_W + 8 * 88 + grab.x, my = 22 + 5 * row + grab.y;
  send_message(sheet, evLeftButtonDown, MAKEDWORD(mx, my), NULL);
  send_message(sheet, evMouseMove, MAKEDWORD(mx - 6, my), NULL);
  ASSERT(g_app->drag.bar == 8 && g_app->drag.track == 5, "small movements keep the clip at its nearest grid origin");
  mx = GR_SHEET_HEADER_W + 6 * 88 + 40 + grab.x;
  my = 22 + 2 * row + 6 + grab.y;
  send_message(sheet, evMouseMove, MAKEDWORD(mx, my), NULL);
  ASSERT_TRUE(g_app->drag.active && g_app->drag.valid);
  ASSERT_TRUE(g_app->drag.bar == 6 && g_app->drag.track == 2);
  send_message(sheet, evLeftButtonUp, MAKEDWORD(mx + 20, my), NULL);
  ASSERT_TRUE(g_app->song.clips[clip].bar == 7 && g_app->song.clips[clip].track == 2);
  ASSERT(g_app->song.nclips == 2, "moves and rejected drops preserve clip count");
  ASSERT_FALSE(g_app->drag.active);
  ASSERT_NULL(g_ui_runtime.captured);

  g_app->drag = (drag_t){ .active = true, .block = block, .from_clip = -1, .grab = grab, .track = -1 };
  sx = window_screen_x(sheet) + GR_SHEET_HEADER_W + 2;
  sy = window_screen_y(sheet) + 24;
  send_message(sheet, shDragOver, MAKEDWORD(sx, sy), NULL);
  ASSERT_TRUE(g_app->drag.valid && g_app->drag.bar == 0 && g_app->drag.track == 0);
  send_message(sheet, shDrop, MAKEDWORD(sx - 3, sy), NULL);
  ASSERT(g_app->song.nclips == 2, "moves and rejected drops preserve clip count");
  ASSERT_FALSE(g_app->drag.active);
  destroy_window(win);
  app_shutdown(g_app);
  test_env_shutdown();
  PASS();
}

static void test_drag_center_boundaries(void) {
  TEST("real drags use the sample center across half-cell boundaries, independent of grab offset and scrolling");
  test_env_init();
  g_app = app_init();
  window_t *win = create_window("Groove", WINDOW_TOOLBAR | WINDOW_STATUSBAR, MAKERECT(0, 0, 1100, 760), NULL, main_win_proc, 0, g_app);
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
          int dx = (88 * percent[x] + (percent[x] > 50 ? 99 : 0)) / 100;
          int dy = (size.y * percent[y] + (percent[y] > 50 ? 99 : 0)) / 100;
          int sx = window_screen_x(sheet) + GR_SHEET_HEADER_W + 3 * 88 - scroll + dx;
          int sy = window_screen_y(sheet) + 22 + 2 * size.y + dy;
          event.message = kEventLeftButtonDragged;
          event.x = (sx + grabs[grab].x) * UI_WINDOW_SCALE;
          event.y = (sy + grabs[grab].y) * UI_WINDOW_SCALE;
          dispatch_message(&event);
          ASSERT(g_app->drag.active && g_app->drag.valid, "drag preview is valid");
          ASSERT(window_screen_x(library) + library->drag_dx == sx && window_screen_y(library) + library->drag_dy == sy, "preview is derived from the actual lifted card");
          ASSERT(g_app->drag.bar == 3 + (percent[x] > 50), "column changes after the center crosses halfway");
          ASSERT(g_app->drag.track == 2 + (percent[y] > 50), "row changes after the center crosses halfway");
        }
        event.message = kEventLeftButtonUp;
        dispatch_message(&event);
        ASSERT(g_app->song.nclips == 1 && song_clip_at(&g_app->song, 3, 4) >= 0, "80% diagonal drag commits to the bottom-right placement");
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
  test_sheet_drop();
  test_two_finger_sheet_pan();
  test_library_search();
  test_shared_block_cards();
  test_drag_anchor();
  test_drag_center_boundaries();
  TEST_END();
}

// builtin_scrollbar_test.c — Regression tests for built-in window scrollbars.
//
// Covers scrollbar thumb hit-testing and drag stability when the owning
// window updates its own scroll position in response to evVScroll.

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>

static int client_left_down_count;

static result_t scrolling_window_proc(window_t *win, uint32_t msg,
                                      uint32_t wparam, void *lparam) {
  (void)lparam;
  if (msg == evCreate || msg == evDestroy || msg == evPaint) return 1;
  if (msg == evVScroll) {
    win->vscroll.pos = (uint16_t)wparam;
    return 1;
  }
  if (msg == evLeftButtonDown) {
    client_left_down_count++;
    return 1;
  }
  return 0;
}

static window_t *make_scrolling_window(int w, int h) {
  set_theme(THEME_CLASSIC);
  irect16_t fr = {0, 0, w, h};
  return create_window("scrolling", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL,
                       &fr, NULL, scrolling_window_proc, 0, NULL);
}

static void set_vscroll(window_t *win, int min_val, int max_val, int page, int pos) {
  scroll_info_t info = {
    .fMask = SIF_RANGE | SIF_PAGE | SIF_POS,
    .nMin = min_val,
    .nMax = max_val,
    .nPage = page,
    .nPos = pos,
  };
  set_scroll_info(win, SB_VERT, &info, false);
}

void test_builtin_vscroll_click_starts_drag(void) {
  TEST("built-in vscroll: clicking the thumb starts a drag");

  test_env_init();
  window_t *win = make_scrolling_window(100, 100);
  ASSERT_NOT_NULL(win);

  set_vscroll(win, 0, 200, 20, 20);

  // With arrow buttons, the thumb starts at y = 20.
  // Viewport y=22 is delivered in content space with vscroll.pos added.
  send_message(win, evLeftButtonDown, MAKEDWORD(95, 46), NULL);

  ASSERT_TRUE(win->vscroll.dragging);
  ASSERT_EQUAL(win->vscroll.drag_start_mouse, 9);
  ASSERT_EQUAL(win->vscroll.drag_mouse, 9);
  ASSERT_EQUAL((int)win->vscroll.pos, 20);

  send_message(win, evLeftButtonUp, MAKEDWORD(95, 26), NULL);
  destroy_window(win);
  test_env_shutdown();
  PASS();
}

void test_builtin_vscroll_drag_ignores_scroll_feedback(void) {
  TEST("built-in vscroll: drag uses mouse delta, not scroll feedback");

  test_env_init();
  window_t *win = make_scrolling_window(100, 100);
  ASSERT_NOT_NULL(win);

  set_vscroll(win, 0, 200, 20, 20);

  // Start on the thumb.
  send_message(win, evLeftButtonDown, MAKEDWORD(95, 46), NULL);
  ASSERT_TRUE(win->vscroll.dragging);

  // Move the mouse by 1 px. The scrollbar changes its position by 2.
  send_message(win, evMouseMove, MAKEDWORD(95, 27), (void *)(intptr_t)MAKEDWORD(0, 1));
  ASSERT_EQUAL((int)win->vscroll.pos, 23);
  ASSERT_EQUAL((int)win->vscroll.pos, 23);

  // The window's scroll position changed, so the next LOCAL_Y reported by the
  // framework would also shift by +2 even if the cursor stayed still.
  // A correct drag handler must ignore that feedback and leave the thumb at 22.
  send_message(win, evMouseMove, MAKEDWORD(95, 30), (void *)(intptr_t)MAKEDWORD(0, 0));
  ASSERT_EQUAL((int)win->vscroll.pos, 23);
  ASSERT_EQUAL((int)win->vscroll.pos, 23);

  send_message(win, evLeftButtonUp, MAKEDWORD(95, 30), NULL);
  destroy_window(win);
  test_env_shutdown();
  PASS();
}

void test_builtin_vscroll_click_does_not_reach_client(void) {
  TEST("built-in vscroll: a scrolled window does not receive scrollbar clicks");

  test_env_init();
  client_left_down_count = 0;
  window_t *win = make_scrolling_window(100, 100);
  ASSERT_NOT_NULL(win);
  set_vscroll(win, 0, 200, 20, 20);

  // The pointer is at viewport (95, 22), hence content-space y=42.
  send_message(win, evLeftButtonDown, MAKEDWORD(95, 46), NULL);

  ASSERT_TRUE(win->vscroll.dragging);
  ASSERT_EQUAL(client_left_down_count, 0);
  send_message(win, evLeftButtonUp, MAKEDWORD(95, 46), NULL);
  destroy_window(win);
  test_env_shutdown();
  PASS();
}

static irect16_t recorded_track;
static void record_scrollbar_part(theme_part_t part, irect16_t rect, ctrl_state_t state) {
  if (part == THEME_PART_SCROLLBAR_TRACK) recorded_track = rect;
}

static void test_root_scrollbar_position(void) {
  TEST("Vertical scrollbar stays at the document edge below its titlebar at nonzero pan");
  test_env_init();
  window_t *root = create_window("", WINDOW_VSCROLL | WINDOW_STATUSBAR,
                                 MAKERECT(40, 60, 300, 220), NULL, scrolling_window_proc, 0, NULL);
  set_vscroll(root, 0, 600, 182, 100);
  root->hscroll.pos = 75;
  theme_t *theme = get_theme();
  void (*saved)(theme_part_t, irect16_t, ctrl_state_t) = theme->draw_part;
  theme->draw_part = record_scrollbar_part;
  draw_builtin_scrollbars(root);
  theme->draw_part = saved;
  ASSERT_EQUAL(recorded_track.x, 300 - SCROLLBAR_WIDTH);
  ASSERT_EQUAL(recorded_track.y, 0);
  ASSERT_EQUAL(recorded_track.w, 17);
  ASSERT_EQUAL(recorded_track.h, 220 - TITLEBAR_HEIGHT - STATUSBAR_HEIGHT);
  test_env_shutdown();
  PASS();
}

static void test_merged_scrollbar_dispatch(void) {
  TEST("Merged horizontal drag captures through release and chrome excludes children after scrolling");
  test_env_init();
  set_theme(THEME_MODERN);
  window_t *root = create_window("", WINDOW_NOTITLE | WINDOW_HSCROLL | WINDOW_VSCROLL | WINDOW_STATUSBAR,
                                 MAKERECT(0, 0, 300, 200), NULL, scrolling_window_proc, 0, NULL);
  scroll_info_t si = {.fMask = SIF_ALL, .nMax = 600, .nPage = 283};
  set_scroll_info(root, SB_HORZ, &si, false);
  si.nPage = 183;
  set_scroll_info(root, SB_VERT, &si, false);
  window_t *child = create_window("", WINDOW_NOTITLE, MAKERECT(0, 0, 283, 183), root, scrolling_window_proc, 0, NULL);
  show_window(root, true);
  show_window(child, true);
  ui_event_t event = {.message = kEventLeftButtonDown, .x = 85 * UI_WINDOW_SCALE, .y = 190 * UI_WINDOW_SCALE};
  dispatch_message(&event);
  ASSERT_TRUE(root->hscroll.dragging);
  ASSERT_TRUE(g_ui_runtime.captured == root);
  event.message = kEventLeftButtonDragged;
  event.x = 125 * UI_WINDOW_SCALE;
  event.dx = 40;
  dispatch_message(&event);
  ASSERT_TRUE(root->hscroll.pos > 0);
  event.message = kEventLeftButtonUp;
  event.y = 50 * UI_WINDOW_SCALE;
  dispatch_message(&event);
  ASSERT_FALSE(root->hscroll.dragging);
  ASSERT_TRUE(g_ui_runtime.captured == NULL);
  ASSERT_TRUE(find_window(250, 50) == child);
  si.nPos = 100;
  set_scroll_info(root, SB_VERT, &si, false);
  ASSERT_TRUE(find_window(290, 50) == root);
  ASSERT_TRUE(find_window(50, 190) == root);
  client_left_down_count = 0;
  event.message = kEventLeftButtonDown;
  event.x = 50 * UI_WINDOW_SCALE;
  event.y = 190 * UI_WINDOW_SCALE;
  dispatch_message(&event);
  ASSERT_EQUAL(client_left_down_count, 0);
  test_env_shutdown();
  PASS();
}

static void test_content_visibility(void) {
  TEST("Both axes use content overflow, including gutter interactions and exact fits");
  test_env_init();
  struct { int w, h; bool horizontal, vertical; } cases[] = {
    {101,  50, true,  false}, {50, 101, false, true},
    {101,  90, true,  true},  {90, 101, true,  true},
    {100, 100, false, false}, {50,  50, false, false}
  };
  window_t *win = create_window("", WINDOW_NOTITLE | WINDOW_HSCROLL | WINDOW_VSCROLL,
                                MAKERECT(0, 0, 100, 100), NULL, scrolling_window_proc, 0, NULL);
  for (size_t i = 0; i < ARRAY_LEN(cases); i++) {
    set_scroll_content(win, cases[i].w, cases[i].h, 100, 100);
    ASSERT_EQUAL(win->hscroll.visible, cases[i].horizontal);
    ASSERT_EQUAL(win->vscroll.visible, cases[i].vertical);
    ASSERT_EQUAL(win->hscroll.page, get_client_rect(win).w);
    ASSERT_EQUAL(win->vscroll.page, get_client_rect(win).h);
  }
  win->flags |= WINDOW_STATUSBAR;
  win->frame.h += STATUSBAR_HEIGHT;
  set_scroll_content(win, 101, 90, 0, 0);
  ASSERT_TRUE(win->hscroll.visible);
  ASSERT_FALSE(win->vscroll.visible);
  ASSERT_EQUAL(win->vscroll.page, 100);
  set_scroll_content(win, 100, 100, 0, 0);
  ASSERT_FALSE(win->hscroll.visible);
  ASSERT_FALSE(win->vscroll.visible);
  ASSERT_EQUAL(get_client_rect(win).w, 100);
  test_env_shutdown();
  PASS();
}

int main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  TEST_START("built-in scrollbar tests");

  test_builtin_vscroll_click_starts_drag();
  test_builtin_vscroll_drag_ignores_scroll_feedback();
  test_builtin_vscroll_click_does_not_reach_client();
  test_merged_scrollbar_dispatch();
  test_root_scrollbar_position();
  test_content_visibility();

  TEST_END();
}

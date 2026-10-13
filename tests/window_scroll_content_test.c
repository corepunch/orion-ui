#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>

static int clicks, scrolls;
static irect16_t target;

static result_t content_proc(window_t *win, uint32_t msg, uint32_t wp, void *lp) {
  (void)wp; (void)lp;
  if (msg == evHScroll || msg == evVScroll) { scrolls++; return true; }
  if (msg == evLeftButtonDown) {
    if (rect_contains_point(target, (ipoint16_t){LOWORD(wp), HIWORD(wp)})) clicks++;
    return true;
  }
  return msg == evCreate || msg == evPaint || msg == evDestroy;
}

static void test_insets_and_reveal(void) {
  TEST("window scrolling: asymmetric padding and fixed header share extent, viewport and reveal geometry");
  test_env_init(); set_theme(THEME_CLASSIC);
  window_t *win = create_window("", WINDOW_NOTITLE | WINDOW_HSCROLL | WINDOW_VSCROLL,
                                MAKERECT(0, 0, 120, 100), NULL, content_proc, 0, NULL);
  window_set_scroll_content(win, 200, 300, R(3, 5, 7, 11), R(2, 13, 4, 17));
  irect16_t content = window_scroll_content_rect(win), viewport = window_scroll_viewport(win);
  ASSERT_EQUAL(content.x, 5); ASSERT_EQUAL(content.y, 18);
  ASSERT_EQUAL(content.w, 200); ASSERT_EQUAL(content.h, 300);
  ASSERT_EQUAL(win->hscroll.max_val, 210); ASSERT_EQUAL(win->vscroll.max_val, 316);
  ASSERT_EQUAL(win->hscroll.page, viewport.w); ASSERT_EQUAL(win->vscroll.page, viewport.h);
  scrolls = 0;
  target = R(185, 298, 20, 20);
  window_ensure_rect_visible(win, target, SB_BOTH);
  irect16_t painted = window_scroll_rect_to_client(win, target);
  ASSERT_EQUAL(painted.x + painted.w, viewport.x + viewport.w - 7);
  ASSERT_EQUAL(painted.y + painted.h, viewport.y + viewport.h - 11);
  ASSERT_EQUAL(scrolls, 2);
  window_ensure_rect_visible(win, target, SB_BOTH);
  ASSERT_EQUAL(scrolls, 2);
  irect16_t restored = window_scroll_rect_from_client(win, painted);
  ASSERT_EQUAL(restored.x, target.x); ASSERT_EQUAL(restored.y, target.y);
  win->frame.w = 500; win->frame.h = 500;
  send_message(win, evResize, 0, NULL);
  ASSERT_EQUAL(win->hscroll.pos, 0); ASSERT_EQUAL(win->vscroll.pos, 0);
  ASSERT_FALSE(win->hscroll.visible); ASSERT_FALSE(win->vscroll.visible);
  test_env_shutdown(); PASS();
}

static void test_overlay_empty_and_oversized(void) {
  TEST("window scrolling: overlay bars, oversized reveal, empty content and rejected geometry");
  test_env_init(); set_theme(THEME_MODERN);
  theme_t *theme = get_theme();
  int saved_width = theme->scrollbar.width;
  bool saved_overlay = theme->scrollbar.overlay;
  theme->scrollbar.width = 0; theme->scrollbar.overlay = true;
  window_t *win = create_window("", WINDOW_NOTITLE | WINDOW_VSCROLL,
                                MAKERECT(0, 0, 100, 80), NULL, content_proc, 0, NULL);
  window_set_scroll_content(win, 0, 400, R(4, 6, 8, 10), R(0, 12, 0, 0));
  bool ok = window_scroll_content_rect(win).w == 88 && win->vscroll.page == 68;
  target = R(4, 118, 20, 100);
  window_ensure_rect_visible(win, target, SB_BOTH);
  ok &= window_scroll_rect_to_client(win, target).y == 18;
  int pos = win->vscroll.pos;
  window_ensure_rect_visible(win, target, SB_BOTH);
  ok &= win->vscroll.pos == pos;
  window_set_scroll_content(win, -1, 4, R(0, 0, 0, 0), R(0, 0, 0, 0));
  ok &= win->scroll_content.height == 400;
  window_set_scroll_content(win, 0, 0, R(4, 6, 8, 10), R(0, 12, 0, 0));
  ok &= win->vscroll.max_val == 0 && win->vscroll.pos == 0 && !win->vscroll.visible;
  test_env_shutdown();
  theme->scrollbar.width = saved_width; theme->scrollbar.overlay = saved_overlay;
  ASSERT_TRUE(ok); PASS();
}

static void test_nested_pointer_geometry(void) {
  TEST("window scrolling: root and nested pointers hit the same content rectangles used for painting");
  for (int nested = 0; nested < 2; nested++) {
    test_env_init(); set_theme(THEME_CLASSIC);
    window_t *root = create_window("", WINDOW_NOTITLE, MAKERECT(30, 40, 300, 200), NULL, content_proc, 0, NULL);
    window_t *win = nested ? create_window("", WINDOW_NOTITLE, MAKERECT(17, 23, 120, 100), root, content_proc, 0, NULL) : root;
    win->flags |= WINDOW_VSCROLL;
    window_set_scroll_content(win, 0, 400, R(5, 7, 5, 9), R(0, 11, 0, 0));
    target = R(5, 378, 60, 40);
    window_ensure_rect_visible(win, target, SB_BOTH);
    irect16_t painted = window_scroll_rect_to_client(win, target);
    show_window(root, true);
    clicks = 0;
    ui_event_t event = {.message = kEventLeftButtonDown,
      .x = (window_screen_x(win) + painted.x + 10) * UI_WINDOW_SCALE,
      .y = (window_screen_y(win) + painted.y + 10) * UI_WINDOW_SCALE};
    dispatch_message(&event);
    ASSERT_EQUAL(clicks, 1);
    test_env_shutdown();
  }
  PASS();
}

int main(void) {
  TEST_START("window scroll content");
  test_insets_and_reveal();
  test_overlay_empty_and_oversized();
  test_nested_pointer_geometry();
  TEST_END();
}

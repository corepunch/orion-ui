#include "test_framework.h"
#include "test_env.h"
#include <orion/user/scrollbar.h>
#include <math.h>

static uint32_t scroll_time;
static longTime_t scroll_clock(void) { return scroll_time; }
// Exercise the timer integration with a deterministic clock.
#define axGetMilliseconds scroll_clock
#include <orion/user/scrollbar.c>
#undef axGetMilliseconds

static result_t scroll_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  return msg == evCreate || msg == evPaint || msg == evDestroy;
}

static window_t *make_scroll_window(void) {
  window_t *win = create_window("Scroll", WINDOW_NOTITLE | WINDOW_HSCROLL | WINDOW_VSCROLL,
                               MAKERECT(0, 0, 200, 300), NULL, scroll_proc, 0, NULL);
  show_window(win, true);
  set_scroll_content(win, 10000, 10000, 2000, 2000);
  return win;
}

static void advance_scroll(window_t *win, uint32_t duration, uint32_t interval) {
  uint32_t end = scroll_time + duration;
  while (scroll_time < end && scrollbar_is_flinging(win)) {
    scroll_time += MIN(interval, end - scroll_time);
    scrollbar_handle_builtin_timer(win, g_fling.timer_id);
  }
}

static void test_normal_coast(void) {
  TEST("UIKit normal coasting travels the same distance at different timer cadences");
  test_env_init();
  set_theme(THEME_CLASSIC);
  window_t *win = make_scroll_window();
  const uint32_t intervals[] = {8, 16, 33, 100, 500};
  for (size_t i = 0; i < ARRAY_LEN(intervals); i++) {
    set_scroll_content(win, 10000, 10000, 2000, 2000);
    scroll_time = 1000;
    scrollbar_fling(win, 1, -1);
    advance_scroll(win, 1000, intervals[i]);
    ASSERT_EQUAL(win->hscroll.pos, 2432);
    ASSERT_EQUAL(win->vscroll.pos, 1568);
    ASSERT_TRUE(scrollbar_is_flinging(win));
    ASSERT_TRUE(fabsf(g_fling.velocity[0] - 0.135065f) < 0.00001f);
    scrollbar_stop_fling(win);
  }
  destroy_window(win);
  test_env_shutdown();
  PASS();
}

static void test_tail_and_interruption(void) {
  TEST("momentum preserves its slow tail and stops on interruption or a content edge");
  test_env_init();
  set_theme(THEME_CLASSIC);
  window_t *win = make_scroll_window();
  scroll_time = 1000;
  scrollbar_fling(win, 1, 0);
  advance_scroll(win, 2000, 16);
  ASSERT_TRUE(scrollbar_is_flinging(win));
  ASSERT_TRUE(g_fling.velocity[0] < 0.05f);
  int tail_start = win->hscroll.pos;
  advance_scroll(win, 1000, 16);
  ASSERT_FALSE(scrollbar_is_flinging(win));
  ASSERT_TRUE(win->hscroll.pos >= tail_start + 6);
  ASSERT_TRUE(abs(win->hscroll.pos - 2497) <= 1);

  scrollbar_fling(win, 1, 0);
  ASSERT_TRUE(scrollbar_stop_fling(win));
  ASSERT_FALSE(scrollbar_is_flinging(win));
  scrollbar_fling(win, 0.02f, 0);
  ASSERT_FALSE(scrollbar_is_flinging(win));
  set_scroll_content(win, 10000, 10000, 1, 2000);
  scrollbar_fling(win, -1, 0);
  advance_scroll(win, 100, 16);
  ASSERT_EQUAL(win->hscroll.pos, 0);
  ASSERT_FALSE(scrollbar_is_flinging(win));
  destroy_window(win);
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("Touch momentum");
  test_normal_coast();
  test_tail_and_interruption();
  TEST_END();
}

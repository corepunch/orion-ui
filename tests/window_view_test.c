#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <math.h>

static ipoint16_t received;
static result_t content_proc(window_t *win, uint32_t msg, uint32_t wp, void *lp) {
  if (msg == evMouseMove) received = (ipoint16_t){(int16_t)LOWORD(wp), (int16_t)HIWORD(wp)};
  return true;
}

static void test_centered_content(void) {
  TEST("pointer delivery uses centered content coordinates");
  window_t win = {.frame = {0, 0, 320, 200}, .flags = WINDOW_NOTITLE, .proc = content_proc};
  window_view_init(&win, 64, 64, 1, false);
  send_pointer_message(&win, evMouseMove, MAKEDWORD(128, 68), NULL);
  ASSERT_EQUAL(received.x, 0); ASSERT_EQUAL(received.y, 0);
  send_pointer_message(&win, evMouseMove, MAKEDWORD(127, 67), NULL);
  ASSERT_EQUAL(received.x, -1); ASSERT_EQUAL(received.y, -1);
  PASS();
}

static void test_scrolled_zoom(void) {
  TEST("zoom and scroll share the painting and input transform");
  window_t win = {.frame = {0, 0, 128, 96}, .flags = WINDOW_NOTITLE, .proc = content_proc};
  window_view_init(&win, 256, 256, 1, false);
  window_view_set_zoom(&win, 2, NULL);
  window_view_set_scroll(&win, SB_HORZ, 37);
  window_view_set_scroll(&win, SB_VERT, 19);
  ipoint16_t point = window_content_to_client(&win, (ipoint16_t){24, 18});
  ASSERT_EQUAL(point.x, 11); ASSERT_EQUAL(point.y, 17);
  send_pointer_message(&win, evMouseMove, MAKEDWORD(point.x, point.y), NULL);
  ASSERT_EQUAL(received.x, 24); ASSERT_EQUAL(received.y, 18);
  PASS();
}

static void test_rotated_content(void) {
  TEST("rotated content keeps direct painting and pointer positions aligned");
  window_t win = {.frame = {0, 0, 200, 200}, .flags = WINDOW_NOTITLE, .proc = content_proc};
  window_view_init(&win, 100, 80, 1, true);
  ax_gesture_t gesture = {AX_GESTURE_UPDATE, 100, 100, 100, 100, 1, 1.57079632679f};
  window_view_apply_gesture(&win, &gesture);
  ipoint16_t point = window_content_to_client(&win, (ipoint16_t){20, 10});
  ASSERT_EQUAL(point.x, 130); ASSERT_EQUAL(point.y, 70);
  send_pointer_message(&win, evMouseMove, MAKEDWORD(130, 70), NULL);
  ASSERT_EQUAL(received.x, 20); ASSERT_EQUAL(received.y, 10);
  frect_t bounds = window_view_bounds(&win);
  ASSERT_TRUE(fabsf(bounds.w - 80) < 0.001f);
  ASSERT_TRUE(fabsf(bounds.h - 100) < 0.001f);
  PASS();
}

static void test_retina_and_clip(void) {
  TEST("Retina image pixels and visible content bounds use the same view");
  window_t win = {.frame = {0, 0, 100, 80}, .flags = WINDOW_NOTITLE, .proc = content_proc};
  window_view_init(&win, 200, 160, 2, true);
  send_pointer_message(&win, evMouseMove, MAKEDWORD(10, 12), NULL);
  ASSERT_EQUAL(received.x, 20); ASSERT_EQUAL(received.y, 24);
  irect16_t visible = window_view_visible_rect(&win);
  ASSERT_EQUAL(visible.w, 200); ASSERT_EQUAL(visible.h, 160);
  window_view_pan(&win, (ipoint16_t){500, 500});
  visible = window_view_visible_rect(&win);
  ASSERT_EQUAL(visible.w, 0); ASSERT_EQUAL(visible.h, 0);
  PASS();
}

static void test_precise_pan(void) {
  TEST("a one-pixel hand drag remains one screen pixel at high zoom");
  window_t win = {.frame = {0, 0, 100, 80}, .flags = WINDOW_NOTITLE, .proc = content_proc};
  window_view_init(&win, 200, 160, 1, true);
  window_view_set_zoom(&win, 32, NULL);
  send_pointer_message(&win, evLeftButtonDown, MAKEDWORD(20, 20), NULL);
  window_view_begin_drag(&win);
  float start = win.view.matrix.tx;
  send_pointer_message(&win, evMouseMove, MAKEDWORD(21, 20), NULL);
  window_view_drag(&win);
  ASSERT_TRUE(fabsf(win.view.matrix.tx - start - 1) < 0.001f);
  PASS();
}

int main(void) {
  TEST_START("Window content views");
  test_env_init();
  test_centered_content();
  test_scrolled_zoom();
  test_rotated_content();
  test_retina_and_clip();
  test_precise_pan();
  test_env_shutdown();
  TEST_END();
}

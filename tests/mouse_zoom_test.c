#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <math.h>

static int zoom_count, wheel_count;
static float received_scale;
static ipoint16_t received_point;

static result_t zoom_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: case evPaint: case evDestroy: return true;
    case evMouseZoom:
      zoom_count++;
      received_scale = *(const float *)lparam;
      received_point = (ipoint16_t){(int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)};
      return true;
    case evWheel: wheel_count++; return true;
    default: return false;
  }
}

static result_t zoom_container_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  return msg == evCreate || msg == evPaint || msg == evDestroy;
}

static void wheel_at(int x, int y, int dx, int dy, bool ctrl) {
  ui_event_t modifiers = {.message = kEventModifiersChanged, .wParam = ctrl ? AX_MOD_CTRL : 0};
  dispatch_message(&modifiers);
  ui_event_t event = {.message = kEventScrollWheel, .x = x * UI_WINDOW_SCALE, .y = y * UI_WINDOW_SCALE, .dx = dx, .dy = dy};
  dispatch_message(&event);
}

static void test_zoom_routing(void) {
  TEST("Ctrl+wheel routes a scale and local anchor through nested scrolled windows without scrolling");
  test_env_init();
  zoom_count = wheel_count = 0;
  window_t *root = create_window("Root", WINDOW_NOTITLE, MAKERECT(20, 30, 400, 300), NULL, zoom_container_proc, 0, NULL);
  window_t *child = create_window("Child", WINDOW_NOTITLE, MAKERECT(15, 25, 200, 180), root, zoom_container_proc, 0, NULL);
  window_t *canvas = create_window("Canvas", WINDOW_NOTITLE, MAKERECT(10, 20, 100, 100), child, zoom_proc, 0, NULL);
  show_window(root, true); show_window(child, true); show_window(canvas, true);
  root->hscroll.pos = 7; root->vscroll.pos = 9;
  child->hscroll.pos = 11; child->vscroll.pos = 13;
  canvas->hscroll.pos = 3; canvas->vscroll.pos = 5;
  wheel_at(65, 95, 0, 1, true);
  ASSERT_EQUAL(zoom_count, 1);
  ASSERT_EQUAL(received_point.x, 23); ASSERT_EQUAL(received_point.y, 25);
  ASSERT_TRUE(fabsf(received_scale - 1.046635f) < 0.0001f);
  ASSERT_EQUAL(wheel_count, 0);
  wheel_at(65, 95, 0, -1, true);
  ASSERT_TRUE(fabsf(received_scale - 1 / 1.046635f) < 0.0001f);
  wheel_at(65, 95, 1, 0, true);
  ASSERT_EQUAL(zoom_count, 2);
  wheel_at(65, 95, 0, 1, false);
  ASSERT_EQUAL(wheel_count, 1);
  window_set_state(child, WINDOW_STATE_DISABLED, true);
  wheel_at(65, 95, 0, 1, true);
  ASSERT_EQUAL(zoom_count, 2);
  wheel_at(65, 95, 0, 0, false);
  test_env_shutdown();
  PASS();
}

static void test_default_zoom(void) {
  TEST("content views zoom around the pointer at rotation and Retina scale; unhandled requests never scroll");
  test_env_init();
  window_t *win = create_window("View", WINDOW_NOTITLE, MAKERECT(20, 30, 400, 300), NULL, zoom_container_proc, 0, NULL);
  window_t *overlay = create_window("Overlay", WINDOW_NOTITLE, MAKERECT(10, 10, 100, 100), win, zoom_container_proc, 0, NULL);
  show_window(win, true); show_window(overlay, true);
  window_view_init(win, 1000, 800, 2, true);
  ax_gesture_t rotation = {AX_GESTURE_UPDATE, 50, 50, 50, 50, 8, 0.8f};
  window_view_apply_gesture(win, &rotation);
  ipoint16_t before = window_client_to_content(win, (ipoint16_t){45, 65});
  wheel_at(65, 95, 0, 1, true);
  ASSERT_TRUE(fabsf(window_view_zoom(win) - 8.373081f) < 0.0001f);
  ipoint16_t after = window_client_to_content(win, (ipoint16_t){45, 65});
  ASSERT_TRUE(abs(before.x - after.x) <= 1 && abs(before.y - after.y) <= 1);
  wheel_at(65, 95, 0, -1, true);
  ASSERT_TRUE(fabsf(window_view_zoom(win) - 8) < 0.0001f);
  wheel_at(65, 95, 0, 32767, true);
  ASSERT_TRUE(fabsf(window_view_zoom(win) - 32) < 0.0001f);
  for (int i = 0; i < 5; i++) wheel_at(65, 95, 0, -32768, true);
  ASSERT_TRUE(fabsf(window_view_zoom(win) - 0.05f) < 0.0001f);
  float invalid = NAN;
  ASSERT_FALSE(send_message(win, evMouseZoom, 0, &invalid));
  ASSERT_FALSE(send_message(win, evMouseZoom, 0, NULL));
  free(win->view); win->view = NULL;
  win->flags |= WINDOW_VSCROLL;
  set_scroll_content(win, 400, 1000, 0, 100);
  wheel_at(65, 95, 0, 1, true);
  ASSERT_EQUAL(win->vscroll.pos, 100);
  wheel_at(65, 95, 0, 0, false);
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("Framework mouse zoom");
  test_zoom_routing();
  test_default_zoom();
  TEST_END();
}

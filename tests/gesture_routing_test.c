#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <math.h>

static ax_gesture_t received;
static int gesture_count;
static result_t gesture_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: case evPaint: case evDestroy: return true;
    case evGesture: received = *(ax_gesture_t *)lparam; gesture_count++; return true;
    default: return false;
  }
}
static result_t container_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  return msg == evCreate || msg == evDestroy;
}

static void test_nested_gesture(void) {
  TEST("gesture coordinates include nested scroll offsets and retain the initial target");
  test_env_init();
  window_t *root = create_window("Root", WINDOW_NOTITLE, MAKERECT(20, 30, 400, 300), NULL, container_proc, 0, NULL);
  window_t *child = create_window("Child", WINDOW_NOTITLE, MAKERECT(15, 25, 200, 180), root, container_proc, 0, NULL);
  window_t *canvas = create_window("Canvas", WINDOW_NOTITLE, MAKERECT(10, 20, 100, 100), child, gesture_proc, 0, NULL);
  show_window(root, true); show_window(child, true); show_window(canvas, true);
  root->hscroll.pos = 7; root->vscroll.pos = 9;
  child->hscroll.pos = 11; child->vscroll.pos = 13;
  canvas->hscroll.pos = 3; canvas->vscroll.pos = 5;
  ui_event_t event = {.message = kEventGesture,
    .gesture = {AX_GESTURE_BEGIN, 65, 95, 63, 92, 1, 0}};
  dispatch_message(&event);
  ASSERT_EQUAL(gesture_count, 1);
  ASSERT_TRUE(fabsf(received.x - 23) < 0.001f);
  ASSERT_TRUE(fabsf(received.y - 25) < 0.001f);
  ASSERT_TRUE(fabsf(received.previous_x - 21) < 0.001f);
  ASSERT_TRUE(fabsf(received.previous_y - 22) < 0.001f);
  event.gesture = (ax_gesture_t){AX_GESTURE_UPDATE, 600, 500, 65, 95, 1.2f, 0.2f};
  dispatch_message(&event);
  ASSERT_EQUAL(gesture_count, 2);
  ASSERT_TRUE(fabsf(received.x - 558) < 0.001f);
  ASSERT_TRUE(fabsf(received.y - 430) < 0.001f);
  destroy_window(root);
  event.gesture.phase = AX_GESTURE_END;
  dispatch_message(&event);
  ASSERT_EQUAL(gesture_count, 2);
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("Gesture routing");
  test_nested_gesture();
  TEST_END();
}

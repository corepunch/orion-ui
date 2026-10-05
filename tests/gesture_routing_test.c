#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <math.h>

static ax_gesture_t received;
static int gesture_count;
static ipoint16_t pointer;
static result_t gesture_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: case evPaint: case evDestroy: return true;
    case evGesture: received = *(ax_gesture_t *)lparam; gesture_count++; return true;
    case evMouseMove: pointer = (ipoint16_t){(int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)}; return true;
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
  canvas->hscroll.pos = canvas->vscroll.pos = 0;
  window_view_init(canvas, 100, 100, 1, true);
  ax_gesture_t rotation = {AX_GESTURE_UPDATE, 50, 50, 50, 50, 2, 1.57079632679f};
  window_view_apply_gesture(canvas, &rotation);
  ui_event_t move = {.message = kEventMouseMoved, .wParam = MAKEDWORD(65, 95)};
  dispatch_message(&move);
  ASSERT_EQUAL(pointer.x, 35);
  ASSERT_EQUAL(pointer.y, 65);
  destroy_window(root);
  event.gesture.phase = AX_GESTURE_END;
  dispatch_message(&event);
  ASSERT_EQUAL(gesture_count, 2);
  test_env_shutdown();
  PASS();
}

static void dispatch_gesture(uint32_t phase, float x, float y, float previous_x, float previous_y) {
  ui_event_t event = {.message = kEventGesture,
    .gesture = {phase, x * UI_WINDOW_SCALE, y * UI_WINDOW_SCALE,
                previous_x * UI_WINDOW_SCALE, previous_y * UI_WINDOW_SCALE, 1.3f, 0.2f}};
  dispatch_message(&event);
}

static void test_builtin_pan(void) {
  TEST("native pan scrolls the starting child by exact points, retaining fractions and clamping at edges");
  test_env_init();
  set_theme(THEME_CLASSIC);
  window_t *root = create_window("Root", WINDOW_NOTITLE, MAKERECT(10, 20, 500, 400), NULL, container_proc, 0, NULL);
  window_t *child = create_window("Canvas", WINDOW_NOTITLE | WINDOW_HSCROLL | WINDOW_VSCROLL,
                                 MAKERECT(20, 30, 200, 150), root, container_proc, 0, NULL);
  window_t *sibling = create_window("Other", WINDOW_NOTITLE | WINDOW_HSCROLL | WINDOW_VSCROLL,
                                   MAKERECT(250, 30, 200, 150), root, container_proc, 0, NULL);
  show_window(root, true); show_window(child, true); show_window(sibling, true);
  set_scroll_content(child, 1000, 800, 100, 100);
  set_scroll_content(sibling, 1000, 800, 100, 100);
  dispatch_gesture(AX_GESTURE_BEGIN, 80, 100, 80, 100);
  dispatch_gesture(AX_GESTURE_UPDATE, 40, 70, 80, 100);
  ASSERT_EQUAL(child->hscroll.pos, 140); ASSERT_EQUAL(child->vscroll.pos, 130);
  dispatch_gesture(AX_GESTURE_UPDATE, 380, 100, 40, 70);
  ASSERT_EQUAL(child->hscroll.pos, 0); ASSERT_EQUAL(child->vscroll.pos, 100);
  ASSERT_EQUAL(sibling->hscroll.pos, 100); ASSERT_EQUAL(sibling->vscroll.pos, 100);
  for (int i = 0; i < 5; i++)
    dispatch_gesture(AX_GESTURE_UPDATE, 380 - (i + 1) * 0.2f, 100, 380 - i * 0.2f, 100);
  ASSERT_EQUAL(child->hscroll.pos, 1);
  dispatch_gesture(AX_GESTURE_UPDATE, -2000, -2000, 379, 100);
  ASSERT_EQUAL(child->hscroll.pos, child->hscroll.max_val - child->hscroll.page);
  ASSERT_EQUAL(child->vscroll.pos, child->vscroll.max_val - child->vscroll.page);
  dispatch_gesture(AX_GESTURE_UPDATE, -1999, -1999, -2000, -2000);
  ASSERT_EQUAL(child->hscroll.pos, child->hscroll.max_val - child->hscroll.page - 1);
  dispatch_gesture(AX_GESTURE_CANCEL, -1999, -1999, -1999, -1999);
  ASSERT_FALSE(child->hscroll.gesture_active); ASSERT_FALSE(child->vscroll.gesture_active);
  int pos = child->hscroll.pos;
  dispatch_gesture(AX_GESTURE_UPDATE, -2100, -2100, -1999, -1999);
  ASSERT_EQUAL(child->hscroll.pos, pos);
  dispatch_gesture(AX_GESTURE_BEGIN, 80, 100, 80, 100);
  destroy_window(child);
  dispatch_gesture(AX_GESTURE_UPDATE, 380, 90, 80, 100);
  ASSERT_EQUAL(sibling->hscroll.pos, 100);
  test_env_shutdown();
  PASS();
}

static void test_pan_axis_availability(void) {
  TEST("native pan respects disabled axes, ignores invalid payloads and leaves exact-fit views alone");
  test_env_init();
  window_t *win = create_window("Canvas", WINDOW_NOTITLE | WINDOW_HSCROLL | WINDOW_VSCROLL,
                               MAKERECT(0, 0, 200, 150), NULL, container_proc, 0, NULL);
  show_window(win, true);
  set_scroll_content(win, 1000, 800, 100, 100);
  enable_scroll_bar(win, SB_VERT, false);
  dispatch_gesture(AX_GESTURE_BEGIN, 50, 50, 50, 50);
  dispatch_gesture(AX_GESTURE_UPDATE, 30, 30, 50, 50);
  ASSERT_EQUAL(win->hscroll.pos, 120); ASSERT_EQUAL(win->vscroll.pos, 100);
  ax_gesture_t invalid = {AX_GESTURE_UPDATE, NAN, 30, 50, 50, 1, 0};
  ASSERT_FALSE(send_message(win, evGesture, 0, &invalid));
  ASSERT_EQUAL(win->hscroll.pos, 120);
  dispatch_gesture(AX_GESTURE_END, 30, 30, 30, 30);
  set_scroll_content(win, 50, 50, 0, 0);
  ax_gesture_t begin = {AX_GESTURE_BEGIN, 30, 30, 30, 30, 1, 0};
  ASSERT_FALSE(send_message(win, evGesture, 0, &begin));
  test_env_shutdown();
  PASS();
}

static int presses, releases, cancels, row_press_y;
static result_t row_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: case evPaint: case evDestroy: return true;
    case evLeftButtonDown: presses++; row_press_y = (int16_t)HIWORD(wparam); return true;
    case evLeftButtonUp: releases++; return true;
    case evPointerCancel: cancels++; return true;
    default: return false;
  }
}
static result_t grab_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (msg == evQueryDrag) return DRAG_NOW;
  return row_proc(win, msg, wparam, lparam);
}
static result_t hold_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (msg == evQueryDrag) return DRAG_AFTER_HOLD;
  return row_proc(win, msg, wparam, lparam);
}

static void dispatch_finger(uint32_t message, int x, int y, uint32_t time) {
  ui_event_t event = {.message = message, .wParam = MAKEDWORD(x * UI_WINDOW_SCALE, y * UI_WINDOW_SCALE),
                      .pointer = {.flags = AX_POINTER_TOUCH, .time = time}};
  dispatch_message(&event);
}

static void test_touch_pan(void) {
  TEST("a finger swipe scrolls the content under it; a tap clicks; claimed drags and the stylus do not scroll");
  test_env_init();
  set_theme(THEME_CLASSIC);
  window_t *list = create_window("List", WINDOW_NOTITLE | WINDOW_VSCROLL, MAKERECT(0, 0, 200, 300), NULL, row_proc, 0, NULL);
  window_t *grab = create_window("Card", WINDOW_NOTITLE, MAKERECT(0, 0, 100, 40), list, grab_proc, 0, NULL);
  show_window(list, true); show_window(grab, true);
  set_scroll_content(list, 200, 2000, 0, 0);

  presses = releases = 0;
  dispatch_finger(kEventLeftButtonDown, 50, 200, 1000);
  ASSERT_EQUAL(presses, 0);
  dispatch_finger(kEventLeftButtonDragged, 52, 196, 1010);
  dispatch_finger(kEventLeftButtonUp, 52, 196, 1020);
  ASSERT_EQUAL(presses, 1); ASSERT_EQUAL(releases, 1); ASSERT_EQUAL(row_press_y, 200);
  ASSERT_EQUAL(list->vscroll.pos, 0);

  presses = releases = 0;
  dispatch_finger(kEventLeftButtonDown, 50, 200, 2000);
  dispatch_finger(kEventLeftButtonDragged, 50, 180, 2016);
  dispatch_finger(kEventLeftButtonDragged, 50, 140, 2032);
  ASSERT_EQUAL(list->vscroll.pos, 60 - TOUCH_SLOP);
  dispatch_finger(kEventLeftButtonUp, 50, 140, 2300);
  ASSERT_EQUAL(presses, 0); ASSERT_EQUAL(releases, 0);
  ASSERT_EQUAL(list->vscroll.fling_timer_id, 0u);

  dispatch_finger(kEventLeftButtonDown, 50, 200, 3000);
  ASSERT_EQUAL(presses, 0);
  dispatch_finger(kEventLeftButtonDragged, 50, 150, 3016);
  dispatch_finger(kEventLeftButtonDragged, 50, 100, 3032);
  dispatch_finger(kEventLeftButtonUp, 50, 100, 3040);
  ASSERT_EQUAL(presses, 0);
  ASSERT_TRUE(list->vscroll.fling_timer_id != 0);
  dispatch_finger(kEventLeftButtonDown, 50, 200, 3100);
  ASSERT_EQUAL(list->vscroll.fling_timer_id, 0u);
  dispatch_finger(kEventLeftButtonUp, 50, 200, 3110);
  ASSERT_EQUAL(presses, 0);

  int pos = list->vscroll.pos;
  dispatch_finger(kEventLeftButtonDown, 20, 20, 4000);
  ASSERT_EQUAL(presses, 1);
  dispatch_finger(kEventLeftButtonDragged, 20, 80, 4016);
  dispatch_finger(kEventLeftButtonUp, 20, 80, 4032);
  ASSERT_EQUAL(list->vscroll.pos, pos);

  window_t *item = create_window("Item", WINDOW_NOTITLE, MAKERECT(0, 100, 100, 40), list, hold_proc, 0, NULL);
  show_window(item, true);
  list->vscroll.pos = 0;
  presses = 0;
  dispatch_finger(kEventLeftButtonDown, 20, 110, 5000);
  dispatch_finger(kEventLeftButtonDragged, 20, 60, 5016);
  dispatch_finger(kEventLeftButtonUp, 20, 60, 5300);
  ASSERT_EQUAL(presses, 0); ASSERT_EQUAL(list->vscroll.pos, 50 - TOUCH_SLOP);
  list->vscroll.pos = 0;
  // Tests run no platform loop; timer ids are sequential, so post the hold timer by hand.
  uint32_t hold = axSetTimer(list, 100000, NULL, false) + 1;
  axCancelTimer(hold - 1);
  dispatch_finger(kEventLeftButtonDown, 20, 110, 6000);
  ASSERT_EQUAL(presses, 0);
  ui_event_t fire = {.target = list, .message = kEventTimer, .wParam = hold};
  dispatch_message(&fire);
  ASSERT_EQUAL(presses, 1);
  dispatch_finger(kEventLeftButtonDragged, 20, 60, 6516);
  ASSERT_EQUAL(list->vscroll.pos, 0);
  dispatch_finger(kEventLeftButtonUp, 20, 60, 6532);
  ASSERT_EQUAL(releases, 2);
  // A busy main thread can queue the hold timer behind the next sample; its timestamp decides.
  presses = 0;
  dispatch_finger(kEventLeftButtonDown, 20, 110, 7000);
  dispatch_finger(kEventLeftButtonDragged, 20, 60, 7000 + TOUCH_LONG_PRESS_MS);
  ASSERT_EQUAL(presses, 1); ASSERT_EQUAL(list->vscroll.pos, 0);
  dispatch_finger(kEventLeftButtonUp, 20, 60, 7600);

  presses = 0;
  ui_event_t pen = {.message = kEventLeftButtonDown, .wParam = MAKEDWORD(50 * UI_WINDOW_SCALE, 200 * UI_WINDOW_SCALE),
                    .pointer = {.flags = AX_POINTER_STYLUS}};
  dispatch_message(&pen);
  ASSERT_EQUAL(presses, 1);
  pen.message = kEventLeftButtonUp;
  dispatch_message(&pen);
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("Gesture routing");
  test_nested_gesture();
  test_builtin_pan();
  test_pan_axis_availability();
  test_touch_pan();
  TEST_END();
}

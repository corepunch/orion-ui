#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <math.h>

static uint32_t now = 50000;
static uint32_t test_clock(void) { return now; }

static int animate_count, finished_count;
static window_t *finished_source;

static result_t plain_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  return msg == evCreate || msg == evDestroy || msg == evPaint;
}

static result_t ticking_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (msg == evAnimate) { animate_count++; return true; }
  return plain_proc(win, msg, wparam, lparam);
}

static result_t parent_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (msg == evCommand && HIWORD(wparam) == awnFinished) { finished_count++; finished_source = lparam; return true; }
  return plain_proc(win, msg, wparam, lparam);
}

static void step_frame(void) {
  now += 16;
  repost_messages();
}

static void test_curves(void) {
  TEST("eased curves run 0 to 1 and a spring carries a fling into its target");
  ui_animation_set_clock(test_clock);
  ASSERT_TRUE(ui_curve_ease(UI_CURVE_EASE_OUT, 0) == 0);
  ASSERT_TRUE(fabsf(ui_curve_ease(UI_CURVE_EASE_OUT, 1) - 1) < 1e-4f);
  ASSERT_TRUE(ui_curve_ease(UI_CURVE_EASE_OUT, .5f) > .5f);
  ASSERT_TRUE(fabsf(ui_curve_ease(UI_CURVE_EASE_IN_OUT, .5f) - .5f) < 1e-3f);

  ui_anim_t a;
  ui_anim_start(&a, 0, 100, 0, UI_EASE_OUT(200));
  now += 100;
  float mid = ui_anim_step(&a);
  ASSERT_TRUE(a.running && mid > 50 && mid < 100);
  now += 100;
  ASSERT_TRUE(ui_anim_step(&a) == 100);
  ASSERT_FALSE(a.running);

  // A fling towards the target overshoots an underdamped spring, then it settles exactly there.
  ui_anim_start(&a, 0, 1, 8, UI_SPRING(400, .5f));
  float peak = 0;
  for (int i = 0; i < 200 && a.running; i++) { now += 16; peak = fmaxf(peak, ui_anim_step(&a)); }
  ASSERT_FALSE(a.running);
  ASSERT_TRUE(peak > 1.05f);
  ASSERT_TRUE(ui_anim_value(&a) == 1);

  // Retargeting continues from the current value and velocity, without a jump.
  ui_anim_start(&a, 0, 1, 0, UI_CURVE_PAGING);
  now += 80;
  float before = ui_anim_value(&a), speed = ui_anim_velocity(&a);
  ui_anim_retarget(&a, 0, UI_CURVE_PAGING);
  ASSERT_TRUE(fabsf(ui_anim_value(&a) - before) < 1e-4f);
  ASSERT_TRUE(speed > 0 && fabsf(ui_anim_velocity(&a) - speed) < speed * .05f);

  ui_velocity_t v;
  ui_velocity_reset(&v, 0);
  for (int i = 1; i <= 5; i++) { now += 10; ui_velocity_add(&v, i * 20.0f); }
  ASSERT_TRUE(fabsf(ui_velocity_get(&v) - 2000) < 1);
  now += 200;
  ASSERT_TRUE(ui_velocity_get(&v) == 0);
  PASS();
}

static void test_request_coalesces(void) {
  TEST("request_animate posts one evAnimate per frame however often it is asked");
  test_env_init();
  ui_animation_set_clock(test_clock);
  window_t *win = create_window("Ticker", WINDOW_NOTITLE, MAKERECT(0, 0, 100, 100), NULL, ticking_proc, 0, NULL);
  animate_count = 0;
  request_animate(win);
  request_animate(win);
  request_animate(win);
  repost_messages();
  ASSERT_EQUAL(animate_count, 1);
  repost_messages();
  ASSERT_EQUAL(animate_count, 1);
  destroy_window(win);
  test_env_shutdown();
  PASS();
}

static void test_animate_window(void) {
  TEST("animate_window zooms a child out of a rectangle, takes no input, and notifies its parent");
  test_env_init();
  ui_animation_set_clock(test_clock);
  window_t *root = create_window("Root", WINDOW_NOTITLE, MAKERECT(0, 0, 400, 300), NULL, parent_proc, 0, NULL);
  window_t *child = create_window("Child", WINDOW_NOTITLE | WINDOW_HIDDEN, MAKERECT(0, 0, 400, 300), root, plain_proc, 0, NULL);
  show_window(root, true);
  finished_count = 0;
  ASSERT_TRUE(animate_window(child, R(100, 50, 40, 60), AW_ZOOM | AW_BLEND, UI_CURVE_ZOOM));
  ASSERT_TRUE(window_is_visible(child));
  ASSERT_TRUE(window_is_animating(child));
  window_visual_t v;
  ASSERT_TRUE(window_get_visual(child, &v));
  ASSERT_TRUE(v.rect.x == 100 && v.rect.w == 40 && v.opacity == 0);
  // The content fills the narrow rectangle: a 40x60 window-shaped slice, cropped from the middle.
  ASSERT_TRUE(fabsf(v.source.w / v.source.h - 40.0f / 60) < 1e-3f && v.source.h == 300);
  step_frame();
  step_frame();
  ASSERT_TRUE(window_get_visual(child, &v));
  ASSERT_TRUE(v.rect.w > 40 && v.rect.x < 100 && v.opacity > 0);
  for (int i = 0; i < 200 && window_is_animating(child); i++) step_frame();
  ASSERT_FALSE(window_is_animating(child));
  ASSERT_FALSE(window_get_visual(child, NULL));
  ASSERT_EQUAL(finished_count, 1);
  ASSERT_TRUE(finished_source == child);

  ASSERT_TRUE(animate_window(child, R(100, 50, 40, 60), AW_ZOOM | AW_HIDE, UI_CURVE_ZOOM));
  for (int i = 0; i < 200 && window_is_animating(child); i++) step_frame();
  ASSERT_FALSE(window_is_visible(child));
  ASSERT_EQUAL(finished_count, 2);
  ASSERT_FALSE(animate_window(child, R(0, 0, 0, 0), AW_ZOOM, UI_CURVE_ZOOM));
  destroy_window(root);
  test_env_shutdown();
  PASS();
}

static void test_overlay(void) {
  TEST("OverlayView lays every child over the whole box and gives the pointer to the top one");
  test_env_init();
  register_commctl_classes();
  window_t *root = create_window("Root", WINDOW_NOTITLE, MAKERECT(0, 0, 400, 300), NULL, plain_proc, 0, NULL);
  winproc_t overlay_proc = find_window_class_proc("OverlayView");
  ASSERT_NOT_NULL(overlay_proc);
  window_t *overlay = create_window("", WINDOW_NOTITLE, MAKERECT(0, 0, 400, 300), root, overlay_proc, 0, NULL);
  ASSERT_NOT_NULL(overlay);
  window_t *below = create_window("Below", WINDOW_NOTITLE | WINDOW_FLEXSPACE, MAKERECT(0, 0, 10, 10), overlay, plain_proc, 0, NULL);
  window_t *above = create_window("Above", WINDOW_NOTITLE | WINDOW_FLEXSPACE, MAKERECT(0, 0, 10, 10), overlay, plain_proc, 0, NULL);
  show_window(root, true);
  send_message(overlay, evResize, 0, NULL);
  ASSERT_EQUAL(below->frame.w, 400);
  ASSERT_EQUAL(above->frame.h, 300);
  ASSERT_TRUE((window_t *)send_message(overlay, evChildHitTest, MAKEDWORD(200, 150), NULL) == above);
  show_window(above, false);
  ASSERT_TRUE((window_t *)send_message(overlay, evChildHitTest, MAKEDWORD(200, 150), NULL) == below);
  destroy_window(root);
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("Animation");
  test_curves();
  test_request_coalesces();
  test_animate_window();
  test_overlay();
  ui_animation_set_clock(NULL);
  TEST_END();
}

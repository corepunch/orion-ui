// sprite_test.c — SpriteButton and SpriteSlider behaviour (commctl/sprite.c).

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>

static int g_clicks, g_changes, g_releases;
static uint32_t g_last_id;

static result_t parent_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)lparam;
  if (msg == evCommand) {
    if (HIWORD(wparam) == btnClicked) { g_clicks++; g_last_id = LOWORD(wparam); }
    if (HIWORD(wparam) == sliderValueChanged) g_changes++;
    if (HIWORD(wparam) == spsnReleased) g_releases++;
  }
  return msg == evCreate || msg == evCommand || msg == evDestroy;
}

static window_t *make(window_t **parent, winproc_t proc, irect16_t frame, uint32_t id) {
  test_env_init();
  g_clicks = g_changes = g_releases = 0;
  *parent = test_env_create_window("P", 0, 0, 300, 200, parent_proc, NULL);
  window_t *w = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL, &frame, *parent, proc, 0, NULL);
  if (w) w->id = id;
  return w;
}

static void click(window_t *w, int x, int y, int ux, int uy) {
  send_message(w, evLeftButtonDown, MAKEDWORD(x, y), NULL);
  send_message(w, evMouseMove, MAKEDWORD(ux, uy), NULL);
  send_message(w, evLeftButtonUp, MAKEDWORD(ux, uy), NULL);
}

static void test_button_click(void) {
  TEST("SpriteButton notifies a click released inside, not outside");
  window_t *p;
  window_t *b = make(&p, win_spritebutton, R(10, 10, 46, 36), 7);   // drawn at 2x its 23x18 sprite
  ASSERT_NOT_NULL(b);
  click(b, 20, 20, 21, 21);
  ASSERT_TRUE(g_clicks == 1 && g_last_id == 7);
  click(b, 20, 20, 200, 200);
  ASSERT_TRUE(g_clicks == 1);
  test_env_shutdown();
  PASS();
}

static void test_button_toggle(void) {
  TEST("SpriteButton auto_check flips; owner-driven toggles do not");
  window_t *p;
  window_t *b = make(&p, win_spritebutton, R(0, 0, 23, 18), 1);
  bitmap_t *bm = bitmap_create(46, 18, NULL);
  sprite_button_t def = { .bm = bm, .up = R(0, 0, 23, 18), .down = R(0, 0, 23, 18), .on = R(23, 0, 23, 18), .toggle = true, .auto_check = true };
  send_message(b, spbSetSprites, 0, &def);
  click(b, 5, 5, 5, 5);
  ASSERT_TRUE(send_message(b, btnGetCheck, 0, NULL) == 1);
  click(b, 5, 5, 5, 5);
  ASSERT_TRUE(send_message(b, btnGetCheck, 0, NULL) == 0);
  def.auto_check = false;
  send_message(b, spbSetSprites, 0, &def);
  click(b, 5, 5, 5, 5);
  ASSERT_TRUE(send_message(b, btnGetCheck, 0, NULL) == 0 && g_clicks == 3);
  send_message(b, btnSetCheck, 1, NULL);
  ASSERT_TRUE(send_message(b, btnGetCheck, 0, NULL) == 1);
  bitmap_free(bm);
  test_env_shutdown();
  PASS();
}

static void test_slider_drag(void) {
  TEST("SpriteSlider maps the pointer to the range at any scale and notifies on change");
  window_t *p;
  window_t *s = make(&p, win_spriteslider, R(0, 0, 136, 26), 3);      // 68x13 skin pixels at 2x
  bitmap_t *bm = bitmap_create(68, 13, NULL);
  sprite_slider_t def = { .bm = bm, .native = { 68, 13 }, .track = R(0, 0, 68, 13), .frames = 1, .columns = 1,
                          .thumb = R(0, 0, 14, 11), .thumb_down = R(0, 0, 14, 11), .travel = R(0, 1, 54, 0) };
  send_message(s, spsSetSprites, 0, &def);
  slider_range_t range = { 0, 100 };
  send_message(s, slSetRange, 0, &range);
  send_message(s, slSetPos, 0, (void *)(intptr_t)40);
  ASSERT_TRUE(g_changes == 0);                                        // programmatic changes are silent
  send_message(s, evLeftButtonDown, MAKEDWORD(14 + 54, 10), NULL);    // thumb centre at 2x: (7 + 54 * 0.5) * 2
  ASSERT_TRUE(send_message(s, spsIsDragging, 0, NULL) == 1);
  ASSERT_TRUE(send_message(s, slGetPos, 0, NULL) == 50);
  send_message(s, evMouseMove, MAKEDWORD(500, 10), NULL);
  ASSERT_TRUE(send_message(s, slGetPos, 0, NULL) == 100);
  send_message(s, evMouseMove, MAKEDWORD(-30, 10), NULL);
  ASSERT_TRUE(send_message(s, slGetPos, 0, NULL) == 0);
  send_message(s, evLeftButtonUp, MAKEDWORD(0, 10), NULL);
  ASSERT_TRUE(send_message(s, spsIsDragging, 0, NULL) == 0 && g_changes == 3 && g_releases == 1);
  bitmap_free(bm);
  test_env_shutdown();
  PASS();
}

static void test_slider_vertical(void) {
  TEST("SpriteSlider vertical: the top is the maximum");
  window_t *p;
  window_t *s = make(&p, win_spriteslider, R(0, 0, 14, 63), 4);
  bitmap_t *bm = bitmap_create(14, 63, NULL);
  sprite_slider_t def = { .bm = bm, .native = { 14, 63 }, .track = R(0, 0, 14, 63), .frames = 1, .columns = 1,
                          .thumb = R(0, 0, 11, 11), .thumb_down = R(0, 0, 11, 11), .travel = R(1, 0, 0, 51), .vertical = true };
  send_message(s, spsSetSprites, 0, &def);
  slider_range_t range = { -12, 12 };
  send_message(s, slSetRange, 0, &range);
  send_message(s, evLeftButtonDown, MAKEDWORD(7, 0), NULL);
  ASSERT_TRUE(send_message(s, slGetPos, 0, NULL) == 12);
  send_message(s, evMouseMove, MAKEDWORD(7, 62), NULL);
  ASSERT_TRUE(send_message(s, slGetPos, 0, NULL) == -12);
  send_message(s, evMouseMove, MAKEDWORD(7, 5 + 26), NULL);            // centre of travel
  ASSERT_TRUE(send_message(s, slGetPos, 0, NULL) == 0);
  send_message(s, evLeftButtonUp, MAKEDWORD(7, 31), NULL);
  bitmap_free(bm);
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("Sprite controls");
  test_button_click();
  test_button_toggle();
  test_slider_drag();
  test_slider_vertical();
  TEST_END();
}

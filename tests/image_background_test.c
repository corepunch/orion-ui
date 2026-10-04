#include "test_framework.h"
#include "test_env.h"
#include <orion/user/image_background.h>

static image_background_t test_background(image_atlas_t *atlas) {
  return (image_background_t){.atlas = atlas, .states = {{0, 0, 20, 10}},
    .source_border = {4, 0, 4, 0}, .border = {3, 0, 3, 0}};
}

static void test_background_ownership(void) {
  TEST("window background copies its descriptor, borrows its atlas and restores the themed face");
  test_env_init();
  image_atlas_t atlas = {1, 20, 10};
  image_background_t bg = test_background(&atlas);
  window_t *win = create_window("Skin", WINDOW_NOTITLE, MAKERECT(0, 0, 100, 30), NULL, win_button, 0, NULL);
  ASSERT_NOT_NULL(win);
  ASSERT_TRUE(window_set_image_background(win, &bg));
  ASSERT_TRUE(win->image_background != &bg && win->image_background->atlas == &atlas);
  bg.states[0].w = 1;
  ASSERT_EQUAL(win->image_background->states[0].w, 20);
  ASSERT_TRUE(window_set_image_background(win, NULL));
  ASSERT_TRUE(win->image_background == NULL);
  test_env_shutdown();
  PASS();
}

static void test_background_validation(void) {
  TEST("invalid state regions and borders preserve the previously installed background");
  test_env_init();
  image_atlas_t atlas = {1, 20, 10};
  image_background_t bg = test_background(&atlas);
  window_t *win = create_window("Skin", WINDOW_NOTITLE, MAKERECT(0, 0, 100, 30), NULL, win_card, 0, NULL);
  ASSERT_TRUE(window_set_image_background(win, &bg));
  image_background_t *original = win->image_background;
  bg.states[IMAGE_BG_HOVER] = R(19, 0, 20, 10);
  ASSERT_FALSE(window_set_image_background(win, &bg));
  ASSERT_TRUE(win->image_background == original);
  bg.states[IMAGE_BG_HOVER] = R(0, 0, 0, 0);
  bg.source_border.w = 16;
  ASSERT_FALSE(window_set_image_background(win, &bg));
  ASSERT_TRUE(win->image_background == original);
  bg.source_border.w = 4;
  bg.border.y = -1;
  ASSERT_FALSE(window_set_image_background(win, &bg));
  ASSERT_TRUE(win->image_background == original);
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("Image backgrounds");
  test_background_ownership();
  test_background_validation();
  TEST_END();
}

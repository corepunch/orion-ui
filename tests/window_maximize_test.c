#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>

static irect16_t workspace;
static int resize_count;

static result_t maximize_test_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: case evPaint: case evDestroy: return true;
    case evGetWorkspaceRect: *(irect16_t *)lparam = workspace; return true;
    case evResize: resize_count++; return true;
    default: return false;
  }
}

static void test_maximize_restore(void) {
  TEST("Maximize preserves frame, child state and unrelated styles across workspace changes");
  test_env_init();
  workspace = R(0, 24, 800, 600);
  window_t *win = test_env_create_window("Document", 30, 50, 320, 240, maximize_test_proc, NULL);
  window_t *child = create_window("Child", WINDOW_NOTITLE, MAKERECT(0, 0, 40, 30),
                                  win, maximize_test_proc, 0, NULL);
  ASSERT_NOT_NULL(win);
  ASSERT_NOT_NULL(child);
  child->value = 73;
  ASSERT_TRUE(maximize_window(win));
  ASSERT_TRUE(win->maximized);
  ASSERT_EQUAL(win->frame.y, 24);
  ASSERT_EQUAL(win->frame.w, 800);
  ASSERT_FALSE(window_in_drag_area(win, 24));
  ASSERT_TRUE(win->flags & WINDOW_NOTITLE);
  ASSERT_TRUE(win->flags & WINDOW_NORESIZE);
  ASSERT_TRUE(maximize_window(win));
  ASSERT_EQUAL(win->restore_frame.w, 320);
  ASSERT_FALSE(maximize_window(child));
  workspace = R(0, 32, 640, 480);
  update_maximized_window(win);
  ASSERT_EQUAL(win->frame.y, 32);
  ASSERT_EQUAL(win->frame.w, 640);
  win->flags |= WINDOW_NOFILL;
  ASSERT_TRUE(restore_window(win));
  ASSERT_FALSE(win->maximized);
  ASSERT_EQUAL(win->frame.x, 30);
  ASSERT_EQUAL(win->frame.y, 50);
  ASSERT_EQUAL(win->frame.w, 320);
  ASSERT_EQUAL(win->frame.h, 240);
  ASSERT_FALSE(win->flags & WINDOW_NOTITLE);
  ASSERT_FALSE(win->flags & WINDOW_NORESIZE);
  ASSERT_TRUE(win->flags & WINDOW_NOFILL);
  ASSERT_EQUAL(child->value, 73);
  ASSERT_TRUE(resize_count > 0);
  ASSERT_FALSE(maximize_window(child));
  workspace.w = 0;
  ASSERT_FALSE(maximize_window(win));
  ASSERT_FALSE(win->maximized);
  workspace = R(0, 24, 200, 150);
  ASSERT_TRUE(maximize_window(win));
  ASSERT_TRUE(restore_window(win));
  ASSERT_EQUAL(win->frame.x, 0);
  ASSERT_EQUAL(win->frame.y, 24);
  ASSERT_EQUAL(win->frame.w, 200);
  ASSERT_EQUAL(win->frame.h, 150);
  destroy_window(win);
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("Window maximize/restore");
  test_maximize_restore();
  TEST_END();
}

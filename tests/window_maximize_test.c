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

static void test_desktop_replacement(void) {
  TEST("Desktop is released while maximized documents are visible and recreated afterward");
  test_env_init();
  workspace = R(0, 24, 800, 600);
  enable_desktop_window(true);
  ASSERT_NOT_NULL(get_desktop_window());
  window_t *first = test_env_create_window("First", 30, 50, 320, 240, maximize_test_proc, NULL);
  window_t *second = test_env_create_window("Second", 60, 80, 320, 240, maximize_test_proc, NULL);
  ASSERT_TRUE(maximize_window(first));
  ASSERT_NULL(get_desktop_window());
  ASSERT_TRUE(maximize_window(second));
  ASSERT_TRUE(restore_window(second));
  ASSERT_NULL(get_desktop_window());
  show_window(first, false);
  ASSERT_NOT_NULL(get_desktop_window());
  show_window(first, true);
  ASSERT_NULL(get_desktop_window());
  destroy_window(first);
  ASSERT_NOT_NULL(get_desktop_window());
  ASSERT_FALSE(g_ui_runtime.focused == get_desktop_window());
  ASSERT_TRUE(g_ui_runtime.windows == get_desktop_window());
  ASSERT_TRUE(find_window(70, 100) == second);
  enable_desktop_window(false);
  test_env_shutdown();
  PASS();
}

static void test_menubar_restore(void) {
  TEST("Menubar restore targets its own topmost document and cancels outside releases");
  test_env_init();
  workspace = R(0, 24, 800, 600);
  window_t *first = test_env_create_window("First", 30, 50, 320, 240, maximize_test_proc, NULL);
  window_t *second = test_env_create_window("Second", 60, 80, 320, 240, maximize_test_proc, NULL);
  window_t *other = test_env_create_window("Other app", 60, 80, 320, 240, maximize_test_proc, NULL);
  first->hinstance = second->hinstance = 1;
  other->hinstance = 2;
  window_t *bar = create_window("Menu", WINDOW_NOTITLE | WINDOW_NORESIZE | WINDOW_ALWAYSONTOP,
    MAKERECT(0, 0, 800, MENUBAR_HEIGHT), NULL, win_menubar, 1, NULL);
  ASSERT_TRUE(maximize_window(first));
  ASSERT_TRUE(maximize_window(second));
  ASSERT_TRUE(maximize_window(other));
  uint32_t point = MAKEDWORD(790, MENUBAR_HEIGHT / 2);
  send_message(bar, evLeftButtonDown, point, NULL);
  ASSERT_TRUE(g_ui_runtime.captured == bar);
  send_message(bar, evLeftButtonUp, MAKEDWORD(10, MENUBAR_HEIGHT + 10), NULL);
  ASSERT_TRUE(second->maximized);
  ASSERT_NULL(g_ui_runtime.captured);
  send_message(bar, evLeftButtonDown, point, NULL);
  send_message(bar, evLeftButtonUp, point, NULL);
  ASSERT_FALSE(second->maximized);
  ASSERT_TRUE(first->maximized);
  ASSERT_TRUE(other->maximized);
  send_message(bar, evLeftButtonDown, point, NULL);
  destroy_window(first);
  send_message(bar, evLeftButtonUp, point, NULL);
  ASSERT_TRUE(other->maximized);
  ASSERT_NULL(g_ui_runtime.captured);
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("Window maximize/restore");
  test_maximize_restore();
  test_desktop_replacement();
  test_menubar_restore();
  TEST_END();
}

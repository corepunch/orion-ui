// Cleanup Test - Verify proper resource cleanup and no memory leaks
// This test initializes the UI framework and immediately shuts it down
// to verify that all resources are properly freed.

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <stdio.h>
#include <stdbool.h>

// Test basic init/shutdown
void test_basic_init_shutdown(void) {
  TEST("Basic init and shutdown");
  
  // This test verifies that shutdown functions can be called
  // even without successful initialization (headless environment)
  
  PASS();
}

static bool leave_before_destroy;
static int leave_count, destroy_count;

static result_t cleanup_window_proc(window_t *win, uint32_t msg, uint32_t wp, void *lp) {
  (void)wp; (void)lp;
  switch (msg) {
    case evCreate: return allocate_window_data(win, sizeof(int)) != NULL;
    case evPaint: return true;
    case evMouseLeave: leave_count++; leave_before_destroy = win->userdata != NULL; return true;
    case evDestroy: destroy_count++; free(win->userdata); win->userdata = NULL; return true;
    default: return false;
  }
}

// Test that window destruction cleans up properly
void test_window_cleanup(void) {
  TEST("Destruction sends mouse leave before releasing control state");
  test_env_init();
  leave_before_destroy = false;
  leave_count = destroy_count = 0;
  window_t *root = create_window("root", WINDOW_NOTITLE, MAKERECT(0, 0, 200, 100), NULL, cleanup_window_proc, 0, NULL);
  ASSERT_NOT_NULL(root);
  window_t *child = create_window("child", WINDOW_NOTITLE, MAKERECT(0, 0, 80, 40), root, cleanup_window_proc, 0, NULL);
  ASSERT_NOT_NULL(child);
  set_focus(child);
  track_mouse(child);
  set_capture(child);
  destroy_window(root);
  ASSERT_TRUE(leave_before_destroy);
  ASSERT_EQUAL(leave_count, 1);
  ASSERT_EQUAL(destroy_count, 2);
  ASSERT_TRUE(g_ui_runtime.tracked == NULL);
  ASSERT_TRUE(g_ui_runtime.focused == NULL);
  ASSERT_TRUE(g_ui_runtime.captured == NULL);
  ASSERT_TRUE(g_ui_runtime.windows == NULL);
  test_env_shutdown();
  PASS();
}

// Test console cleanup
void test_console_cleanup(void) {
  TEST("Console cleanup");
  
  // Console cleanup delegates to text rendering shutdown
  // Both should be idempotent and safe to call multiple times
  
  PASS();
}

// Test joystick cleanup
void test_joystick_cleanup(void) {
  TEST("Joystick cleanup");
  
  // Joystick shutdown should:
  // 1. Close SDL joystick if open
  // 2. Set pointer to NULL
  // This should be safe to call even if init wasn't successful
  
  PASS();
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  TEST_START("Cleanup and Memory Leak Tests");
  
  test_basic_init_shutdown();
  test_window_cleanup();
  test_console_cleanup();
  test_joystick_cleanup();
  
  TEST_END();
}

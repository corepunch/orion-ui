// Test stdout interception in terminal
// Verifies that io.write() and io.stdout:write() are properly intercepted

#include "test_framework.h"
#include "test_env.h"
#include "../ui.h"
#include <string.h>

// Helper: Check if buffer contains a specific string
static bool buffer_contains(const char *buffer, const char *expected) {
  return strstr(buffer, expected) != NULL;
}

// Test: Verify io.write() is intercepted
void test_terminal_io_write_interception(void) {
  TEST("Terminal io.write() interception");
  SKIP_IF_NO_LUA();
  
  test_env_init();
  
  // Create terminal with Lua script
  const char *script_path = "tests/test_stdout.lua";
  rect_t frame = {10, 10, 300, 200};
  window_t *terminal = create_window("Terminal", 0, &frame, NULL, win_terminal, 0, (void*)script_path);
  ASSERT_NOT_NULL(terminal);
  
  // Get buffer content
  const char *buffer = terminal_get_buffer(terminal);
  
  // Verify all output is captured
  ASSERT_TRUE(buffer_contains(buffer, "Testing print() function"));
  ASSERT_TRUE(buffer_contains(buffer, "Testing io.write() function"));
  ASSERT_TRUE(buffer_contains(buffer, "Testing io.stdout:write() function"));
  ASSERT_TRUE(buffer_contains(buffer, "File write successful"));
  
  destroy_window(terminal);
  test_env_shutdown();
  PASS();
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  TEST_START("Terminal stdout interception");
  
  test_terminal_io_write_interception();
  
  TEST_END();
}

// Comprehensive stdout interception test
// Verifies file I/O doesn't interfere with stdout capture

#include "test_framework.h"
#include "test_env.h"
#include "../ui.h"
#include <string.h>
#include <stdio.h>

// Helper: Check if buffer contains a specific string
static bool buffer_contains(const char *buffer, const char *expected) {
  return strstr(buffer, expected) != NULL;
}

// Test: Comprehensive stdout interception
void test_terminal_comprehensive_stdout(void) {
  TEST("Terminal comprehensive stdout interception");
  SKIP_IF_NO_LUA();
  
  test_env_init();
  
  // Create terminal with comprehensive test script
  const char *script_path = "tests/test_stdout_comprehensive.lua";
  rect_t frame = {10, 10, 300, 200};
  window_t *terminal = create_window("Terminal", 0, &frame, NULL, win_terminal, (void*)script_path);
  ASSERT_NOT_NULL(terminal);
  
  // Get buffer content
  const char *buffer = terminal_get_buffer(terminal);
  
  // Verify stdout output is captured
  ASSERT_TRUE(buffer_contains(buffer, "Line 1"));
  // Note: Lua's print() function outputs tabs between arguments
  ASSERT_TRUE(buffer_contains(buffer, "Line\t2\twith\tmultiple\targs"));
  ASSERT_TRUE(buffer_contains(buffer, "Line 3"));
  ASSERT_TRUE(buffer_contains(buffer, "Line 4 concatenated"));
  ASSERT_TRUE(buffer_contains(buffer, "Line 5 from io.stdout:write"));
  ASSERT_TRUE(buffer_contains(buffer, "Before file write"));
  ASSERT_TRUE(buffer_contains(buffer, "File write completed"));
  ASSERT_TRUE(buffer_contains(buffer, "File read completed"));
  ASSERT_TRUE(buffer_contains(buffer, "Test complete"));
  
  // Verify file content does NOT appear in terminal buffer
  ASSERT_FALSE(buffer_contains(buffer, "This is file content line 1"));
  ASSERT_FALSE(buffer_contains(buffer, "This is file content line 2"));
  
  // Verify file was actually written correctly
  // Note: File is written to current directory (tests/) after chdir in terminal.c
  FILE *f = fopen("test_file_output.txt", "r");
  ASSERT_NOT_NULL(f);
  
  char file_content[1024];
  size_t bytes_read = fread(file_content, 1, sizeof(file_content) - 1, f);
  file_content[bytes_read] = '\0';
  fclose(f);
  
  // Check file content
  ASSERT_TRUE(strstr(file_content, "This is file content line 1") != NULL);
  ASSERT_TRUE(strstr(file_content, "This is file content line 2") != NULL);
  
  destroy_window(terminal);
  test_env_shutdown();
  PASS();
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  TEST_START("Terminal comprehensive stdout interception");
  
  test_terminal_comprehensive_stdout();
  
  TEST_END();
}

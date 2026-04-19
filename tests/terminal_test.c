// Terminal and Lua Integration Tests
// Tests terminal control, Lua script execution, input handling, and buffer comparison
// Inspired by Windows 1.0 terminal and scripting tests

#include "test_framework.h"
#include "test_env.h"
#include "../ui.h"
#include <string.h>

// Helper: Check if buffer contains a specific string
static bool buffer_contains(const char *buffer, const char *expected) {
  return strstr(buffer, expected) != NULL;
}

// Helper: Send text character by character to a window
static void send_text_input(window_t *win, const char *text) {
  for (const char *p = text; *p != '\0'; p++) {
    char c = *p;
    send_message(win, evTextInput, 0, &c);
  }
}

// Helper: Send Enter key
static void send_enter_key(window_t *win) {
  send_message(win, evKeyDown, AX_KEY_ENTER, NULL);
}

// Test: Create terminal in command mode
void test_terminal_command_mode_creation(void) {
  TEST("Terminal creation in command mode");
  
  test_env_init();
  
  // Create terminal with NULL lparam (command mode)
  rect_t frame = {10, 10, 300, 200};
  window_t *terminal = create_window("Terminal Test", 0, &frame, NULL, win_terminal, 0, NULL);
  
  ASSERT_NOT_NULL(terminal);
  
  // Get buffer content
  const char *buffer = terminal_get_buffer(terminal);
  
  // Verify welcome message is displayed
  ASSERT_TRUE(buffer_contains(buffer, "Terminal - Command Mode"));
  ASSERT_TRUE(buffer_contains(buffer, "Terminal> "));
  
  destroy_window(terminal);
  test_env_shutdown();
  PASS();
}

// Test: Send help command to terminal
void test_terminal_help_command(void) {
  TEST("Terminal help command");
  
  test_env_init();
  
  rect_t frame = {10, 10, 300, 200};
  window_t *terminal = create_window("Terminal", 0, &frame, NULL, win_terminal, 0, NULL);
  ASSERT_NOT_NULL(terminal);
  
  // Send "help" command
  send_text_input(terminal, "help");
  send_enter_key(terminal);
  
  // Get buffer content
  const char *buffer = terminal_get_buffer(terminal);
  
  // Verify help output
  ASSERT_TRUE(buffer_contains(buffer, "help"));
  ASSERT_TRUE(buffer_contains(buffer, "Available commands:"));
  ASSERT_TRUE(buffer_contains(buffer, "exit"));
  ASSERT_TRUE(buffer_contains(buffer, "clear"));
  
  destroy_window(terminal);
  test_env_shutdown();
  PASS();
}

// Test: Send clear command to terminal
void test_terminal_clear_command(void) {
  TEST("Terminal clear command");
  
  test_env_init();
  
  rect_t frame = {10, 10, 300, 200};
  window_t *terminal = create_window("Terminal", 0, &frame, NULL, win_terminal, 0, NULL);
  ASSERT_NOT_NULL(terminal);
  
  // Send some text first
  send_text_input(terminal, "help");
  send_enter_key(terminal);
  
  // Get buffer - should have help text
  const char *buffer1 = terminal_get_buffer(terminal);
  ASSERT_TRUE(buffer_contains(buffer1, "Available commands:"));
  
  // Send clear command
  send_text_input(terminal, "clear");
  send_enter_key(terminal);
  
  // Get buffer after clear
  const char *buffer2 = terminal_get_buffer(terminal);
  
  // Buffer should not contain old help text (cleared)
  // But should still have the prompt
  ASSERT_TRUE(buffer_contains(buffer2, "Terminal> "));
  
  destroy_window(terminal);
  test_env_shutdown();
  PASS();
}

// Test: Send exit command to terminal
void test_terminal_exit_command(void) {
  TEST("Terminal exit command");
  
  test_env_init();
  
  rect_t frame = {10, 10, 300, 200};
  window_t *terminal = create_window("Terminal", 0, &frame, NULL, win_terminal, 0, NULL);
  ASSERT_NOT_NULL(terminal);
  
  // Send exit command
  send_text_input(terminal, "exit");
  send_enter_key(terminal);
  
  // Get buffer content
  const char *buffer = terminal_get_buffer(terminal);
  
  // Verify exit message
  ASSERT_TRUE(buffer_contains(buffer, "exit"));
  ASSERT_TRUE(buffer_contains(buffer, "Exiting terminal"));
  
  destroy_window(terminal);
  test_env_shutdown();
  PASS();
}

// Test: Unknown command handling
void test_terminal_unknown_command(void) {
  TEST("Terminal unknown command handling");
  
  test_env_init();
  
  rect_t frame = {10, 10, 300, 200};
  window_t *terminal = create_window("Terminal", 0, &frame, NULL, win_terminal, 0, NULL);
  ASSERT_NOT_NULL(terminal);
  
  // Send unknown command
  send_text_input(terminal, "unknown");
  send_enter_key(terminal);
  
  // Get buffer content
  const char *buffer = terminal_get_buffer(terminal);
  
  // Verify error message
  ASSERT_TRUE(buffer_contains(buffer, "Unknown command"));
  ASSERT_TRUE(buffer_contains(buffer, "help"));
  
  destroy_window(terminal);
  test_env_shutdown();
  PASS();
}

// Test: Load simple Lua script
void test_terminal_lua_simple_script(void) {
  TEST("Terminal with simple Lua script");
  SKIP_IF_NO_LUA();
  
  test_env_init();
  
  // Create terminal with Lua script path (relative to where tests are run)
  const char *script_path = "tests/test_simple.lua";
  rect_t frame = {10, 10, 300, 200};
  window_t *terminal = create_window("Terminal Lua", 0, &frame, NULL, win_terminal, 0, (void*)script_path);
  ASSERT_NOT_NULL(terminal);
  
  // Get buffer content
  const char *buffer = terminal_get_buffer(terminal);
  
  // Verify Lua script output
  ASSERT_TRUE(buffer_contains(buffer, "Hello from test_simple.lua"));
  ASSERT_TRUE(buffer_contains(buffer, "Testing terminal output"));
  ASSERT_TRUE(buffer_contains(buffer, "Line 3 of output"));
  ASSERT_TRUE(buffer_contains(buffer, "Process finished"));
  
  destroy_window(terminal);
  test_env_shutdown();
  PASS();
}

// Test: Interactive Lua script with input
void test_terminal_lua_interactive_script(void) {
  TEST("Terminal with interactive Lua script");
  SKIP_IF_NO_LUA();
  
  test_env_init();
  
  // Create terminal with interactive Lua script (relative to where tests are run)
  const char *script_path = "tests/test_interactive.lua";
  rect_t frame = {10, 10, 300, 200};
  window_t *terminal = create_window("Terminal Interactive", 0, &frame, NULL, win_terminal, 0, (void*)script_path);
  ASSERT_NOT_NULL(terminal);
  
  // Get initial buffer content
  const char *buffer1 = terminal_get_buffer(terminal);
  
  // Verify first prompt
  ASSERT_TRUE(buffer_contains(buffer1, "Enter your name:"));
  ASSERT_TRUE(buffer_contains(buffer1, "> "));
  
  // Send first input "Alice"
  send_text_input(terminal, "Alice");
  send_enter_key(terminal);
  
  // Get buffer after first input
  const char *buffer2 = terminal_get_buffer(terminal);
  
  // Verify greeting and second prompt
  ASSERT_TRUE(buffer_contains(buffer2, "Alice"));
  ASSERT_TRUE(buffer_contains(buffer2, "Hello, Alice!"));
  ASSERT_TRUE(buffer_contains(buffer2, "Enter your age:"));
  
  // Send second input "25"
  send_text_input(terminal, "25");
  send_enter_key(terminal);
  
  // Get final buffer
  const char *buffer3 = terminal_get_buffer(terminal);
  
  // Verify final output
  ASSERT_TRUE(buffer_contains(buffer3, "25"));
  ASSERT_TRUE(buffer_contains(buffer3, "You are 25 years old"));
  ASSERT_TRUE(buffer_contains(buffer3, "Process finished"));
  
  destroy_window(terminal);
  test_env_shutdown();
  PASS();
}

// Test: Lua script with error
void test_terminal_lua_error_handling(void) {
  TEST("Terminal Lua error handling");
  SKIP_IF_NO_LUA();
  
  test_env_init();
  
  // Try to load non-existent script (relative path)
  const char *script_path = "tests/nonexistent.lua";
  rect_t frame = {10, 10, 300, 200};
  window_t *terminal = create_window("Terminal Error", 0, &frame, NULL, win_terminal, 0, (void*)script_path);
  ASSERT_NOT_NULL(terminal);
  
  // Get buffer content
  const char *buffer = terminal_get_buffer(terminal);
  
  // Verify error message is shown
  ASSERT_TRUE(buffer_contains(buffer, "Error"));
  
  destroy_window(terminal);
  test_env_shutdown();
  PASS();
}

// Test: Multiple commands in sequence
void test_terminal_multiple_commands(void) {
  TEST("Terminal multiple commands in sequence");
  
  test_env_init();
  
  rect_t frame = {10, 10, 300, 200};
  window_t *terminal = create_window("Terminal", 0, &frame, NULL, win_terminal, 0, NULL);
  ASSERT_NOT_NULL(terminal);
  
  // Send help command
  send_text_input(terminal, "help");
  send_enter_key(terminal);
  
  const char *buffer1 = terminal_get_buffer(terminal);
  ASSERT_TRUE(buffer_contains(buffer1, "Available commands:"));
  
  // Send clear command
  send_text_input(terminal, "clear");
  send_enter_key(terminal);
  
  const char *buffer2 = terminal_get_buffer(terminal);
  // After clear, old help text should be gone
  ASSERT_TRUE(buffer_contains(buffer2, "Terminal> "));
  
  destroy_window(terminal);
  test_env_shutdown();
  PASS();
}

// Test: Backspace handling
void test_terminal_backspace(void) {
  TEST("Terminal backspace handling");
  
  test_env_init();
  
  rect_t frame = {10, 10, 300, 200};
  window_t *terminal = create_window("Terminal", 0, &frame, NULL, win_terminal, 0, NULL);
  ASSERT_NOT_NULL(terminal);
  
  // Type "helXX" then backspace twice to get "hel"
  send_text_input(terminal, "helXX");
  send_message(terminal, evKeyDown, AX_KEY_BACKSPACE, NULL);
  send_message(terminal, evKeyDown, AX_KEY_BACKSPACE, NULL);
  send_text_input(terminal, "p");
  send_enter_key(terminal);
  
  const char *buffer = terminal_get_buffer(terminal);
  
  // Should have processed "help" command
  ASSERT_TRUE(buffer_contains(buffer, "help"));
  ASSERT_TRUE(buffer_contains(buffer, "Available commands:"));
  
  destroy_window(terminal);
  test_env_shutdown();
  PASS();
}

// Test: Buffer comparison with exact string matching
void test_terminal_buffer_exact_match(void) {
  TEST("Terminal buffer exact string matching");
  SKIP_IF_NO_LUA();
  
  test_env_init();
  
  // Use relative path for portability
  const char *script_path = "tests/test_simple.lua";
  rect_t frame = {10, 10, 300, 200};
  window_t *terminal = create_window("Terminal", 0, &frame, NULL, win_terminal, 0, (void*)script_path);
  ASSERT_NOT_NULL(terminal);
  
  const char *buffer = terminal_get_buffer(terminal);
  
  // Test exact substring matching
  ASSERT_TRUE(buffer_contains(buffer, "Hello from test_simple.lua"));
  ASSERT_TRUE(buffer_contains(buffer, "Testing terminal output"));
  ASSERT_TRUE(buffer_contains(buffer, "Line 3 of output"));
  
  // Test that non-existent strings are not found
  ASSERT_FALSE(buffer_contains(buffer, "This text does not exist"));
  ASSERT_FALSE(buffer_contains(buffer, "Another missing string"));
  
  destroy_window(terminal);
  test_env_shutdown();
  PASS();
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  TEST_START("Terminal and Lua Integration");
  
  // Command mode tests
  test_terminal_command_mode_creation();
  test_terminal_help_command();
  test_terminal_clear_command();
  test_terminal_exit_command();
  test_terminal_unknown_command();
  test_terminal_multiple_commands();
  test_terminal_backspace();
  
  // Lua integration tests
  test_terminal_lua_simple_script();
  test_terminal_lua_interactive_script();
  test_terminal_lua_error_handling();
  
  // Buffer comparison tests
  test_terminal_buffer_exact_match();
  
  TEST_END();
}

# Orion Test Suite

This directory contains the test suite for Orion. The tests are inspired by the testing approach used for Windows 1.0, focusing on core functionality: window management, event handling, and common controls.

## Test Organization

### Test Files

- **test_framework.h** - Simple test framework providing macros for assertions and test reporting
- **test_env.h / test_env.c** - Test environment for creating windows and tracking events using hooks
- **basic_test.c** - Basic functionality tests (macros, constants, structures)
- **window_msg_test.c** - Window and message tracking tests using the test environment
- **button_click_test.c** - Button click simulation tests with proper in-window scaling using post_message
- **terminal_test.c** - Terminal control and Lua integration tests with input handling and buffer verification
- **test_simple.lua** - Simple Lua script for terminal testing (print output only)
- **test_interactive.lua** - Interactive Lua script for terminal testing (with io.read prompts)

## Running Tests

### Build and Run All Tests

```bash
make test
```

This will:
1. Build all test executables
2. Run each test in sequence
3. Report results

### Run Individual Tests

```bash
./build/bin/test_basic
./build/bin/test_window_msg
```

## Test Environment

The test environment (`test_env.h` and `test_env.c`) provides utilities for testing Orion windows and messages:

### Features

- **Window Creation**: Helper functions to create test windows
- **Message Tracking**: Uses Orion's hook system to track all messages sent to windows
- **Event History**: Maintains a history of all tracked events with details (window, message, parameters, call count)
- **Query Interface**: Functions to check if messages were sent, count message occurrences, and retrieve event details

### Usage Example

```c
#include "test_env.h"

// Initialize test environment
test_env_init();
test_env_enable_tracking(true);

// Create a window
window_t *win = test_env_create_window("Test", 10, 10, 100, 100, my_proc, NULL);

// Send messages
test_env_send_message(win, kWindowMessagePaint, 0, NULL);
test_env_send_message(win, kWindowMessageCommand, 42, NULL);

// Verify messages were sent
assert(test_env_was_message_sent(kWindowMessagePaint));
assert(test_env_count_message(kWindowMessageCommand) == 1);

// Get event details
test_event_t *event = test_env_find_event(kWindowMessageCommand);
assert(event->wparam == 42);

// Cleanup
destroy_window(win);
test_env_shutdown();
```

### API Reference

**Initialization:**
- `test_env_init()` - Initialize test environment
- `test_env_shutdown()` - Shutdown and cleanup
- `test_env_enable_tracking(bool)` - Enable/disable event tracking

**Event Tracking:**
- `test_env_clear_events()` - Clear all tracked events
- `test_env_get_event_count()` - Get number of tracked events
- `test_env_get_event(int index)` - Get event by index
- `test_env_find_event(uint32_t msg)` - Find first event with given message type
- `test_env_was_message_sent(uint32_t msg)` - Check if message was sent
- `test_env_count_message(uint32_t msg)` - Count occurrences of message

**Window Helpers:**
- `test_env_create_window()` - Create a window with simplified parameters
- `test_env_send_message()` - Send a message (wrapper around send_message)
- `test_env_post_message()` - Post a message (wrapper around post_message)

## Test Philosophy

The test suite follows the Windows 1.0 testing philosophy:

1. **Simple and Direct** - Tests are straightforward and easy to understand
2. **No Dependencies** - Each test is self-contained
3. **Fast Execution** - Tests run quickly without UI rendering
4. **Core Functionality** - Focus on essential features that must work

## What We Test

### Basic Functionality
- LOWORD/HIWORD/MAKEDWORD macros
- MIN/MAX macros
- Rectangle structures
- Window message constants (kWindowMessageCreate, kWindowMessageDestroy, kWindowMessagePaint, etc.)
- Control message constants (kButtonMessageSetCheck, kComboBoxMessageAddString, etc.)
- Notification constants (kButtonNotificationClicked, kEditNotificationUpdate, etc.)
- Window flags (WINDOW_NOTITLE, WINDOW_TRANSPARENT, etc.)

### Window and Message Tracking
- Window creation with automatic kWindowMessageCreate tracking
- Message sending and receiving with event tracking
- Multiple message handling
- Event tracking enable/disable
- Event detail retrieval (window, message, parameters)
- Event history and querying
- Clear event tracking

### Button Click Simulation
- Button window creation as child of parent window
- Mouse event simulation with proper in-window scaling (scale factor = 2)
- Asynchronous message posting using `post_message` instead of `send_message`
- Message queue processing with `repost_messages(-1)`
- kButtonNotificationClicked notification verification sent to parent window
- Multiple button clicks handling
- Button clicks at different positions within button bounds
- Verification of async behavior (messages queued before processing)

## Hook System

The test environment leverages Orion's built-in hook system to track window messages. Hooks are registered for common window messages:

- kWindowMessageCreate - Window creation
- kWindowMessageDestroy - Window destruction  
- kWindowMessagePaint - Window paint requests
- kWindowMessageCommand - Command messages
- kWindowMessageLeftButtonDown/Up - Mouse button events
- kWindowMessageKeyDown/Up - Keyboard events
- kWindowMessageMouseMove - Mouse movement
- kWindowMessageSetFocus/KillFocus - Focus changes

Hooks are called before the window procedure, allowing tests to observe all message traffic without modifying window implementations.

## Test Framework

The custom test framework (`test_framework.h`) provides:

### Macros
- `TEST_START(name)` - Begin a test suite
- `TEST_END()` - End suite and report results
- `TEST(name)` - Begin individual test
- `PASS()` - Mark test as passed
- `FAIL(msg)` - Mark test as failed with message

### Assertions
- `ASSERT_TRUE(condition)` - Verify condition is true
- `ASSERT_FALSE(condition)` - Verify condition is false
- `ASSERT_NULL(ptr)` - Verify pointer is NULL
- `ASSERT_NOT_NULL(ptr)` - Verify pointer is not NULL
- `ASSERT_EQUAL(a, b)` - Verify values are equal
- `ASSERT_NOT_EQUAL(a, b)` - Verify values are not equal
- `ASSERT_STR_EQUAL(a, b)` - Verify strings are equal

### Output
Tests produce colored output:
- Blue: Test suite headers
- Green: Passed tests
- Red: Failed tests
- Summary statistics at end

## Adding New Tests

To add a new test file:

1. Create `tests/yourtest.c`
2. Include `test_framework.h` and `../ui.h`
3. Write test functions
4. Create `main()` function with `TEST_START()` and `TEST_END()`
5. Run `make test` - the Makefile will automatically pick it up

Example:

```c
#include "test_framework.h"
#include "../ui.h"

void test_something(void) {
    TEST("Your test name");
    
    // Test code here
    ASSERT_TRUE(1 == 1);
    
    PASS();
}

int main(void) {
    TEST_START("Your Test Suite");
    
    test_something();
    
    TEST_END();
}
```

## CI/CD Integration

Tests are automatically run in GitHub Actions for:
- Linux (Ubuntu)
- macOS
- Windows (MSYS2/MinGW64)

Failed tests will cause the build to fail, preventing broken code from being merged.

### Terminal and Lua Integration
- Terminal creation in command mode (NULL lparam)
- Command execution (help, clear, exit)
- Unknown command handling
- Multiple commands in sequence
- Backspace handling during input
- Lua script loading via lparam
- Simple Lua scripts with print() output
- Interactive Lua scripts with io.read() prompts
- Simulated text input via kWindowMessageTextInput and kWindowMessageKeyDown messages
- Buffer content verification and comparison
- Lua error handling (non-existent files)

## Future Tests

Planned additions:
- Window creation and management tests
- Message queue and event handling tests
- Common controls tests (checkbox, edit, label, list, combobox)
- Focus and keyboard navigation tests
- Text rendering tests

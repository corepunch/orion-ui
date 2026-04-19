#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../ui.h"

// Simple test framework
#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s\n", msg); \
        return 1; \
    } \
} while(0)

#define TEST_PASS(msg) printf("PASS: %s\n", msg)

result_t statusbar_test_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
    switch (msg) {
        case kWindowMessageCreate:
            return true;
        case kWindowMessageDestroy:
            g_ui_runtime.running = false;
            return true;
        default:
            return false;
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
#ifdef _WIN32
    // Skip this test on Windows in CI environments as it requires a display
    printf("Status Bar Feature Tests\n");
    printf("=========================\n\n");
    printf("SKIP: Skipping on Windows (requires display in CI environment)\n");
    return 0;
#endif
    
    printf("Status Bar Feature Tests\n");
    printf("=========================\n\n");
    
    // Test 1: WINDOW_STATUSBAR flag is defined
    TEST_ASSERT(WINDOW_STATUSBAR == (1 << 12), "WINDOW_STATUSBAR flag has correct value");
    TEST_PASS("WINDOW_STATUSBAR flag is defined correctly");
    
    // Test 2: kWindowMessageStatusBar message is defined
    TEST_ASSERT(kWindowMessageStatusBar > 0, "kWindowMessageStatusBar message is defined");
    TEST_PASS("kWindowMessageStatusBar message is defined");
    
    // Test 3: STATUSBAR_HEIGHT constant is defined
    TEST_ASSERT(STATUSBAR_HEIGHT == 12, "STATUSBAR_HEIGHT has correct value");
    TEST_PASS("STATUSBAR_HEIGHT constant is correct");
    
    // Test 4: kColorStatusbarBg default value is correct
    TEST_ASSERT(get_sys_color(kColorStatusbarBg) == 0xff2c2c2c, "kColorStatusbarBg has correct default value");
    TEST_PASS("kColorStatusbarBg default is defined correctly");
    
    // Test 5: Initialize graphics (headless mode)
    if (!ui_init_graphics(UI_INIT_DESKTOP|UI_INIT_TRAY, "StatusBar Test", 320, 240)) {
        printf("SKIP: Graphics initialization (requires display)\n");
    } else {
        printf("Graphics initialized\n");
        
        // Test 6: Create window with WINDOW_STATUSBAR flag
        window_t *win = create_window(
            "Test Window",
            WINDOW_STATUSBAR,
            MAKERECT(10, 10, 200, 100),
            NULL,
            statusbar_test_proc,
            0,
            NULL
        );
        TEST_ASSERT(win != NULL, "Window with WINDOW_STATUSBAR created");
        TEST_ASSERT(win->flags & WINDOW_STATUSBAR, "WINDOW_STATUSBAR flag is set");
        TEST_PASS("Window with status bar flag created successfully");
        
        // Test 7: statusbar_text field exists and is initialized
        TEST_ASSERT(strlen(win->statusbar_text) == 0, "statusbar_text is initially empty");
        TEST_PASS("statusbar_text field is initialized correctly");
        
        // Test 8: Send kWindowMessageStatusBar message
        const char *test_text = "Test Status";
        send_message(win, kWindowMessageStatusBar, 0, (void*)test_text);
        TEST_ASSERT(strcmp(win->statusbar_text, test_text) == 0, "kWindowMessageStatusBar updates statusbar_text");
        TEST_PASS("kWindowMessageStatusBar message handler works correctly");
        
        // Test 9: statusbar_text truncation
        char long_text[100];
        memset(long_text, 'X', sizeof(long_text) - 1);
        long_text[sizeof(long_text) - 1] = '\0';
        send_message(win, kWindowMessageStatusBar, 0, (void*)long_text);
        TEST_ASSERT(strlen(win->statusbar_text) < sizeof(long_text), "statusbar_text is properly truncated");
        TEST_ASSERT(strlen(win->statusbar_text) < 64, "statusbar_text respects buffer size");
        TEST_PASS("Long status text is properly truncated");
        
        destroy_window(win);
        ui_shutdown_graphics();
    }
    
    printf("\nAll status bar tests passed!\n");
    return 0;
}

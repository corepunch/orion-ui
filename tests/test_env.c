// Test environment implementation
// Provides utilities to create windows, send messages, and track events

#include "test_env.h"
#include <string.h>
#include <stdlib.h>

static void test_env_reset_ui_state(void) {
    while (g_ui_runtime.windows) {
        destroy_window(g_ui_runtime.windows);
    }
    cleanup_all_hooks();
    reset_message_queue();
    g_ui_runtime.focused = NULL;
    g_ui_runtime.tracked = NULL;
    g_ui_runtime.captured = NULL;
    g_ui_runtime.dragging = NULL;
    g_ui_runtime.resizing = NULL;
    g_ui_runtime.toolbar_down_win = NULL;
    g_ui_runtime.running = false;
}

// Global test environment
test_env_t test_env = {0};

// Initialize test environment
void test_env_init(void) {
    test_env_reset_ui_state();
    memset(&test_env, 0, sizeof(test_env_t));
    test_env.tracking_enabled = false;
    test_env.event_count = 0;
}

// Shutdown test environment
void test_env_shutdown(void) {
    test_env_clear_events();
    test_env.tracking_enabled = false;
    test_env_reset_ui_state();
}

// Enable/disable event tracking
void test_env_enable_tracking(bool enable) {
    test_env.tracking_enabled = enable;
    if (enable) {
        // Register hooks for all common messages
        register_window_hook(evCreate, test_env_hook_callback, NULL);
        register_window_hook(evDestroy, test_env_hook_callback, NULL);
        register_window_hook(evPaint, test_env_hook_callback, NULL);
        register_window_hook(evCommand, test_env_hook_callback, NULL);
        register_window_hook(evLeftButtonDown, test_env_hook_callback, NULL);
        register_window_hook(evLeftButtonUp, test_env_hook_callback, NULL);
        register_window_hook(evKeyDown, test_env_hook_callback, NULL);
        register_window_hook(evKeyUp, test_env_hook_callback, NULL);
        register_window_hook(evMouseMove, test_env_hook_callback, NULL);
        register_window_hook(evSetFocus, test_env_hook_callback, NULL);
        register_window_hook(evKillFocus, test_env_hook_callback, NULL);
    }
}

// Clear tracked events
void test_env_clear_events(void) {
    test_env.event_count = 0;
    memset(test_env.events, 0, sizeof(test_env.events));
}

// Get number of tracked events
int test_env_get_event_count(void) {
    return test_env.event_count;
}

// Get specific tracked event
test_event_t* test_env_get_event(int index) {
    if (index < 0 || index >= test_env.event_count) {
        return NULL;
    }
    return &test_env.events[index];
}

// Find event by message type
test_event_t* test_env_find_event(uint32_t msg) {
    for (int i = 0; i < test_env.event_count; i++) {
        if (test_env.events[i].msg == msg) {
            return &test_env.events[i];
        }
    }
    return NULL;
}

// Check if a message was sent
bool test_env_was_message_sent(uint32_t msg) {
    return test_env_find_event(msg) != NULL;
}

// Count how many times a message was sent
int test_env_count_message(uint32_t msg) {
    int count = 0;
    for (int i = 0; i < test_env.event_count; i++) {
        if (test_env.events[i].msg == msg) {
            count++;
        }
    }
    return count;
}

// Hook callback for tracking events
void test_env_hook_callback(window_t *win, uint32_t msg, uint32_t wparam, 
                            void *lparam, void *userdata) {
    (void)userdata; // Not used
    
    if (!test_env.tracking_enabled) {
        return;
    }
    
    if (test_env.event_count >= MAX_TRACKED_EVENTS) {
        return; // Buffer full
    }
    
    // Check if we already tracked this exact event
    for (int i = 0; i < test_env.event_count; i++) {
        if (test_env.events[i].window == win && 
            test_env.events[i].msg == msg &&
            test_env.events[i].wparam == wparam) {
            test_env.events[i].call_count++;
            return;
        }
    }
    
    // Add new event
    test_event_t *event = &test_env.events[test_env.event_count++];
    event->window = win;
    event->msg = msg;
    event->wparam = wparam;
    event->lparam = lparam;
    event->call_count = 1;
}

// Helper: Create a test window with tracking
window_t* test_env_create_window(const char *title, int x, int y, int w, int h,
                                  winproc_t proc, void *userdata) {
    rect_t frame = {x, y, w, h};
    return create_window(title, 0, &frame, NULL, proc, 0, userdata);
}

// Helper: Send a tracked message
int test_env_send_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
    return send_message(win, msg, wparam, lparam);
}

// Helper: Post a tracked message
void test_env_post_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
    post_message(win, msg, wparam, lparam);
}

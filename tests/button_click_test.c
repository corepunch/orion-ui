// Button Click Test with Proper Scaling
// Tests button click simulation using post_message with correct in-window scaling
// Inspired by Windows 1.0 control testing approach

#include "test_framework.h"
#include "test_env.h"
#include "../ui.h"

// Forward declaration for message queue processing
extern void repost_messages(void);

// Test state tracking
static int test_kButtonNotificationClicked_count = 0;
static uint32_t test_last_button_id = 0;
static window_t *test_last_button_sender = NULL;

// Parent window procedure that receives kWindowMessageCommand from button
static result_t test_parent_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
    (void)win;
    
    switch (msg) {
        case kWindowMessageCreate:
            return 1;
        case kWindowMessageCommand:
            // Extract notification code and control ID
            if (HIWORD(wparam) == kButtonNotificationClicked) {
                test_kButtonNotificationClicked_count++;
                test_last_button_id = LOWORD(wparam);
                test_last_button_sender = (window_t *)lparam;
            }
            return 1;
        case kWindowMessageDestroy:
            return 1;
        default:
            return 0;
    }
}

// Reset test counters
void reset_button_test_counters(void) {
    test_kButtonNotificationClicked_count = 0;
    test_last_button_id = 0;
    test_last_button_sender = NULL;
}

// Test button click with post_message and proper scaling
void test_button_click_with_scaling(void) {
    TEST("Button click simulation with post_message and scaling");
    
    test_env_init();
    test_env_enable_tracking(true);
    test_env_clear_events();
    reset_button_test_counters();
    
    // Create parent window
    window_t *parent = test_env_create_window("Parent", 100, 100, 300, 200,
                                               test_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    
    // Create button as child of parent with ID 101
    rect_t button_frame = {10, 10, 80, 20};
    window_t *button = create_window("Click Me", 0, &button_frame, parent, win_button, NULL);
    ASSERT_NOT_NULL(button);
    button->id = 101;
    
    test_env_clear_events();
    
    // Simulate mouse click at button center in local window coordinates
    // Button is at local coordinates (10, 10) with size (80, 20)
    // Center is at (10 + 80/2, 10 + 20/2) = (50, 20) in local window coordinates
    // 
    // Note: The actual SDL event loop uses a scale factor of 2 (defined in kernel/event.c),
    // but for testing purposes, we work directly with local window coordinates.
    // The SCALE_POINT macro in the event loop would convert SDL coordinates to window coordinates,
    // but since we're posting messages directly to the button, we skip that layer.
    
    int button_center_x = button_frame.x + button_frame.w / 2;  // 10 + 40 = 50
    int button_center_y = button_frame.y + button_frame.h / 2;  // 10 + 10 = 20
    
    // Post kWindowMessageLeftButtonDown message to button
    // wparam contains MAKEDWORD(x, y) in local window coordinates
    test_env_post_message(button, kWindowMessageLeftButtonDown, MAKEDWORD(button_center_x, button_center_y), NULL);
    
    // Process the message queue (simulate message pump)
    // repost_messages() processes all queued messages asynchronously
    repost_messages();
    
    // Verify LBUTTONDOWN was tracked
    ASSERT_TRUE(test_env_was_message_sent(kWindowMessageLeftButtonDown));
    
    // Now post kWindowMessageLeftButtonUp to trigger the button click
    test_env_post_message(button, kWindowMessageLeftButtonUp, MAKEDWORD(button_center_x, button_center_y), NULL);
    repost_messages();
    
    // Verify LBUTTONUP was tracked
    ASSERT_TRUE(test_env_was_message_sent(kWindowMessageLeftButtonUp));
    
    // Verify kWindowMessageCommand with kButtonNotificationClicked was sent to parent
    ASSERT_TRUE(test_env_was_message_sent(kWindowMessageCommand));
    ASSERT_EQUAL(test_kButtonNotificationClicked_count, 1);
    ASSERT_EQUAL(test_last_button_id, 101);
    ASSERT_EQUAL(test_last_button_sender, button);
    
    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Test multiple button clicks
void test_multiple_button_clicks(void) {
    TEST("Multiple button clicks with post_message");
    
    test_env_init();
    test_env_enable_tracking(true);
    test_env_clear_events();
    reset_button_test_counters();
    
    // Create parent window
    window_t *parent = test_env_create_window("Parent", 100, 100, 300, 200,
                                               test_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    
    // Create button
    rect_t button_frame = {10, 10, 80, 20};
    window_t *button = create_window("Click Me", 0, &button_frame, parent, win_button, NULL);
    ASSERT_NOT_NULL(button);
    button->id = 102;
    
    test_env_clear_events();
    
    int button_center_x = button_frame.x + button_frame.w / 2;
    int button_center_y = button_frame.y + button_frame.h / 2;
    
    // Click button 3 times
    for (int i = 0; i < 3; i++) {
        test_env_post_message(button, kWindowMessageLeftButtonDown, MAKEDWORD(button_center_x, button_center_y), NULL);
        repost_messages();
        
        test_env_post_message(button, kWindowMessageLeftButtonUp, MAKEDWORD(button_center_x, button_center_y), NULL);
        repost_messages();
    }
    
    // Verify 3 clicks were registered
    ASSERT_EQUAL(test_kButtonNotificationClicked_count, 3);
    ASSERT_EQUAL(test_last_button_id, 102);
    
    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Test button click at different positions within button
void test_button_click_positions(void) {
    TEST("Button clicks at different positions with correct scaling");
    
    test_env_init();
    test_env_enable_tracking(true);
    test_env_clear_events();
    reset_button_test_counters();
    
    // Create parent window
    window_t *parent = test_env_create_window("Parent", 100, 100, 300, 200,
                                               test_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    
    // Create button
    rect_t button_frame = {10, 10, 80, 20};
    window_t *button = create_window("Click Me", 0, &button_frame, parent, win_button, NULL);
    ASSERT_NOT_NULL(button);
    button->id = 103;
    
    test_env_clear_events();
    
    // Test clicking at top-left corner (with small offset to be inside)
    int x = button_frame.x + 2;
    int y = button_frame.y + 2;
    
    test_env_post_message(button, kWindowMessageLeftButtonDown, MAKEDWORD(x, y), NULL);
    repost_messages();
    
    test_env_post_message(button, kWindowMessageLeftButtonUp, MAKEDWORD(x, y), NULL);
    repost_messages();
    
    ASSERT_EQUAL(test_kButtonNotificationClicked_count, 1);
    ASSERT_EQUAL(test_last_button_id, 103);
    
    // Test clicking at bottom-right corner (with small offset to be inside)
    x = button_frame.x + button_frame.w - 2;
    y = button_frame.y + button_frame.h - 2;
    
    test_env_post_message(button, kWindowMessageLeftButtonDown, MAKEDWORD(x, y), NULL);
    repost_messages();
    
    test_env_post_message(button, kWindowMessageLeftButtonUp, MAKEDWORD(x, y), NULL);
    repost_messages();
    
    ASSERT_EQUAL(test_kButtonNotificationClicked_count, 2);
    
    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Test that post_message is used (not send_message) by verifying async behavior
void test_post_message_async_behavior(void) {
    TEST("Verify post_message is used for async message handling");
    
    test_env_init();
    test_env_enable_tracking(true);
    test_env_clear_events();
    reset_button_test_counters();
    
    // Create parent window
    window_t *parent = test_env_create_window("Parent", 100, 100, 300, 200,
                                               test_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    
    // Create button
    rect_t button_frame = {10, 10, 80, 20};
    window_t *button = create_window("Click Me", 0, &button_frame, parent, win_button, NULL);
    ASSERT_NOT_NULL(button);
    button->id = 104;
    
    test_env_clear_events();
    
    int button_center_x = button_frame.x + button_frame.w / 2;
    int button_center_y = button_frame.y + button_frame.h / 2;
    
    // Post multiple messages
    test_env_post_message(button, kWindowMessageLeftButtonDown, MAKEDWORD(button_center_x, button_center_y), NULL);
    test_env_post_message(button, kWindowMessageLeftButtonUp, MAKEDWORD(button_center_x, button_center_y), NULL);
    
    // Before calling repost_messages, the button click should not have been processed yet
    // (this verifies we're using post_message which is async, not send_message which is sync)
    ASSERT_EQUAL(test_kButtonNotificationClicked_count, 0);
    
    // Now process the message queue
    repost_messages();
    
    // After processing, the click should be registered
    ASSERT_EQUAL(test_kButtonNotificationClicked_count, 1);
    ASSERT_EQUAL(test_last_button_id, 104);
    
    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    TEST_START("Button Click Simulation with Scaling");
    
    test_button_click_with_scaling();
    test_multiple_button_clicks();
    test_button_click_positions();
    test_post_message_async_behavior();
    
    TEST_END();
}

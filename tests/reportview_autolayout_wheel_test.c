// tests/reportview_autolayout_wheel_test.c — test wheel scrolling in auto-layout containers
// Reproduces filter gallery setup: reportview inside grid layout container

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/commctl/commctl.h>

static result_t reportview_parent_proc(window_t *win, uint32_t msg,
                                       uint32_t wparam, void *lparam) {
    if (msg == evCreate || msg == evDestroy) return true;
    if (msg == evPaint) {
        // Paint children
        for (window_t *child = win->children; child; child = child->next) {
            send_message(child, evPaint, 0, NULL);
        }
        return true;
    }
    (void)win;
    (void)wparam;
    (void)lparam;
    return false;
}

// Test 1: Reportview in plain parent window (control case - should work)
void test_reportview_wheel_plain_parent(void) {
    TEST("reportview wheel scrolling in plain parent");
    
    test_env_init();
    
    // Create parent window
    window_t *parent = create_window("parent", WINDOW_NOTITLE | WINDOW_NOFILL,
                                     MAKERECT(0, 0, 300, 400), NULL,
                                     reportview_parent_proc, 0, NULL);
    ASSERT(parent != NULL, "Failed to create parent");
    
    // Create reportview child
    window_t *rv = create_window("rv", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL,
                                 MAKERECT(10, 10, 280, 380), parent,
                                 win_reportview, 0, NULL);
    ASSERT(rv != NULL, "Failed to create reportview");
    
    // Add enough items to require scrolling
    for (int i = 0; i < 50; i++) {
        char name[32];
        snprintf(name, sizeof(name), "Item %d", i);
        reportview_item_t item = {
            .text = name,
            .icon = 0,
            .color = 0xffffffff,
            .userdata = (uint32_t)i,
        };
        send_message(rv, RVM_ADDITEM, 0, &item);
    }
    
    // Trigger resize to sync scrollbars
    send_message(rv, evResize, 0, NULL);
    
    ASSERT(rv->vscroll.visible, "Scrollbar should be visible with 50 items");
    
    // Send wheel event: dy=-3 so user/message.c negation yields +3 (scroll down).
    uint32_t wheel_wparam = MAKEDWORD(0, 0);  // mouse pos (unused in test)
    void *wheel_lparam = (void*)(intptr_t)MAKEDWORD(0, (uint16_t)-3);  // raw dy=-3; negation makes +3 (scroll down)
    int old_scroll = rv->vscroll.pos;
    send_message(rv, evWheel, wheel_wparam, wheel_lparam);
    
    ASSERT((int)rv->vscroll.pos > old_scroll, "Scroll position should have moved down");
    
    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Test 2: Reportview in auto-layout container (stack)
void test_reportview_wheel_in_stack(void) {
    TEST("reportview wheel scrolling in stack layout container");
    
    test_env_init();
    
    // Create stack container with auto-layout
    window_t *stack = create_window("stack", WINDOW_NOTITLE | WINDOW_NOFILL,
                                    MAKERECT(0, 0, 300, 400), NULL,
                                    reportview_parent_proc, 0, NULL);
    ASSERT(stack != NULL, "Failed to create stack");
    
    stack->flags |= WINDOW_AUTO_LAYOUT;
    stack->flags &= ~WINDOW_STACK_HORIZONTAL;
    stack->layout.layout_spacing = 8;
    
    // Create reportview child (will be arranged by layout)
    window_t *rv = create_window("rv", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_FLEXSPACE | WINDOW_VSCROLL,
                                 MAKERECT(0, 0, 280, 380), stack,
                                 win_reportview, 0, NULL);
    ASSERT(rv != NULL, "Failed to create reportview");
    
    // Add enough items to require scrolling
    for (int i = 0; i < 50; i++) {
        char name[32];
        snprintf(name, sizeof(name), "Item %d", i);
        reportview_item_t item = {
            .text = name,
            .icon = 0,
            .color = 0xffffffff,
            .userdata = (uint32_t)i,
        };
        send_message(rv, RVM_ADDITEM, 0, &item);
    }
    
    // Trigger layout (this should send evArrange to reportview)
    window_layout_sync(stack);
    
    ASSERT(rv->vscroll.visible, "Scrollbar should be visible after layout");
    
    // Send wheel event: dy=-3 so user/message.c negation yields +3 (scroll down).
    uint32_t wheel_wparam = MAKEDWORD(0, 0);  // mouse pos (unused in test)
    void *wheel_lparam = (void*)(intptr_t)MAKEDWORD(0, (uint16_t)-3);  // raw dy=-3; negation makes +3 (scroll down)
    int old_scroll = rv->vscroll.pos;
    send_message(rv, evWheel, wheel_wparam, wheel_lparam);
    
    ASSERT((int)rv->vscroll.pos > old_scroll, "Scroll position should have moved down");
    
    destroy_window(stack);
    test_env_shutdown();
    PASS();
}

// Test 3: Reportview in grid layout (like filter gallery)
void test_reportview_wheel_in_grid(void) {
    TEST("reportview wheel scrolling in grid layout container");
    
    test_env_init();
    
    // Create grid container with auto-layout
    window_t *grid = create_window("grid", WINDOW_NOTITLE | WINDOW_NOFILL,
                                   MAKERECT(0, 0, 400, 300), NULL,
                                   reportview_parent_proc, 0, NULL);
    ASSERT(grid != NULL, "Failed to create grid");
    
    grid->flags |= WINDOW_AUTO_LAYOUT;
    grid->layout.layout_spacing = 12;
    
    // Create two columns: preview + reportview (simulating filter gallery)
    window_t *col1 = create_window("col1", WINDOW_NOTITLE | WINDOW_NOFILL,
                                   MAKERECT(0, 0, 180, 300), grid,
                                   "column", 0, NULL);
    col1->flags |= WINDOW_AUTO_LAYOUT;
    
    window_t *col2 = create_window("col2", WINDOW_NOTITLE | WINDOW_NOFILL,
                                   MAKERECT(0, 0, 180, 300), grid,
                                   "column", 0, NULL);
    col2->flags |= WINDOW_AUTO_LAYOUT;
    
    // Create reportview in second column
    window_t *rv = create_window("rv", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_FLEXSPACE | WINDOW_VSCROLL,
                                 MAKERECT(0, 0, 180, 300), col2,
                                 win_reportview, 0, NULL);
    ASSERT(rv != NULL, "Failed to create reportview");
    
    // Add enough items to require scrolling
    for (int i = 0; i < 50; i++) {
        char name[32];
        snprintf(name, sizeof(name), "Filter %d", i);
        reportview_item_t item = {
            .text = name,
            .icon = 0,
            .color = 0xffffffff,
            .userdata = (uint32_t)i,
        };
        send_message(rv, RVM_ADDITEM, 0, &item);
    }
    
    // Trigger layout (this should send evArrange through the hierarchy)
    window_layout_sync(grid);
    
    ASSERT(rv->vscroll.visible, "Scrollbar should be visible after layout");
    
    // Send wheel event: dy=-3 so user/message.c negation yields +3 (scroll down).
    uint32_t wheel_wparam3 = MAKEDWORD(0, 0);  // mouse pos (unused in test)
    void *wheel_lparam3 = (void*)(intptr_t)MAKEDWORD(0, (uint16_t)-3);  // raw dy=-3; negation makes +3 (scroll down)
    int old_scroll3 = rv->vscroll.pos;
    send_message(rv, evWheel, wheel_wparam3, wheel_lparam3);
    
    ASSERT((int)rv->vscroll.pos > old_scroll3, "Scroll position should have moved down");
    test_env_shutdown();
    PASS();
}

int main(void) {
    TEST_START("Reportview Auto-Layout Wheel Scrolling Tests");
    
    test_reportview_wheel_plain_parent();
    test_reportview_wheel_in_stack();
    test_reportview_wheel_in_grid();
    
    TEST_END();
}

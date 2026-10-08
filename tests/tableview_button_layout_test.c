// Test: Verify tableview + button row layout - only tableview should stretch
//
// Tests form-based window creation with vertical stack layout where
// tableview (WINDOW_FLEXSPACE) expands while button row stays fixed height.

#include <orion/ui.h>
#include "test_framework.h"
#include "test_env.h"

// Simple window proc for testing
static result_t test_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
    (void)win; (void)wparam; (void)lparam;
    if (msg == evCreate) return 1;
    return 0;
}

// Test: Verify tableview expands, button row stays fixed
void test_tableview_button_layout(void) {
    TEST("Tableview expands vertically, button row fixed height (form-based)");
    
    // Define action button children (horizontal stack)
    static const form_ctrl_def_t action_buttons[] = {
        {.class_name = "Button", .id = 1, .size = {60, 19}, .flags = 0, .text = "Button 1"},
        {.class_name = "Button", .id = 2, .size = {60, 19}, .flags = 0, .text = "Button 2"},
        {.class_name = "Space",  .id = 3, .size = {0, 0},   .flags = WINDOW_FLEXSPACE},
    };
    
    // Define main form structure (vertical stack)
    static const form_ctrl_def_t main_children[] = {
        // Tableview with WINDOW_FLEXSPACE - should expand
        {.class_name = "ReportView", .id = 100, .size = {0, 0},
         .flags = WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL | WINDOW_FLEXSPACE},
        
        // Separator
        {.class_name = "Separator", .id = 101, .size = {0, 1}, .flags = 0},
        
        // Actions stack (horizontal) - no WINDOW_FLEXSPACE, should stay fixed
        {.class_name = "StackView", .id = 102, .size = {0, 0},
         .flags = WINDOW_STACK_HORIZONTAL, .layout_spacing = 4,
         .children = action_buttons, .child_count = 3},
    };
    
    // Create main form
    static const form_def_t main_form = {
        .name = "Test Layout",
        .width = 400,
        .height = 300,
        .flags = WINDOW_AUTO_LAYOUT,
        .children = main_children,
        .child_count = 3,
        .layout_spacing = 4,
    };
    
    // Create window from form
    window_t *main = create_window_from_form(&main_form, 100, 100, NULL, test_proc, 0, NULL);
    ASSERT_NOT_NULL(main);
    
    // Find children by ID
    window_t *tableview = get_window_item(main, 100);
    window_t *separator = get_window_item(main, 101);
    window_t *actions = get_window_item(main, 102);
    window_t *btn1 = get_window_item(main, 1);
    window_t *btn2 = get_window_item(main, 2);
    window_t *space = get_window_item(main, 3);
    
    ASSERT_NOT_NULL(tableview);
    ASSERT_NOT_NULL(separator);
    ASSERT_NOT_NULL(actions);
    ASSERT_NOT_NULL(btn1);
    ASSERT_NOT_NULL(btn2);
    ASSERT_NOT_NULL(space);
    
    // Get client rect
    irect16_t cr = get_client_rect(main);
    
    // Verify tableview has WINDOW_FLEXSPACE
    ASSERT((tableview->flags & WINDOW_FLEXSPACE) != 0, "tableview should have WINDOW_FLEXSPACE");
    
    // Verify actions stack does NOT have WINDOW_FLEXSPACE
    ASSERT((actions->flags & WINDOW_FLEXSPACE) == 0, "actions stack should NOT have WINDOW_FLEXSPACE");
    
    // Print layout info for debugging
    printf("\n    Main client rect: y=%d h=%d\n", cr.y, cr.h);
    printf("    Tableview: y=%d h=%d flags=0x%llx (should expand)\n",
           tableview->frame.y, tableview->frame.h, (unsigned long long)tableview->flags);
    printf("    Separator: y=%d h=%d\n", separator->frame.y, separator->frame.h);
    printf("    Actions:   y=%d h=%d flags=0x%llx (should be ~19-25px)\n",
           actions->frame.y, actions->frame.h, (unsigned long long)actions->flags);
    printf("    Button1:   y=%d h=%d\n", btn1->frame.y, btn1->frame.h);
    printf("    Button2:   y=%d h=%d\n", btn2->frame.y, btn2->frame.h);
    printf("    Space:     y=%d h=%d flags=0x%llx\n", space->frame.y, space->frame.h, (unsigned long long)space->flags);
    
    // Verify tableview takes significant vertical space (at least 70% of client height)
    ASSERT(tableview->frame.h >= cr.h * 0.7,
           "tableview should take at least 70%% of vertical space");
    
    // Verify actions stack is at fixed height (not expanded)
    ASSERT(actions->frame.h >= 19, "actions height should be at least button height");
    ASSERT(actions->frame.h <= 35, "actions should not expand beyond button height + padding");
    
    // Verify no overlap - elements should be arranged top to bottom
    ASSERT(separator->frame.y >= tableview->frame.y + tableview->frame.h,
           "separator should be below tableview");
    ASSERT(actions->frame.y >= separator->frame.y + separator->frame.h,
           "actions should be below separator");
    
    // Cleanup
    destroy_window(main);
    
    PASS();
}

int main(void) {
    TEST_START("tableview button layout");
    
    test_env_init();
    
    test_tableview_button_layout();
    
    test_env_shutdown();
    
    TEST_END();
}

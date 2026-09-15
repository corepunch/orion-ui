// combobox_test.c — Unit tests for commctl/combobox.c (win_combobox).
//
// Covers: cbAddString, cbGetCurrentSelection / cbSetCurrentSelection,
// cbGetListBoxText, cbClear, and Up/Down arrow keyboard navigation.

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/commctl/commctl.h>

// ── notification capture ──────────────────────────────────────────────────

static int  g_sel_change_count = 0;
static int  g_last_sel_id      = -1;

static result_t cb_parent_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
    (void)win; (void)lparam;
    if (msg == evCreate || msg == evDestroy) return 1;
    if (msg == evCommand && HIWORD(wparam) == cbSelectionChange) {
        g_sel_change_count++;
        g_last_sel_id = (int)LOWORD(wparam);
    }
    return 0;
}

static void reset_state(void) {
    g_sel_change_count = 0;
    g_last_sel_id      = -1;
}

// ── helpers ───────────────────────────────────────────────────────────────

static window_t *make_combobox(window_t *parent, int id) {
    irect16_t fr = {10, 10, 100, 20};
    window_t *cb = create_window("", 0, &fr, parent, win_combobox, 0, NULL);
    if (cb) cb->id = (uint32_t)id;
    return cb;
}

// ── tests ─────────────────────────────────────────────────────────────────

void test_cb_add_string(void) {
    TEST("win_combobox: cbAddString adds items");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               cb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *cb = make_combobox(parent, 1);
    ASSERT_NOT_NULL(cb);

    result_t r1 = send_message(cb, cbAddString, 0, "Alpha");
    result_t r2 = send_message(cb, cbAddString, 0, "Beta");
    ASSERT_TRUE(r1);
    ASSERT_TRUE(r2);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_cb_get_list_box_text(void) {
    TEST("win_combobox: cbGetListBoxText retrieves text by index");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               cb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *cb = make_combobox(parent, 2);
    ASSERT_NOT_NULL(cb);

    send_message(cb, cbAddString, 0, "First");
    send_message(cb, cbAddString, 0, "Second");

    char buf[64] = {0};
    result_t r = send_message(cb, cbGetListBoxText, 0, buf);
    ASSERT_TRUE(r);
    ASSERT_STR_EQUAL(buf, "First");

    memset(buf, 0, sizeof(buf));
    r = send_message(cb, cbGetListBoxText, 1, buf);
    ASSERT_TRUE(r);
    ASSERT_STR_EQUAL(buf, "Second");

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_cb_get_list_box_text_out_of_range(void) {
    TEST("win_combobox: cbGetListBoxText returns false for out-of-range index");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               cb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *cb = make_combobox(parent, 3);
    ASSERT_NOT_NULL(cb);

    send_message(cb, cbAddString, 0, "OnlyItem");

    char buf[64] = {0};
    result_t r = send_message(cb, cbGetListBoxText, 5, buf);
    ASSERT_FALSE(r);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_cb_no_selection_initially(void) {
    TEST("win_combobox: cbGetCurrentSelection returns kComboBoxError on empty combobox");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               cb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *cb = make_combobox(parent, 4);
    ASSERT_NOT_NULL(cb);

    result_t sel = send_message(cb, cbGetCurrentSelection, 0, NULL);
    ASSERT_EQUAL((int)sel, (int)kComboBoxError);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_cb_set_and_get_selection(void) {
    TEST("win_combobox: cbSetCurrentSelection + cbGetCurrentSelection round-trip");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               cb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *cb = make_combobox(parent, 5);
    ASSERT_NOT_NULL(cb);

    send_message(cb, cbAddString, 0, "Zero");
    send_message(cb, cbAddString, 0, "One");
    send_message(cb, cbAddString, 0, "Two");

    result_t r = send_message(cb, cbSetCurrentSelection, 1, NULL);
    ASSERT_TRUE(r);

    result_t sel = send_message(cb, cbGetCurrentSelection, 0, NULL);
    ASSERT_EQUAL((int)sel, 1);

    // After selection, the title should reflect the selected item
    ASSERT_STR_EQUAL(cb->title, "One");

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_cb_set_selection_out_of_range(void) {
    TEST("win_combobox: cbSetCurrentSelection out-of-range returns false");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               cb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *cb = make_combobox(parent, 6);
    ASSERT_NOT_NULL(cb);

    send_message(cb, cbAddString, 0, "A");
    send_message(cb, cbAddString, 0, "B");

    result_t r = send_message(cb, cbSetCurrentSelection, 99, NULL);
    ASSERT_FALSE(r);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_cb_clear(void) {
    TEST("win_combobox: cbClear removes all items");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               cb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *cb = make_combobox(parent, 7);
    ASSERT_NOT_NULL(cb);

    send_message(cb, cbAddString, 0, "X");
    send_message(cb, cbAddString, 0, "Y");
    send_message(cb, cbSetCurrentSelection, 0, NULL);

    // Verify items exist
    result_t sel_before = send_message(cb, cbGetCurrentSelection, 0, NULL);
    ASSERT_EQUAL((int)sel_before, 0);

    send_message(cb, cbClear, 0, NULL);

    // After clear, no selection possible
    result_t sel_after = send_message(cb, cbGetCurrentSelection, 0, NULL);
    ASSERT_EQUAL((int)sel_after, (int)kComboBoxError);

    // Re-adding should work from scratch
    result_t r = send_message(cb, cbAddString, 0, "New");
    ASSERT_TRUE(r);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_cb_down_arrow_selects_first_item(void) {
    TEST("win_combobox: Down arrow on empty selection selects item 0 and notifies");

    test_env_init();
    reset_state();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               cb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *cb = make_combobox(parent, 8);
    ASSERT_NOT_NULL(cb);

    send_message(cb, cbAddString, 0, "First");
    send_message(cb, cbAddString, 0, "Second");

    // cbAddString sets the title to the last added item; clear it so that
    // cbGetCurrentSelection returns kComboBoxError (no current selection).
    cb->title[0] = '\0';
    ASSERT_EQUAL((int)send_message(cb, cbGetCurrentSelection, 0, NULL),
                 (int)kComboBoxError);

    result_t r = send_message(cb, evKeyDown, AX_KEY_DOWNARROW, NULL);
    ASSERT_TRUE(r);

    ASSERT_EQUAL((int)send_message(cb, cbGetCurrentSelection, 0, NULL), 0);
    ASSERT_EQUAL(g_sel_change_count, 1);
    ASSERT_EQUAL(g_last_sel_id, 8);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_cb_down_arrow_advances_selection(void) {
    TEST("win_combobox: Down arrow advances selection and notifies");

    test_env_init();
    reset_state();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               cb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *cb = make_combobox(parent, 9);
    ASSERT_NOT_NULL(cb);

    send_message(cb, cbAddString, 0, "A");
    send_message(cb, cbAddString, 0, "B");
    send_message(cb, cbAddString, 0, "C");
    send_message(cb, cbSetCurrentSelection, 0, NULL);
    reset_state();

    send_message(cb, evKeyDown, AX_KEY_DOWNARROW, NULL);

    ASSERT_EQUAL((int)send_message(cb, cbGetCurrentSelection, 0, NULL), 1);
    ASSERT_EQUAL(g_sel_change_count, 1);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_cb_down_arrow_at_last_item_no_change(void) {
    TEST("win_combobox: Down arrow at last item sends no notification");

    test_env_init();
    reset_state();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               cb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *cb = make_combobox(parent, 10);
    ASSERT_NOT_NULL(cb);

    send_message(cb, cbAddString, 0, "Only");
    send_message(cb, cbSetCurrentSelection, 0, NULL);
    reset_state();

    send_message(cb, evKeyDown, AX_KEY_DOWNARROW, NULL);

    // Still at 0, no notification
    ASSERT_EQUAL((int)send_message(cb, cbGetCurrentSelection, 0, NULL), 0);
    ASSERT_EQUAL(g_sel_change_count, 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_cb_up_arrow_moves_selection(void) {
    TEST("win_combobox: Up arrow decrements selection and notifies");

    test_env_init();
    reset_state();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               cb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *cb = make_combobox(parent, 11);
    ASSERT_NOT_NULL(cb);

    send_message(cb, cbAddString, 0, "A");
    send_message(cb, cbAddString, 0, "B");
    send_message(cb, cbSetCurrentSelection, 1, NULL);
    reset_state();

    send_message(cb, evKeyDown, AX_KEY_UPARROW, NULL);

    ASSERT_EQUAL((int)send_message(cb, cbGetCurrentSelection, 0, NULL), 0);
    ASSERT_EQUAL(g_sel_change_count, 1);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_cb_up_arrow_at_first_item_no_change(void) {
    TEST("win_combobox: Up arrow at first item sends no notification");

    test_env_init();
    reset_state();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               cb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *cb = make_combobox(parent, 12);
    ASSERT_NOT_NULL(cb);

    send_message(cb, cbAddString, 0, "Item");
    send_message(cb, cbSetCurrentSelection, 0, NULL);
    reset_state();

    send_message(cb, evKeyDown, AX_KEY_UPARROW, NULL);

    ASSERT_EQUAL((int)send_message(cb, cbGetCurrentSelection, 0, NULL), 0);
    ASSERT_EQUAL(g_sel_change_count, 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

static window_t *open_test_dropdown(window_t *cb, int count, int selected) {
    char text[32];
    for (int i = 0; i < count; i++) {
        snprintf(text, sizeof(text), "Item %d", i);
        send_message(cb, cbAddString, 0, text);
    }
    send_message(cb, cbSetCurrentSelection, (uint32_t)selected, NULL);
    send_message(cb, evLeftButtonUp, 0, NULL);
    return g_ui_runtime.captured;
}

static void dispatch_mouse_at(int x, int y, uint32_t message) {
    ui_event_t event = {0};
    event.message = message;
    event.x = (uint16_t)(x * UI_WINDOW_SCALE);
    event.y = (uint16_t)(y * UI_WINDOW_SCALE);
    dispatch_message(&event);
}

void test_cb_dropdown_shows_at_most_eight_items(void) {
    TEST("win_combobox: dropdown shows at most eight items with scrollbar");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               cb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cb = make_combobox(parent, 13);
    ASSERT_NOT_NULL(cb);
    window_t *list = open_test_dropdown(cb, 10, 0);
    ASSERT_NOT_NULL(list);

    int row_h = CONTROL_HEIGHT_REGULAR;
    ASSERT_EQUAL(list->frame.h, 8 * row_h + MENU_START_Y * 2);
    ASSERT_TRUE(list->vscroll.visible);
    ASSERT_EQUAL(list->vscroll.max_val, 10 * row_h + MENU_START_Y * 2);
    ASSERT_EQUAL(list->vscroll.page, 8 * row_h + MENU_START_Y * 2);

    destroy_window(list);
    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_cb_dropdown_keyboard_keeps_selection_visible(void) {
    TEST("win_combobox: dropdown arrow navigation keeps selection visible");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               cb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cb = make_combobox(parent, 14);
    ASSERT_NOT_NULL(cb);
    window_t *list = open_test_dropdown(cb, 10, 0);
    ASSERT_NOT_NULL(list);

    int row_h = CONTROL_HEIGHT_REGULAR;
    for (int i = 0; i < 8; i++) send_message(list, evKeyDown, AX_KEY_DOWNARROW, NULL);
    ASSERT_EQUAL((int)list->cursor_pos, 8);
    ASSERT_EQUAL((int)list->vscroll.pos, row_h);
    for (int i = 0; i < 8; i++) send_message(list, evKeyDown, AX_KEY_UPARROW, NULL);
    ASSERT_EQUAL((int)list->cursor_pos, 0);
    ASSERT_EQUAL((int)list->vscroll.pos, 0);

    destroy_window(list);
    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_cb_dropdown_mouse_wheel_scrolls(void) {
    TEST("win_combobox: dropdown mouse wheel scrolls the list");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               cb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cb = make_combobox(parent, 15);
    ASSERT_NOT_NULL(cb);
    window_t *list = open_test_dropdown(cb, 10, 0);
    ASSERT_NOT_NULL(list);

    int row_h = CONTROL_HEIGHT_REGULAR;
    send_message(list, evWheel, 0, (void *)(intptr_t)MAKEDWORD(0, (uint16_t)-row_h));
    ASSERT_EQUAL((int)list->vscroll.pos, row_h);

    destroy_window(list);
    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_cb_dropdown_scrollbar_release_does_not_close(void) {
    TEST("win_combobox: releasing scrollbar does not close dropdown");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               cb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cb = make_combobox(parent, 16);
    ASSERT_NOT_NULL(cb);
    window_t *list = open_test_dropdown(cb, 10, 0);
    ASSERT_NOT_NULL(list);

    uint32_t scrollbar_point = MAKEDWORD((uint16_t)(list->frame.w - 1),
                                         (uint16_t)(list->frame.h - 1));
    send_message(list, evLeftButtonDown, scrollbar_point, NULL);
    send_message(list, evLeftButtonUp, scrollbar_point, NULL);
    ASSERT_TRUE(is_window(list));

    destroy_window(list);
    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_cb_dropdown_outside_click_cancels(void) {
    TEST("win_combobox: outside click cancels dropdown and restores selection");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               cb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cb = make_combobox(parent, 17);
    ASSERT_NOT_NULL(cb);
    window_t *list = open_test_dropdown(cb, 10, 2);
    ASSERT_NOT_NULL(list);
    int font_h = text_char_height(FONT_SYSTEM);
    ASSERT_EQUAL(list->frame.x + MENU_SIDE_PAD, window_screen_x(cb) + WINDOW_PADDING + 2);
    ASSERT_EQUAL(list->frame.y + MENU_START_Y + 2 * CONTROL_HEIGHT_REGULAR
                 - (int)list->vscroll.pos + (CONTROL_HEIGHT_REGULAR - font_h) / 2,
                 window_screen_y(cb) + (cb->frame.h - font_h) / 2);

    send_message(list, evKeyDown, AX_KEY_DOWNARROW, NULL);
    ASSERT_EQUAL((int)list->cursor_pos, 3);
    send_message(list, evLeftButtonDown, MAKEDWORD((uint16_t)-1, 0), NULL);
    ASSERT_FALSE(is_window(list));
    ASSERT_STR_EQUAL(cb->title, "Item 2");
    ASSERT_NULL(g_ui_runtime.captured);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_cb_dropdown_mouse_click_commits_selection(void) {
    TEST("win_combobox: captured mouse click commits selected item");

    test_env_init();
    reset_state();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               cb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cb = make_combobox(parent, 18);
    ASSERT_NOT_NULL(cb);
    window_t *list = open_test_dropdown(cb, 10, 0);
    ASSERT_NOT_NULL(list);

    int row_h = CONTROL_HEIGHT_REGULAR;
    int x = list->frame.x + 4;
    int y = list->frame.y + row_h + row_h / 2;
    dispatch_mouse_at(x, y, kEventLeftButtonDown);
    ASSERT_STR_EQUAL(cb->title, "Item 0");
    dispatch_mouse_at(x, y, kEventLeftButtonUp);
    ASSERT_FALSE(is_window(list));
    ASSERT_STR_EQUAL(cb->title, "Item 1");
    ASSERT_EQUAL(g_sel_change_count, 1);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_cb_dropdown_scrolled_mouse_click_commits_visible_item(void) {
    TEST("win_combobox: scrolled captured click commits the visible item");

    test_env_init();
    reset_state();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               cb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cb = make_combobox(parent, 19);
    ASSERT_NOT_NULL(cb);
    window_t *list = open_test_dropdown(cb, 10, 8);
    ASSERT_NOT_NULL(list);

    int row_h = CONTROL_HEIGHT_REGULAR;
    ASSERT_EQUAL((int)list->vscroll.pos, row_h);
    int x = list->frame.x + 4;
    int y = list->frame.y + 2 * row_h + row_h / 2;
    dispatch_mouse_at(x, y, kEventLeftButtonDown);
    dispatch_mouse_at(x, y, kEventLeftButtonUp);
    ASSERT_FALSE(is_window(list));
    ASSERT_STR_EQUAL(cb->title, "Item 3");
    ASSERT_EQUAL(g_sel_change_count, 1);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// ── main ──────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    TEST_START("win_combobox tests");

    test_cb_add_string();
    test_cb_get_list_box_text();
    test_cb_get_list_box_text_out_of_range();
    test_cb_no_selection_initially();
    test_cb_set_and_get_selection();
    test_cb_set_selection_out_of_range();
    test_cb_clear();
    test_cb_down_arrow_selects_first_item();
    test_cb_down_arrow_advances_selection();
    test_cb_down_arrow_at_last_item_no_change();
    test_cb_up_arrow_moves_selection();
    test_cb_up_arrow_at_first_item_no_change();
    test_cb_dropdown_shows_at_most_eight_items();
    test_cb_dropdown_keyboard_keeps_selection_visible();
    test_cb_dropdown_mouse_wheel_scrolls();
    test_cb_dropdown_scrollbar_release_does_not_close();
    test_cb_dropdown_outside_click_cancels();
    test_cb_dropdown_mouse_click_commits_selection();
    test_cb_dropdown_scrolled_mouse_click_commits_visible_item();

    TEST_END();
}

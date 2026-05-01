// combobox_test.c — Unit tests for commctl/combobox.c (win_combobox).
//
// Covers: cbAddString, cbGetCurrentSelection / cbSetCurrentSelection,
// cbGetListBoxText, cbClear, and Up/Down arrow keyboard navigation.

#include "test_framework.h"
#include "test_env.h"
#include "../ui.h"
#include "../commctl/commctl.h"

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

    TEST_END();
}

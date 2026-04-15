// tests/columnview_keyboard_test.c — headless tests for win_columnview keyboard
// navigation.  Covers: arrow key selection changes, Enter → CVN_DBLCLK,
// Delete → CVN_DELETE, no-notification behaviour when nothing is selected, and
// the auto-scroll helper that keeps the focused item visible.

#include "test_framework.h"
#include "test_env.h"
#include "../ui.h"
#include "../commctl/columnview.h"

// ---- shared notification capture ----------------------------------------- //

static int  g_cmd_count        = 0;
static int  g_last_notification = 0;
static int  g_last_index        = -1;

static result_t cmd_capture_proc(window_t *win, uint32_t msg,
                                  uint32_t wparam, void *lparam) {
    (void)lparam;
    if (msg == kWindowMessageCreate || msg == kWindowMessageDestroy) return 1;
    if (msg == kWindowMessageCommand) {
        int notif = (int)HIWORD(wparam);
        if (notif == CVN_SELCHANGE || notif == CVN_DBLCLK || notif == CVN_DELETE) {
            g_cmd_count++;
            g_last_notification = notif;
            g_last_index        = (int)(uint16_t)LOWORD(wparam);
        }
        return 1;
    }
    (void)win;
    return 0;
}

static void reset_cmd_state(void) {
    g_cmd_count        = 0;
    g_last_notification = 0;
    g_last_index        = -1;
}

// ---- helpers --------------------------------------------------------------- //

static window_t *make_columnview(window_t *parent, int w, int h) {
    rect_t fr = {0, 0, w, h};
    return create_window("cv", WINDOW_NOTITLE | WINDOW_NOFILL,
                         &fr, parent, win_columnview, 0, NULL);
}

static void add_items(window_t *cv, int n) {
    for (int i = 0; i < n; i++) {
        char name[32];
        snprintf(name, sizeof(name), "Item%d", i);
        columnview_item_t item = {
            .text     = name,
            .icon     = 0,
            .color    = 0xffffffff,
            .userdata = (uint32_t)i,
        };
        send_message(cv, CVM_ADDITEM, 0, &item);
    }
}

// ---- tests ----------------------------------------------------------------- //

// Down arrow with no prior selection selects item 0 and fires CVN_SELCHANGE.
void test_cv_down_from_no_selection(void) {
    TEST("win_columnview: Down with no selection selects item 0");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 3);

    result_t r = send_message(cv, kWindowMessageKeyDown, AX_KEY_DOWNARROW, NULL);

    ASSERT_TRUE(r);
    ASSERT_EQUAL(g_cmd_count, 1);
    ASSERT_EQUAL(g_last_notification, CVN_SELCHANGE);
    ASSERT_EQUAL(g_last_index, 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Down arrow from item 0 moves selection to item 1 (single-column layout).
void test_cv_down_advances_selection(void) {
    TEST("win_columnview: Down advances selection by one row");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 5);

    // Pre-select item 0.
    send_message(cv, CVM_SETSELECTION, 0, NULL);
    reset_cmd_state();

    send_message(cv, kWindowMessageKeyDown, AX_KEY_DOWNARROW, NULL);

    ASSERT_EQUAL(g_cmd_count, 1);
    ASSERT_EQUAL(g_last_notification, CVN_SELCHANGE);
    ASSERT_EQUAL(g_last_index, 1);
    ASSERT_EQUAL((int)send_message(cv, CVM_GETSELECTION, 0, NULL), 1);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Down arrow on last item stays put and fires no notification.
void test_cv_down_at_last_item_stays(void) {
    TEST("win_columnview: Down on last item clamps and sends no notification");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 3);

    // Select last item (index 2).
    send_message(cv, CVM_SETSELECTION, 2, NULL);
    reset_cmd_state();

    send_message(cv, kWindowMessageKeyDown, AX_KEY_DOWNARROW, NULL);

    ASSERT_EQUAL(g_cmd_count, 0);
    ASSERT_EQUAL((int)send_message(cv, CVM_GETSELECTION, 0, NULL), 2);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Up arrow from item 1 moves to item 0 and fires CVN_SELCHANGE.
void test_cv_up_moves_selection(void) {
    TEST("win_columnview: Up moves selection to previous row");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 5);

    send_message(cv, CVM_SETSELECTION, 1, NULL);
    reset_cmd_state();

    send_message(cv, kWindowMessageKeyDown, AX_KEY_UPARROW, NULL);

    ASSERT_EQUAL(g_cmd_count, 1);
    ASSERT_EQUAL(g_last_notification, CVN_SELCHANGE);
    ASSERT_EQUAL(g_last_index, 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Up arrow on the first item (top row) stays put and fires no notification.
void test_cv_up_at_first_item_stays(void) {
    TEST("win_columnview: Up on first item clamps and sends no notification");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 3);

    send_message(cv, CVM_SETSELECTION, 0, NULL);
    reset_cmd_state();

    send_message(cv, kWindowMessageKeyDown, AX_KEY_UPARROW, NULL);

    ASSERT_EQUAL(g_cmd_count, 0);
    ASSERT_EQUAL((int)send_message(cv, CVM_GETSELECTION, 0, NULL), 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Right arrow with no selection selects item 0.
void test_cv_right_from_no_selection(void) {
    TEST("win_columnview: Right with no selection selects item 0");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 3);

    result_t r = send_message(cv, kWindowMessageKeyDown, AX_KEY_RIGHTARROW, NULL);

    ASSERT_TRUE(r);
    ASSERT_EQUAL(g_cmd_count, 1);
    ASSERT_EQUAL(g_last_notification, CVN_SELCHANGE);
    ASSERT_EQUAL(g_last_index, 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Left arrow moves selection back one item.
void test_cv_left_moves_selection(void) {
    TEST("win_columnview: Left moves selection to previous item");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 5);

    send_message(cv, CVM_SETSELECTION, 2, NULL);
    reset_cmd_state();

    send_message(cv, kWindowMessageKeyDown, AX_KEY_LEFTARROW, NULL);

    ASSERT_EQUAL(g_cmd_count, 1);
    ASSERT_EQUAL(g_last_notification, CVN_SELCHANGE);
    ASSERT_EQUAL(g_last_index, 1);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Enter fires CVN_DBLCLK for the currently selected item.
void test_cv_enter_fires_dblclk(void) {
    TEST("win_columnview: Enter fires CVN_DBLCLK for selected item");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 3);

    send_message(cv, CVM_SETSELECTION, 1, NULL);
    reset_cmd_state();

    result_t r = send_message(cv, kWindowMessageKeyDown, AX_KEY_ENTER, NULL);

    ASSERT_TRUE(r);
    ASSERT_EQUAL(g_cmd_count, 1);
    ASSERT_EQUAL(g_last_notification, CVN_DBLCLK);
    ASSERT_EQUAL(g_last_index, 1);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Delete fires CVN_DELETE for the currently selected item.
void test_cv_delete_fires_cvn_delete(void) {
    TEST("win_columnview: Delete fires CVN_DELETE for selected item");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 3);

    send_message(cv, CVM_SETSELECTION, 0, NULL);
    reset_cmd_state();

    result_t r = send_message(cv, kWindowMessageKeyDown, AX_KEY_DEL, NULL);

    ASSERT_TRUE(r);
    ASSERT_EQUAL(g_cmd_count, 1);
    ASSERT_EQUAL(g_last_notification, CVN_DELETE);
    ASSERT_EQUAL(g_last_index, 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Enter with no selection returns false (allows framework default-button handling).
void test_cv_enter_no_selection_returns_false(void) {
    TEST("win_columnview: Enter with no selection returns false (falls through)");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 3);
    // No CVM_SETSELECTION call — selection remains -1.

    result_t r = send_message(cv, kWindowMessageKeyDown, AX_KEY_ENTER, NULL);

    ASSERT_FALSE(r);
    ASSERT_EQUAL(g_cmd_count, 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Delete with no selection returns false (does not silently consume the key).
void test_cv_delete_no_selection_returns_false(void) {
    TEST("win_columnview: Delete with no selection returns false (falls through)");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 3);
    // No CVM_SETSELECTION call — selection remains -1.

    result_t r = send_message(cv, kWindowMessageKeyDown, AX_KEY_DEL, NULL);

    ASSERT_FALSE(r);
    ASSERT_EQUAL(g_cmd_count, 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Arrow keys on an empty columnview return false (no items to navigate).
void test_cv_keys_on_empty_list_return_false(void) {
    TEST("win_columnview: arrow keys on empty list return false");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    // No items added.

    ASSERT_FALSE(send_message(cv, kWindowMessageKeyDown, AX_KEY_DOWNARROW, NULL));
    ASSERT_FALSE(send_message(cv, kWindowMessageKeyDown, AX_KEY_UPARROW,   NULL));
    ASSERT_EQUAL(g_cmd_count, 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Navigating Down past the visible area updates win->scroll[1] so the newly
// selected item is scrolled into view.
void test_cv_down_scrolls_selection_into_view(void) {
    TEST("win_columnview: Down past visible area updates scroll position");

    test_env_init();
    reset_cmd_state();

    // Create a very short window so that only the first row is visible.
    // With ENTRY_HEIGHT=13 and WIN_PADDING=4, row 0 occupies y=[4,17).
    // window height 13 means only one row fits.
    window_t *parent = test_env_create_window("P", 0, 0, 300, 13,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 13);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 5);

    // Navigate down to item 2 (3 key presses from no selection).
    send_message(cv, kWindowMessageKeyDown, AX_KEY_DOWNARROW, NULL); // → 0
    send_message(cv, kWindowMessageKeyDown, AX_KEY_DOWNARROW, NULL); // → 1
    send_message(cv, kWindowMessageKeyDown, AX_KEY_DOWNARROW, NULL); // → 2

    ASSERT_EQUAL((int)send_message(cv, CVM_GETSELECTION, 0, NULL), 2);
    // Scroll must have advanced so item 2 is visible.
    ASSERT_TRUE((int)cv->scroll[1] > 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// ---- main ------------------------------------------------------------------ //

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    TEST_START("win_columnview keyboard navigation");

    test_cv_down_from_no_selection();
    test_cv_down_advances_selection();
    test_cv_down_at_last_item_stays();
    test_cv_up_moves_selection();
    test_cv_up_at_first_item_stays();
    test_cv_right_from_no_selection();
    test_cv_left_moves_selection();
    test_cv_enter_fires_dblclk();
    test_cv_delete_fires_cvn_delete();
    test_cv_enter_no_selection_returns_false();
    test_cv_delete_no_selection_returns_false();
    test_cv_keys_on_empty_list_return_false();
    test_cv_down_scrolls_selection_into_view();

    TEST_END();
}

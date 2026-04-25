// tests/columnview_keyboard_test.c — headless tests for win_reportview keyboard
// navigation.  Covers: arrow key selection changes, Enter → RVN_DBLCLK,
// Delete → RVN_DELETE, no-notification behaviour when nothing is selected, and
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
    if (msg == evCreate || msg == evDestroy) return 1;
    if (msg == evCommand) {
        int notif = (int)HIWORD(wparam);
        if (notif == RVN_SELCHANGE || notif == RVN_DBLCLK || notif == RVN_DELETE) {
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
                         &fr, parent, win_reportview, 0, NULL);
}

static void add_items(window_t *cv, int n) {
    for (int i = 0; i < n; i++) {
        char name[32];
        snprintf(name, sizeof(name), "Item%d", i);
        reportview_item_t item = {
            .text     = name,
            .icon     = 0,
            .color    = 0xffffffff,
            .userdata = (uint32_t)i,
        };
        send_message(cv, RVM_ADDITEM, 0, &item);
    }
}

// ---- tests ----------------------------------------------------------------- //

// Down arrow with no prior selection selects item 0 and fires RVN_SELCHANGE.
void test_cv_down_from_no_selection(void) {
    TEST("win_reportview: Down with no selection selects item 0");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 3);

    result_t r = send_message(cv, evKeyDown, AX_KEY_DOWNARROW, NULL);

    ASSERT_TRUE(r);
    ASSERT_EQUAL(g_cmd_count, 1);
    ASSERT_EQUAL(g_last_notification, RVN_SELCHANGE);
    ASSERT_EQUAL(g_last_index, 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Down arrow from item 0 moves selection to item 1 (single-column layout).
void test_cv_down_advances_selection(void) {
    TEST("win_reportview: Down advances selection by one row");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 5);

    // Pre-select item 0.
    send_message(cv, RVM_SETSELECTION, 0, NULL);
    reset_cmd_state();

    send_message(cv, evKeyDown, AX_KEY_DOWNARROW, NULL);

    ASSERT_EQUAL(g_cmd_count, 1);
    ASSERT_EQUAL(g_last_notification, RVN_SELCHANGE);
    ASSERT_EQUAL(g_last_index, 1);
    ASSERT_EQUAL((int)send_message(cv, RVM_GETSELECTION, 0, NULL), 1);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Down arrow on last item stays put and fires no notification.
void test_cv_down_at_last_item_stays(void) {
    TEST("win_reportview: Down on last item clamps and sends no notification");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 3);

    // Select last item (index 2).
    send_message(cv, RVM_SETSELECTION, 2, NULL);
    reset_cmd_state();

    send_message(cv, evKeyDown, AX_KEY_DOWNARROW, NULL);

    ASSERT_EQUAL(g_cmd_count, 0);
    ASSERT_EQUAL((int)send_message(cv, RVM_GETSELECTION, 0, NULL), 2);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Up arrow from item 1 moves to item 0 and fires RVN_SELCHANGE.
void test_cv_up_moves_selection(void) {
    TEST("win_reportview: Up moves selection to previous row");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 5);

    send_message(cv, RVM_SETSELECTION, 1, NULL);
    reset_cmd_state();

    send_message(cv, evKeyDown, AX_KEY_UPARROW, NULL);

    ASSERT_EQUAL(g_cmd_count, 1);
    ASSERT_EQUAL(g_last_notification, RVN_SELCHANGE);
    ASSERT_EQUAL(g_last_index, 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Up arrow on the first item (top row) stays put and fires no notification.
void test_cv_up_at_first_item_stays(void) {
    TEST("win_reportview: Up on first item clamps and sends no notification");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 3);

    send_message(cv, RVM_SETSELECTION, 0, NULL);
    reset_cmd_state();

    send_message(cv, evKeyDown, AX_KEY_UPARROW, NULL);

    ASSERT_EQUAL(g_cmd_count, 0);
    ASSERT_EQUAL((int)send_message(cv, RVM_GETSELECTION, 0, NULL), 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Right arrow with no selection selects item 0.
void test_cv_right_from_no_selection(void) {
    TEST("win_reportview: Right with no selection selects item 0");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 3);

    result_t r = send_message(cv, evKeyDown, AX_KEY_RIGHTARROW, NULL);

    ASSERT_TRUE(r);
    ASSERT_EQUAL(g_cmd_count, 1);
    ASSERT_EQUAL(g_last_notification, RVN_SELCHANGE);
    ASSERT_EQUAL(g_last_index, 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Left arrow moves selection back one item.
void test_cv_left_moves_selection(void) {
    TEST("win_reportview: Left moves selection to previous item");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 5);

    send_message(cv, RVM_SETSELECTION, 2, NULL);
    reset_cmd_state();

    send_message(cv, evKeyDown, AX_KEY_LEFTARROW, NULL);

    ASSERT_EQUAL(g_cmd_count, 1);
    ASSERT_EQUAL(g_last_notification, RVN_SELCHANGE);
    ASSERT_EQUAL(g_last_index, 1);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Enter fires RVN_DBLCLK for the currently selected item.
void test_cv_enter_fires_dblclk(void) {
    TEST("win_reportview: Enter fires RVN_DBLCLK for selected item");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 3);

    send_message(cv, RVM_SETSELECTION, 1, NULL);
    reset_cmd_state();

    result_t r = send_message(cv, evKeyDown, AX_KEY_ENTER, NULL);

    ASSERT_TRUE(r);
    ASSERT_EQUAL(g_cmd_count, 1);
    ASSERT_EQUAL(g_last_notification, RVN_DBLCLK);
    ASSERT_EQUAL(g_last_index, 1);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Delete fires RVN_DELETE for the currently selected item.
void test_cv_delete_fires_cvn_delete(void) {
    TEST("win_reportview: Delete fires RVN_DELETE for selected item");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 3);

    send_message(cv, RVM_SETSELECTION, 0, NULL);
    reset_cmd_state();

    result_t r = send_message(cv, evKeyDown, AX_KEY_DEL, NULL);

    ASSERT_TRUE(r);
    ASSERT_EQUAL(g_cmd_count, 1);
    ASSERT_EQUAL(g_last_notification, RVN_DELETE);
    ASSERT_EQUAL(g_last_index, 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Enter with no selection returns false (allows framework default-button handling).
void test_cv_enter_no_selection_returns_false(void) {
    TEST("win_reportview: Enter with no selection returns false (falls through)");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 3);
    // No RVM_SETSELECTION call — selection remains -1.

    result_t r = send_message(cv, evKeyDown, AX_KEY_ENTER, NULL);

    ASSERT_FALSE(r);
    ASSERT_EQUAL(g_cmd_count, 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Delete with no selection returns false (does not silently consume the key).
void test_cv_delete_no_selection_returns_false(void) {
    TEST("win_reportview: Delete with no selection returns false (falls through)");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 3);
    // No RVM_SETSELECTION call — selection remains -1.

    result_t r = send_message(cv, evKeyDown, AX_KEY_DEL, NULL);

    ASSERT_FALSE(r);
    ASSERT_EQUAL(g_cmd_count, 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Arrow keys on an empty columnview return false (no items to navigate).
void test_cv_keys_on_empty_list_return_false(void) {
    TEST("win_reportview: arrow keys on empty list return false");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    // No items added.

    ASSERT_FALSE(send_message(cv, evKeyDown, AX_KEY_DOWNARROW, NULL));
    ASSERT_FALSE(send_message(cv, evKeyDown, AX_KEY_UPARROW,   NULL));
    ASSERT_EQUAL(g_cmd_count, 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Navigating Down past the visible area updates win->scroll[1] so the newly
// selected item is scrolled into view.
void test_cv_down_scrolls_selection_into_view(void) {
    TEST("win_reportview: Down past visible area updates scroll position");

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
    send_message(cv, evKeyDown, AX_KEY_DOWNARROW, NULL); // → 0
    send_message(cv, evKeyDown, AX_KEY_DOWNARROW, NULL); // → 1
    send_message(cv, evKeyDown, AX_KEY_DOWNARROW, NULL); // → 2

    ASSERT_EQUAL((int)send_message(cv, RVM_GETSELECTION, 0, NULL), 2);
    // Scroll must have advanced so item 2 is visible.
    ASSERT_TRUE((int)cv->scroll[1] > 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// ---- click-after-scroll tests ---------------------------------------------- //
// Coordinate system note
// ----------------------
// Mouse events arrive at win_reportview in **viewport-local** coordinates:
//   (0,0) = child window top-left corner, independent of scroll position.
//
// For ROOT windows event.c's LOCAL_X/LOCAL_Y already adds win->scroll[] so
// coords are already in content space.  For CHILD windows handle_mouse
// subtracts only c->frame.{x,y}, leaving scroll out.
//
// Consequence: rv_hit_index must add win->scroll[] for child windows so that
// "viewport y + scroll = content y" and the hit row matches the drawn row.
// If that addition is accidentally removed, clicks after scrolling will land
// on a row offset by the scroll distance.
//
// These tests guard against that regression: they set cv->scroll[1] directly
// and simulate a left-button click at a known viewport position, then assert
// that the selected index matches the item that is VISUALLY at that position,
// not the item whose natural (unscrolled) position is there.

// HEADER_HEIGHT and ENTRY_HEIGHT are internal to columnview.c; mirror them here.
// These match COLUMNVIEW_HEADER_HEIGHT / COLUMNVIEW_ENTRY_HEIGHT (FONT_SIZE + 6/5).
#define TEST_RV_HEADER_HEIGHT COLUMNVIEW_HEADER_HEIGHT
#define TEST_RV_ENTRY_HEIGHT  COLUMNVIEW_ENTRY_HEIGHT

// ---- helpers for report mode ------------------------------------------------ //

static window_t *make_report_columnview(window_t *parent, int w, int h) {
    rect_t fr = {0, 0, w, h};
    window_t *cv = create_window("rv", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL,
                                 &fr, parent, win_reportview, 0, NULL);
    if (!cv) return NULL;
    send_message(cv, RVM_SETVIEWMODE, RVM_VIEW_REPORT, NULL);
    reportview_column_t col = { "Name", 0 };
    send_message(cv, RVM_ADDCOLUMN, 0, &col);
    return cv;
}

// Click after scroll — child window, report mode.
// rv_hit_index uses the raw wparam coordinates directly (event.c already
// delivers content-space y to child windows, accounting for the child's
// scroll before the message is sent).  So clicking at content y =
// HEADER_HEIGHT + N*ENTRY_HEIGHT must select item N regardless of the
// current scroll position.
void test_cv_report_click_after_scroll_child(void) {
    TEST("win_reportview report child: click after scroll selects visual item");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_report_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 10);

    // Simulate a scrolled state (item K is at the top of the visible area).
    const int K = 3;
    cv->scroll[1] = (uint32_t)(K * TEST_RV_ENTRY_HEIGHT);

    // event.c delivers content-space y, so pass y = HEADER_HEIGHT + K*ENTRY_HEIGHT
    // to select item K.
    send_message(cv, evLeftButtonDown,
                 MAKEDWORD(5, TEST_RV_HEADER_HEIGHT + K * TEST_RV_ENTRY_HEIGHT), NULL);

    ASSERT_EQUAL(g_last_notification, RVN_SELCHANGE);
    ASSERT_EQUAL(g_last_index, K);

    // A content-space click one row lower selects K+1.
    reset_cmd_state();
    send_message(cv, evLeftButtonDown,
                 MAKEDWORD(5, TEST_RV_HEADER_HEIGHT + (K + 1) * TEST_RV_ENTRY_HEIGHT), NULL);

    ASSERT_EQUAL(g_last_notification, RVN_SELCHANGE);
    ASSERT_EQUAL(g_last_index, K + 1);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Click with no scroll still selects item 0 at the first body row.
void test_cv_report_click_no_scroll_child(void) {
    TEST("win_reportview report child: click with no scroll selects item 0");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_report_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 5);

    // No scroll — click first body row.
    send_message(cv, evLeftButtonDown,
                 MAKEDWORD(5, TEST_RV_HEADER_HEIGHT), NULL);

    ASSERT_EQUAL(g_last_notification, RVN_SELCHANGE);
    ASSERT_EQUAL(g_last_index, 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// ---- main ------------------------------------------------------------------ //

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    TEST_START("win_reportview keyboard navigation");

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
    test_cv_report_click_no_scroll_child();
    test_cv_report_click_after_scroll_child();

    TEST_END();
}

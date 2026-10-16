// tests/columnview_keyboard_test.c — headless tests for win_reportview keyboard
// navigation.  Covers: arrow key selection changes, Enter → RVN_DBLCLK,
// Delete → RVN_DELETE, no-notification behaviour when nothing is selected, and
// the auto-scroll helper that keeps the focused item visible.

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/commctl/columnview.h>
#include <orion/commctl/commctl.h>

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

static result_t wheel_blocking_parent_proc(window_t *win, uint32_t msg,
                                           uint32_t wparam, void *lparam) {
    (void)win;
    (void)wparam;
    (void)lparam;
    if (msg == evCreate || msg == evDestroy) return 1;
    if (msg == evParentNotify) return 1;
    return 0;
}

static void reset_cmd_state(void) {
    g_cmd_count        = 0;
    g_last_notification = 0;
    g_last_index        = -1;
}

// ---- helpers --------------------------------------------------------------- //

static window_t *make_columnview(window_t *parent, int w, int h) {
    irect16_t fr = {0, 0, w, h};
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

// Navigating Down past the visible area updates win->vscroll.pos so the newly
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
    ASSERT_TRUE((int)cv->vscroll.pos > 0);

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
// These tests guard against that regression: they set cv->vscroll.pos directly
// and simulate a left-button click at a known viewport position, then assert
// that the selected index matches the item that is VISUALLY at that position,
// not the item whose natural (unscrolled) position is there.

// HEADER_HEIGHT and ENTRY_HEIGHT are internal to columnview.c; mirror them here.
// These match COLUMNVIEW_HEADER_HEIGHT (FONT_SIZE + 6) and
// COLUMNVIEW_ENTRY_HEIGHT (FONT_SIZE_SMALL + 5).
#define TEST_RV_HEADER_HEIGHT COLUMNVIEW_HEADER_HEIGHT
#define TEST_RV_ENTRY_HEIGHT  COLUMNVIEW_ENTRY_HEIGHT

// ---- helpers for report mode ------------------------------------------------ //

static window_t *make_report_columnview(window_t *parent, int w, int h) {
    irect16_t fr = {0, 0, w, h};
    window_t *cv = create_window("rv", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL,
                                 &fr, parent, win_reportview, 0, NULL);
    if (!cv) return NULL;
    send_message(cv, RVM_SETVIEWMODE, RVM_VIEW_REPORT, NULL);
    reportview_column_t col = { "Name", 0 };
    send_message(cv, RVM_ADDCOLUMN, 0, &col);
    return cv;
}

static window_t *make_tableview_passthrough(window_t *parent, int w, int h) {
    irect16_t fr = {0, 0, w, h};
    return create_window("tv", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL,
                         &fr, parent, win_tableview, 0, NULL);
}

static void add_two_fixed_columns(window_t *cv, int w0, int w1) {
    reportview_column_t c0 = { "Col0", (uint32_t)w0 };
    reportview_column_t c1 = { "Col1", (uint32_t)w1 };
    send_message(cv, RVM_ADDCOLUMN, 0, &c0);
    send_message(cv, RVM_ADDCOLUMN, 0, &c1);
}

static void add_gitclient_like_columns(window_t *cv) {
    reportview_column_t c0 = { "Subject", 0 };
    reportview_column_t c1 = { "Author", 110 };
    reportview_column_t c2 = { "Date", 90 };
    reportview_column_t c3 = { "Hash", 50 };
    send_message(cv, RVM_ADDCOLUMN, 0, &c0);
    send_message(cv, RVM_ADDCOLUMN, 0, &c1);
    send_message(cv, RVM_ADDCOLUMN, 0, &c2);
    send_message(cv, RVM_ADDCOLUMN, 0, &c3);
}

static void run_resize_drag_sequence(window_t *cv, int edge_x, int drag_to_x) {
    int y = TEST_RV_HEADER_HEIGHT / 2;

    result_t cur = send_message(cv, evGetCursor, MAKEDWORD((uint16_t)edge_x, (uint16_t)y), NULL);
    ASSERT_EQUAL((int)cur, curResizeH);

    // Hover on edge, then press and drag the first divider.
    send_message(cv, evMouseMove, MAKEDWORD((uint16_t)edge_x, (uint16_t)y), NULL);
    result_t down = send_message(cv, evLeftButtonDown, MAKEDWORD((uint16_t)edge_x, (uint16_t)y), NULL);
    ASSERT_TRUE(down);

    send_message(cv, evMouseMove, MAKEDWORD((uint16_t)drag_to_x, (uint16_t)y), NULL);
    send_message(cv, evLeftButtonUp, MAKEDWORD((uint16_t)drag_to_x, (uint16_t)y), NULL);

    int w = (int)send_message(cv, RVM_GETREPORTCOLUMNWIDTH, 0, NULL);
    ASSERT_EQUAL(w, drag_to_x);
}

// Click after scroll — child window, report mode.
// Mouse events are delivered in viewport-local coordinates (event.c subtracts
// only the child's frame.{x,y}, not its scroll).  rv_hit_index adds
// win->vscroll.pos to convert viewport y to content y.
// With scroll[1] = K*ENTRY_HEIGHT the first visible row is at
// viewport y = HEADER_HEIGHT, which rv_hit_index maps to item K.
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
    cv->vscroll.pos = (uint32_t)(K * TEST_RV_ENTRY_HEIGHT);

    // event.c LOCAL_Y bakes scroll offset into mouse coordinates, so the
    // click y must be in content space (already includes scroll_y).
    // Item K is at content y = HEADER_HEIGHT + K * ENTRY_HEIGHT.
    send_message(cv, evLeftButtonDown,
                 MAKEDWORD(5, TEST_RV_HEADER_HEIGHT + K * TEST_RV_ENTRY_HEIGHT),
                 NULL);

    ASSERT_EQUAL(g_last_notification, RVN_SELCHANGE);
    ASSERT_EQUAL(g_last_index, K);

    // One row lower selects K+1.
    reset_cmd_state();
    send_message(cv, evLeftButtonDown,
                 MAKEDWORD(5, TEST_RV_HEADER_HEIGHT + (K + 1) * TEST_RV_ENTRY_HEIGHT),
                 NULL);

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

void test_cv_report_wheel_scrolls_child(void) {
    TEST("win_reportview report child: mouse wheel scrolls even with noisy parent");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               wheel_blocking_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_report_columnview(parent, 300, 200);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 20);

    ASSERT_EQUAL((int)cv->vscroll.pos, 0);

    // Wheel-down gives dy=-4 in this test input.
    // user/message.c negates HIWORD(lparam), so delta becomes +4 and vscroll.pos increases.
    send_message(cv, evWheel, MAKEDWORD(0, 0), (void*)(intptr_t)MAKEDWORD(0, (uint16_t)-4));
    ASSERT_TRUE((int)cv->vscroll.pos > 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_cv_report_header_resize_drag_and_cursor(void) {
    TEST("win_reportview report: header divider hover/drag resizes first column");

    test_env_init();

    window_t *parent = test_env_create_window("P", 0, 0, 320, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_report_columnview(parent, 300, 160);
    ASSERT_NOT_NULL(cv);

    add_two_fixed_columns(cv, 100, 100);
    run_resize_drag_sequence(cv, 100, 132);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_cv_tableview_header_resize_drag_and_cursor(void) {
    TEST("win_tableview(report path): gitclient-like flex column divider hover/drag works");

    test_env_init();

    window_t *parent = test_env_create_window("P", 0, 0, 320, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *tv = make_tableview_passthrough(parent, 300, 160);
    ASSERT_NOT_NULL(tv);

    add_gitclient_like_columns(tv);
    int edge_x = (int)send_message(tv, RVM_GETREPORTCOLUMNWIDTH, 0, NULL);
    ASSERT_TRUE(edge_x > 20);
    run_resize_drag_sequence(tv, edge_x, edge_x + 28);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// ---- main ------------------------------------------------------------------ //

// ---- large-icon view tests ------------------------------------------------- //

// Helper: create a win_icongrid with a fixed cell width so ncol is
// predictable.
static window_t *make_large_icon_columnview(window_t *parent, int w, int h,
                                             int cell_w, int icon_sz) {
    irect16_t fr = {0, 0, w, h};
    window_t *cv = create_window("li", WINDOW_NOTITLE | WINDOW_NOFILL,
                                 &fr, parent, win_icongrid, 0, NULL);
    if (!cv) return NULL;
    send_message(cv, RVM_SETCOLUMNWIDTH, (uint32_t)cell_w,   NULL);
    send_message(cv, RVM_SETICONSIZE,   (uint32_t)icon_sz,   NULL);
    return cv;
}

static window_t *make_fixed_large_icon_columnview(window_t *parent, int w, int h,
                                                  int cell_w, int icon_sz, int cols) {
    window_t *cv = make_large_icon_columnview(parent, w, h, cell_w, icon_sz);
    if (!cv) return NULL;
    send_message(cv, RVM_SETLARGEICONCOLS, (uint32_t)cols, NULL);
    return cv;
}

// RVM_SETICONSIZE accepts a positive value and rejects zero.
void test_cv_large_icon_seticonsize(void) {
    TEST("win_reportview large-icon: RVM_SETICONSIZE accepts positive, rejects zero");

    test_env_init();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_large_icon_columnview(parent, 300, 200, 72, 32);
    ASSERT_NOT_NULL(cv);

    // Valid size should succeed.
    result_t r = send_message(cv, RVM_SETICONSIZE, 64, NULL);
    ASSERT_TRUE(r);

    // Zero should fail.
    r = send_message(cv, RVM_SETICONSIZE, 0, NULL);
    ASSERT_FALSE(r);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// RVM_SETVIEWMODE accepts the large-icon mode and rejects invalid values.
void test_cv_large_icon_setviewmode(void) {
    TEST("win_reportview large-icon: RVM_SETVIEWMODE accepts large-icon mode");

    test_env_init();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    irect16_t fr = {0, 0, 300, 200};
    window_t *cv = create_window("cv", WINDOW_NOTITLE | WINDOW_NOFILL,
                                 &fr, parent, win_icongrid, 0, NULL);
    ASSERT_NOT_NULL(cv);

    result_t r = send_message(cv, RVM_SETVIEWMODE, RVM_VIEW_LARGE_ICON, NULL);
    ASSERT_TRUE(r);

    // An out-of-range mode value should fail.
    r = send_message(cv, RVM_SETVIEWMODE, 99, NULL);
    ASSERT_FALSE(r);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// In large-icon mode, Down from no selection selects item 0 and notifies.
void test_cv_large_icon_down_from_no_selection(void) {
    TEST("win_reportview large-icon: Down with no selection selects item 0");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    // cell_w=72, icon_sz=64 → ncol = MAX(1,(300-16)/72) = 3 columns
    window_t *cv = make_large_icon_columnview(parent, 300, 200, 72, 64);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 6);

    result_t r = send_message(cv, evKeyDown, AX_KEY_DOWNARROW, NULL);

    ASSERT_TRUE(r);
    ASSERT_EQUAL(g_cmd_count, 1);
    ASSERT_EQUAL(g_last_notification, RVN_SELCHANGE);
    ASSERT_EQUAL(g_last_index, 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// In large-icon mode, Down from item 0 jumps an entire row (ncol items).
void test_cv_large_icon_down_advances_row(void) {
    TEST("win_reportview large-icon: Down advances by ncol items");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    // cell_w=72, icon_sz=64, window_w=300
    // ncol = MAX(1, (300 - 2*8) / 72) = MAX(1, 284/72) = 3
    window_t *cv = make_large_icon_columnview(parent, 300, 200, 72, 64);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 9);

    send_message(cv, RVM_SETSELECTION, 0, NULL);
    reset_cmd_state();

    send_message(cv, evKeyDown, AX_KEY_DOWNARROW, NULL);

    // ncol=3, so item 0 + 3 = item 3
    ASSERT_EQUAL(g_cmd_count, 1);
    ASSERT_EQUAL(g_last_notification, RVN_SELCHANGE);
    ASSERT_EQUAL(g_last_index, 3);
    ASSERT_EQUAL((int)send_message(cv, RVM_GETSELECTION, 0, NULL), 3);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// In large-icon mode, Left and Right move within the same row by one cell.
void test_cv_large_icon_left_right(void) {
    TEST("win_reportview large-icon: Left/Right move within row");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_large_icon_columnview(parent, 300, 200, 72, 64);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 6);

    send_message(cv, RVM_SETSELECTION, 1, NULL);
    reset_cmd_state();

    // Right → item 2
    send_message(cv, evKeyDown, AX_KEY_RIGHTARROW, NULL);
    ASSERT_EQUAL(g_last_index, 2);

    // Left back → item 1
    reset_cmd_state();
    send_message(cv, evKeyDown, AX_KEY_LEFTARROW, NULL);
    ASSERT_EQUAL(g_last_index, 1);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// Extra horizontal room is distributed between icons before the column count
// snaps upward, so the last icon in a row reaches the right edge.
void test_cv_large_icon_stretches_columns_to_width(void) {
    TEST("win_reportview large-icon: stretches icon columns to fill width");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 240, 120,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_large_icon_columnview(parent, 240, 120, 72, 32);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 3);

    int x = 240 - LARGE_ICON_PAD - 2;
    int y = LARGE_ICON_PAD + LARGE_ICON_TOP_PAD + 4;
    result_t hit = send_message(cv, RVM_HITTEST,
                                MAKEDWORD((uint16_t)x, (uint16_t)y), NULL);

    ASSERT_EQUAL((int)hit, 2);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// With a fixed column count, shrinking the width must not change the vertical
// scroll position.  This guards against the scrollbar-width feedback loop that
// used to reflow the grid when the scrollbar appeared.
void test_cv_large_icon_fixed_columns_stable_scroll(void) {
    TEST("win_reportview large-icon: fixed columns keep scroll stable across width changes");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 320, 220,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *cv = make_fixed_large_icon_columnview(parent, 168, 72, 40, 24, 4);
    ASSERT_NOT_NULL(cv);
    add_items(cv, 17);

    send_message(cv, RVM_SETSELECTION, 16, NULL);
    int before = (int)cv->vscroll.pos;
    ASSERT_TRUE(before > 0);

    resize_window(cv, 152, 72);
    int after = (int)cv->vscroll.pos;

    ASSERT_EQUAL(after, before);
    ASSERT_EQUAL((int)send_message(cv, RVM_GETSELECTION, 0, NULL), 16);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

static int thumb_hit(window_t *cv, int x, int y) {
    return (int)send_message(cv, RVM_HITTEST, MAKEDWORD((uint16_t)x, (uint16_t)y), NULL);
}

// The photo grid stretches square tiles to fill the row and adds a column only
// once another minimum-size tile fits, with the theme gap between tiles.
void test_cv_thumbnail_fills_width_and_adds_columns(void) {
    TEST("win_icongrid thumbnail: tiles fill the width, columns grow with it");

    test_env_init();
    reset_cmd_state();

    window_t *parent = test_env_create_window("P", 0, 0, 400, 300,
                                               cmd_capture_proc, NULL);
    ASSERT_NOT_NULL(parent);
    irect16_t fr = {0, 0, 240, 240};
    window_t *cv = create_window("th", WINDOW_NOTITLE | WINDOW_NOFILL,
                                 &fr, parent, win_icongrid, 0, NULL);
    ASSERT_NOT_NULL(cv);
    ASSERT_TRUE(send_message(cv, RVM_SETVIEWMODE, RVM_VIEW_THUMBNAIL, NULL));
    ASSERT_TRUE(send_message(cv, RVM_SETICONSIZE, 70, NULL));
    add_items(cv, 8);

    int gap = get_theme()->thumbnail.gap;
    int cols = (240 + gap) / (70 + gap);
    int tile = (240 - (cols - 1) * gap) / cols;
    ASSERT_EQUAL(cols, 3);
    ASSERT_TRUE(tile > 70);
    ASSERT_EQUAL(thumb_hit(cv, 0, 0), 0);
    ASSERT_EQUAL(thumb_hit(cv, 239, 1), 2);       // last column reaches the right edge
    ASSERT_EQUAL(thumb_hit(cv, 1, tile + gap), 3); // square rows, gap below
    if (gap > 0) ASSERT_EQUAL(thumb_hit(cv, tile, 1), -1);

    resize_window(cv, 300, 240);
    ASSERT_EQUAL(thumb_hit(cv, 299, 1), 3);       // a fourth column now fits

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

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
    test_cv_report_wheel_scrolls_child();
    test_cv_report_header_resize_drag_and_cursor();
    test_cv_tableview_header_resize_drag_and_cursor();
    test_cv_large_icon_seticonsize();
    test_cv_large_icon_setviewmode();
    test_cv_large_icon_down_from_no_selection();
    test_cv_large_icon_down_advances_row();
    test_cv_large_icon_left_right();
    test_cv_large_icon_stretches_columns_to_width();
    test_cv_large_icon_fixed_columns_stable_scroll();
    test_cv_thumbnail_fills_width_and_adds_columns();

    TEST_END();
}

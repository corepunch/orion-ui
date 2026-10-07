// TileGrid + Card + Badge: adaptive columns, per-row heights, selection, keyboard, notifications.

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/commctl/commctl.h>

static int s_select = -1, s_activate = -1;

static result_t host_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
    (void)win; (void)lparam;
    if (msg == evCommand) {
        if (HIWORD(wparam) == tgnSelChange) s_select   = LOWORD(wparam);
        if (HIWORD(wparam) == tgnActivate)  s_activate = LOWORD(wparam);
        return true;
    }
    return msg == evCreate || msg == evDestroy;
}

static window_t *make_grid(window_t *host, int tiles, int min_w) {
    irect16_t frame = {0, 0, 640, 400};
    window_t *grid = create_window("grid", 0, &frame, host, win_tilegrid, 0, NULL);
    if (!grid) return NULL;
    send_message(grid, tgSetMinTileWidth, (uint32_t)min_w, NULL);
    for (int i = 0; i < tiles; i++) {
        irect16_t f = {0, 0, 10, 10};
        window_t *card = create_window("card", 0, &f, grid, win_card, 0, NULL);
        for (int line = 0; line <= i % 2; line++) {          // odd cards are taller
            irect16_t lf = {0, 0, 10, CONTROL_HEIGHT};
            create_window("line", 0, &lf, card, win_label, 0, NULL);
        }
    }
    window_layout_sync(grid);
    return grid;
}

static window_t *tile(window_t *grid, int i) {
    window_t *c = grid->children;
    while (i-- > 0 && c) c = c->next;
    return c;
}

static void test_adaptive_columns_and_rows(void) {
    TEST("TileGrid: adaptive columns, equal widths, rows take the tallest tile");
    test_env_init();
    window_t *host = test_env_create_window("host", 0, 0, 640, 480, host_proc, NULL);
    window_t *grid = make_grid(host, 7, 200);
    ASSERT_NOT_NULL(grid);
    // (640 - 20 padding + 6 gap) / (200 + 6) = 3 columns.
    ASSERT_EQUAL(tile(grid, 0)->frame.y, tile(grid, 1)->frame.y);
    ASSERT_EQUAL(tile(grid, 1)->frame.y, tile(grid, 2)->frame.y);
    ASSERT_TRUE(tile(grid, 3)->frame.y > tile(grid, 2)->frame.y);
    ASSERT_EQUAL(tile(grid, 3)->frame.x, tile(grid, 0)->frame.x);
    ASSERT_EQUAL(tile(grid, 0)->frame.w, tile(grid, 1)->frame.w);
    ASSERT_TRUE(tile(grid, 0)->frame.w >= 200);
    ASSERT_EQUAL(tile(grid, 0)->frame.h, tile(grid, 1)->frame.h);   // a row is as tall as its tallest card
    ASSERT_TRUE(tile(grid, 0)->frame.h > 0);
    // Narrower grid wraps into fewer columns.
    send_message(grid, tgSetMinTileWidth, 400, NULL);
    ASSERT_TRUE(tile(grid, 1)->frame.y > tile(grid, 0)->frame.y);
    test_env_shutdown();
    PASS();
}

static void test_selection_keyboard_and_notifications(void) {
    TEST("TileGrid: click selects and activates, arrows move, Enter activates, clear resets");
    test_env_init();
    window_t *host = test_env_create_window("host", 0, 0, 640, 480, host_proc, NULL);
    window_t *grid = make_grid(host, 7, 200);
    ASSERT_EQUAL((int)send_message(grid, tgGetSelection, 0, NULL), -1);

    window_t *third = tile(grid, 2);
    send_message(grid, evCommand, MAKEDWORD(third->id, cdnClicked), third);
    ASSERT_EQUAL((int)send_message(grid, tgGetSelection, 0, NULL), 2);
    ASSERT_EQUAL(s_select, 2);
    ASSERT_EQUAL(s_activate, 2);

    s_activate = -1;
    send_message(grid, evCommand, MAKEDWORD(third->id, cdnClicked), third);
    ASSERT_EQUAL(s_activate, 2);                                // the selected tile still opens

    send_message(grid, evKeyDown, AX_KEY_LEFTARROW, NULL);
    ASSERT_EQUAL(s_select, 1);
    send_message(grid, evKeyDown, AX_KEY_DOWNARROW, NULL);
    ASSERT_EQUAL(s_select, 4);                                  // same column, next row
    send_message(grid, evKeyDown, AX_KEY_ENTER, NULL);
    ASSERT_EQUAL(s_activate, 4);

    s_activate = -1;
    window_t *first = tile(grid, 0);
    send_message(grid, evCommand, MAKEDWORD(first->id, cdnActivated), first);
    ASSERT_EQUAL(s_activate, 0);

    send_message(grid, tgClear, 0, NULL);
    ASSERT_NULL(grid->children);
    ASSERT_EQUAL((int)send_message(grid, tgGetSelection, 0, NULL), -1);
    test_env_shutdown();
    PASS();
}

static void test_scroll_range_follows_content(void) {
    TEST("TileGrid: scrollbar range covers the content and the position stays inside it");
    test_env_init();
    window_t *host = test_env_create_window("host", 0, 0, 640, 480, host_proc, NULL);
    window_t *grid = make_grid(host, 40, 200);                 // far taller than 400px
    scroll_info_t si = {.fMask = SIF_ALL};
    get_scroll_info(grid, SB_VERT, &si);
    ASSERT_TRUE(si.nMax > 400);
    win_sb_t *bar = &grid->vscroll;
    ASSERT_TRUE(bar->visible);
    send_message(grid, evVScroll, 100000, NULL);                // beyond the end
    ASSERT_TRUE((int)grid->vscroll.pos <= si.nMax);
    send_message(grid, tgClear, 0, NULL);
    ASSERT_EQUAL((int)grid->vscroll.pos, 0);
    test_env_shutdown();
    PASS();
}

static void test_badge_and_truncating_label_measure(void) {
    TEST("Badge measures a fixed height; a truncating label stays one line");
    test_env_init();
    window_t *host = test_env_create_window("host", 0, 0, 200, 100, host_proc, NULL);
    irect16_t f = {0, 0, 10, 10};
    window_t *badge = create_window("3 modified", 0, &f, host, win_badge, 0, NULL);
    layout_measure_t m = {100, 100, 1, 1};
    send_message(badge, evMeasure, 0, &m);
    ASSERT_EQUAL(m.desired_h, BADGE_HEIGHT);
    ASSERT_EQUAL(send_message(badge, bdSetColor, 9999, NULL), 0);   // rejected: outside the palette

    window_t *label = create_window("a fairly long line of text that would normally wrap", 0, &f, host, win_label, 0, NULL);
    label_create_params_t style = {.color_index = brTextNormal, .font = FONT_SMALL, .color_set = true, .truncate = true};
    send_message(label, lbSetStyle, 0, &style);
    layout_measure_t lm = {20, 100, 1, 1};                          // far narrower than the text
    send_message(label, evMeasure, 0, &lm);
    ASSERT_EQUAL(lm.desired_h, CONTROL_HEIGHT);
    test_env_shutdown();
    PASS();
}

int main(void) {
    TEST_START("TileGrid / Card / Badge");
    test_adaptive_columns_and_rows();
    test_selection_keyboard_and_notifications();
    test_scroll_range_follows_content();
    test_badge_and_truncating_label_measure();
    TEST_END();
}

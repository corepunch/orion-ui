// Tests for WINDOW_TOOLBAR wrapping, kToolBarMessageSetStrip,
// and kToolBarMessageSetActiveButton.
//
// These tests are headless (no SDL/OpenGL rendering).  The titlebar_height()
// function and the new message handlers are exercised through the linked
// liborion symbols; rendering calls are no-ops because running==false.

#include "test_framework.h"
#include "test_env.h"
#include "../ui.h"
#include "../commctl/commctl.h"

// ---- helpers ----------------------------------------------------------------

// Inline copy of the toolbar-height computation to allow white-box assertions
// without depending on draw_impl.c's titlebar_height() linkage in tests.
// Must stay synchronised with the formula in draw_impl.c:titlebar_height().
// win_w is the total frame width; the usable bevel-inner width is win_w - 2
// (the toolbar rect is inset by 1px per side: rect.x = frame.x+1, rect.w = frame.w-2).
static int compute_toolbar_height(int num_buttons, int win_w) {
    int bsz = TB_SPACING;
    int inner_w = win_w - 2;
    if (num_buttons <= 0 || inner_w <= 0) {
        return bsz + 2 * TOOLBAR_PADDING;  // default: 1 row
    }
    int bpr = (inner_w - 2*TOOLBAR_PADDING + TOOLBAR_SPACING) / (bsz + TOOLBAR_SPACING);
    if (bpr < 1) bpr = 1;
    int num_rows = (num_buttons + bpr - 1) / bpr;
    return num_rows * bsz + 2 * TOOLBAR_PADDING;
}

static result_t noop_proc(window_t *win, uint32_t msg,
                           uint32_t wparam, void *lparam) {
    (void)wparam; (void)lparam;
    if (msg == kWindowMessageCreate || msg == kWindowMessageDestroy) return 1;
    return 0;
}

// ---- tests ------------------------------------------------------------------

void test_toolbar_wrapping_height(void) {
    TEST("Toolbar wrapping: toolbar height grows with wrapping rows");

    // With TOOLBAR_PADDING=2 and TOOLBAR_SPACING=4, inner_w = win_w - 2:
    //   bpr = MAX(1, (inner_w - 4 + 4) / 26) = MAX(1, inner_w / 26)
    //   height = nrows * TB_SPACING + 2 * TOOLBAR_PADDING

    // 5 buttons, 80px → inner_w=78, bpr=78/26=3 → 2 rows
    int h1 = compute_toolbar_height(5, 80);
    ASSERT_EQUAL(h1, 2 * TB_SPACING + 2 * TOOLBAR_PADDING);

    // 3 buttons, 80px → inner_w=78, bpr=3 → 1 row
    int h2 = compute_toolbar_height(3, 80);
    ASSERT_EQUAL(h2, 1 * TB_SPACING + 2 * TOOLBAR_PADDING);

    // 7 buttons, 80px → inner_w=78, bpr=3 → 3 rows
    int h3 = compute_toolbar_height(7, 80);
    ASSERT_EQUAL(h3, 3 * TB_SPACING + 2 * TOOLBAR_PADDING);

    // 5 buttons, 54px → inner_w=52, bpr=52/26=2 → 3 rows
    int h4 = compute_toolbar_height(5, 54);
    ASSERT_EQUAL(h4, 3 * TB_SPACING + 2 * TOOLBAR_PADDING);

    // 1 button in any window → 1 row
    int h5 = compute_toolbar_height(1, 64);
    ASSERT_EQUAL(h5, 1 * TB_SPACING + 2 * TOOLBAR_PADDING);

    // Boundary: 78px → inner_w=76, bpr=76/26=2 (NOT 3 as frame.w/26 would give).
    // Demonstrates the -2 inset: without it bpr=78/26=3 giving 2 rows.
    int h6 = compute_toolbar_height(5, 78);
    ASSERT_EQUAL(h6, 3 * TB_SPACING + 2 * TOOLBAR_PADDING);  // 3 rows because bpr=2

    PASS();
}

void test_toolbar_set_strip(void) {
    TEST("kToolBarMessageSetStrip stores strip in window");

    test_env_init();
    test_env_enable_tracking(true);
    test_env_clear_events();

    window_t *win = test_env_create_window("W", 0, 0, 200, 100, noop_proc, NULL);
    ASSERT_NOT_NULL(win);

    // Strip should be zero-initialised by default (tex == 0 means no strip).
    ASSERT_EQUAL((int)win->toolbar_strip.tex, 0);

    bitmap_strip_t strip = {
        .tex     = 42,
        .icon_w  = 16,
        .icon_h  = 16,
        .cols    = 2,
        .sheet_w = 32,
        .sheet_h = 160,
    };
    send_message(win, kToolBarMessageSetStrip, 0, &strip);
    repost_messages();

    ASSERT_EQUAL((int)win->toolbar_strip.tex,     42);
    ASSERT_EQUAL(win->toolbar_strip.icon_w,       16);
    ASSERT_EQUAL(win->toolbar_strip.cols,          2);
    ASSERT_EQUAL(win->toolbar_strip.sheet_w,      32);
    ASSERT_EQUAL(win->toolbar_strip.sheet_h,     160);

    // Clearing with NULL should zero the strip.
    send_message(win, kToolBarMessageSetStrip, 0, NULL);
    repost_messages();
    ASSERT_EQUAL((int)win->toolbar_strip.tex, 0);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_set_active_button(void) {
    TEST("kToolBarMessageSetActiveButton marks correct button active");

    test_env_init();
    test_env_enable_tracking(true);
    test_env_clear_events();

    window_t *win = test_env_create_window("W", 0, 0, 200, 100, noop_proc, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_button_t buttons[] = {
        {.icon = 0, .ident = 10, .active = true},
        {.icon = 1, .ident = 11, .active = false},
        {.icon = 2, .ident = 12, .active = false},
    };
    send_message(win, kToolBarMessageAddButtons, 3, buttons);
    repost_messages();

    ASSERT_EQUAL((int)win->num_toolbar_buttons, 3);
    ASSERT_TRUE(win->toolbar_buttons[0].active);   // ident 10 is active
    ASSERT_FALSE(win->toolbar_buttons[1].active);
    ASSERT_FALSE(win->toolbar_buttons[2].active);

    // Activate button with ident 11; button 10 should become inactive.
    send_message(win, kToolBarMessageSetActiveButton, 11, NULL);
    repost_messages();

    ASSERT_FALSE(win->toolbar_buttons[0].active);
    ASSERT_TRUE(win->toolbar_buttons[1].active);
    ASSERT_FALSE(win->toolbar_buttons[2].active);

    // Activate button with ident 12.
    send_message(win, kToolBarMessageSetActiveButton, 12, NULL);
    repost_messages();

    ASSERT_FALSE(win->toolbar_buttons[0].active);
    ASSERT_FALSE(win->toolbar_buttons[1].active);
    ASSERT_TRUE(win->toolbar_buttons[2].active);

    // Unknown ident clears all active flags.
    send_message(win, kToolBarMessageSetActiveButton, 99, NULL);
    repost_messages();

    ASSERT_FALSE(win->toolbar_buttons[0].active);
    ASSERT_FALSE(win->toolbar_buttons[1].active);
    ASSERT_FALSE(win->toolbar_buttons[2].active);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_add_buttons_replaces(void) {
    TEST("kToolBarMessageAddButtons replaces existing buttons");

    test_env_init();
    test_env_enable_tracking(true);
    test_env_clear_events();

    window_t *win = test_env_create_window("W", 0, 0, 100, 100, noop_proc, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_button_t first[] = {{.icon=0, .ident=1, .active=false}};
    send_message(win, kToolBarMessageAddButtons, 1, first);
    repost_messages();
    ASSERT_EQUAL((int)win->num_toolbar_buttons, 1);

    toolbar_button_t second[] = {
        {.icon=0, .ident=10, .active=true},
        {.icon=1, .ident=11, .active=false},
    };
    send_message(win, kToolBarMessageAddButtons, 2, second);
    repost_messages();
    ASSERT_EQUAL((int)win->num_toolbar_buttons, 2);
    ASSERT_EQUAL(win->toolbar_buttons[0].ident, 10);
    ASSERT_EQUAL(win->toolbar_buttons[1].ident, 11);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

// Test that the close-button geometry is in the title bar strip, not the toolbar.
//
// Regression test for the bug where clicking the right side of the toolbar
// would close the window.  The fix in kernel/event.c uses window_title_bar_y()
// to restrict the close-button hit-test to the title bar strip at the top of
// the non-client area, excluding the toolbar rows below it.
void test_close_button_y_excludes_toolbar(void) {
    TEST("Close button Y range is at window top for WINDOW_TOOLBAR windows");

    test_env_init();

    // Create a WINDOW_TOOLBAR window at a known position.
    // frame: x=10, y=100, w=44, h=150
    // With TOOLBAR_PADDING=2 and TOOLBAR_SPACING=4:
    //   bpr = MAX(1, 44/26) = 1
    //   5 buttons → 5 rows → toolbar_h = 5*22 + 4 = 114
    //   titlebar_height = TITLEBAR_HEIGHT + toolbar_h = 12 + 114 = 126
    // frame.y=100 is the window top.
    // window_title_bar_y = frame.y + 2 = 102.
    // Title bar row: [frame.y, frame.y + TITLEBAR_HEIGHT) = [100, 112).
    // Toolbar rows: [frame.y + TITLEBAR_HEIGHT, frame.y + titlebar_height) = [112, 226).
    rect_t frame = {10, 100, 44, 150};
    window_t *win = create_window("Tools", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_button_t buttons[] = {
        {.icon=0, .ident=1, .active=false},
        {.icon=1, .ident=2, .active=false},
        {.icon=2, .ident=3, .active=false},
        {.icon=3, .ident=4, .active=false},
        {.icon=4, .ident=5, .active=false},
    };
    send_message(win, kToolBarMessageAddButtons, 5, buttons);

    int toolbar_h = compute_toolbar_height((int)win->num_toolbar_buttons, win->frame.w);

    // window_title_bar_y now returns frame.y + 2 (title row is AT the window top).
    int title_y = window_title_bar_y(win);
    ASSERT_EQUAL(title_y, win->frame.y + 2);

    // The title bar row starts at frame.y.
    ASSERT_TRUE(title_y >= win->frame.y);
    ASSERT_TRUE(title_y < win->frame.y + TITLEBAR_HEIGHT);

    // Toolbar rows start immediately after the title bar.
    int toolbar_top = win->frame.y + TITLEBAR_HEIGHT;
    int toolbar_bottom = toolbar_top + toolbar_h;

    // A Y inside the toolbar must be outside the title bar row.
    int toolbar_y = toolbar_top + 5;
    ASSERT_TRUE(toolbar_y >= win->frame.y + TITLEBAR_HEIGHT);

    // A Y inside the title bar row.
    int titlebar_y = win->frame.y + 2;
    ASSERT_TRUE(titlebar_y >= win->frame.y && titlebar_y < win->frame.y + TITLEBAR_HEIGHT);

    // The close button X range is within the window.
    int close_x = win->frame.x + win->frame.w - CONTROL_BUTTON_WIDTH - CONTROL_BUTTON_PADDING;
    ASSERT_TRUE(close_x >= win->frame.x);
    ASSERT_TRUE(close_x + CONTROL_BUTTON_WIDTH <= win->frame.x + win->frame.w);

    // Suppress unused variable warning (toolbar_bottom is illustrative).
    (void)toolbar_bottom;

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_button_pressed_on_nonclient_mousedown(void) {
    TEST("WINDOW_TOOLBAR: kWindowMessageNonClientLeftButtonDown sets pressed, Up clears it");

    test_env_init();
    test_env_enable_tracking(true);
    test_env_clear_events();

    // Window at (10, 100), 44px wide, with 1 toolbar button.
    // title_only_h = TITLEBAR_HEIGHT = 12
    // base_x = frame.x + 1 + TOOLBAR_PADDING = 10 + 1 + 2 = 13
    // base_y = frame.y + title_only_h + 1 + TOOLBAR_PADDING = 100 + 12 + 1 + 2 = 115
    // Button 0: bx=13, by=115, hit area = bsz x bsz
    // Hit centre: (13 + bsz/2, 115 + bsz/2)
    rect_t frame = {10, 100, 44, 50};
    window_t *win = create_window("T", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_button_t buttons[] = {{.icon=0, .ident=7, .active=false}};
    send_message(win, kToolBarMessageAddButtons, 1, buttons);

    int bsz    = TB_SPACING;
    int base_x = win->frame.x + 1 + TOOLBAR_PADDING;
    int base_y = win->frame.y + TITLEBAR_HEIGHT + 1 + TOOLBAR_PADDING;
    int hit_x  = base_x + bsz / 2;
    int hit_y  = base_y + bsz / 2;

    // Button should start unpressed.
    ASSERT_FALSE(win->toolbar_buttons[0].pressed);

    // Simulate non-client left button down on the toolbar button.
    send_message(win, kWindowMessageNonClientLeftButtonDown,
                 MAKEDWORD(hit_x, hit_y), NULL);

    ASSERT_TRUE(win->toolbar_buttons[0].pressed);

    // Simulate non-client left button up — pressed state must be cleared.
    send_message(win, kWindowMessageNonClientLeftButtonUp,
                 MAKEDWORD(hit_x, hit_y), NULL);

    ASSERT_FALSE(win->toolbar_buttons[0].pressed);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_button_pressed_cleared_on_up_outside(void) {
    TEST("WINDOW_TOOLBAR: pressed flag is cleared on NonClientLeftButtonUp even outside button");

    test_env_init();
    test_env_enable_tracking(true);
    test_env_clear_events();

    rect_t frame = {10, 100, 44, 50};
    window_t *win = create_window("T", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_button_t buttons[] = {{.icon=0, .ident=8, .active=false}};
    send_message(win, kToolBarMessageAddButtons, 1, buttons);

    int bsz    = TB_SPACING;
    int base_x = win->frame.x + 1 + TOOLBAR_PADDING;
    int base_y = win->frame.y + TITLEBAR_HEIGHT + 1 + TOOLBAR_PADDING;
    int hit_x  = base_x + bsz / 2;
    int hit_y  = base_y + bsz / 2;

    // Press the button.
    send_message(win, kWindowMessageNonClientLeftButtonDown,
                 MAKEDWORD(hit_x, hit_y), NULL);
    ASSERT_TRUE(win->toolbar_buttons[0].pressed);

    // Release outside the button — pressed must still be cleared.
    send_message(win, kWindowMessageNonClientLeftButtonUp,
                 MAKEDWORD(0, 0), NULL);
    ASSERT_FALSE(win->toolbar_buttons[0].pressed);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

// ---- main -------------------------------------------------------------------

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    TEST_START("WINDOW_TOOLBAR wrapping and new message tests");

    test_toolbar_wrapping_height();
    test_toolbar_set_strip();
    test_toolbar_set_active_button();
    test_toolbar_add_buttons_replaces();
    test_close_button_y_excludes_toolbar();
    test_toolbar_button_pressed_on_nonclient_mousedown();
    test_toolbar_button_pressed_cleared_on_up_outside();

    TEST_END();
}

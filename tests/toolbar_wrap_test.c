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

extern void repost_messages(void);

// ---- helpers ----------------------------------------------------------------

// Inline copy of the toolbar-height computation to allow white-box assertions
// without depending on draw_impl.c's titlebar_height() linkage in tests.
// Must stay synchronised with the formula in draw_impl.c:titlebar_height().
static int compute_toolbar_height(int num_buttons, int win_w) {
    if (num_buttons <= 0 || win_w <= 0) {
        return TOOLBAR_HEIGHT;  // default: 1 row
    }
    int buttons_per_row = win_w / TB_SPACING;
    if (buttons_per_row < 1) buttons_per_row = 1;
    int num_rows = (num_buttons + buttons_per_row - 1) / buttons_per_row;
    return num_rows * TOOLBAR_HEIGHT;
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

    // 5 buttons in a 64px wide window → TB_SPACING=20 → 3 per row → 2 rows
    int h1 = compute_toolbar_height(5, 64);
    ASSERT_EQUAL(h1, 2 * TOOLBAR_HEIGHT);

    // 3 buttons in a 64px wide window → 3 per row → 1 row
    int h2 = compute_toolbar_height(3, 64);
    ASSERT_EQUAL(h2, 1 * TOOLBAR_HEIGHT);

    // 7 buttons in a 64px wide window → 3 per row → 3 rows
    int h3 = compute_toolbar_height(7, 64);
    ASSERT_EQUAL(h3, 3 * TOOLBAR_HEIGHT);

    // 5 buttons in a 40px wide window → 40/20 = 2 per row → 3 rows
    int h4 = compute_toolbar_height(5, 40);
    ASSERT_EQUAL(h4, 3 * TOOLBAR_HEIGHT);

    // 1 button in any window → 1 row
    int h5 = compute_toolbar_height(1, 64);
    ASSERT_EQUAL(h5, 1 * TOOLBAR_HEIGHT);

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

// ---- main -------------------------------------------------------------------

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    TEST_START("WINDOW_TOOLBAR wrapping and new message tests");

    test_toolbar_wrapping_height();
    test_toolbar_set_strip();
    test_toolbar_set_active_button();
    test_toolbar_add_buttons_replaces();

    TEST_END();
}

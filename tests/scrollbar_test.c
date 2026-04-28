// scrollbar_test.c — Unit tests for commctl/scrollbar.c (win_scrollbar).
//
// Covers: initial default state, sbSetInfo/sbGetPos, clamping, track-click
// page-scroll, and parent notification (sbChanged via evCommand).

#include "test_framework.h"
#include "test_env.h"
#include "../ui.h"
#include "../commctl/commctl.h"

// ── notification capture ──────────────────────────────────────────────────

static int  g_changed_count = 0;
static int  g_last_pos      = -1;
static int  g_last_cmd_id   = -1;

static result_t sb_parent_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
    (void)win;
    if (msg == evCreate || msg == evDestroy) return 1;
    if (msg == evCommand && HIWORD(wparam) == sbChanged) {
        g_changed_count++;
        g_last_cmd_id = (int)LOWORD(wparam);
        g_last_pos    = (int)(intptr_t)lparam;
    }
    return 0;
}

static void reset_state(void) {
    g_changed_count = 0;
    g_last_pos      = -1;
    g_last_cmd_id   = -1;
}

// ── helpers ───────────────────────────────────────────────────────────────

static window_t *make_vscrollbar(window_t *parent, int id, int h) {
    rect_t fr = {0, 0, 13, h};
    window_t *sb = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL,
                                 &fr, parent, win_scrollbar,
                                 0, (void *)1 /* SB_VERT */);
    if (sb) sb->id = (uint32_t)id;
    return sb;
}

static window_t *make_hscrollbar(window_t *parent, int id, int w) {
    rect_t fr = {0, 0, w, 13};
    window_t *sb = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL,
                                 &fr, parent, win_scrollbar,
                                 0, (void *)0 /* SB_HORZ */);
    if (sb) sb->id = (uint32_t)id;
    return sb;
}

static void set_info(window_t *sb, int min_val, int max_val, int page, int pos) {
    scrollbar_info_t info = { min_val, max_val, page, pos };
    send_message(sb, sbSetInfo, 0, &info);
}

// ── tests ─────────────────────────────────────────────────────────────────

void test_sb_default_pos_zero(void) {
    TEST("win_scrollbar: initial position is 0");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 300,
                                               sb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *sb = make_vscrollbar(parent, 1, 100);
    ASSERT_NOT_NULL(sb);

    ASSERT_EQUAL((int)send_message(sb, sbGetPos, 0, NULL), 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_sb_set_info_updates_pos(void) {
    TEST("win_scrollbar: sbSetInfo updates pos reported by sbGetPos");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 300,
                                               sb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *sb = make_vscrollbar(parent, 2, 100);
    ASSERT_NOT_NULL(sb);

    set_info(sb, 0, 200, 20, 50);
    ASSERT_EQUAL((int)send_message(sb, sbGetPos, 0, NULL), 50);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_sb_set_info_clamps_pos_to_max(void) {
    TEST("win_scrollbar: sbSetInfo clamps pos to max_val-page");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 300,
                                               sb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *sb = make_vscrollbar(parent, 3, 100);
    ASSERT_NOT_NULL(sb);

    // range 0–100, page 10 → max pos = 90
    set_info(sb, 0, 100, 10, 200);
    ASSERT_EQUAL((int)send_message(sb, sbGetPos, 0, NULL), 90);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_sb_set_info_clamps_pos_to_min(void) {
    TEST("win_scrollbar: sbSetInfo clamps pos to min_val");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 300,
                                               sb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *sb = make_vscrollbar(parent, 4, 100);
    ASSERT_NOT_NULL(sb);

    set_info(sb, 10, 100, 10, 5); // pos=5 below min=10
    ASSERT_EQUAL((int)send_message(sb, sbGetPos, 0, NULL), 10);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_sb_null_info_returns_false(void) {
    TEST("win_scrollbar: sbSetInfo with NULL lparam returns false");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 300,
                                               sb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *sb = make_vscrollbar(parent, 5, 100);
    ASSERT_NOT_NULL(sb);

    result_t r = send_message(sb, sbSetInfo, 0, NULL);
    ASSERT_FALSE(r);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_sb_track_click_scrolls_forward(void) {
    TEST("win_scrollbar: click past thumb page-scrolls forward and notifies parent");

    test_env_init();
    reset_state();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 300,
                                               sb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    // Vertical scrollbar, height=100
    window_t *sb = make_vscrollbar(parent, 6, 100);
    ASSERT_NOT_NULL(sb);

    // range 0–100, page 10, pos=0
    // thumb_len = 100 * 10 / 100 = 10 px, thumb_off = 0
    // Click at y=50 (beyond thumb end at 10) → forward page-scroll → pos=10
    set_info(sb, 0, 100, 10, 0);
    reset_state();

    send_message(sb, evLeftButtonDown, MAKEDWORD(5, 50), NULL);

    ASSERT_EQUAL(g_changed_count, 1);
    ASSERT_EQUAL(g_last_cmd_id, 6);
    ASSERT_EQUAL(g_last_pos, 10); // pos + page = 0 + 10 = 10

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_sb_track_click_scrolls_backward(void) {
    TEST("win_scrollbar: click before thumb page-scrolls backward and notifies parent");

    test_env_init();
    reset_state();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 300,
                                               sb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *sb = make_vscrollbar(parent, 7, 100);
    ASSERT_NOT_NULL(sb);

    // range 0–100, page 10, pos=50
    // thumb_len = 10, travel = 90, thumb_off = 50 * (100-10) / (100-0-10) = 50
    // Click at y=10 (before thumb at 50) → backward: pos = 50 - 10 = 40
    set_info(sb, 0, 100, 10, 50);
    reset_state();

    send_message(sb, evLeftButtonDown, MAKEDWORD(5, 10), NULL);

    ASSERT_EQUAL(g_changed_count, 1);
    ASSERT_EQUAL(g_last_pos, 40);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_sb_track_click_at_max_no_overflow(void) {
    TEST("win_scrollbar: forward click at max pos sends no notification (already at end)");

    test_env_init();
    reset_state();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 300,
                                               sb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *sb = make_vscrollbar(parent, 8, 100);
    ASSERT_NOT_NULL(sb);

    // range 0–100, page 10, pos=90 (already at max)
    // Another forward click should not move further → no notification
    set_info(sb, 0, 100, 10, 90);
    reset_state();

    send_message(sb, evLeftButtonDown, MAKEDWORD(5, 95), NULL);

    ASSERT_EQUAL(g_changed_count, 0);
    ASSERT_EQUAL((int)send_message(sb, sbGetPos, 0, NULL), 90);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_sb_horizontal_track_click(void) {
    TEST("win_scrollbar: horizontal bar click uses x-axis for scroll direction");

    test_env_init();
    reset_state();
    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                               sb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    // Horizontal scrollbar, width=100
    window_t *sb = make_hscrollbar(parent, 9, 100);
    ASSERT_NOT_NULL(sb);

    // range 0–100, page 10, pos=0
    // thumb at x=0, len=10
    // Click at x=50 → forward → pos=10
    set_info(sb, 0, 100, 10, 0);
    reset_state();

    send_message(sb, evLeftButtonDown, MAKEDWORD(50, 5), NULL);

    ASSERT_EQUAL(g_changed_count, 1);
    ASSERT_EQUAL(g_last_pos, 10);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_sb_set_info_non_zero_min(void) {
    TEST("win_scrollbar: sbSetInfo with non-zero min_val and pos at min is reported correctly");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 300,
                                               sb_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *sb = make_vscrollbar(parent, 10, 100);
    ASSERT_NOT_NULL(sb);

    // min=50, max=150, page=20, pos=50 → effective max pos = 130
    set_info(sb, 50, 150, 20, 50);
    ASSERT_EQUAL((int)send_message(sb, sbGetPos, 0, NULL), 50);

    // Move to the effective maximum
    set_info(sb, 50, 150, 20, 200); // clamped to 130
    ASSERT_EQUAL((int)send_message(sb, sbGetPos, 0, NULL), 130);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// ── main ──────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    TEST_START("win_scrollbar tests");

    test_sb_default_pos_zero();
    test_sb_set_info_updates_pos();
    test_sb_set_info_clamps_pos_to_max();
    test_sb_set_info_clamps_pos_to_min();
    test_sb_null_info_returns_false();
    test_sb_track_click_scrolls_forward();
    test_sb_track_click_scrolls_backward();
    test_sb_track_click_at_max_no_overflow();
    test_sb_horizontal_track_click();
    test_sb_set_info_non_zero_min();

    TEST_END();
}

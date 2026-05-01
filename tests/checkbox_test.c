// checkbox_test.c — Unit tests for commctl/checkbox.c (win_checkbox).
//
// Covers: initial state, btnSetCheck/btnGetCheck, click-to-toggle,
// parent notification (btnClicked via evCommand), and keyboard activation
// via SPACE and ENTER keys.

#include "test_framework.h"
#include "test_env.h"
#include "../ui.h"
#include "../commctl/commctl.h"

// ── shared notification capture ───────────────────────────────────────────

static int      g_cmd_count    = 0;
static uint32_t g_last_wparam  = 0;
static window_t *g_last_sender = NULL;

static result_t parent_proc(window_t *win, uint32_t msg,
                             uint32_t wparam, void *lparam) {
    (void)win;
    if (msg == evCreate || msg == evDestroy) return 1;
    if (msg == evCommand && HIWORD(wparam) == btnClicked) {
        g_cmd_count++;
        g_last_wparam  = wparam;
        g_last_sender  = (window_t *)lparam;
    }
    return 0;
}

static void reset_state(void) {
    g_cmd_count   = 0;
    g_last_wparam = 0;
    g_last_sender = NULL;
}

static window_t *make_checkbox(window_t *parent, int id) {
    irect16_t fr = {10, 10, 80, 20};
    window_t *cb = create_window("Check", 0, &fr, parent, win_checkbox, 0, NULL);
    if (cb) cb->id = (uint32_t)id;
    return cb;
}

// ── tests ─────────────────────────────────────────────────────────────────

void test_checkbox_initial_unchecked(void) {
    TEST("win_checkbox: newly created checkbox is unchecked");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *cb = make_checkbox(parent, 10);
    ASSERT_NOT_NULL(cb);

    result_t state = send_message(cb, btnGetCheck, 0, NULL);
    ASSERT_EQUAL((int)state, (int)btnStateUnchecked);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_checkbox_set_check(void) {
    TEST("win_checkbox: btnSetCheck(checked) makes btnGetCheck return checked");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *cb = make_checkbox(parent, 11);
    ASSERT_NOT_NULL(cb);

    send_message(cb, btnSetCheck, btnStateChecked, NULL);
    ASSERT_EQUAL((int)send_message(cb, btnGetCheck, 0, NULL),
                 (int)btnStateChecked);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_checkbox_set_uncheck(void) {
    TEST("win_checkbox: btnSetCheck(unchecked) clears the check");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *cb = make_checkbox(parent, 12);
    ASSERT_NOT_NULL(cb);

    send_message(cb, btnSetCheck, btnStateChecked, NULL);
    send_message(cb, btnSetCheck, btnStateUnchecked, NULL);
    ASSERT_EQUAL((int)send_message(cb, btnGetCheck, 0, NULL),
                 (int)btnStateUnchecked);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_checkbox_click_toggles(void) {
    TEST("win_checkbox: LButtonDown+LButtonUp toggles check state");

    test_env_init();
    test_env_enable_tracking(true);
    reset_state();

    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *cb = make_checkbox(parent, 13);
    ASSERT_NOT_NULL(cb);

    // Initial state: unchecked
    ASSERT_EQUAL((int)send_message(cb, btnGetCheck, 0, NULL),
                 (int)btnStateUnchecked);

    int cx = cb->frame.x + cb->frame.w / 2;
    int cy = cb->frame.y + cb->frame.h / 2;

    // First click: unchecked → checked
    send_message(cb, evLeftButtonDown, MAKEDWORD(cx, cy), NULL);
    send_message(cb, evLeftButtonUp,   MAKEDWORD(cx, cy), NULL);
    ASSERT_EQUAL((int)send_message(cb, btnGetCheck, 0, NULL),
                 (int)btnStateChecked);

    // Second click: checked → unchecked
    send_message(cb, evLeftButtonDown, MAKEDWORD(cx, cy), NULL);
    send_message(cb, evLeftButtonUp,   MAKEDWORD(cx, cy), NULL);
    ASSERT_EQUAL((int)send_message(cb, btnGetCheck, 0, NULL),
                 (int)btnStateUnchecked);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_checkbox_click_notifies_parent(void) {
    TEST("win_checkbox: click sends btnClicked evCommand to parent");

    test_env_init();
    test_env_enable_tracking(true);
    reset_state();

    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *cb = make_checkbox(parent, 14);
    ASSERT_NOT_NULL(cb);

    int cx = cb->frame.x + cb->frame.w / 2;
    int cy = cb->frame.y + cb->frame.h / 2;

    send_message(cb, evLeftButtonDown, MAKEDWORD(cx, cy), NULL);
    send_message(cb, evLeftButtonUp,   MAKEDWORD(cx, cy), NULL);

    ASSERT_EQUAL(g_cmd_count, 1);
    ASSERT_EQUAL((int)HIWORD(g_last_wparam), (int)btnClicked);
    ASSERT_EQUAL((int)LOWORD(g_last_wparam), 14);
    ASSERT_EQUAL(g_last_sender, cb);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_checkbox_keyboard_space_toggles(void) {
    TEST("win_checkbox: SPACE keyDown+keyUp toggles check and notifies parent");

    test_env_init();
    test_env_enable_tracking(true);
    reset_state();

    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *cb = make_checkbox(parent, 15);
    ASSERT_NOT_NULL(cb);

    // SPACE down: just sets pressed flag
    send_message(cb, evKeyDown, AX_KEY_SPACE, NULL);
    ASSERT_TRUE(cb->pressed);

    // SPACE up: toggles, notifies
    send_message(cb, evKeyUp, AX_KEY_SPACE, NULL);
    ASSERT_FALSE(cb->pressed);
    ASSERT_EQUAL((int)send_message(cb, btnGetCheck, 0, NULL),
                 (int)btnStateChecked);
    ASSERT_EQUAL(g_cmd_count, 1);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_checkbox_keyboard_enter_toggles(void) {
    TEST("win_checkbox: ENTER keyDown+keyUp toggles check and notifies parent");

    test_env_init();
    test_env_enable_tracking(true);
    reset_state();

    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *cb = make_checkbox(parent, 16);
    ASSERT_NOT_NULL(cb);

    send_message(cb, evKeyDown, AX_KEY_ENTER, NULL);
    send_message(cb, evKeyUp,   AX_KEY_ENTER, NULL);
    ASSERT_EQUAL((int)send_message(cb, btnGetCheck, 0, NULL),
                 (int)btnStateChecked);
    ASSERT_EQUAL(g_cmd_count, 1);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_checkbox_other_key_ignored(void) {
    TEST("win_checkbox: non-toggle key does not change state or notify");

    test_env_init();
    test_env_enable_tracking(true);
    reset_state();

    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *cb = make_checkbox(parent, 17);
    ASSERT_NOT_NULL(cb);

    // A regular letter key should be ignored by the checkbox
    result_t r = send_message(cb, evKeyDown, AX_KEY_A, NULL);
    ASSERT_FALSE(r); // not handled
    ASSERT_EQUAL(g_cmd_count, 0);
    ASSERT_EQUAL((int)send_message(cb, btnGetCheck, 0, NULL),
                 (int)btnStateUnchecked);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_checkbox_multiple_clicks_alternate(void) {
    TEST("win_checkbox: three clicks produce alternating checked/unchecked/checked");

    test_env_init();
    test_env_enable_tracking(true);
    reset_state();

    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *cb = make_checkbox(parent, 18);
    ASSERT_NOT_NULL(cb);

    int cx = cb->frame.x + cb->frame.w / 2;
    int cy = cb->frame.y + cb->frame.h / 2;

    for (int i = 1; i <= 3; i++) {
        send_message(cb, evLeftButtonDown, MAKEDWORD(cx, cy), NULL);
        send_message(cb, evLeftButtonUp,   MAKEDWORD(cx, cy), NULL);
        bool expected = (i % 2 != 0);
        bool actual   = (send_message(cb, btnGetCheck, 0, NULL) == btnStateChecked);
        ASSERT_EQUAL((int)actual, (int)expected);
    }
    ASSERT_EQUAL(g_cmd_count, 3);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// ── main ──────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    TEST_START("win_checkbox tests");

    test_checkbox_initial_unchecked();
    test_checkbox_set_check();
    test_checkbox_set_uncheck();
    test_checkbox_click_toggles();
    test_checkbox_click_notifies_parent();
    test_checkbox_keyboard_space_toggles();
    test_checkbox_keyboard_enter_toggles();
    test_checkbox_other_key_ignored();
    test_checkbox_multiple_clicks_alternate();

    TEST_END();
}

// accel_test.c — Unit tests for user/accel.c
//
// Covers: load_accelerators, free_accelerators, accel_find_cmd, accel_format,
// and translate_accelerator (matching and non-matching cases).

#include "test_framework.h"
#include "test_env.h"
#include "../ui.h"
#include "../user/accel.h"
#include "../platform/events.h"

// ── helpers ───────────────────────────────────────────────────────────────

// Small accelerator table used by most tests.
static const accel_t kTestAccels[] = {
    { FVIRTKEY | FCONTROL,           AX_KEY_S, 101 }, // Ctrl+S  → ID 101
    { FVIRTKEY | FCONTROL | FSHIFT,  AX_KEY_S, 102 }, // Ctrl+Shift+S → ID 102
    { FVIRTKEY | FSHIFT,             AX_KEY_A, 103 }, // Shift+A → ID 103
    { FVIRTKEY,                      AX_KEY_F5, 104 }, // F5 (bare) → ID 104
};
#define NUM_TEST_ACCELS ((int)(sizeof(kTestAccels)/sizeof(kTestAccels[0])))

// Window proc that captures the last evCommand received.
static uint32_t g_last_cmd_wparam = 0;
static int      g_cmd_count       = 0;

static result_t accel_test_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
    (void)win; (void)lparam;
    if (msg == evCreate || msg == evDestroy) return 1;
    if (msg == evCommand) {
        g_last_cmd_wparam = wparam;
        g_cmd_count++;
        return 1;
    }
    return 0;
}

static void reset_cmd(void) {
    g_last_cmd_wparam = 0;
    g_cmd_count       = 0;
}

// Build a ui_event_t for a key-down event.
static ui_event_t make_keydown(uint16_t keyCode, uint16_t modflags) {
    ui_event_t e;
    memset(&e, 0, sizeof(e));
    e.message  = kEventKeyDown;
    e.keyCode  = keyCode;
    e.modflags = modflags;
    return e;
}

// Modifier bit values as used in evt->modflags (already right-shifted by 16).
#define MOD_SHIFT  ((uint16_t)(AX_MOD_SHIFT >> 16))
#ifdef __APPLE__
#  define MOD_CTRL ((uint16_t)(AX_MOD_CMD   >> 16))
#else
#  define MOD_CTRL ((uint16_t)(AX_MOD_CTRL  >> 16))
#endif
#define MOD_ALT    ((uint16_t)(AX_MOD_ALT   >> 16))

// ── load_accelerators / free_accelerators ────────────────────────────────

void test_load_accelerators_valid(void) {
    TEST("load_accelerators: valid table is created and has correct count");

    accel_table_t *tbl = load_accelerators(kTestAccels, NUM_TEST_ACCELS);
    ASSERT_NOT_NULL(tbl);
    free_accelerators(tbl);
    PASS();
}

void test_load_accelerators_null_entries(void) {
    TEST("load_accelerators: NULL entries returns NULL");
    accel_table_t *tbl = load_accelerators(NULL, 5);
    ASSERT_NULL(tbl);
    PASS();
}

void test_load_accelerators_zero_count(void) {
    TEST("load_accelerators: count<=0 returns NULL");
    accel_table_t *tbl = load_accelerators(kTestAccels, 0);
    ASSERT_NULL(tbl);
    tbl = load_accelerators(kTestAccels, -1);
    ASSERT_NULL(tbl);
    PASS();
}

void test_free_accelerators_null(void) {
    TEST("free_accelerators: NULL table does not crash");
    free_accelerators(NULL);
    PASS();
}

// ── accel_find_cmd ────────────────────────────────────────────────────────

void test_accel_find_cmd_found(void) {
    TEST("accel_find_cmd: returns correct entry when cmd exists");

    accel_table_t *tbl = load_accelerators(kTestAccels, NUM_TEST_ACCELS);
    ASSERT_NOT_NULL(tbl);

    const accel_t *a = accel_find_cmd(tbl, 101);
    ASSERT_NOT_NULL(a);
    ASSERT_EQUAL((int)a->cmd, 101);
    ASSERT_EQUAL((int)a->key, (int)AX_KEY_S);

    free_accelerators(tbl);
    PASS();
}

void test_accel_find_cmd_not_found(void) {
    TEST("accel_find_cmd: returns NULL when cmd does not exist");

    accel_table_t *tbl = load_accelerators(kTestAccels, NUM_TEST_ACCELS);
    ASSERT_NOT_NULL(tbl);

    const accel_t *a = accel_find_cmd(tbl, 999);
    ASSERT_NULL(a);

    free_accelerators(tbl);
    PASS();
}

void test_accel_find_cmd_null_table(void) {
    TEST("accel_find_cmd: NULL table returns NULL");
    const accel_t *a = accel_find_cmd(NULL, 101);
    ASSERT_NULL(a);
    PASS();
}

// ── accel_format ─────────────────────────────────────────────────────────

void test_accel_format_null_args(void) {
    TEST("accel_format: NULL accel or buffer returns 0");
    char buf[32];
    ASSERT_EQUAL(accel_format(NULL, buf, sizeof(buf)), 0);
    ASSERT_EQUAL(accel_format(&kTestAccels[0], NULL, 32), 0);
    ASSERT_EQUAL(accel_format(&kTestAccels[0], buf, 0), 0);
    PASS();
}

void test_accel_format_ctrl_s(void) {
    TEST("accel_format: Ctrl+S produces 'Ctrl+' prefix");
    char buf[64] = {0};
    int n = accel_format(&kTestAccels[0], buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_NOT_NULL(strstr(buf, "Ctrl+"));
    PASS();
}

void test_accel_format_ctrl_shift_s(void) {
    TEST("accel_format: Ctrl+Shift+S produces both prefixes");
    char buf[64] = {0};
    int n = accel_format(&kTestAccels[1], buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_NOT_NULL(strstr(buf, "Ctrl+"));
    ASSERT_NOT_NULL(strstr(buf, "Shift+"));
    PASS();
}

void test_accel_format_shift_a(void) {
    TEST("accel_format: Shift+A produces 'Shift+' prefix, no 'Ctrl+'");
    char buf[64] = {0};
    int n = accel_format(&kTestAccels[2], buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_NOT_NULL(strstr(buf, "Shift+"));
    ASSERT_NULL(strstr(buf, "Ctrl+"));
    PASS();
}

void test_accel_format_bare_key(void) {
    TEST("accel_format: bare key (no modifiers) has no prefix");
    char buf[64] = {0};
    // F5: FVIRTKEY only, no FCONTROL/FSHIFT/FALT
    int n = accel_format(&kTestAccels[3], buf, sizeof(buf));
    ASSERT_TRUE(n > 0);
    ASSERT_NULL(strstr(buf, "Ctrl+"));
    ASSERT_NULL(strstr(buf, "Shift+"));
    ASSERT_NULL(strstr(buf, "Alt+"));
    PASS();
}

// ── translate_accelerator ─────────────────────────────────────────────────

void test_translate_accel_null_args(void) {
    TEST("translate_accelerator: NULL args return false");

    test_env_init();
    window_t *win = test_env_create_window("W", 0, 0, 100, 100,
                                            accel_test_proc, NULL);
    ASSERT_NOT_NULL(win);

    accel_table_t *tbl = load_accelerators(kTestAccels, NUM_TEST_ACCELS);
    ASSERT_NOT_NULL(tbl);

    ui_event_t e = make_keydown(AX_KEY_S, MOD_CTRL);

    ASSERT_FALSE(translate_accelerator(NULL, &e, tbl));
    ASSERT_FALSE(translate_accelerator(win,  NULL, tbl));
    ASSERT_FALSE(translate_accelerator(win,  &e,   NULL));

    free_accelerators(tbl);
    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_translate_accel_non_keydown(void) {
    TEST("translate_accelerator: non-keydown event returns false");

    test_env_init();
    reset_cmd();
    window_t *win = test_env_create_window("W", 0, 0, 100, 100,
                                            accel_test_proc, NULL);
    ASSERT_NOT_NULL(win);

    accel_table_t *tbl = load_accelerators(kTestAccels, NUM_TEST_ACCELS);
    ASSERT_NOT_NULL(tbl);

    ui_event_t e;
    memset(&e, 0, sizeof(e));
    e.message = 0xDEAD; // not kEventKeyDown

    bool result = translate_accelerator(win, &e, tbl);
    ASSERT_FALSE(result);
    ASSERT_EQUAL(g_cmd_count, 0);

    free_accelerators(tbl);
    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_translate_accel_no_match(void) {
    TEST("translate_accelerator: unregistered key returns false and sends no command");

    test_env_init();
    reset_cmd();
    window_t *win = test_env_create_window("W", 0, 0, 100, 100,
                                            accel_test_proc, NULL);
    ASSERT_NOT_NULL(win);

    accel_table_t *tbl = load_accelerators(kTestAccels, NUM_TEST_ACCELS);
    ASSERT_NOT_NULL(tbl);

    // AX_KEY_Z with Ctrl — not in the table
    ui_event_t e = make_keydown(AX_KEY_Z, MOD_CTRL);
    bool result = translate_accelerator(win, &e, tbl);
    ASSERT_FALSE(result);
    ASSERT_EQUAL(g_cmd_count, 0);

    free_accelerators(tbl);
    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_translate_accel_ctrl_s_match(void) {
    TEST("translate_accelerator: Ctrl+S fires evCommand(101, kAcceleratorNotification)");

    test_env_init();
    reset_cmd();
    window_t *win = test_env_create_window("W", 0, 0, 100, 100,
                                            accel_test_proc, NULL);
    ASSERT_NOT_NULL(win);

    accel_table_t *tbl = load_accelerators(kTestAccels, NUM_TEST_ACCELS);
    ASSERT_NOT_NULL(tbl);

    ui_event_t e = make_keydown(AX_KEY_S, MOD_CTRL);
    bool result = translate_accelerator(win, &e, tbl);
    ASSERT_TRUE(result);
    ASSERT_EQUAL(g_cmd_count, 1);
    ASSERT_EQUAL((int)LOWORD(g_last_cmd_wparam), 101);
    ASSERT_EQUAL((int)HIWORD(g_last_cmd_wparam), kAcceleratorNotification);

    free_accelerators(tbl);
    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_translate_accel_lowercase_normalized(void) {
    TEST("translate_accelerator: lowercase 's' key is normalized to uppercase S");

    test_env_init();
    reset_cmd();
    window_t *win = test_env_create_window("W", 0, 0, 100, 100,
                                            accel_test_proc, NULL);
    ASSERT_NOT_NULL(win);

    accel_table_t *tbl = load_accelerators(kTestAccels, NUM_TEST_ACCELS);
    ASSERT_NOT_NULL(tbl);

    // 's' = 115 = AX_KEY_S + 32; translate_accelerator must normalize it
    ui_event_t e = make_keydown((uint16_t)'s', MOD_CTRL);
    bool result = translate_accelerator(win, &e, tbl);
    ASSERT_TRUE(result);
    ASSERT_EQUAL((int)LOWORD(g_last_cmd_wparam), 101);

    free_accelerators(tbl);
    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_translate_accel_modifier_mismatch(void) {
    TEST("translate_accelerator: wrong modifier does not fire Ctrl+S entry");

    test_env_init();
    reset_cmd();
    window_t *win = test_env_create_window("W", 0, 0, 100, 100,
                                            accel_test_proc, NULL);
    ASSERT_NOT_NULL(win);

    accel_table_t *tbl = load_accelerators(kTestAccels, NUM_TEST_ACCELS);
    ASSERT_NOT_NULL(tbl);

    // S with no modifiers — requires Ctrl
    ui_event_t e = make_keydown(AX_KEY_S, 0);
    bool result = translate_accelerator(win, &e, tbl);
    ASSERT_FALSE(result);
    ASSERT_EQUAL(g_cmd_count, 0);

    free_accelerators(tbl);
    destroy_window(win);
    test_env_shutdown();
    PASS();
}

// ── main ──────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    TEST_START("user/accel.c accelerator table");

    test_load_accelerators_valid();
    test_load_accelerators_null_entries();
    test_load_accelerators_zero_count();
    test_free_accelerators_null();

    test_accel_find_cmd_found();
    test_accel_find_cmd_not_found();
    test_accel_find_cmd_null_table();

    test_accel_format_null_args();
    test_accel_format_ctrl_s();
    test_accel_format_ctrl_shift_s();
    test_accel_format_shift_a();
    test_accel_format_bare_key();

    test_translate_accel_null_args();
    test_translate_accel_non_keydown();
    test_translate_accel_no_match();
    test_translate_accel_ctrl_s_match();
    test_translate_accel_lowercase_normalized();
    test_translate_accel_modifier_mismatch();

    TEST_END();
}

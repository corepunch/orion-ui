// Theme regression tests — issue #216
// Covers: palette round-trip, invalid-style safety, same-theme noop,
// reentrant-handler noop, scrollbar gutter policy, and button-state safety.

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/user/theme.h>
#include <orion/user/messages.h>

// ── Helpers ───────────────────────────────────────────────────────────────── //

// Ensure the active theme is THEME_CLASSIC regardless of previous test state.
// Uses set_theme() directly; if Classic is already active this is a no-op
// (returns false), which is fine — the palette is already correct.
static void force_classic(void) {
    // If g_active_theme is already Classic set_theme returns false; the
    // palette is already the Classic one, so the result is still correct.
    set_theme(THEME_CLASSIC);
    // Belt-and-suspenders: if the above was a noop (already Classic) the
    // next assertion in the caller will still hold because Classic's palette
    // was applied either at startup or the last time we switched.
}

static void force_modern(void) {
    set_theme(THEME_MODERN);
}

// Classic brControlBg value from theme_classic.c::classic_apply_palette().
#define CLASSIC_CONTROL_BG  0xff3c3c3cu
// Modern brControlBg value from theme_modern.c::modern_apply_palette().
#define MODERN_CONTROL_BG   0xffF3F3F3u

// ── Test: palette_round_trip ──────────────────────────────────────────────── //

void test_palette_round_trip(void) {
    TEST("theme: Classic→Modern→Classic round-trip changes and restores brControlBg");

    test_env_init();

    // Guarantee a clean Classic baseline.
    // To make set_theme(THEME_CLASSIC) actually fire the apply_palette we must
    // first be on Modern (so the same-theme guard doesn't block it).
    force_modern();
    force_classic();

    uint32_t baseline = g_sys_colors[brControlBg];
    ASSERT_EQUAL((unsigned)baseline, (unsigned)CLASSIC_CONTROL_BG);

    // Switch to Modern — palette must change.
    bool ok = set_theme(THEME_MODERN);
    ASSERT_TRUE(ok);
    ASSERT_NOT_EQUAL((unsigned)g_sys_colors[brControlBg], (unsigned)CLASSIC_CONTROL_BG);
    ASSERT_EQUAL((unsigned)g_sys_colors[brControlBg], (unsigned)MODERN_CONTROL_BG);

    // Switch back to Classic — palette must match the original baseline.
    ok = set_theme(THEME_CLASSIC);
    ASSERT_TRUE(ok);
    ASSERT_EQUAL((unsigned)g_sys_colors[brControlBg], (unsigned)baseline);

    test_env_shutdown();
    PASS();
}

// ── Test: null_theme_rejected ─────────────────────────────────────────────── //
//
// set_theme() takes a theme_style_t enum, not a pointer, so the "NULL pointer"
// concept maps to casting NULL (0) to the enum.  0 == THEME_CLASSIC; any
// unknown out-of-range value also falls through to the Classic branch via the
// ternary in theme.c.  The contract: no crash, get_theme() remains non-NULL.

void test_null_theme_rejected(void) {
    TEST("theme: out-of-range style value doesn't crash and leaves theme valid");

    test_env_init();

    // Start from Modern so any Classic-producing style value causes a real switch.
    force_modern();
    theme_t *before = get_theme();
    ASSERT_NOT_NULL(before);

    // 0xFF is not a defined theme_style_t — the ternary resolves it to Classic.
    // We expect no crash.  If Classic was active it would be a noop; since we
    // started from Modern the switch succeeds.
    (void)set_theme((theme_style_t)0xFF);

    theme_t *after = get_theme();
    ASSERT_NOT_NULL(after);

    test_env_shutdown();
    PASS();
}

// ── Test: same_theme_noop ─────────────────────────────────────────────────── //

void test_same_theme_noop(void) {
    TEST("theme: set_theme with same style returns false without re-applying palette");

    test_env_init();

    // Establish Classic.
    force_modern();
    force_classic();

    // Manually corrupt one color to detect whether apply_palette is called.
    uint32_t sentinel = 0xDEADBEEFu;
    g_sys_colors[brControlBg] = sentinel;

    // Calling set_theme(THEME_CLASSIC) while Classic is already active must be
    // a no-op: same-theme guard fires, returns false, palette NOT re-applied.
    bool result = set_theme(THEME_CLASSIC);
    ASSERT_FALSE(result);

    // The sentinel must survive — apply_palette was NOT called.
    ASSERT_EQUAL((unsigned)g_sys_colors[brControlBg], (unsigned)sentinel);

    // Restore the real palette before leaving.
    force_modern();
    force_classic();

    test_env_shutdown();
    PASS();
}

// ── Test: reentrant_switch_rejected ──────────────────────────────────────── //
//
// A window proc that handles evThemeChanged by calling set_theme() with the
// same style that was just applied must get a false return (same-theme guard).
// The UI remains stable — no crash, no inconsistent theme pointer.
//
// Note: set_theme() broadcasts evThemeChanged via post_message (queued), so
// s_switching is already false by the time the message is delivered.  The
// "reentrant" scenario exercised here is therefore the practical case: a
// window proc calls set_theme(same-style) in response to evThemeChanged and
// the call is correctly rejected by the same-theme noop guard.

static bool g_reentrant_set_theme_result = true;  // sentinel: starts "true"
static theme_style_t g_reentrant_called_with = 99; // sentinel: starts unknown

static result_t reentrant_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
    (void)win; (void)lparam;
    if (msg == evCreate || msg == evDestroy) return 1;
    if (msg == evThemeChanged) {
        // Attempt to switch to the same theme that was just applied.
        // Should hit the same-theme noop guard and return false.
        theme_style_t incoming = (theme_style_t)wparam;
        g_reentrant_called_with = incoming;
        g_reentrant_set_theme_result = set_theme(incoming);
    }
    return 0;
}

void test_reentrant_switch_rejected(void) {
    TEST("theme: set_theme(same-style) from evThemeChanged handler returns false");

    test_env_init();

    // Establish Modern so we can observe the evThemeChanged round-trip.
    force_classic();
    force_modern();

    window_t *win = test_env_create_window("ThemeTestWin", 0, 0, 100, 100,
                                           reentrant_proc, NULL);
    ASSERT_NOT_NULL(win);

    // Reset sentinels before the probe.
    g_reentrant_set_theme_result = true;
    g_reentrant_called_with = 99;

    // Simulate evThemeChanged delivery (as if broadcast by set_theme).
    // send_message delivers synchronously so the handler runs before we check.
    send_message(win, evThemeChanged, (uint32_t)THEME_MODERN, NULL);

    // Handler must have been invoked.
    ASSERT_EQUAL((int)g_reentrant_called_with, (int)THEME_MODERN);

    // And the same-theme guard must have returned false.
    ASSERT_FALSE(g_reentrant_set_theme_result);

    // Active theme must still be Modern — no corruption.
    ASSERT_EQUAL((int)get_theme()->style, (int)THEME_MODERN);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

// ── Test: scrollbar_gutter_zero_when_overlay ─────────────────────────────── //

void test_scrollbar_gutter_zero_when_overlay(void) {
    TEST("theme: Modern gives 0 scrollbar gutter; Classic gives SCROLLBAR_WIDTH");

    test_env_init();

    force_modern();
    theme_t *modern = get_theme();
    ASSERT_NOT_NULL(modern);
    ASSERT_EQUAL((int)modern->style, (int)THEME_MODERN);
    ASSERT_TRUE(modern->scrollbar_overlay);
    ASSERT_EQUAL(modern->scrollbar_width, 0);

    force_classic();
    theme_t *classic = get_theme();
    ASSERT_NOT_NULL(classic);
    ASSERT_EQUAL((int)classic->style, (int)THEME_CLASSIC);
    ASSERT_FALSE(classic->scrollbar_overlay);
    ASSERT_EQUAL(classic->scrollbar_width, SCROLLBAR_WIDTH);

    test_env_shutdown();
    PASS();
}

// ── Test: button_states_no_crash ─────────────────────────────────────────── //
//
// Call draw_button_bg for every ctrl_state_t bitmask combination (0..63).
// fill_rect returns early when g_ui_runtime.running==false (headless), so
// no GL calls are made; the test verifies only that no crash or assert fires.

static void exercise_button_states(theme_t *theme) {
    irect16_t r = {5, 5, 60, 20};
    // CTRL_DEFAULT is 1<<5 == 32; all combinations 0..63 cover every pair.
    for (int s = 0; s <= 63; s++) {
        theme->draw_button_bg(r, (ctrl_state_t)s);
    }
}

void test_button_states_no_crash(void) {
    TEST("theme: draw_button_bg with all ctrl_state_t combinations (0..63) doesn't crash");

    test_env_init();

    // Both themes must survive all flag combinations.
    force_classic();
    exercise_button_states(get_theme());

    force_modern();
    exercise_button_states(get_theme());

    test_env_shutdown();
    PASS();
}

// ── main ─────────────────────────────────────────────────────────────────── //

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    TEST_START("theme regression tests");

    test_palette_round_trip();
    test_null_theme_rejected();
    test_same_theme_noop();
    test_reentrant_switch_rejected();
    test_scrollbar_gutter_zero_when_overlay();
    test_button_states_no_crash();

    TEST_END();
}

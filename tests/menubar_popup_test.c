// Menubar Popup Tests
// Tests for the popup dismissal behavior introduced in the menu-fix PR:
//   1. Popup opens when a top-level label is clicked.
//   2. Popup closes immediately when a click lands outside its bounds.
//   3. Popup is destroyed BEFORE the evCommand notification is
//      dispatched, so that e.g. a modal dialog opened from a menu item
//      does not render on top of the still-visible popup.

#include "test_framework.h"
#include "test_env.h"
#include "../ui.h"
#include "../commctl/menubar.h"

// Count the number of top-level (non-child) windows.
static int count_windows(void) {
    int n = 0;
    for (window_t *w = g_ui_runtime.windows; w; w = w->next) n++;
    return n;
}

// Return the first top-level window that is NOT `exclude`.
static window_t *find_other_window(window_t *exclude) {
    for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
        if (w != exclude) return w;
    }
    return NULL;
}

static window_t *find_window_not(window_t *a, window_t *b) {
    for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
        if (w != a && w != b) return w;
    }
    return NULL;
}

// ---- shared test state ------------------------------------------------------

static int   g_cmd_count         = 0;
static uint16_t g_cmd_last_id    = 0;
// Used in the "closes before command" test to verify ordering.
static window_t *g_popup_ptr     = NULL;
static bool  g_popup_alive_at_cmd = false;

static void reset_counters(void) {
    g_cmd_count         = 0;
    g_cmd_last_id       = 0;
    g_popup_ptr         = NULL;
    g_popup_alive_at_cmd = false;
}

// ---- menu-bar window procs --------------------------------------------------

// Basic proc: just counts received item-click commands.
static result_t menubar_proc_basic(window_t *win, uint32_t msg,
                                   uint32_t wparam, void *lparam) {
    if (msg == evCommand &&
        HIWORD(wparam) == kMenuBarNotificationItemClick) {
        g_cmd_count++;
        g_cmd_last_id = LOWORD(wparam);
        return 1;
    }
    return win_menubar(win, msg, wparam, lparam);
}

// Proc that additionally checks whether the popup is still alive the moment
// the command arrives.
static result_t menubar_proc_order_check(window_t *win, uint32_t msg,
                                         uint32_t wparam, void *lparam) {
    if (msg == evCommand &&
        HIWORD(wparam) == kMenuBarNotificationItemClick) {
        g_cmd_count++;
        g_cmd_last_id = LOWORD(wparam);
        // The popup should already be destroyed at this point.
        if (g_popup_ptr)
            g_popup_alive_at_cmd = is_window(g_popup_ptr);
        return 1;
    }
    return win_menubar(win, msg, wparam, lparam);
}

// ---- menu definition used by all tests --------------------------------------

static const menu_item_t kSubItems[] = {
    {"Advanced", 4, NULL, 0},
};

static const menu_item_t kBlurItems[] = {
    {"Gaussian", 11, NULL, 0},
};

static const menu_item_t kSharpenItems[] = {
    {"Strong", 12, NULL, 0},
};

static const menu_item_t kTestItems[] = {
    {"Open", 1, NULL, 0},
    {"Save", 2, NULL, 0},
    {NULL,   0, NULL, 0},   // separator
    {"Quit", 3, NULL, 0},
    {"More", 0, kSubItems, (int)(sizeof(kSubItems)/sizeof(kSubItems[0]))},
};

static const menu_def_t kTestMenus[] = {
    {"File", kTestItems, (int)(sizeof(kTestItems)/sizeof(kTestItems[0]))},
};

static const menu_item_t kFilterItems[] = {
    {"Blur", 0, kBlurItems, (int)(sizeof(kBlurItems)/sizeof(kBlurItems[0]))},
    {"Sharpen", 0, kSharpenItems, (int)(sizeof(kSharpenItems)/sizeof(kSharpenItems[0]))},
};

static const menu_def_t kFilterMenus[] = {
    {"Filter", kFilterItems, (int)(sizeof(kFilterItems)/sizeof(kFilterItems[0]))},
};

// Helper: create a configured menu-bar window.
static window_t *make_menubar(winproc_t proc) {
    window_t *mb = test_env_create_window("mb", 0, 0, 400, 12, proc, NULL);
    if (!mb) return NULL;
    send_message(mb, kMenuBarMessageSetMenus,
                 (uint32_t)(sizeof(kTestMenus)/sizeof(kTestMenus[0])),
                 (void *)kTestMenus);
    return mb;
}

static window_t *make_filter_menubar(winproc_t proc) {
    window_t *mb = test_env_create_window("mb", 0, 0, 400, 12, proc, NULL);
    if (!mb) return NULL;
    send_message(mb, kMenuBarMessageSetMenus,
                 (uint32_t)(sizeof(kFilterMenus)/sizeof(kFilterMenus[0])),
                 (void *)kFilterMenus);
    return mb;
}

// Helper: open the popup by simulating a click on the first label.
// menu_x[0] = 4, label_w = MENU_LABEL_PAD (12) when font not initialised.
// Clicking at (x=6, y=4) lands inside the label hit-region [2, 14).
static void open_popup(window_t *mb) {
    send_message(mb, evLeftButtonDown, MAKEDWORD(6, 4), NULL);
}

// ---- tests ------------------------------------------------------------------

void test_popup_opens_on_label_click(void) {
    TEST("Menubar popup opens when a label is clicked");

    test_env_init();
    reset_counters();

    window_t *mb = make_menubar(menubar_proc_basic);
    ASSERT_NOT_NULL(mb);
    ASSERT_EQUAL(count_windows(), 1);

    open_popup(mb);

    // A second top-level window (the popup) should now exist.
    ASSERT_EQUAL(count_windows(), 2);
    // The popup should have grabbed mouse capture.
    ASSERT_NOT_NULL(g_ui_runtime.captured);

    destroy_window(mb);
    test_env_shutdown();
    PASS();
}

void test_popup_closes_on_outside_click(void) {
    TEST("Menubar popup closes when clicking outside its bounds");

    test_env_init();
    reset_counters();

    window_t *mb = make_menubar(menubar_proc_basic);
    ASSERT_NOT_NULL(mb);

    open_popup(mb);
    ASSERT_EQUAL(count_windows(), 2);

    window_t *popup = find_other_window(mb);
    ASSERT_NOT_NULL(popup);

    // Simulate click at coordinates outside the popup bounds.
    // MAKEDWORD casts its args to uint16_t, so passing -10 yields 0xFFF6,
    // which the popup proc reads back as int16_t -10 — outside [0, w).
    send_message(popup, evLeftButtonDown,
                 MAKEDWORD(-10, -10), NULL);

    // Popup must be gone and capture released.
    ASSERT_EQUAL(count_windows(), 1);
    ASSERT_NULL(g_ui_runtime.captured);

    destroy_window(mb);
    test_env_shutdown();
    PASS();
}

void test_popup_closes_before_command_on_item_click(void) {
    TEST("Menubar popup is destroyed before evCommand fires");

    test_env_init();
    reset_counters();

    window_t *mb = make_menubar(menubar_proc_order_check);
    ASSERT_NOT_NULL(mb);

    open_popup(mb);
    ASSERT_EQUAL(count_windows(), 2);

    window_t *popup = find_other_window(mb);
    ASSERT_NOT_NULL(popup);
    g_popup_ptr = popup; // save so the command handler can check it

    // Press on the first item "Open" (id=1): highlights item, does NOT fire yet.
    // Items start at y=2; first item occupies y in [2, 14) (MENU_ITEM_H=12).
    // x=10 is inside popup width (MENU_MIN_W=90).
    send_message(popup, evLeftButtonDown,
                 MAKEDWORD(10, 5), NULL);

    // Popup must still be open after mouse-down (action fires on mouse-up).
    ASSERT_EQUAL(count_windows(), 2);
    ASSERT_TRUE(is_window(popup));
    ASSERT_EQUAL(g_cmd_count, 0);

    // Release on the same item – popup closes and command fires.
    send_message(popup, evLeftButtonUp,
                 MAKEDWORD(10, 5), NULL);

    // The popup must have been destroyed.
    ASSERT_EQUAL(count_windows(), 1);
    ASSERT_FALSE(is_window(popup));

    // The command must have been delivered.
    ASSERT_EQUAL(g_cmd_count, 1);
    ASSERT_EQUAL(g_cmd_last_id, 1);

    // The popup was already gone when the command arrived.
    ASSERT_FALSE(g_popup_alive_at_cmd);

    destroy_window(mb);
    test_env_shutdown();
    PASS();
}

void test_popup_cancel_on_different_item_release(void) {
    TEST("Menubar popup cancels when mouse released on different item");

    test_env_init();
    reset_counters();

    window_t *mb = make_menubar(menubar_proc_basic);
    ASSERT_NOT_NULL(mb);

    open_popup(mb);
    window_t *popup = find_other_window(mb);
    ASSERT_NOT_NULL(popup);

    // Press on "Open" (y=5, item index 0).
    send_message(popup, evLeftButtonDown, MAKEDWORD(10, 5), NULL);
    ASSERT_EQUAL(count_windows(), 2);   // popup still open

    // Release on "Save" (y=22, item index 1) – different item, must cancel.
    // MENU_ITEM_H=TITLEBAR_HEIGHT=17, MENU_START_Y=1:
    //   "Open" [1,18), "Save" [18,35) — y=22 is inside "Save".
    send_message(popup, evLeftButtonUp, MAKEDWORD(10, 22), NULL);
    ASSERT_EQUAL(g_cmd_count, 0);       // no command fired
    ASSERT_EQUAL(count_windows(), 1);   // popup closed

    destroy_window(mb);
    test_env_shutdown();
    PASS();
}

void test_popup_command_delivered_for_each_item(void) {
    TEST("Menubar delivers correct item id for each item click");

    test_env_init();
    reset_counters();

    window_t *mb = make_menubar(menubar_proc_basic);
    ASSERT_NOT_NULL(mb);

    // Press and release on "Save" (id=2).
    // MENU_ITEM_H=TITLEBAR_HEIGHT=17, MENU_START_Y=1:
    // "Open" occupies y in [1, 18), "Save" in [18, 35).
    open_popup(mb);
    window_t *popup = find_other_window(mb);
    ASSERT_NOT_NULL(popup);

    send_message(popup, evLeftButtonDown, MAKEDWORD(10, 22), NULL);
    send_message(popup, evLeftButtonUp,   MAKEDWORD(10, 22), NULL);

    ASSERT_EQUAL(g_cmd_count, 1);
    ASSERT_EQUAL(g_cmd_last_id, 2);
    ASSERT_EQUAL(count_windows(), 1); // popup is gone

    // Now open again and press/release on "Quit" (id=3).
    // "Open" [1,18), "Save" [18,35), sep [35,40), "Quit" [40,57)
    open_popup(mb);
    popup = find_other_window(mb);
    ASSERT_NOT_NULL(popup);

    send_message(popup, evLeftButtonDown, MAKEDWORD(10, 43), NULL);
    send_message(popup, evLeftButtonUp,   MAKEDWORD(10, 43), NULL);

    ASSERT_EQUAL(g_cmd_count, 2);
    ASSERT_EQUAL(g_cmd_last_id, 3);

    destroy_window(mb);
    test_env_shutdown();
    PASS();
}

void test_nonclient_buttonup_closes_popup(void) {
    TEST("Menubar popup closes on evNCLeftButtonUp");

    test_env_init();
    reset_counters();

    window_t *mb = make_menubar(menubar_proc_basic);
    ASSERT_NOT_NULL(mb);

    open_popup(mb);
    ASSERT_EQUAL(count_windows(), 2);

    window_t *popup = find_other_window(mb);
    ASSERT_NOT_NULL(popup);

    send_message(popup, evNCLeftButtonUp,
                 MAKEDWORD(0, 0), NULL);

    ASSERT_EQUAL(count_windows(), 1);
    ASSERT_NULL(g_ui_runtime.captured);

    destroy_window(mb);
    test_env_shutdown();
    PASS();
}

void test_popup_no_action_on_mouseup_without_press(void) {
    TEST("Menubar popup ignores mouse-up when no item was pressed first");

    test_env_init();
    reset_counters();

    window_t *mb = make_menubar(menubar_proc_basic);
    ASSERT_NOT_NULL(mb);

    open_popup(mb);
    ASSERT_EQUAL(count_windows(), 2);

    window_t *popup = find_other_window(mb);
    ASSERT_NOT_NULL(popup);

    // Send mouse-up on a valid item without a prior mouse-down inside popup.
    // This simulates the case where the popup opens from a menubar click
    // and the mouse-up of that click arrives in the popup.
    send_message(popup, evLeftButtonUp, MAKEDWORD(10, 5), NULL);

    // Popup must still be open (no action, no close).
    ASSERT_EQUAL(count_windows(), 2);
    ASSERT_EQUAL(g_cmd_count, 0);

    // Clean up.
    destroy_window(popup);
    destroy_window(mb);
    test_env_shutdown();
    PASS();
}

void test_submenu_opens_and_dispatches_child_item(void) {
    TEST("Menubar submenu opens and dispatches child item");

    test_env_init();
    reset_counters();

    window_t *mb = make_menubar(menubar_proc_basic);
    ASSERT_NOT_NULL(mb);

    open_popup(mb);
    ASSERT_EQUAL(count_windows(), 2);

    window_t *popup = find_other_window(mb);
    ASSERT_NOT_NULL(popup);

    // "More" is after Open, Save, separator, Quit:
    // [1,18), [18,35), [35,40), [40,57), so y=62 is inside More.
    send_message(popup, evMouseMove, MAKEDWORD(10, 62), NULL);
    ASSERT_EQUAL(count_windows(), 3);

    window_t *submenu = find_window_not(mb, popup);
    ASSERT_NOT_NULL(submenu);

    send_message(submenu, evLeftButtonDown, MAKEDWORD(10, 5), NULL);
    send_message(submenu, evLeftButtonUp, MAKEDWORD(10, 5), NULL);

    ASSERT_EQUAL(g_cmd_count, 1);
    ASSERT_EQUAL(g_cmd_last_id, 4);
    ASSERT_EQUAL(count_windows(), 1);

    destroy_window(mb);
    test_env_shutdown();
    PASS();
}

void test_submenu_click_transfers_to_sibling_item(void) {
    TEST("Menubar submenu switches to sibling submenu on click");

    test_env_init();
    reset_counters();

    window_t *mb = make_filter_menubar(menubar_proc_basic);
    ASSERT_NOT_NULL(mb);

    open_popup(mb);
    ASSERT_EQUAL(count_windows(), 2);

    window_t *popup = find_other_window(mb);
    ASSERT_NOT_NULL(popup);

    // Click "Blur" to open its submenu.
    send_message(popup, evLeftButtonDown, MAKEDWORD(10, 5), NULL);
    ASSERT_EQUAL(count_windows(), 3);

    window_t *blur_submenu = find_window_not(mb, popup);
    ASSERT_NOT_NULL(blur_submenu);

    // While the Blur submenu is open, click the sibling "Sharpen" item on the
    // parent popup.  The child submenu should forward the click to its parent
    // so the menu switches instead of collapsing the whole tree.
    int sharpen_x = popup->frame.x + 10 - blur_submenu->frame.x;
    int sharpen_y = popup->frame.y + 22 - blur_submenu->frame.y;
    send_message(blur_submenu, evLeftButtonDown,
                 MAKEDWORD(sharpen_x, sharpen_y), NULL);

    ASSERT_EQUAL(count_windows(), 3);

    window_t *sharpen_submenu = find_window_not(mb, popup);
    ASSERT_NOT_NULL(sharpen_submenu);

    // Click the child item under Sharpen to confirm the switched submenu is active.
    send_message(sharpen_submenu, evLeftButtonDown, MAKEDWORD(10, 5), NULL);
    send_message(sharpen_submenu, evLeftButtonUp, MAKEDWORD(10, 5), NULL);

    ASSERT_EQUAL(g_cmd_count, 1);
    ASSERT_EQUAL(g_cmd_last_id, 12);
    ASSERT_EQUAL(count_windows(), 1);

    destroy_window(mb);
    test_env_shutdown();
    PASS();
}

// ---- main -------------------------------------------------------------------

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    TEST_START("Menubar Popup Dismissal");

    test_popup_opens_on_label_click();
    test_popup_closes_on_outside_click();
    test_popup_closes_before_command_on_item_click();
    test_popup_cancel_on_different_item_release();
    test_popup_command_delivered_for_each_item();
    test_nonclient_buttonup_closes_popup();
    test_popup_no_action_on_mouseup_without_press();
    test_submenu_opens_and_dispatches_child_item();
    test_submenu_click_transfers_to_sibling_item();

    TEST_END();
}

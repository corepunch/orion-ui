// Tests for the child-window-based toolbar implementation.
//
// These tests are headless (no SDL/OpenGL rendering). The new toolbar
// creates real child windows in parent->toolbar_children instead of
// maintaining a flat toolbar_button_t[] array.

#include "test_framework.h"
#include "test_env.h"
#include "../ui.h"
#include "../commctl/commctl.h"

// ---- helpers ----------------------------------------------------------------

static result_t noop_proc(window_t *win, uint32_t msg,
                           uint32_t wparam, void *lparam) {
    (void)win; (void)wparam; (void)lparam;
    if (msg == kWindowMessageCreate || msg == kWindowMessageDestroy) return 1;
    return 0;
}

// Window proc that records the last kToolBarMessageButtonClick ident.
static int g_last_click_ident = -1;
static int g_click_count = 0;
static result_t click_capture_proc(window_t *win, uint32_t msg,
                                    uint32_t wparam, void *lparam) {
    (void)win; (void)lparam;
    if (msg == kWindowMessageCreate || msg == kWindowMessageDestroy) return 1;
    if (msg == kToolBarMessageButtonClick) {
        g_last_click_ident = (int)wparam;
        g_click_count++;
        return 1;
    }
    return 0;
}

// Count the children in a window's toolbar_children list.
static int count_toolbar_children(window_t *win) {
    int n = 0;
    for (window_t *tc = win->toolbar_children; tc; tc = tc->next) n++;
    return n;
}

// Find a toolbar child by id.
static window_t *find_toolbar_child(window_t *win, uint32_t id) {
    for (window_t *tc = win->toolbar_children; tc; tc = tc->next)
        if (tc->id == id) return tc;
    return NULL;
}

// ---- tests ------------------------------------------------------------------

void test_toolbar_add_buttons_creates_children(void) {
    TEST("kToolBarMessageAddButtons creates one toolbar child per real button");

    test_env_init();

    rect_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_button_t buttons[] = {
        {.icon=0, .ident=10, .flags=0},
        {.icon=1, .ident=11, .flags=0},
        {.icon=2, .ident=12, .flags=0},
    };
    send_message(win, kToolBarMessageAddButtons, 3, buttons);

    // Must have exactly 3 toolbar children.
    ASSERT_EQUAL(count_toolbar_children(win), 3);

    // IDs come from the ident field.
    ASSERT_NOT_NULL(find_toolbar_child(win, 10));
    ASSERT_NOT_NULL(find_toolbar_child(win, 11));
    ASSERT_NOT_NULL(find_toolbar_child(win, 12));

    // Children must NOT appear in the regular children list.
    ASSERT_NULL(win->children);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_spacing_token_skipped(void) {
    TEST("kToolBarMessageAddButtons: spacing tokens (icon==-1) do not create children");

    test_env_init();

    rect_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_button_t buttons[] = {
        {.icon=0, .ident=1, .flags=0},
        {.icon=1, .ident=2, .flags=0},
        TOOLBAR_SPACING_TOKEN,
        {.icon=2, .ident=3, .flags=0},
    };
    send_message(win, kToolBarMessageAddButtons, 4, buttons);

    // 4 entries, but the spacing token does not create a child.
    ASSERT_EQUAL(count_toolbar_children(win), 3);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_add_buttons_replaces(void) {
    TEST("kToolBarMessageAddButtons replaces existing toolbar children");

    test_env_init();

    rect_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_button_t first[] = {{.icon=0, .ident=1, .flags=0}};
    send_message(win, kToolBarMessageAddButtons, 1, first);
    ASSERT_EQUAL(count_toolbar_children(win), 1);

    toolbar_button_t second[] = {
        {.icon=0, .ident=10, .flags=0},
        {.icon=1, .ident=11, .flags=0},
    };
    send_message(win, kToolBarMessageAddButtons, 2, second);

    // Old children are gone, new ones are present.
    ASSERT_EQUAL(count_toolbar_children(win), 2);
    ASSERT_NULL(find_toolbar_child(win, 1));
    ASSERT_NOT_NULL(find_toolbar_child(win, 10));
    ASSERT_NOT_NULL(find_toolbar_child(win, 11));

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_set_active_button(void) {
    TEST("kToolBarMessageSetActiveButton sets value on correct child");

    test_env_init();

    rect_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_button_t buttons[] = {
        {.icon=0, .ident=10, .flags=TOOLBAR_BUTTON_FLAG_ACTIVE},
        {.icon=1, .ident=11, .flags=0},
        {.icon=2, .ident=12, .flags=0},
    };
    send_message(win, kToolBarMessageAddButtons, 3, buttons);

    window_t *btn10 = find_toolbar_child(win, 10);
    window_t *btn11 = find_toolbar_child(win, 11);
    window_t *btn12 = find_toolbar_child(win, 12);
    ASSERT_NOT_NULL(btn10);
    ASSERT_NOT_NULL(btn11);
    ASSERT_NOT_NULL(btn12);

    // After AddButtons, TOOLBAR_BUTTON_FLAG_ACTIVE → value==true on btn10.
    ASSERT_TRUE(btn10->value);
    ASSERT_FALSE(btn11->value);
    ASSERT_FALSE(btn12->value);

    // Activate ident 11.
    send_message(win, kToolBarMessageSetActiveButton, 11, NULL);

    ASSERT_FALSE(btn10->value);
    ASSERT_TRUE(btn11->value);
    ASSERT_FALSE(btn12->value);

    // Activate ident 12.
    send_message(win, kToolBarMessageSetActiveButton, 12, NULL);

    ASSERT_FALSE(btn10->value);
    ASSERT_FALSE(btn11->value);
    ASSERT_TRUE(btn12->value);

    // Unknown ident clears all.
    send_message(win, kToolBarMessageSetActiveButton, 99, NULL);

    ASSERT_FALSE(btn10->value);
    ASSERT_FALSE(btn11->value);
    ASSERT_FALSE(btn12->value);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_set_strip(void) {
    TEST("kToolBarMessageSetStrip stores strip in window");

    test_env_init();

    rect_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", 0, &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    ASSERT_EQUAL((int)win->toolbar_strip.tex, 0);

    bitmap_strip_t strip = {
        .tex=42, .icon_w=16, .icon_h=16,
        .cols=2, .sheet_w=32, .sheet_h=160,
    };
    send_message(win, kToolBarMessageSetStrip, 0, &strip);
    ASSERT_EQUAL((int)win->toolbar_strip.tex, 42);
    ASSERT_EQUAL(win->toolbar_strip.icon_w, 16);
    ASSERT_EQUAL(win->toolbar_strip.cols, 2);
    ASSERT_EQUAL(win->toolbar_strip.sheet_w, 32);
    ASSERT_EQUAL(win->toolbar_strip.sheet_h, 160);

    send_message(win, kToolBarMessageSetStrip, 0, NULL);
    ASSERT_EQUAL((int)win->toolbar_strip.tex, 0);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_set_items_button(void) {
    TEST("kToolBarMessageSetItems: TOOLBAR_ITEM_BUTTON creates button child");

    test_env_init();

    rect_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {
        {TOOLBAR_ITEM_BUTTON, 20, -1, 0, 0, "New"},
        {TOOLBAR_ITEM_BUTTON, 21, -1, 0, 0, "Open"},
    };
    send_message(win, kToolBarMessageSetItems, 2, items);

    ASSERT_EQUAL(count_toolbar_children(win), 2);
    ASSERT_NOT_NULL(find_toolbar_child(win, 20));
    ASSERT_NOT_NULL(find_toolbar_child(win, 21));

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_set_items_label(void) {
    TEST("kToolBarMessageSetItems: TOOLBAR_ITEM_LABEL creates label child");

    test_env_init();

    rect_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {
        {TOOLBAR_ITEM_LABEL, 30, -1, 40, 0, "Filter:"},
    };
    send_message(win, kToolBarMessageSetItems, 1, items);

    ASSERT_EQUAL(count_toolbar_children(win), 1);
    window_t *lbl = find_toolbar_child(win, 30);
    ASSERT_NOT_NULL(lbl);
    // Label text is stored in title.
    ASSERT_TRUE(strncmp(lbl->title, "Filter:", 7) == 0);
    // Explicit width was respected (with label auto-sizing in win_label).
    ASSERT_TRUE(lbl->frame.w >= 40);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_set_items_combobox(void) {
    TEST("kToolBarMessageSetItems: TOOLBAR_ITEM_COMBOBOX creates combobox child");

    test_env_init();

    rect_t frame = {0, 0, 300, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {
        {TOOLBAR_ITEM_COMBOBOX, 40, -1, 80, 0, NULL},
    };
    send_message(win, kToolBarMessageSetItems, 1, items);

    ASSERT_EQUAL(count_toolbar_children(win), 1);
    window_t *cb = find_toolbar_child(win, 40);
    ASSERT_NOT_NULL(cb);
    ASSERT_EQUAL(cb->frame.w, 80);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_set_items_separator(void) {
    TEST("kToolBarMessageSetItems: TOOLBAR_ITEM_SEPARATOR creates narrow child");

    test_env_init();

    rect_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {
        {TOOLBAR_ITEM_BUTTON,    1, -1, 0, 0, "A"},
        {TOOLBAR_ITEM_SEPARATOR, 0, -1, 0, 0, NULL},
        {TOOLBAR_ITEM_BUTTON,    2, -1, 0, 0, "B"},
    };
    send_message(win, kToolBarMessageSetItems, 3, items);

    // Separator creates a child; total = 3.
    ASSERT_EQUAL(count_toolbar_children(win), 3);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_set_items_spacer(void) {
    TEST("kToolBarMessageSetItems: TOOLBAR_ITEM_SPACER does NOT create a child");

    test_env_init();

    rect_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {
        {TOOLBAR_ITEM_BUTTON, 1, -1, 0, 0, "A"},
        {TOOLBAR_ITEM_SPACER, 0, -1, 8, 0, NULL},
        {TOOLBAR_ITEM_BUTTON, 2, -1, 0, 0, "B"},
    };
    send_message(win, kToolBarMessageSetItems, 3, items);

    // Spacer does not create a child; total = 2.
    ASSERT_EQUAL(count_toolbar_children(win), 2);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_button_click_fires_command(void) {
    TEST("Clicking a toolbar child fires kToolBarMessageButtonClick on parent");

    test_env_init();

    g_last_click_ident = -1;
    g_click_count = 0;

    rect_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, click_capture_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_button_t buttons[] = {{.icon=0, .ident=55, .flags=0}};
    send_message(win, kToolBarMessageAddButtons, 1, buttons);

    window_t *btn = find_toolbar_child(win, 55);
    ASSERT_NOT_NULL(btn);

    // Simulate a complete left-button click on the toolbar child.
    send_message(btn, kWindowMessageLeftButtonDown, MAKEDWORD(4, 4), NULL);
    send_message(btn, kWindowMessageLeftButtonUp,   MAKEDWORD(4, 4), NULL);

    // kWindowMessageCommand (kButtonNotificationClicked) should have been
    // forwarded by the default handler as kToolBarMessageButtonClick.
    ASSERT_EQUAL(g_click_count, 1);
    ASSERT_EQUAL(g_last_click_ident, 55);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_notitle_nonclient_mouseup_fires(void) {
    TEST("WINDOW_NOTITLE toolbar: NonClientLeftButtonUp activates child");

    test_env_init();

    g_last_click_ident = -1;
    g_click_count = 0;

    // A title-less tool palette window (all toolbar, no title bar).
    rect_t frame = {10, 20, 80, 30};
    window_t *win = create_window("Palette",
                                  WINDOW_TOOLBAR | WINDOW_NOTITLE | WINDOW_NORESIZE,
                                  &frame, NULL, click_capture_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_button_t buttons[] = {{.icon=0, .ident=77, .flags=0}};
    send_message(win, kToolBarMessageAddButtons, 1, buttons);

    window_t *btn = find_toolbar_child(win, 77);
    ASSERT_NOT_NULL(btn);

    // Compute a hit point inside the button's screen frame.
    int hit_x = btn->frame.x + btn->frame.w / 2;
    int hit_y = btn->frame.y + btn->frame.h / 2;

    // For WINDOW_NOTITLE windows the toolbar band is the drag area, so
    // LeftButtonDown goes through _dragging.  On release the framework sends
    // kWindowMessageNonClientLeftButtonUp to the parent with screen coords.
    // The default handler must then activate the toolbar child.
    send_message(win, kWindowMessageNonClientLeftButtonUp,
                 MAKEDWORD(hit_x, hit_y), NULL);

    ASSERT_EQUAL(g_click_count, 1);
    ASSERT_EQUAL(g_last_click_ident, 77);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_destroy_clears_children(void) {
    TEST("destroy_window frees toolbar_children");

    test_env_init();

    rect_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_button_t buttons[] = {
        {.icon=0, .ident=1, .flags=0},
        {.icon=1, .ident=2, .flags=0},
    };
    send_message(win, kToolBarMessageAddButtons, 2, buttons);
    ASSERT_EQUAL(count_toolbar_children(win), 2);

    // After destroy the window is freed; if toolbar_children were not freed
    // this would be a memory leak but not a crash observable here.
    // The test is mainly a sanity check that destroy_window doesn't crash.
    destroy_window(win);

    test_env_shutdown();
    PASS();
}

void test_toolbar_move_shifts_children(void) {
    TEST("move_window shifts toolbar children to new screen positions");

    test_env_init();

    rect_t frame = {50, 50, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_button_t buttons[] = {{.icon=0, .ident=1, .flags=0}};
    send_message(win, kToolBarMessageAddButtons, 1, buttons);

    window_t *btn = find_toolbar_child(win, 1);
    ASSERT_NOT_NULL(btn);

    int orig_x = btn->frame.x;
    int orig_y = btn->frame.y;

    // Move parent by (+10, +20).
    move_window(win, 60, 70);

    ASSERT_EQUAL(btn->frame.x, orig_x + 10);
    ASSERT_EQUAL(btn->frame.y, orig_y + 20);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_titlebar_height_single_row(void) {
    TEST("titlebar_height is always a single toolbar row regardless of button count");

    test_env_init();

    rect_t frame = {0, 0, 40, 60};  // narrow: would have wrapped with old code
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    int bsz = TB_SPACING;
    int expected = TITLEBAR_HEIGHT + bsz + 2 * TOOLBAR_PADDING;
    ASSERT_EQUAL(titlebar_height(win), expected);

    // Adding many buttons does not increase the non-client height.
    toolbar_button_t buttons[10];
    for (int i = 0; i < 10; i++)
        buttons[i] = (toolbar_button_t){.icon=0, .ident=i, .flags=0};
    send_message(win, kToolBarMessageAddButtons, 10, buttons);

    ASSERT_EQUAL(titlebar_height(win), expected);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

// ---- main -------------------------------------------------------------------

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    TEST_START("Toolbar child-window tests");

    test_toolbar_add_buttons_creates_children();
    test_toolbar_spacing_token_skipped();
    test_toolbar_add_buttons_replaces();
    test_toolbar_set_active_button();
    test_toolbar_set_strip();
    test_toolbar_set_items_button();
    test_toolbar_set_items_label();
    test_toolbar_set_items_combobox();
    test_toolbar_set_items_separator();
    test_toolbar_set_items_spacer();
    test_toolbar_button_click_fires_command();
    test_toolbar_notitle_nonclient_mouseup_fires();
    test_toolbar_destroy_clears_children();
    test_toolbar_move_shifts_children();
    test_titlebar_height_single_row();

    TEST_END();
}

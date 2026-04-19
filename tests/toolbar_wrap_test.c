// Tests for the child-window-based toolbar implementation.
//
// These tests are headless (no SDL/OpenGL rendering). The new toolbar
// creates real child windows in parent->toolbar_children using
// toolbar_item_t and kToolBarMessageSetItems.

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

void test_toolbar_set_items_creates_children(void) {
    TEST("kToolBarMessageSetItems creates one toolbar child per real button");

    test_env_init();

    rect_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {
        {TOOLBAR_ITEM_BUTTON, 10, 0, 0, 0, NULL},
        {TOOLBAR_ITEM_BUTTON, 11, 1, 0, 0, NULL},
        {TOOLBAR_ITEM_BUTTON, 12, 2, 0, 0, NULL},
    };
    send_message(win, kToolBarMessageSetItems, 3, items);

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

void test_toolbar_spacer_skipped(void) {
    TEST("kToolBarMessageSetItems: TOOLBAR_ITEM_SPACER does not create a child");

    test_env_init();

    rect_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {
        {TOOLBAR_ITEM_BUTTON, 1, 0, 0, 0, NULL},
        {TOOLBAR_ITEM_BUTTON, 2, 1, 0, 0, NULL},
        {TOOLBAR_ITEM_SPACER, 0, 0, 0, 0, NULL},
        {TOOLBAR_ITEM_BUTTON, 3, 2, 0, 0, NULL},
    };
    send_message(win, kToolBarMessageSetItems, 4, items);

    // 4 entries, but the spacer does not create a child.
    ASSERT_EQUAL(count_toolbar_children(win), 3);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_set_items_replaces(void) {
    TEST("kToolBarMessageSetItems replaces existing toolbar children");

    test_env_init();

    rect_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t first[] = {{TOOLBAR_ITEM_BUTTON, 1, 0, 0, 0, NULL}};
    send_message(win, kToolBarMessageSetItems, 1, first);
    ASSERT_EQUAL(count_toolbar_children(win), 1);

    toolbar_item_t second[] = {
        {TOOLBAR_ITEM_BUTTON, 10, 0, 0, 0, NULL},
        {TOOLBAR_ITEM_BUTTON, 11, 1, 0, 0, NULL},
    };
    send_message(win, kToolBarMessageSetItems, 2, second);

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

    toolbar_item_t items[] = {
        {TOOLBAR_ITEM_BUTTON, 10, 0, 0, TOOLBAR_BUTTON_FLAG_ACTIVE, NULL},
        {TOOLBAR_ITEM_BUTTON, 11, 1, 0, 0,                          NULL},
        {TOOLBAR_ITEM_BUTTON, 12, 2, 0, 0,                          NULL},
    };
    send_message(win, kToolBarMessageSetItems, 3, items);

    window_t *btn10 = find_toolbar_child(win, 10);
    window_t *btn11 = find_toolbar_child(win, 11);
    window_t *btn12 = find_toolbar_child(win, 12);
    ASSERT_NOT_NULL(btn10);
    ASSERT_NOT_NULL(btn11);
    ASSERT_NOT_NULL(btn12);

    // After SetItems, TOOLBAR_BUTTON_FLAG_ACTIVE → value==true on btn10.
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

    toolbar_item_t items[] = {{TOOLBAR_ITEM_BUTTON, 55, 0, 0, 0, NULL}};
    send_message(win, kToolBarMessageSetItems, 1, items);

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

    toolbar_item_t items[] = {{TOOLBAR_ITEM_BUTTON, 77, 0, 0, 0, NULL}};
    send_message(win, kToolBarMessageSetItems, 1, items);

    window_t *btn = find_toolbar_child(win, 77);
    ASSERT_NOT_NULL(btn);

    // Compute a hit point inside the button's screen frame.
    // btn->frame.x/y are toolbar-band-relative; for WINDOW_NOTITLE title_h=0,
    // so screen coords = win->frame.{x,y} + btn->frame.{x,y}.
    int title_h = 0; /* WINDOW_NOTITLE */
    int hit_x = win->frame.x + btn->frame.x + btn->frame.w / 2;
    int hit_y = (win->frame.y + title_h) + btn->frame.y + btn->frame.h / 2;

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

    toolbar_item_t items[] = {
        {TOOLBAR_ITEM_BUTTON, 1, 0, 0, 0, NULL},
        {TOOLBAR_ITEM_BUTTON, 2, 1, 0, 0, NULL},
    };
    send_message(win, kToolBarMessageSetItems, 2, items);
    ASSERT_EQUAL(count_toolbar_children(win), 2);

    // After destroy the window is freed; if toolbar_children were not freed
    // this would be a memory leak but not a crash observable here.
    // The test is mainly a sanity check that destroy_window doesn't crash.
    destroy_window(win);

    test_env_shutdown();
    PASS();
}

void test_toolbar_move_shifts_children(void) {
    TEST("move_window: toolbar children frames are parent-relative and stable after move");

    test_env_init();

    rect_t frame = {50, 50, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {{TOOLBAR_ITEM_BUTTON, 1, 0, 0, 0, NULL}};
    send_message(win, kToolBarMessageSetItems, 1, items);

    window_t *btn = find_toolbar_child(win, 1);
    ASSERT_NOT_NULL(btn);

    int orig_x = btn->frame.x;
    int orig_y = btn->frame.y;

    // Move parent by (+10, +20): toolbar child frames are parent-relative, so
    // they must NOT change when the parent moves (unlike the old screen-absolute
    // design, which required an explicit shift for every move).
    move_window(win, 60, 70);

    ASSERT_EQUAL(btn->frame.x, orig_x);
    ASSERT_EQUAL(btn->frame.y, orig_y);

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
    int expected = TITLEBAR_HEIGHT + bsz + 2 * (TOOLBAR_PADDING + TOOLBAR_BEVEL_WIDTH);
    ASSERT_EQUAL(titlebar_height(win), expected);

    // Adding many buttons does not increase the non-client height.
    toolbar_item_t items[10];
    for (int i = 0; i < 10; i++)
        items[i] = (toolbar_item_t){TOOLBAR_ITEM_BUTTON, i, 0, 0, 0, NULL};
    send_message(win, kToolBarMessageSetItems, 10, items);

    ASSERT_EQUAL(titlebar_height(win), expected);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

// ---- main -------------------------------------------------------------------

void test_toolbar_button_click_cancelled_if_released_outside(void) {
    TEST("Toolbar button: releasing outside the child does NOT fire click");

    test_env_init();

    g_last_click_ident = -1;
    g_click_count = 0;

    rect_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, click_capture_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {{TOOLBAR_ITEM_BUTTON, 88, 0, 0, 0, NULL}};
    send_message(win, kToolBarMessageSetItems, 1, items);

    window_t *btn = find_toolbar_child(win, 88);
    ASSERT_NOT_NULL(btn);

    // Make the window visible so dispatch_message can find it via find_window().
    win->visible = true;

    // btn->frame.x/y are toolbar-band-relative (not screen-absolute).
    // Screen position = win->frame.{x,y} + TITLEBAR_HEIGHT + btn->frame.{x,y}.
    // (win has a title bar: WINDOW_TOOLBAR without WINDOW_NOTITLE)
    int title_h = TITLEBAR_HEIGHT;

    // Drive the real event-layer path: press inside the toolbar button via
    // dispatch_message so the event layer records _toolbar_down_win, then
    // release outside so the event layer clears pressed state without firing
    // a click notification — matching the previous hit-tested behavior.
    ui_event_t ev = {0};
    ev.message = kEventLeftButtonDown;
    ev.x = (uint16_t)((win->frame.x + btn->frame.x + 4) * UI_WINDOW_SCALE);
    ev.y = (uint16_t)((win->frame.y + title_h + btn->frame.y + 4) * UI_WINDOW_SCALE);
    dispatch_message(&ev);
    ASSERT_TRUE(btn->pressed);

    // Release well outside the button (to the right of it, same toolbar row).
    ev.message = kEventLeftButtonUp;
    ev.x = (uint16_t)((win->frame.x + btn->frame.x + btn->frame.w + 10) * UI_WINDOW_SCALE);
    ev.y = (uint16_t)((win->frame.y + title_h + btn->frame.y + 4) * UI_WINDOW_SCALE);
    dispatch_message(&ev);

    ASSERT_FALSE(btn->pressed);
    ASSERT_EQUAL(g_click_count, 0);
    ASSERT_EQUAL(g_last_click_ident, -1);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_item_button_frame_clamped(void) {
    TEST("kToolBarMessageSetItems: text button frame is clamped to requested size");

    test_env_init();

    rect_t frame = {0, 0, 300, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    // Use a very long button title that would normally expand win_button's frame.w.
    toolbar_item_t items[] = {
        { TOOLBAR_ITEM_BUTTON, 50, -1, 40, 0, "A very long label that would overflow" },
    };
    send_message(win, kToolBarMessageSetItems, 1, items);

    window_t *btn = find_toolbar_child(win, 50);
    ASSERT_NOT_NULL(btn);

    // Frame must be exactly the requested 40-pixel width, not auto-expanded.
    ASSERT_EQUAL(btn->frame.w, 40);

    // Height must be clamped to bsz, not BUTTON_HEIGHT.
    int bsz = TB_SPACING;
    ASSERT_EQUAL(btn->frame.h, bsz);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    TEST_START("Toolbar child-window tests");

    test_toolbar_set_items_creates_children();
    test_toolbar_spacer_skipped();
    test_toolbar_set_items_replaces();
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
    test_toolbar_button_click_cancelled_if_released_outside();
    test_toolbar_item_button_frame_clamped();

    TEST_END();
}

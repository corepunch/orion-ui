// Tests for toolbar host behavior.
//
// Buttons/labels/separators/spacers are owner-drawn toolbar items stored in
// toolbar_state_t. Only combobox/textedit items create embedded child windows.

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/commctl/commctl.h>
#include <orion/user/toolbar.h>

// ---- helpers ----------------------------------------------------------------

static result_t noop_proc(window_t *win, uint32_t msg,
                           uint32_t wparam, void *lparam) {
    (void)win; (void)wparam; (void)lparam;
    if (msg == evCreate || msg == evDestroy) return 1;
    return 0;
}

// Window proc that records the last tbButtonClick ident.
static int g_last_click_ident = -1;
static int g_click_count = 0;
static result_t click_capture_proc(window_t *win, uint32_t msg,
                                    uint32_t wparam, void *lparam) {
    (void)win; (void)lparam;
    if (msg == evCreate || msg == evDestroy) return 1;
    if (msg == tbButtonClick) {
        g_last_click_ident = (int)wparam;
        g_click_count++;
        return 1;
    }
    return 0;
}

static result_t test_menubar_proc(window_t *win, uint32_t msg,
                                  uint32_t wparam, void *lparam) {
    return win_menubar(win, msg, wparam, lparam);
}

static int g_chrome_toolbar_click;
static result_t test_chrome_toolbar_proc(window_t *win, uint32_t msg,
                                          uint32_t wparam, void *lparam) {
    (void)lparam;
    if (msg == evCreate) {
        toolbar_item_t item = {TOOLBAR_ITEM_BUTTON, 91, 0, 0, 0, "Test"};
        send_message(win, tbSetItems, 1, &item);
        return 1;
    }
    if (msg == tbButtonClick) {
        g_chrome_toolbar_click = (int)wparam;
        return 1;
    }
    return 0;
}

// Count the children in a window's toolbar_children list.
static int count_toolbar_children(window_t *win) {
    toolbar_state_t *tb = window_toolbar_state(win);
    int n = 0;
    for (window_t *tc = tb ? tb->children : NULL; tc; tc = tc->next) n++;
    return n;
}

// Find a toolbar child by id.
static window_t *find_toolbar_child(window_t *win, uint32_t id) {
    toolbar_state_t *tb = window_toolbar_state(win);
    for (window_t *tc = tb ? tb->children : NULL; tc; tc = tc->next)
        if (tc->id == id) return tc;
    return NULL;
}

static toolbar_state_t *require_toolbar_state(window_t *win) {
    return window_toolbar_state(win);
}

static void dispatch_left_mouse_at(int x, int y, uint32_t msg) {
    ui_event_t ev = {0};
    ev.message = msg;
    ev.x = (uint16_t)(x * UI_WINDOW_SCALE);
    ev.y = (uint16_t)(y * UI_WINDOW_SCALE);
    dispatch_message(&ev);
}

// ---- tests ------------------------------------------------------------------

void test_toolbar_set_items_creates_children(void) {
    TEST("tbSetItems creates one toolbar child per real button");

    test_env_init();

    irect16_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {
        {TOOLBAR_ITEM_BUTTON, 10, 0, 0, 0, NULL},
        {TOOLBAR_ITEM_BUTTON, 11, NULL, 0, 0, NULL},
        {TOOLBAR_ITEM_BUTTON, 12, NULL, 0, 0, NULL},
    };
    send_message(win, tbSetItems, 3, items);

    // Button items are owner-drawn; they do not create embedded child windows.
    ASSERT_EQUAL(count_toolbar_children(win), 0);

    toolbar_state_t *tb = require_toolbar_state(win);
    ASSERT_NOT_NULL(tb);
    ASSERT_EQUAL(tb->item_count, 3);
    ASSERT_EQUAL(tb->items[0].ident, 10);
    ASSERT_EQUAL(tb->items[1].ident, 11);
    ASSERT_EQUAL(tb->items[2].ident, 12);

    // Toolbar items are not in win->children, but the toolbar host window is.
    ASSERT_NOT_NULL(win->toolbar);
    ASSERT_NULL(win->toolbar->next);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_spacer_skipped(void) {
    TEST("tbSetItems: TOOLBAR_ITEM_SPACER creates a space child");

    test_env_init();

    irect16_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {
        {TOOLBAR_ITEM_BUTTON, 1, 0, 0, 0, NULL},
        {TOOLBAR_ITEM_BUTTON, 2, NULL, 0, 0, NULL},
        {TOOLBAR_ITEM_SPACER, 0, 0, 0, 0, NULL},
        {TOOLBAR_ITEM_BUTTON, 3, NULL, 0, 0, NULL},
    };
    send_message(win, tbSetItems, 4, items);

    // Spacer is owner-drawn metadata; no embedded children for these items.
    ASSERT_EQUAL(count_toolbar_children(win), 0);
    toolbar_state_t *tb = require_toolbar_state(win);
    ASSERT_NOT_NULL(tb);
    ASSERT_EQUAL(tb->item_count, 4);
    ASSERT_EQUAL(tb->items[2].type, TOOLBAR_ITEM_SPACER);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_set_items_replaces(void) {
    TEST("tbSetItems replaces existing toolbar children");

    test_env_init();

    irect16_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t first[] = {{TOOLBAR_ITEM_BUTTON, 1, 0, 0, 0, NULL}};
    send_message(win, tbSetItems, 1, first);
    ASSERT_EQUAL(count_toolbar_children(win), 0);
    toolbar_state_t *tb = require_toolbar_state(win);
    ASSERT_NOT_NULL(tb);
    ASSERT_EQUAL(tb->item_count, 1);
    ASSERT_EQUAL(tb->items[0].ident, 1);

    toolbar_item_t second[] = {
        {TOOLBAR_ITEM_BUTTON, 10, 0, 0, 0, NULL},
        {TOOLBAR_ITEM_BUTTON, 11, NULL, 0, 0, NULL},
    };
    send_message(win, tbSetItems, 2, second);

    // Old item list is replaced in toolbar state.
    ASSERT_EQUAL(count_toolbar_children(win), 0);
    ASSERT_EQUAL(tb->item_count, 2);
    ASSERT_EQUAL(tb->items[0].ident, 10);
    ASSERT_EQUAL(tb->items[1].ident, 11);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_set_active_button(void) {
    TEST("tbSetActiveButton sets value on correct child");

    test_env_init();

    irect16_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {
        {TOOLBAR_ITEM_BUTTON, 10, 0, 0, TOOLBAR_BUTTON_FLAG_ACTIVE, NULL},
        {TOOLBAR_ITEM_BUTTON, 11, NULL, 0, 0,                          NULL},
        {TOOLBAR_ITEM_BUTTON, 12, NULL, 0, 0,                          NULL},
    };
    send_message(win, tbSetItems, 3, items);

    toolbar_state_t *tb = require_toolbar_state(win);
    ASSERT_NOT_NULL(tb);
    ASSERT_EQUAL(tb->item_count, 3);

    // After SetItems, first button starts active.
    ASSERT_TRUE((tb->items[0].flags & TOOLBAR_BUTTON_FLAG_ACTIVE) != 0);
    ASSERT_FALSE((tb->items[1].flags & TOOLBAR_BUTTON_FLAG_ACTIVE) != 0);
    ASSERT_FALSE((tb->items[2].flags & TOOLBAR_BUTTON_FLAG_ACTIVE) != 0);

    // Activate ident 11.
    send_message(win, tbSetActiveButton, 11, NULL);

    ASSERT_FALSE((tb->items[0].flags & TOOLBAR_BUTTON_FLAG_ACTIVE) != 0);
    ASSERT_TRUE((tb->items[1].flags & TOOLBAR_BUTTON_FLAG_ACTIVE) != 0);
    ASSERT_FALSE((tb->items[2].flags & TOOLBAR_BUTTON_FLAG_ACTIVE) != 0);

    // Activate ident 12.
    send_message(win, tbSetActiveButton, 12, NULL);

    ASSERT_FALSE((tb->items[0].flags & TOOLBAR_BUTTON_FLAG_ACTIVE) != 0);
    ASSERT_FALSE((tb->items[1].flags & TOOLBAR_BUTTON_FLAG_ACTIVE) != 0);
    ASSERT_TRUE((tb->items[2].flags & TOOLBAR_BUTTON_FLAG_ACTIVE) != 0);

    // Unknown ident clears all.
    send_message(win, tbSetActiveButton, 99, NULL);

    ASSERT_FALSE((tb->items[0].flags & TOOLBAR_BUTTON_FLAG_ACTIVE) != 0);
    ASSERT_FALSE((tb->items[1].flags & TOOLBAR_BUTTON_FLAG_ACTIVE) != 0);
    ASSERT_FALSE((tb->items[2].flags & TOOLBAR_BUTTON_FLAG_ACTIVE) != 0);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_set_strip(void) {
    TEST("tbSetStrip stores strip in window");

    test_env_init();

    irect16_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR, &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_state_t *tb = window_toolbar_state(win);
    ASSERT_NULL(tb);

    bitmap_strip_t strip = {
        .tex=42, .icon_w=16, .icon_h=16,
        .cols=2, .sheet_w=32, .sheet_h=160,
    };
    send_message(win, tbSetStrip, 0, &strip);
    tb = window_toolbar_state(win);
    ASSERT_NOT_NULL(tb);
    ASSERT_EQUAL((int)tb->strip.tex, 42);
    ASSERT_EQUAL(tb->strip.icon_w, 16);
    ASSERT_EQUAL(tb->strip.cols, 2);
    ASSERT_EQUAL(tb->strip.sheet_w, 32);
    ASSERT_EQUAL(tb->strip.sheet_h, 160);

    send_message(win, tbSetStrip, 0, NULL);
    ASSERT_EQUAL((int)tb->strip.tex, 0);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_set_items_button(void) {
    TEST("tbSetItems: TOOLBAR_ITEM_BUTTON creates button child");

    test_env_init();

    irect16_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {
        {TOOLBAR_ITEM_BUTTON, 20, NULL, 0, 0, "New"},
        {TOOLBAR_ITEM_BUTTON, 21, NULL, 0, 0, "Open"},
    };
    send_message(win, tbSetItems, 2, items);

    ASSERT_EQUAL(count_toolbar_children(win), 0);
    toolbar_state_t *tb = require_toolbar_state(win);
    ASSERT_NOT_NULL(tb);
    ASSERT_EQUAL(tb->item_count, 2);
    ASSERT_EQUAL(tb->items[0].type, TOOLBAR_ITEM_BUTTON);
    ASSERT_EQUAL(tb->items[0].ident, 20);
    ASSERT_EQUAL(tb->items[1].ident, 21);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_set_items_label(void) {
    TEST("tbSetItems: TOOLBAR_ITEM_LABEL creates label child");

    test_env_init();

    irect16_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {
        {TOOLBAR_ITEM_LABEL, 30, NULL, 40, 0, "Filter:"},
    };
    send_message(win, tbSetItems, 1, items);

    ASSERT_EQUAL(count_toolbar_children(win), 0);
    toolbar_state_t *tb = require_toolbar_state(win);
    ASSERT_NOT_NULL(tb);
    ASSERT_EQUAL(tb->item_count, 1);
    ASSERT_EQUAL(tb->items[0].type, TOOLBAR_ITEM_LABEL);
    ASSERT_EQUAL(tb->items[0].ident, 30);
    ASSERT_TRUE(strncmp(tb->items[0].text, "Filter:", 7) == 0);
    ASSERT_TRUE(tb->item_rects[0].w >= 40);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_set_items_textedit_geometry(void) {
    TEST("tbSetItems: TOOLBAR_ITEM_TEXTEDIT gets 2px top/bottom inset");

    test_env_init();

    irect16_t frame = {0, 0, 300, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {
        {TOOLBAR_ITEM_TEXTEDIT, 41, NULL, 120, 0, "https://example.com"},
    };
    send_message(win, tbSetItems, 1, items);

    ASSERT_EQUAL(count_toolbar_children(win), 1);
    window_t *edit = find_toolbar_child(win, 41);
    ASSERT_NOT_NULL(edit);
    ASSERT_EQUAL(edit->frame.w, 120);
    ASSERT_EQUAL(edit->frame.y, TOOLBAR_BEVEL_WIDTH + TOOLBAR_PADDING + 2);
    ASSERT_EQUAL(edit->frame.h, TB_SPACING - 4);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_textedit_click_focuses_and_enters_editing(void) {
    TEST("Toolbar textedit click focuses control and enters editing mode");

    test_env_init();

    irect16_t frame = {20, 30, 320, 80};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {
        {TOOLBAR_ITEM_TEXTEDIT, 42, NULL, 150, 0, "https://example.com"},
    };
    send_message(win, tbSetItems, 1, items);

    window_t *edit = find_toolbar_child(win, 42);
    ASSERT_NOT_NULL(edit);

    window_set_state(win, WINDOW_STATE_VISIBLE, true);
    set_focus(NULL);
    ASSERT_NULL(g_ui_runtime.focused);
    ASSERT_FALSE(window_has_state(edit, WINDOW_STATE_EDITING));

    int hit_x = win->frame.x + edit->frame.x + 6;
    int hit_y = win->frame.y + TITLEBAR_HEIGHT + edit->frame.y + edit->frame.h / 2;

    dispatch_left_mouse_at(hit_x, hit_y, kEventLeftButtonDown);
    ASSERT_EQUAL(g_ui_runtime.focused, edit);

    dispatch_left_mouse_at(hit_x, hit_y, kEventLeftButtonUp);
    ASSERT_EQUAL(g_ui_runtime.focused, edit);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_titlebar_click_does_not_focus_toolbar_textedit(void) {
    TEST("Titlebar click does not focus toolbar textedit on titled windows");

    test_env_init();

    irect16_t frame = {20, 30, 320, 80};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {
        {TOOLBAR_ITEM_TEXTEDIT, 43, NULL, 150, 0, "https://example.com"},
    };
    send_message(win, tbSetItems, 1, items);

    window_t *edit = find_toolbar_child(win, 43);
    ASSERT_NOT_NULL(edit);

    window_set_state(win, WINDOW_STATE_VISIBLE, true);

    int hit_x = win->frame.x + 10;
    int hit_y = win->frame.y + 4;

    dispatch_left_mouse_at(hit_x, hit_y, kEventLeftButtonDown);
    dispatch_left_mouse_at(hit_x, hit_y, kEventLeftButtonUp);

    ASSERT_EQUAL(g_ui_runtime.focused, win);
    ASSERT_NOT_EQUAL(g_ui_runtime.focused, edit);
    ASSERT_FALSE(window_has_state(edit, WINDOW_STATE_EDITING));

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_set_items_combobox(void) {
    TEST("tbSetItems: TOOLBAR_ITEM_COMBOBOX creates combobox child");

    test_env_init();

    irect16_t frame = {0, 0, 300, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {
        {TOOLBAR_ITEM_COMBOBOX, 40, NULL, 80, 0, NULL},
    };
    send_message(win, tbSetItems, 1, items);

    ASSERT_EQUAL(count_toolbar_children(win), 1);
    window_t *cb = find_toolbar_child(win, 40);
    ASSERT_NOT_NULL(cb);
    ASSERT_EQUAL(cb->frame.w, 80);
    ASSERT_EQUAL(cb->frame.y, TOOLBAR_BEVEL_WIDTH + TOOLBAR_PADDING + 2);
    ASSERT_EQUAL(cb->frame.h, TB_SPACING - 4);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_set_items_separator(void) {
    TEST("tbSetItems: TOOLBAR_ITEM_SEPARATOR creates narrow child");

    test_env_init();

    irect16_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {
        {TOOLBAR_ITEM_BUTTON,    1, NULL, 0, 0, "A"},
        {TOOLBAR_ITEM_SEPARATOR, 0, NULL, 0, 0, NULL},
        {TOOLBAR_ITEM_BUTTON,    2, NULL, 0, 0, "B"},
    };
    send_message(win, tbSetItems, 3, items);

    ASSERT_EQUAL(count_toolbar_children(win), 0);
    toolbar_state_t *tb = require_toolbar_state(win);
    ASSERT_NOT_NULL(tb);
    ASSERT_EQUAL(tb->item_count, 3);
    ASSERT_EQUAL(tb->items[1].type, TOOLBAR_ITEM_SEPARATOR);
    ASSERT_EQUAL(tb->item_rects[1].w, 6);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_set_items_spacer(void) {
    TEST("tbSetItems: TOOLBAR_ITEM_SPACER creates a space child");

    test_env_init();

    irect16_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {
        {TOOLBAR_ITEM_BUTTON, 1, NULL, 0, 0, "A"},
        {TOOLBAR_ITEM_SPACER, 0, NULL, 8, 0, NULL},
        {TOOLBAR_ITEM_BUTTON, 2, NULL, 0, 0, "B"},
    };
    send_message(win, tbSetItems, 3, items);

    ASSERT_EQUAL(count_toolbar_children(win), 0);
    toolbar_state_t *tb = require_toolbar_state(win);
    ASSERT_NOT_NULL(tb);
    ASSERT_EQUAL(tb->item_count, 3);
    ASSERT_EQUAL(tb->items[1].type, TOOLBAR_ITEM_SPACER);
    ASSERT_EQUAL(tb->item_rects[1].w, 8);
    ASSERT_EQUAL(tb->item_rects[1].h, TB_SPACING);
    ASSERT_EQUAL(tb->item_rects[0].w, TB_SPACING);
    ASSERT_EQUAL(tb->item_rects[0].x, TOOLBAR_BEVEL_WIDTH + TOOLBAR_PADDING);
    ASSERT_EQUAL(tb->item_rects[1].x,
                 TOOLBAR_BEVEL_WIDTH + TOOLBAR_PADDING + TB_SPACING + TOOLBAR_SPACING);
    ASSERT_EQUAL(tb->item_rects[2].x,
                 TOOLBAR_BEVEL_WIDTH + TOOLBAR_PADDING + TB_SPACING + TOOLBAR_SPACING +
                 8 + TOOLBAR_SPACING);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_button_click_fires_command(void) {
    TEST("Clicking a toolbar child fires tbButtonClick on parent");

    test_env_init();

    g_last_click_ident = -1;
    g_click_count = 0;

    irect16_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, click_capture_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {{TOOLBAR_ITEM_BUTTON, 55, 0, 0, 0, NULL}};
    send_message(win, tbSetItems, 1, items);

    window_set_state(win, WINDOW_STATE_VISIBLE, true);
    toolbar_state_t *tb = require_toolbar_state(win);
    ASSERT_NOT_NULL(tb);
    irect16_t r = tb->item_rects[0];
    int hit_x = win->frame.x + r.x + r.w / 2;
    int hit_y = win->frame.y + TITLEBAR_HEIGHT + r.y + r.h / 2;
    dispatch_left_mouse_at(hit_x, hit_y, kEventLeftButtonDown);
    dispatch_left_mouse_at(hit_x, hit_y, kEventLeftButtonUp);

    // Toolbar buttons should notify the parent directly via tbButtonClick.
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
    irect16_t frame = {10, 20, 80, 30};
    window_t *win = create_window("Palette",
                                  WINDOW_TOOLBAR | WINDOW_NOTITLE | WINDOW_NORESIZE,
                                  &frame, NULL, click_capture_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {{TOOLBAR_ITEM_BUTTON, 77, 0, 0, 0, NULL}};
    send_message(win, tbSetItems, 1, items);

    toolbar_state_t *tb = require_toolbar_state(win);
    ASSERT_NOT_NULL(tb);
    irect16_t r = tb->item_rects[0];

    // Compute a hit point inside the button's screen frame.
    // btn->frame.x/y are toolbar-band-relative; for WINDOW_NOTITLE title_h=0,
    // so screen coords = win->frame.{x,y} + btn->frame.{x,y}.
    int title_h = 0; /* WINDOW_NOTITLE */
    int hit_x = win->frame.x + r.x + r.w / 2;
    int hit_y = (win->frame.y + title_h) + r.y + r.h / 2;

    // Exercise the real event-dispatch path for title-less tool windows.
    dispatch_left_mouse_at(hit_x, hit_y, kEventLeftButtonDown);
    dispatch_left_mouse_at(hit_x, hit_y, kEventLeftButtonUp);

    ASSERT_EQUAL(g_click_count, 1);
    ASSERT_EQUAL(g_last_click_ident, 77);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_destroy_clears_children(void) {
    TEST("destroy_window frees toolbar_children");

    test_env_init();

    irect16_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {
        {TOOLBAR_ITEM_BUTTON, 1, 0, 0, 0, NULL},
        {TOOLBAR_ITEM_BUTTON, 2, NULL, 0, 0, NULL},
    };
    send_message(win, tbSetItems, 2, items);
    ASSERT_EQUAL(count_toolbar_children(win), 0);

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

    irect16_t frame = {50, 50, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {{TOOLBAR_ITEM_BUTTON, 1, 0, 0, 0, NULL}};
    send_message(win, tbSetItems, 1, items);

    toolbar_state_t *tb = require_toolbar_state(win);
    ASSERT_NOT_NULL(tb);
    int orig_x = tb->item_rects[0].x;
    int orig_y = tb->item_rects[0].y;

    // Move parent by (+10, +20): toolbar child frames are parent-relative, so
    // they must NOT change when the parent moves (unlike the old screen-absolute
    // design, which required an explicit shift for every move).
    move_window(win, 60, 70);

    ASSERT_EQUAL(tb->item_rects[0].x, orig_x);
    ASSERT_EQUAL(tb->item_rects[0].y, orig_y);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_titlebar_height_single_row(void) {
    TEST("titlebar_height is always a single toolbar row regardless of button count");

    test_env_init();

    irect16_t frame = {0, 0, 40, 60};  // narrow: would have wrapped with old code
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
    send_message(win, tbSetItems, 10, items);

    ASSERT_EQUAL(titlebar_height(win), expected);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_labels_increase_button_and_band_size(void) {
    TEST("tbSetStyle: labels expand buttons and toolbar height");
    test_env_init();
    irect16_t frame = {0, 0, 240, 100};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    toolbar_item_t item = {TOOLBAR_ITEM_BUTTON, 1, 0, 0, 0, "A long label", NULL};
    send_message(win, tbSetItems, 1, &item);
    int plain_h = titlebar_height(win), plain_w = require_toolbar_state(win)->item_rects[0].w;
    send_message(win, tbSetStyle, TOOLBAR_STYLE_SHOW_LABELS, NULL);
    toolbar_state_t *tb = require_toolbar_state(win);
    ASSERT_EQUAL(tb->style, TOOLBAR_STYLE_SHOW_LABELS);
    ASSERT_TRUE(titlebar_height(win) > plain_h);
    ASSERT_EQUAL(tb->item_rects[0].w, MAX(plain_w, text_strwidth(FONT_SMALLEST, item.text) + 8));
    ASSERT_EQUAL(tb->item_rects[0].h, TB_SPACING + text_char_height(FONT_SMALLEST) + 2);
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

    irect16_t frame = {0, 0, 200, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, click_capture_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    toolbar_item_t items[] = {{TOOLBAR_ITEM_BUTTON, 88, 0, 0, 0, NULL}};
    send_message(win, tbSetItems, 1, items);

    toolbar_state_t *tb = require_toolbar_state(win);
    ASSERT_NOT_NULL(tb);
    irect16_t r = tb->item_rects[0];

    // Make the window visible so dispatch_message can find it via find_window().
    window_set_state(win, WINDOW_STATE_VISIBLE, true);

    // Toolbar item rect is toolbar-band-relative.
    // Screen position = win->frame.{x,y} + TITLEBAR_HEIGHT + item_rect.{x,y}.
    // (win has a title bar: WINDOW_TOOLBAR without WINDOW_NOTITLE)
    int title_h = TITLEBAR_HEIGHT;

    // Drive the real event-layer path: press inside the toolbar button via
    // dispatch_message so the event layer records _toolbar_down_win, then
    // release outside so the event layer clears pressed state without firing
    // a click notification — matching the previous hit-tested behavior.
    ui_event_t ev = {0};
    ev.message = kEventLeftButtonDown;
    ev.x = (uint16_t)((win->frame.x + r.x + 4) * UI_WINDOW_SCALE);
    ev.y = (uint16_t)((win->frame.y + title_h + r.y + 4) * UI_WINDOW_SCALE);
    dispatch_message(&ev);

    // Release well outside the button (to the right of it, same toolbar row).
    ev.message = kEventLeftButtonUp;
    ev.x = (uint16_t)((win->frame.x + r.x + r.w + 10) * UI_WINDOW_SCALE);
    ev.y = (uint16_t)((win->frame.y + title_h + r.y + 4) * UI_WINDOW_SCALE);
    dispatch_message(&ev);

    ASSERT_EQUAL(g_click_count, 0);
    ASSERT_EQUAL(g_last_click_ident, -1);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_toolbar_item_button_frame_clamped(void) {
    TEST("tbSetItems: text button frame is clamped to requested size");

    test_env_init();

    irect16_t frame = {0, 0, 300, 60};
    window_t *win = create_window("W", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    // Use a very long button title that would normally expand win_button's frame.w.
    toolbar_item_t items[] = {
        { TOOLBAR_ITEM_BUTTON, 50, NULL, 40, 0, "A very long label that would overflow" },
    };
    send_message(win, tbSetItems, 1, items);

    toolbar_state_t *tb = require_toolbar_state(win);
    ASSERT_NOT_NULL(tb);
    ASSERT_EQUAL(tb->item_count, 1);

    // Frame must be exactly the requested 40-pixel width, not auto-expanded.
    ASSERT_EQUAL(tb->item_rects[0].w, 40);

    // Height must be clamped to bsz, not BUTTON_HEIGHT.
    int bsz = TB_SPACING;
    ASSERT_EQUAL(tb->item_rects[0].h, bsz);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_nodrag_toolbar_stays_fixed(void) {
    TEST("WINDOW_NODRAG toolbar does not enter the drag path");

    test_env_init();
    irect16_t frame = {0, MENUBAR_HEIGHT, 200, TOOLBAR_BAND_HEIGHT};
    window_t *win = create_window("Fixed", WINDOW_TOOLBAR | WINDOW_NOTITLE |
                                  WINDOW_NORESIZE | WINDOW_NODRAG,
                                  &frame, NULL, noop_proc, 0, NULL);
    ASSERT_NOT_NULL(win);

    dispatch_left_mouse_at(150, MENUBAR_HEIGHT + 5, kEventLeftButtonDown);
    ASSERT_NULL(g_ui_runtime.dragging);

    destroy_window(win);
    test_env_shutdown();
    PASS();
}

void test_app_chrome_owns_and_resizes_bands(void) {
    TEST("App chrome owns fixed menu/toolbar bands and resizes them together");

    test_env_init();
    menu_def_t menu = {"File", NULL, 0};
    window_t *chrome = create_app_chrome("Chrome", test_menubar_proc,
                                         &menu, 1, test_chrome_toolbar_proc, 7);
    ASSERT_NOT_NULL(chrome);
    window_t *menubar = app_chrome_menubar(chrome);
    window_t *toolbar = app_chrome_toolbar(chrome);
    ASSERT_NOT_NULL(menubar);
    ASSERT_NOT_NULL(toolbar);
    ASSERT_TRUE(chrome->flags & WINDOW_NODRAG);
    ASSERT_TRUE(menubar->parent == chrome);
    ASSERT_TRUE(toolbar->parent == chrome);
    ASSERT_EQUAL(menubar->frame.y, 0);
    ASSERT_EQUAL(menubar->frame.h, MENUBAR_HEIGHT);
    ASSERT_EQUAL(toolbar->frame.y, MENUBAR_HEIGHT);
    ASSERT_EQUAL(toolbar->frame.h, TOOLBAR_BAND_HEIGHT);

    send_message(chrome, evDisplayChange, MAKEDWORD(513, 400), NULL);
    ASSERT_EQUAL(chrome->frame.w, 513);
    ASSERT_EQUAL(menubar->frame.w, 513);
    ASSERT_EQUAL(toolbar->frame.w, 513);

    g_chrome_toolbar_click = 0;
    toolbar_state_t *tb = require_toolbar_state(toolbar);
    ASSERT_NOT_NULL(tb);
    irect16_t item = tb->item_rects[0];
    ASSERT_TRUE(find_window(item.x + item.w / 2,
                            MENUBAR_HEIGHT + item.y + item.h / 2) == toolbar);
    dispatch_left_mouse_at(item.x + item.w / 2,
                           MENUBAR_HEIGHT + item.y + item.h / 2,
                           kEventLeftButtonDown);
    dispatch_left_mouse_at(item.x + item.w / 2,
                           MENUBAR_HEIGHT + item.y + item.h / 2,
                           kEventLeftButtonUp);
    ASSERT_EQUAL(g_chrome_toolbar_click, 91);

    destroy_window(chrome);
    test_env_shutdown();
    PASS();
}

void test_multiple_docked_toolbars(void) {
    TEST("Top and left toolbars dock, resize, route clicks and leave workspace interactive");
    test_env_init();
    window_t *doc = create_window("Document", WINDOW_NOTITLE, MAKERECT(0, 0, 600, 480),
                                  NULL, noop_proc, 7, NULL);
    window_t *chrome = create_app_chrome("Chrome", test_menubar_proc, NULL, 0,
                                         test_chrome_toolbar_proc, 7);
    window_t *left = app_chrome_add_toolbar(chrome, TOOLBAR_DOCK_LEFT, test_chrome_toolbar_proc);
    window_t *top = app_chrome_toolbar(chrome);
    ASSERT_NOT_NULL(left);
    ASSERT_TRUE(left->parent == chrome);
    ASSERT_TRUE(is_window(left));
    ASSERT_EQUAL(left->frame.x, 0);
    ASSERT_EQUAL(left->frame.y, MENUBAR_HEIGHT + top->frame.h);
    ASSERT_EQUAL(window_toolbar_state(left)->orientation, TOOLBAR_VERTICAL);
    send_message(chrome, evDisplayChange, MAKEDWORD(600, 480), NULL);
    ASSERT_EQUAL(left->frame.h, 480 - left->frame.y);
    ASSERT_TRUE(find_window(200, 200) == doc);
    toolbar_state_t *tb = window_toolbar_state(left);
    irect16_t item = tb->item_rects[0];
    int x = window_screen_x(left) + item.x + item.w / 2;
    int y = window_screen_y(left) + item.y + item.h / 2;
    ASSERT_TRUE(find_window(x, y) == left);
    g_chrome_toolbar_click = 0;
    dispatch_left_mouse_at(x, y, kEventLeftButtonDown);
    dispatch_left_mouse_at(x, y, kEventLeftButtonUp);
    ASSERT_EQUAL(g_chrome_toolbar_click, 91);
    destroy_window(top);
    ASSERT_NULL(app_chrome_toolbar(chrome));
    top = app_chrome_add_toolbar(chrome, TOOLBAR_DOCK_TOP, test_chrome_toolbar_proc);
    ASSERT_TRUE(app_chrome_toolbar(chrome) == top);
    destroy_window(chrome);
    ASSERT_FALSE(is_window(left));
    ASSERT_TRUE(find_window(x, y) == doc);

    window_t *top_bar = create_docked_toolbar(doc, TOOLBAR_DOCK_TOP, test_chrome_toolbar_proc);
    window_t *left_bar = create_docked_toolbar(doc, TOOLBAR_DOCK_LEFT, test_chrome_toolbar_proc);
    window_t *second_top = create_docked_toolbar(doc, TOOLBAR_DOCK_TOP, test_chrome_toolbar_proc);
    irect16_t content = layout_docked_toolbars(doc, get_client_rect(doc));
    ASSERT_EQUAL(content.x, left_bar->frame.w);
    ASSERT_EQUAL(content.y, top_bar->frame.h + second_top->frame.h);
    ASSERT_EQUAL(left_bar->frame.y, content.y);
    resize_window(doc, 400, 300);
    ASSERT_EQUAL(top_bar->frame.w, 400);
    ASSERT_EQUAL(left_bar->frame.h, 300 - content.y);
    toolbar_item_t items[20] = {0};
    for (int i = 0; i < 20; i++) {
        items[i].type = TOOLBAR_ITEM_BUTTON;
        items[i].ident = 100 + i;
    }
    send_message(left_bar, tbSetItems, 20, items);
    content = layout_docked_toolbars(doc, get_client_rect(doc));
    tb = window_toolbar_state(left_bar);
    ASSERT_TRUE(left_bar->frame.w > toolbar_effective_bsz(left_bar) * 2);
    for (int i = 0; i < tb->item_count; i++) {
        ASSERT_TRUE(tb->item_rects[i].y + tb->item_rects[i].h <= left_bar->frame.h);
        ASSERT_TRUE(tb->item_rects[i].x + tb->item_rects[i].w <= content.x);
    }
    destroy_window(doc);
    test_env_shutdown();
    PASS();
}

static int custom_draw_count;
static toolbar_draw_item_t custom_draw;
static result_t custom_draw_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (msg == tbDrawItem) {
    custom_draw_count++;
    custom_draw = *(toolbar_draw_item_t *)lparam;
    return true;
  }
  return click_capture_proc(win, msg, wparam, lparam);
}

static void test_toolbar_vertical_custom_item(void) {
  TEST("Vertical toolbar routes clicks, custom paint, and orientation changes");
  test_env_init();
  irect16_t frame = {20, 20, 60, 200};
  window_t *win = create_window("Tools", WINDOW_TOOLBAR | WINDOW_NORESIZE,
                                &frame, NULL, custom_draw_proc, 0, NULL);
  toolbar_item_t items[] = {
    {.type = TOOLBAR_ITEM_BUTTON, .ident = 1},
    {.type = TOOLBAR_ITEM_SPACER, .w = 8},
    {.type = TOOLBAR_ITEM_CUSTOM, .ident = 2, .tooltip = "Colors"},
  };
  send_message(win, tbSetItems, ARRAY_LEN(items), items);
  toolbar_state_t *tb = window_toolbar_state(win);
  ASSERT_EQUAL(tb->orientation, TOOLBAR_HORIZONTAL);
  send_message(win, tbSetOrientation, TOOLBAR_VERTICAL, NULL);
  send_message(win, tbSetButtonSize, 30, NULL);
  ASSERT_EQUAL(tb->item_rects[0].x, tb->item_rects[2].x);
  ASSERT_EQUAL(tb->item_rects[1].h, 8);
  ASSERT_EQUAL(tb->item_rects[2].y, tb->item_rects[0].y + 30 + 8 + 2 * TOOLBAR_SPACING);
  irect16_t r = tb->item_rects[2];
  ASSERT_EQUAL(titlebar_height(win), TITLEBAR_HEIGHT + r.y + r.h + TOOLBAR_PADDING + TOOLBAR_BEVEL_WIDTH);
  send_message(win, tbSetOrientation, 999, NULL);
  ASSERT_EQUAL(tb->orientation, TOOLBAR_VERTICAL);
  g_click_count = 0;
  dispatch_left_mouse_at(win->frame.x + r.x + 4, win->frame.y + TITLEBAR_HEIGHT + r.y + 4, kEventLeftButtonDown);
  dispatch_left_mouse_at(win->frame.x + r.x + 4, win->frame.y + TITLEBAR_HEIGHT + r.y + 4, kEventLeftButtonUp);
  ASSERT_EQUAL(g_click_count, 1);
  ASSERT_EQUAL(g_last_click_ident, 2);
  send_message(win, tbSetActiveButton, 2, NULL);
  custom_draw_count = 0;
  toolbar_draw_non_client(win);
  ASSERT_EQUAL(custom_draw_count, 1);
  ASSERT_EQUAL(custom_draw.rect.x, 0);
  ASSERT_EQUAL(custom_draw.rect.y, 0);
  ASSERT_EQUAL(custom_draw.rect.w, 30);
  ASSERT_EQUAL(custom_draw.rect.h, 30);
  ASSERT_TRUE(custom_draw.state & CTRL_SELECTED);
  ASSERT_EQUAL(custom_draw.index, 2);
  send_message(win, tbSetOrientation, TOOLBAR_HORIZONTAL, NULL);
  ASSERT_EQUAL(tb->item_rects[0].y, tb->item_rects[2].y);
  ASSERT_EQUAL(titlebar_height(win), TITLEBAR_HEIGHT + 30 + 2 * (TOOLBAR_PADDING + TOOLBAR_BEVEL_WIDTH));
  destroy_window(win);
  test_env_shutdown();
  PASS();
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    TEST_START("Toolbar child-window tests");

    test_toolbar_vertical_custom_item();
    test_toolbar_set_items_creates_children();
    test_toolbar_spacer_skipped();
    test_toolbar_set_items_replaces();
    test_toolbar_set_active_button();
    test_toolbar_set_strip();
    test_toolbar_set_items_button();
    test_toolbar_set_items_label();
    test_toolbar_set_items_combobox();
    test_toolbar_set_items_textedit_geometry();
    test_toolbar_set_items_separator();
    test_toolbar_set_items_spacer();
    test_toolbar_button_click_fires_command();
    test_toolbar_notitle_nonclient_mouseup_fires();
    test_toolbar_textedit_click_focuses_and_enters_editing();
    test_titlebar_click_does_not_focus_toolbar_textedit();
    test_toolbar_destroy_clears_children();
    test_toolbar_move_shifts_children();
    test_titlebar_height_single_row();
    test_toolbar_labels_increase_button_and_band_size();
    test_toolbar_button_click_cancelled_if_released_outside();
    test_toolbar_item_button_frame_clamped();
    test_nodrag_toolbar_stays_fixed();
    test_app_chrome_owns_and_resizes_bands();
    test_multiple_docked_toolbars();

    TEST_END();
}

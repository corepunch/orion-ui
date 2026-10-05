// edit_test.c — Unit tests for commctl/edit.c (win_textedit).
//
// Covers: character insertion (evTextInput), cursor movement (Left/Right),
// backspace delete, Enter to start/commit editing (fires edUpdate),
// Escape to cancel editing, and Tab to commit (fires edUpdate).

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/commctl/commctl.h>

// ── notification capture ──────────────────────────────────────────────────

static int      g_update_count = 0;
static int      g_change_count = 0;
static window_t *g_last_edit   = NULL;

static result_t edit_parent_proc(window_t *win, uint32_t msg,
                                  uint32_t wparam, void *lparam) {
    (void)win;
    if (msg == evCreate || msg == evDestroy) return 1;
    if (msg == evCommand && HIWORD(wparam) == edUpdate) {
        g_update_count++;
        g_last_edit = (window_t *)lparam;
    }
    if (msg == evCommand && HIWORD(wparam) == ednChange) g_change_count++;
    return 0;
}

static void reset_state(void) {
    g_update_count = 0;
    g_change_count = 0;
    g_last_edit    = NULL;
}

// ── helpers ───────────────────────────────────────────────────────────────

static window_t *make_edit(window_t *parent, int id, const char *initial) {
    irect16_t fr = {10, 10, 120, 16};
    window_t *ed = create_window(initial ? initial : "",
                                  0, &fr, parent, win_textedit, 0, NULL);
    if (ed) ed->id = (uint32_t)id;
    return ed;
}

// Start editing by simulating a focus + left-button-up at x=0 of the edit box.
static void begin_editing(window_t *ed) {
    set_focus(ed);
    send_message(ed, evLeftButtonUp, MAKEDWORD(3, 5), NULL);
}

// ── tests ─────────────────────────────────────────────────────────────────

void test_edit_initial_text(void) {
    TEST("win_textedit: create with initial text preserves it");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               edit_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *ed = make_edit(parent, 1, "hello");
    ASSERT_NOT_NULL(ed);
    ASSERT_STR_EQUAL(ed->title, "hello");

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_edit_text_input_inserts_char(void) {
    TEST("win_textedit: evTextInput inserts character at cursor");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               edit_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *ed = make_edit(parent, 2, "ab");
    ASSERT_NOT_NULL(ed);

    // Start editing with the cursor near the start (click near x=0 sets pos=0).
    begin_editing(ed);
    // Move cursor to the end of "ab".
    send_message(ed, evKeyDown, AX_KEY_RIGHTARROW, NULL);
    send_message(ed, evKeyDown, AX_KEY_RIGHTARROW, NULL);

    // Insert 'c'
    char ch[2] = {'c', '\0'};
    send_message(ed, evTextInput, 0, ch);
    ASSERT_STR_EQUAL(ed->title, "abc");

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_edit_text_input_at_cursor(void) {
    TEST("win_textedit: evTextInput inserts at cursor position, not always at end");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               edit_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *ed = make_edit(parent, 3, "ac");
    ASSERT_NOT_NULL(ed);

    begin_editing(ed);
    // Move cursor right by 1 (cursor at pos=1, between 'a' and 'c')
    send_message(ed, evKeyDown, AX_KEY_RIGHTARROW, NULL);
    ASSERT_EQUAL(ed->cursor_pos, 1);

    char ch[2] = {'b', '\0'};
    send_message(ed, evTextInput, 0, ch);
    ASSERT_STR_EQUAL(ed->title, "abc");
    ASSERT_EQUAL(ed->cursor_pos, 2);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_edit_backspace_deletes(void) {
    TEST("win_textedit: Backspace removes character before cursor");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               edit_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *ed = make_edit(parent, 4, "abc");
    ASSERT_NOT_NULL(ed);

    begin_editing(ed);
    // Move to end (pos=3)
    send_message(ed, evKeyDown, AX_KEY_RIGHTARROW, NULL);
    send_message(ed, evKeyDown, AX_KEY_RIGHTARROW, NULL);
    send_message(ed, evKeyDown, AX_KEY_RIGHTARROW, NULL);
    ASSERT_EQUAL(ed->cursor_pos, 3);

    send_message(ed, evKeyDown, AX_KEY_BACKSPACE, NULL);
    ASSERT_STR_EQUAL(ed->title, "ab");
    ASSERT_EQUAL(ed->cursor_pos, 2);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_edit_backspace_at_start_noop(void) {
    TEST("win_textedit: Backspace at cursor pos=0 does nothing");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               edit_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *ed = make_edit(parent, 5, "hello");
    ASSERT_NOT_NULL(ed);

    begin_editing(ed);
    // Cursor is at 0 after click near start
    ASSERT_EQUAL(ed->cursor_pos, 0);

    send_message(ed, evKeyDown, AX_KEY_BACKSPACE, NULL);
    ASSERT_STR_EQUAL(ed->title, "hello"); // unchanged

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_edit_left_arrow_moves_cursor(void) {
    TEST("win_textedit: Left arrow decrements cursor pos");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               edit_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *ed = make_edit(parent, 6, "abc");
    ASSERT_NOT_NULL(ed);

    begin_editing(ed);
    // Move to pos=2
    send_message(ed, evKeyDown, AX_KEY_RIGHTARROW, NULL);
    send_message(ed, evKeyDown, AX_KEY_RIGHTARROW, NULL);
    ASSERT_EQUAL(ed->cursor_pos, 2);

    send_message(ed, evKeyDown, AX_KEY_LEFTARROW, NULL);
    ASSERT_EQUAL(ed->cursor_pos, 1);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_edit_right_arrow_moves_cursor(void) {
    TEST("win_textedit: Right arrow increments cursor pos");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               edit_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *ed = make_edit(parent, 7, "abc");
    ASSERT_NOT_NULL(ed);

    begin_editing(ed);
    ASSERT_EQUAL(ed->cursor_pos, 0);

    send_message(ed, evKeyDown, AX_KEY_RIGHTARROW, NULL);
    ASSERT_EQUAL(ed->cursor_pos, 1);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_edit_right_arrow_clamps_at_end(void) {
    TEST("win_textedit: Right arrow does not go past end of text");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               edit_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *ed = make_edit(parent, 8, "hi");
    ASSERT_NOT_NULL(ed);

    begin_editing(ed);
    // Jump past end
    for (int i = 0; i < 5; i++)
        send_message(ed, evKeyDown, AX_KEY_RIGHTARROW, NULL);

    ASSERT_EQUAL(ed->cursor_pos, (int)strlen("hi"));

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_edit_enter_commits_editing(void) {
    TEST("win_textedit: Enter commits editing and sends edUpdate to parent");

    test_env_init();
    reset_state();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               edit_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *ed = make_edit(parent, 9, "test");
    ASSERT_NOT_NULL(ed);

    begin_editing(ed);
    ASSERT_TRUE(window_has_state(ed, WINDOW_STATE_EDITING));

    send_message(ed, evKeyDown, AX_KEY_ENTER, NULL);

    ASSERT_FALSE(window_has_state(ed, WINDOW_STATE_EDITING));
    ASSERT_EQUAL(g_update_count, 1);
    ASSERT_EQUAL(g_last_edit, ed);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_edit_enter_starts_editing_if_not_editing(void) {
    TEST("win_textedit: Enter when not editing starts editing and moves cursor to end");

    test_env_init();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               edit_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *ed = make_edit(parent, 10, "foo");
    ASSERT_NOT_NULL(ed);

    // Edit box is not in editing mode yet
    ASSERT_FALSE(window_has_state(ed, WINDOW_STATE_EDITING));

    send_message(ed, evKeyDown, AX_KEY_ENTER, NULL);

    ASSERT_TRUE(window_has_state(ed, WINDOW_STATE_EDITING));
    ASSERT_EQUAL(ed->cursor_pos, (int)strlen("foo"));

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_edit_escape_exits_editing(void) {
    TEST("win_textedit: Escape exits editing mode without sending edUpdate");

    test_env_init();
    reset_state();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               edit_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *ed = make_edit(parent, 11, "text");
    ASSERT_NOT_NULL(ed);

    begin_editing(ed);
    ASSERT_TRUE(window_has_state(ed, WINDOW_STATE_EDITING));

    send_message(ed, evKeyDown, AX_KEY_ESCAPE, NULL);

    ASSERT_FALSE(window_has_state(ed, WINDOW_STATE_EDITING));
    ASSERT_EQUAL(g_update_count, 0); // no edUpdate on Escape

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_edit_tab_commits_editing(void) {
    TEST("win_textedit: Tab commits editing and sends edUpdate to parent");

    test_env_init();
    reset_state();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               edit_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *ed = make_edit(parent, 12, "tab");
    ASSERT_NOT_NULL(ed);

    begin_editing(ed);
    ASSERT_TRUE(window_has_state(ed, WINDOW_STATE_EDITING));

    // Tab while editing: must commit (editing=false) and fire edUpdate.
    // Note: win_textedit returns false for Tab even when it handles it,
    // so we only verify the side-effects, not the return value.
    send_message(ed, evKeyDown, AX_KEY_TAB, NULL);

    ASSERT_FALSE(window_has_state(ed, WINDOW_STATE_EDITING));
    ASSERT_EQUAL(g_update_count, 1);
    ASSERT_EQUAL(g_last_edit, ed);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

void test_edit_tab_noop_when_not_editing(void) {
    TEST("win_textedit: Tab when not editing does not fire edUpdate");

    test_env_init();
    reset_state();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               edit_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    window_t *ed = make_edit(parent, 13, "noedit");
    ASSERT_NOT_NULL(ed);

    ASSERT_FALSE(window_has_state(ed, WINDOW_STATE_EDITING));

    send_message(ed, evKeyDown, AX_KEY_TAB, NULL);

    ASSERT_FALSE(window_has_state(ed, WINDOW_STATE_EDITING));
    ASSERT_EQUAL(g_update_count, 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// ── main ──────────────────────────────────────────────────────────────────

void test_edit_change_notifies_parent_per_edit(void) {
    TEST("win_textedit: every typed char and backspace sends ednChange to the parent");

    test_env_init();
    reset_state();
    window_t *parent = test_env_create_window("P", 0, 0, 200, 100,
                                               edit_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);
    window_t *ed = make_edit(parent, 9, "");
    ASSERT_NOT_NULL(ed);
    ASSERT_TRUE(send_message(ed, edSetPlaceholder, 0, "Search..."));

    begin_editing(ed);
    send_message(ed, evTextInput, 0, "h");
    send_message(ed, evTextInput, 0, "i");
    ASSERT_EQUAL(g_change_count, 2);
    send_message(ed, evKeyDown, AX_KEY_BACKSPACE, NULL);
    ASSERT_STR_EQUAL(ed->title, "h");
    ASSERT_EQUAL(g_change_count, 3);
    ASSERT_EQUAL(g_update_count, 0);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

static void test_edit_leading_icon(void) {
  TEST("leading icon reserves text space and cursor hit-testing without changing text or focus");
  test_env_init();
  reset_state();
  window_t *parent = test_env_create_window("P", 0, 0, 400, 100, edit_parent_proc, NULL);
  const char *text = "                                        ";
  window_t *ed = make_edit(parent, 9, text);
  ASSERT_NOT_NULL(ed);
  layout_measure_t before = {0}, after = {0};
  send_message(ed, evMeasure, 0, &before);
  send_message(ed, edSetPlaceholder, 0, "Search...");
  begin_editing(ed);
  ed->cursor_pos = 2;
  char icon[] = "search";
  ASSERT_TRUE(send_message(ed, edSetLeadingIcon, 0, icon));
  icon[0] = '\0';
  send_message(ed, edSetPlaceholder, 0, "Find...");
  send_message(ed, evMeasure, 0, &after);
  // The icon sits in the capsule end (same inset as top/bottom), then a 6 px gap.
  int size = MIN(20, MAX(0, ed->frame.h - 4)), text_x = (ed->frame.h - size) / 2 + size + 6;
  int inset = text_x - TEXTEDIT_PADDING_HORZ;
  ASSERT(text_x < TEXTEDIT_PADDING_HORZ + size + TEXTEDIT_PADDING_HORZ, "the icon padding is tighter than plain text padding on both sides");
  ASSERT_EQUAL(after.desired_w, before.desired_w + inset);
  ASSERT_TRUE(g_ui_runtime.focused == ed);
  ASSERT_EQUAL(ed->cursor_pos, 2);
  ASSERT_EQUAL(g_change_count, 0);
  int x = TEXTEDIT_PADDING_HORZ + inset + text_strnwidth(FONT_SMALL, ed->title, 2);
  send_message(ed, evLeftButtonUp, MAKEDWORD(x, ed->frame.h / 2), NULL);
  ASSERT_EQUAL(ed->cursor_pos, 2);
  send_message(ed, evLeftButtonUp, MAKEDWORD(TEXTEDIT_PADDING_HORZ, ed->frame.h / 2), NULL);
  ASSERT_EQUAL(ed->cursor_pos, 0);
  ASSERT_TRUE(send_message(ed, edSetLeadingIcon, 0, NULL));
  send_message(ed, evMeasure, 0, &after);
  ASSERT_EQUAL(after.desired_w, before.desired_w);
  x = TEXTEDIT_PADDING_HORZ + text_strnwidth(FONT_SMALL, ed->title, 2);
  send_message(ed, evLeftButtonUp, MAKEDWORD(x, ed->frame.h / 2), NULL);
  ASSERT_EQUAL(ed->cursor_pos, 2);
  ASSERT_STR_EQUAL(ed->title, text);
  destroy_window(parent);
  test_env_shutdown();
  PASS();
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    TEST_START("win_textedit tests");

    test_edit_initial_text();
    test_edit_text_input_inserts_char();
    test_edit_text_input_at_cursor();
    test_edit_backspace_deletes();
    test_edit_backspace_at_start_noop();
    test_edit_left_arrow_moves_cursor();
    test_edit_right_arrow_moves_cursor();
    test_edit_right_arrow_clamps_at_end();
    test_edit_enter_commits_editing();
    test_edit_enter_starts_editing_if_not_editing();
    test_edit_escape_exits_editing();
    test_edit_tab_commits_editing();
    test_edit_tab_noop_when_not_editing();
    test_edit_change_notifies_parent_per_edit();
    test_edit_leading_icon();

    TEST_END();
}

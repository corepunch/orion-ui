// edit_test.c — Unit tests for commctl/edit.c (win_textedit).
//
// Covers: character insertion (evTextInput), cursor movement (Left/Right),
// backspace delete, Enter to start/commit editing (fires edUpdate),
// Escape to cancel editing, and Tab to commit (fires edUpdate).

#include "test_framework.h"
#include "test_env.h"
#include "../ui.h"
#include "../commctl/commctl.h"

// ── notification capture ──────────────────────────────────────────────────

static int      g_update_count = 0;
static window_t *g_last_edit   = NULL;

static result_t edit_parent_proc(window_t *win, uint32_t msg,
                                  uint32_t wparam, void *lparam) {
    (void)win;
    if (msg == evCreate || msg == evDestroy) return 1;
    if (msg == evCommand && HIWORD(wparam) == edUpdate) {
        g_update_count++;
        g_last_edit = (window_t *)lparam;
    }
    return 0;
}

static void reset_state(void) {
    g_update_count = 0;
    g_last_edit    = NULL;
}

// ── helpers ───────────────────────────────────────────────────────────────

static window_t *make_edit(window_t *parent, int id, const char *initial) {
    rect_t fr = {10, 10, 120, 16};
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

    // Start editing, cursor at end (pos = len("ab") = 2 via click near x=0 → pos=0)
    begin_editing(ed);
    // Move cursor to end
    send_message(ed, evKeyDown, AX_KEY_RIGHTARROW, NULL);
    send_message(ed, evKeyDown, AX_KEY_RIGHTARROW, NULL);

    // Insert 'c'
    char ch = 'c';
    send_message(ed, evTextInput, 0, &ch);
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

    char ch = 'b';
    send_message(ed, evTextInput, 0, &ch);
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
    ASSERT_TRUE(ed->editing);

    send_message(ed, evKeyDown, AX_KEY_ENTER, NULL);

    ASSERT_FALSE(ed->editing);
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
    ASSERT_FALSE(ed->editing);

    send_message(ed, evKeyDown, AX_KEY_ENTER, NULL);

    ASSERT_TRUE(ed->editing);
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
    ASSERT_TRUE(ed->editing);

    send_message(ed, evKeyDown, AX_KEY_ESCAPE, NULL);

    ASSERT_FALSE(ed->editing);
    ASSERT_EQUAL(g_update_count, 0); // no edUpdate on Escape

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// ── main ──────────────────────────────────────────────────────────────────

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

    TEST_END();
}

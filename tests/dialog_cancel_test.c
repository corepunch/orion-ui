// Dialog Cancel Crash Test
//
// Regression test for: pressing "Cancel" on a modal dialog crashes with
// SIGSEGV at 0x0 (null function-pointer dereference) on macOS.
//
// Root cause: win_button calls invalidate_window(win) AFTER the send_message
// that fires evCommand.  If the command handler calls end_dialog,
// end_dialog destroys the dialog and all its children – including the Cancel
// button itself – so 'win' is freed by the time invalidate_window is called.
// invalidate_window calls get_root_window(win) which dereferences win->parent
// on freed memory; on macOS this raises SIGSEGV.
//
// Fix: in win_button (and win_toolbar_button), call invalidate_window(win)
// BEFORE send_message(root, evCommand, ...).  invalidate_window
// just posts async repaint messages, so there is no visible difference in
// behaviour, but win is guaranteed to be alive at that point.
//
// A secondary guard: repost_messages() validates every target window with
// is_valid_window_ptr() before dispatching, so any stale messages that do
// make it into the queue are harmlessly dropped.

#include "test_framework.h"
#include "test_env.h"
#include "../ui.h"

extern void end_dialog(window_t *win, uint32_t code);

// Tracks whether the dialog's Cancel command was received.
static bool g_cancel_received = false;

// Minimal dialog window procedure.  When it sees a kButtonNotificationClicked
// command from a "Cancel" child it calls end_dialog, which destroys the
// dialog window (and all its children, including the button that is still
// on the call stack).
static result_t dialog_proc(window_t *win, uint32_t msg,
                             uint32_t wparam, void *lparam) {
    switch (msg) {
        case evCreate:
            return 1;
        case evCommand: {
            uint16_t code = HIWORD(wparam);
            if (code == kButtonNotificationClicked) {
                window_t *btn = (window_t *)lparam;
                if (btn && strcmp(btn->title, "Cancel") == 0) {
                    g_cancel_received = true;
                    end_dialog(win, 0);
                    return 1;
                }
            }
            return 0;
        }
        case evDestroy:
            return 1;
        default:
            return 0;
    }
}

// Test: clicking Cancel on a dialog must not crash repost_messages.
void test_dialog_cancel_no_crash(void) {
    TEST("Dialog Cancel – repost_messages does not crash on freed button pointer");

    test_env_init();
    g_cancel_received = false;

    // Create a root parent window (acts as the owner of the dialog).
    window_t *parent = test_env_create_window("Owner", 0, 0, 400, 300,
                                              dialog_proc, NULL);
    ASSERT_NOT_NULL(parent);

    // Create the dialog window (child-less root, like show_dialog would).
    rect_t dlg_frame = {50, 50, 200, 150};
    window_t *dlg = create_window("Test Dialog", WINDOW_DIALOG | WINDOW_NOTITLE,
                                  &dlg_frame, NULL, dialog_proc, 0, NULL);
    ASSERT_NOT_NULL(dlg);

    // Create the Cancel button as a child of the dialog.
    rect_t btn_frame = {70, 110, 50, 20};
    window_t *cancel_btn = create_window("Cancel", 0, &btn_frame,
                                         dlg, win_button, 0, NULL);
    ASSERT_NOT_NULL(cancel_btn);

    int cx = btn_frame.x + btn_frame.w / 2;
    int cy = btn_frame.y + btn_frame.h / 2;

    // evLeftButtonDown just sets win->pressed = true and
    // calls invalidate_window(win); it never destroys the window, so
    // cancel_btn remains valid after this call.
    send_message(cancel_btn, evLeftButtonDown, MAKEDWORD(cx, cy), NULL);

    // evLeftButtonUp triggers the full destroy chain inside
    // send_message (synchronously), exactly as dispatch_message → handle_mouse
    // would do in production.  With the fix, win_button calls invalidate_window
    // BEFORE send_message(root, evCommand), so 'win' is still alive
    // when invalidate_window runs.  The command handler then calls end_dialog →
    // destroy_window(dlg) → destroy_window(cancel_btn) → free(cancel_btn).
    // Any stale messages remaining in the queue are safely skipped by the
    // is_valid_window_ptr guard in repost_messages.
    send_message(cancel_btn, evLeftButtonUp, MAKEDWORD(cx, cy), NULL);

    // repost_messages processes the queue.  The is_valid_window_ptr guard
    // ensures any stale messages (if any leaked through) are dropped safely.
    repost_messages();

    // The dialog was destroyed; cancel command must have been processed.
    ASSERT_TRUE(g_cancel_received);

    // The parent window is still alive; clean it up.
    destroy_window(parent);

    test_env_shutdown();
    PASS();
}

// Parent proc for the regular-click test.
static int g_cmd_count = 0;

static result_t regular_parent_proc(window_t *win, uint32_t msg,
                                    uint32_t wparam, void *lparam) {
    (void)win; (void)lparam;
    if (msg == evCommand &&
        HIWORD(wparam) == kButtonNotificationClicked) {
        g_cmd_count++;
        return 1;
    }
    return msg == evCreate || msg == evDestroy;
}

// Test: normal button click (no dialog close) still fires correctly.
void test_regular_button_click_unaffected(void) {
    TEST("Regular button click still fires after repost_messages guard");

    test_env_init();
    test_env_enable_tracking(true);
    test_env_clear_events();
    g_cmd_count = 0;

    window_t *parent = test_env_create_window("P", 0, 0, 300, 200,
                                              regular_parent_proc, NULL);
    ASSERT_NOT_NULL(parent);

    rect_t btn_frame = {10, 10, 60, 20};
    window_t *btn = create_window("OK", 0, &btn_frame, parent, win_button, 0, NULL);
    ASSERT_NOT_NULL(btn);

    int cx = btn_frame.x + btn_frame.w / 2;
    int cy = btn_frame.y + btn_frame.h / 2;

    post_message(btn, evLeftButtonDown, MAKEDWORD(cx, cy), NULL);
    post_message(btn, evLeftButtonUp,   MAKEDWORD(cx, cy), NULL);
    repost_messages();

    ASSERT_EQUAL(g_cmd_count, 1);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

// ---------------------------------------------------------------------------
// end_dialog return-code tests
//
// These tests validate the per-dialog context mechanism introduced to fix
// the global _return_code reentrancy bug.  show_dialog stores a plain
// uint32_t* in userdata2; end_dialog writes through that pointer.
// The tests simulate this without running the SDL event loop.
// ---------------------------------------------------------------------------

// Test: end_dialog writes the correct return code into the per-dialog context.
void test_end_dialog_sets_return_code(void) {
    TEST("end_dialog stores correct return code in per-dialog context");

    test_env_init();

    rect_t dlg_frame = {50, 50, 200, 150};
    window_t *dlg = create_window("ReturnCode Dialog",
                                  WINDOW_DIALOG | WINDOW_NOTITLE,
                                  &dlg_frame, NULL, dialog_proc, 0, NULL);
    ASSERT_NOT_NULL(dlg);

    // Simulate what show_dialog does: store the result pointer in userdata2.
    uint32_t result = 0;
    dlg->userdata2 = &result;

    // end_dialog should write 42 into result and destroy the dialog.
    end_dialog(dlg, 42);

    // The dialog window should now be gone.
    ASSERT_FALSE(is_window(dlg));
    // And our result variable should contain the code passed to end_dialog.
    ASSERT_EQUAL((int)result, 42);

    test_env_shutdown();
    PASS();
}

// Test: nested dialogs each get their own result (reentrancy).
// Simulate two show_dialog calls by manually setting up two result variables
// and verifying that end_dialog writes to the correct one via userdata2.
void test_end_dialog_reentrancy(void) {
    TEST("end_dialog reentrancy: each dialog context holds its own result");

    test_env_init();

    rect_t f1 = {10, 10, 200, 150};
    rect_t f2 = {20, 20, 200, 150};

    window_t *dlg1 = create_window("Outer", WINDOW_DIALOG | WINDOW_NOTITLE,
                                   &f1, NULL, dialog_proc, 0, NULL);
    window_t *dlg2 = create_window("Inner", WINDOW_DIALOG | WINDOW_NOTITLE,
                                   &f2, NULL, dialog_proc, 0, NULL);
    ASSERT_NOT_NULL(dlg1);
    ASSERT_NOT_NULL(dlg2);

    uint32_t result1 = 0;
    uint32_t result2 = 0;
    dlg1->userdata2 = &result1;
    dlg2->userdata2 = &result2;

    // Close inner dialog with code 7, outer with code 3.
    end_dialog(dlg2, 7);
    end_dialog(dlg1, 3);

    ASSERT_EQUAL((int)result1, 3);
    ASSERT_EQUAL((int)result2, 7);

    test_env_shutdown();
    PASS();
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    TEST_START("Dialog Cancel Crash Regression");

    test_dialog_cancel_no_crash();
    test_regular_button_click_unaffected();
    test_end_dialog_sets_return_code();
    test_end_dialog_reentrancy();

    TEST_END();
}

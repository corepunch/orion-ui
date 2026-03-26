// Dialog Cancel Crash Test
//
// Regression test for: pressing "Cancel" on a modal dialog crashes with
// SIGSEGV at 0x0 (null function-pointer dereference inside repost_messages).
//
// Root cause: win_button calls invalidate_window(win) after the send_message
// that triggers end_dialog.  end_dialog destroys the dialog and all its
// children – including the Cancel button itself – so 'win' is freed by the
// time invalidate_window is called.  The resulting post_message queues a
// stale pointer; repost_messages then calls send_message on freed memory
// whose proc field happens to be NULL, causing the crash.
//
// The fix in repost_messages validates every target window against the live
// window tree before dispatching it.

#include "test_framework.h"
#include "test_env.h"
#include "../ui.h"

extern void repost_messages(void);
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
        case kWindowMessageCreate:
            return 1;
        case kWindowMessageCommand: {
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
        case kWindowMessageDestroy:
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
                                  &dlg_frame, NULL, dialog_proc, NULL);
    ASSERT_NOT_NULL(dlg);

    // Create the Cancel button as a child of the dialog.
    rect_t btn_frame = {70, 110, 50, 20};
    window_t *cancel_btn = create_window("Cancel", 0, &btn_frame,
                                         dlg, win_button, NULL);
    ASSERT_NOT_NULL(cancel_btn);

    int cx = btn_frame.x + btn_frame.w / 2;
    int cy = btn_frame.y + btn_frame.h / 2;

    // kWindowMessageLeftButtonDown just sets win->pressed = true and
    // calls invalidate_window(win); it never destroys the window, so
    // cancel_btn remains valid after this call.
    send_message(cancel_btn, kWindowMessageLeftButtonDown, MAKEDWORD(cx, cy), NULL);

    // kWindowMessageLeftButtonUp triggers the full destroy chain inside
    // send_message (synchronously), exactly as dispatch_message → handle_mouse
    // would do in production.  During this call win_button will:
    //   1. Call send_message(dialog, kWindowMessageCommand, ...) →
    //        dialog_proc → end_dialog → destroy_window(dlg) →
    //        destroy_window(cancel_btn) → free(cancel_btn)
    //   2. Then call invalidate_window(freed_cancel_btn) →
    //        post_message(freed_cancel_btn, kWindowMessagePaint)
    // The stale message is now sitting in the queue.
    send_message(cancel_btn, kWindowMessageLeftButtonUp, MAKEDWORD(cx, cy), NULL);

    // Before the fix, repost_messages would SIGSEGV (pc=0x0) because it
    // dispatched the stale kWindowMessagePaint targeting the freed cancel_btn,
    // whose proc field reads as NULL from freed memory.
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
    if (msg == kWindowMessageCommand &&
        HIWORD(wparam) == kButtonNotificationClicked) {
        g_cmd_count++;
        return 1;
    }
    return msg == kWindowMessageCreate || msg == kWindowMessageDestroy;
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
    window_t *btn = create_window("OK", 0, &btn_frame, parent, win_button, NULL);
    ASSERT_NOT_NULL(btn);

    int cx = btn_frame.x + btn_frame.w / 2;
    int cy = btn_frame.y + btn_frame.h / 2;

    post_message(btn, kWindowMessageLeftButtonDown, MAKEDWORD(cx, cy), NULL);
    post_message(btn, kWindowMessageLeftButtonUp,   MAKEDWORD(cx, cy), NULL);
    repost_messages();

    ASSERT_EQUAL(g_cmd_count, 1);

    destroy_window(parent);
    test_env_shutdown();
    PASS();
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    TEST_START("Dialog Cancel Crash Regression");

    test_dialog_cancel_no_crash();
    test_regular_button_click_unaffected();

    TEST_END();
}

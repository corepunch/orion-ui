// Palette Drag Tests
// Tests for the drag-by-header behavior added to the floating palette windows.
//
// The palette procs (win_tool_palette_proc, win_color_palette_proc) live inside
// the imageeditor example and cannot be linked into a standalone test binary.
// Instead we define a minimal "drag-header" window proc here that implements
// exactly the same pattern; the tests exercise the mechanism end-to-end using
// the real window / message / capture infrastructure from liborion.
//
// Behavior under test:
//   - Click in header area  → capture set, dragging flag true
//   - Click below header    → capture NOT set, dragging flag false
//   - MouseMove while dragging  → window position updated by (dx, dy)
//   - MouseMove without drag    → window position unchanged
//   - LeftButtonUp ends drag    → capture released, dragging flag cleared
//   - Multiple drag steps accumulate correctly

#include "test_framework.h"
#include "../ui.h"
#include <stdlib.h>
#include <string.h>

extern window_t *_captured;

// ---- minimal drag-header window proc ----------------------------------------

#define TEST_HEADER_H 14

typedef struct {
    bool dragging;
} drag_data_t;

static result_t drag_win_proc(window_t *win, uint32_t msg,
                               uint32_t wparam, void *lparam) {
    drag_data_t *d = (drag_data_t *)win->userdata;
    switch (msg) {
        case kWindowMessageCreate: {
            drag_data_t *nd = malloc(sizeof(drag_data_t));
            memset(nd, 0, sizeof(drag_data_t));
            win->userdata = nd;
            d = nd;
            return 1;
        }
        case kWindowMessageLeftButtonDown: {
            int ly = (int16_t)HIWORD(wparam);
            if (ly < TEST_HEADER_H) {
                if (d) d->dragging = true;
                set_capture(win);
            }
            return 1;
        }
        case kWindowMessageMouseMove: {
            if (!d || !d->dragging) return 0;
            int16_t dx = (int16_t)LOWORD((uint32_t)(intptr_t)lparam);
            int16_t dy = (int16_t)HIWORD((uint32_t)(intptr_t)lparam);
            move_window(win, win->frame.x + dx, win->frame.y + dy);
            return 1;
        }
        case kWindowMessageLeftButtonUp: {
            if (d && d->dragging) {
                d->dragging = false;
                set_capture(NULL);
            }
            return 1;
        }
        case kWindowMessageDestroy: {
            if (d && d->dragging) set_capture(NULL);
            free(d);
            win->userdata = NULL;
            return 1;
        }
        default:
            return 0;
    }
}

// Helper: create a palette-style window (no parent, NOTITLE|NORESIZE flags).
static window_t *make_palette_win(int x, int y, int w, int h) {
    rect_t frame = {x, y, w, h};
    return create_window("Palette",
                         WINDOW_NOTITLE | WINDOW_NORESIZE,
                         &frame, NULL, drag_win_proc, NULL);
}

// Helper: deliver a MouseMove with relative deltas (dx, dy) to a window.
// Mirrors the calling convention used by kernel/event.c and the palette procs.
static void send_mouse_move(window_t *win, int x, int y, int dx, int dy) {
    send_message(win, kWindowMessageMouseMove,
                 MAKEDWORD((uint16_t)x, (uint16_t)y),
                 (void *)(intptr_t)MAKEDWORD((uint16_t)dx, (uint16_t)dy));
}

// ---- tests ------------------------------------------------------------------

void test_header_click_starts_drag(void) {
    TEST("Header click sets capture and marks dragging");

    window_t *win = make_palette_win(100, 200, 64, 120);
    ASSERT_NOT_NULL(win);
    ASSERT_NULL(_captured);

    // Click inside the header (y < TEST_HEADER_H).
    send_message(win, kWindowMessageLeftButtonDown, MAKEDWORD(10, 5), NULL);

    ASSERT_EQUAL(_captured, win);
    drag_data_t *d = (drag_data_t *)win->userdata;
    ASSERT_NOT_NULL(d);
    ASSERT_TRUE(d->dragging);

    destroy_window(win);
    PASS();
}

void test_body_click_does_not_start_drag(void) {
    TEST("Click below header does not start drag");

    window_t *win = make_palette_win(100, 200, 64, 120);
    ASSERT_NOT_NULL(win);

    // Click below the header.
    send_message(win, kWindowMessageLeftButtonDown,
                 MAKEDWORD(10, TEST_HEADER_H + 5), NULL);

    ASSERT_NULL(_captured);
    drag_data_t *d = (drag_data_t *)win->userdata;
    ASSERT_NOT_NULL(d);
    ASSERT_FALSE(d->dragging);

    destroy_window(win);
    PASS();
}

void test_drag_moves_window(void) {
    TEST("MouseMove while dragging moves the window by the delta");

    window_t *win = make_palette_win(100, 200, 64, 120);
    ASSERT_NOT_NULL(win);

    // Start drag.
    send_message(win, kWindowMessageLeftButtonDown, MAKEDWORD(10, 5), NULL);
    ASSERT_TRUE(((drag_data_t *)win->userdata)->dragging);

    int orig_x = win->frame.x; // 100
    int orig_y = win->frame.y; // 200

    // Move +20, +15.
    send_mouse_move(win, 0, 0, 20, 15);

    ASSERT_EQUAL(win->frame.x, orig_x + 20);
    ASSERT_EQUAL(win->frame.y, orig_y + 15);

    destroy_window(win);
    PASS();
}

void test_drag_moves_window_negative_delta(void) {
    TEST("MouseMove with negative delta moves window upward/leftward");

    window_t *win = make_palette_win(100, 200, 64, 120);
    ASSERT_NOT_NULL(win);

    send_message(win, kWindowMessageLeftButtonDown, MAKEDWORD(10, 5), NULL);

    int orig_x = win->frame.x;
    int orig_y = win->frame.y;

    // Move -10, -30 (drag upward and left).
    send_mouse_move(win, 0, 0, -10, -30);

    ASSERT_EQUAL(win->frame.x, orig_x - 10);
    ASSERT_EQUAL(win->frame.y, orig_y - 30);

    destroy_window(win);
    PASS();
}

void test_drag_accumulates_multiple_moves(void) {
    TEST("Consecutive MouseMove deltas accumulate correctly");

    window_t *win = make_palette_win(50, 50, 64, 120);
    ASSERT_NOT_NULL(win);

    send_message(win, kWindowMessageLeftButtonDown, MAKEDWORD(10, 5), NULL);

    int orig_x = win->frame.x;
    int orig_y = win->frame.y;

    send_mouse_move(win, 0, 0,  5,  3);
    send_mouse_move(win, 0, 0, -2,  7);
    send_mouse_move(win, 0, 0, 10, -4);

    // Net delta: x += 5 - 2 + 10 = 13, y += 3 + 7 - 4 = 6
    ASSERT_EQUAL(win->frame.x, orig_x + 13);
    ASSERT_EQUAL(win->frame.y, orig_y +  6);

    destroy_window(win);
    PASS();
}

void test_button_up_ends_drag_and_releases_capture(void) {
    TEST("LeftButtonUp ends drag and releases capture");

    window_t *win = make_palette_win(100, 200, 64, 120);
    ASSERT_NOT_NULL(win);

    send_message(win, kWindowMessageLeftButtonDown, MAKEDWORD(10, 5), NULL);
    ASSERT_EQUAL(_captured, win);

    send_message(win, kWindowMessageLeftButtonUp, MAKEDWORD(10, 5), NULL);

    ASSERT_NULL(_captured);
    ASSERT_FALSE(((drag_data_t *)win->userdata)->dragging);

    destroy_window(win);
    PASS();
}

void test_mousemove_without_drag_is_noop(void) {
    TEST("MouseMove without drag does not move the window");

    window_t *win = make_palette_win(100, 200, 64, 120);
    ASSERT_NOT_NULL(win);

    int orig_x = win->frame.x;
    int orig_y = win->frame.y;

    // No drag started — MouseMove should be ignored.
    send_mouse_move(win, 0, 0, 20, 15);

    ASSERT_EQUAL(win->frame.x, orig_x);
    ASSERT_EQUAL(win->frame.y, orig_y);

    destroy_window(win);
    PASS();
}

void test_mousemove_ignored_after_drag_ends(void) {
    TEST("MouseMove after LeftButtonUp does not move the window");

    window_t *win = make_palette_win(100, 200, 64, 120);
    ASSERT_NOT_NULL(win);

    // Full drag cycle: down, move, up.
    send_message(win, kWindowMessageLeftButtonDown, MAKEDWORD(10, 5), NULL);
    send_mouse_move(win, 0, 0, 10, 10);
    send_message(win, kWindowMessageLeftButtonUp, MAKEDWORD(10, 5), NULL);

    int x_after_drag = win->frame.x; // 110
    int y_after_drag = win->frame.y; // 210

    // Further moves should be ignored now that drag ended.
    send_mouse_move(win, 0, 0, 50, 50);

    ASSERT_EQUAL(win->frame.x, x_after_drag);
    ASSERT_EQUAL(win->frame.y, y_after_drag);

    destroy_window(win);
    PASS();
}

// ---- main -------------------------------------------------------------------

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    TEST_START("Palette Window Drag-by-Header");

    test_header_click_starts_drag();
    test_body_click_does_not_start_drag();
    test_drag_moves_window();
    test_drag_moves_window_negative_delta();
    test_drag_accumulates_multiple_moves();
    test_button_up_ends_drag_and_releases_capture();
    test_mousemove_without_drag_is_noop();
    test_mousemove_ignored_after_drag_ends();

    TEST_END();
}

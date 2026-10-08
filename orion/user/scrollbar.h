#ifndef __UI_SCROLLBAR_H__
#define __UI_SCROLLBAR_H__

#include <stdbool.h>
#include <stdint.h>

#include "user.h"

bool scrollbar_handle_builtin_mouse(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
void scrollbar_handle_builtin_wheel(window_t *win, void *lparam);
bool scrollbar_handle_builtin_gesture(window_t *win, const ax_gesture_t *gesture);
// True when either built-in scrollbar has room to move.
bool scrollbar_can_scroll(window_t *win);
// Momentum after a touch swipe: content velocity in points/ms per axis,
// decaying by FLING_DECAY until it stops or reaches an edge.
void scrollbar_fling(window_t *win, float vx, float vy);
// Stops momentum; returns true when the window was still moving.
bool scrollbar_stop_fling(window_t *win);
bool scrollbar_is_flinging(const window_t *win);
// Called from message.c on evTimer for windows with built-in scrollbars.
// Checks whether timer_id matches a pending overlay-hide timer and, if so,
// hides the thumb.  Does not consume the timer event.
void scrollbar_handle_builtin_timer(window_t *win, uint32_t timer_id);
void scrollbar_draw_statusbar_merged_hscroll(window_t *win, irect16_t row, int split_x);

#endif

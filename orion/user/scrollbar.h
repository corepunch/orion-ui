#ifndef __UI_SCROLLBAR_H__
#define __UI_SCROLLBAR_H__

#include <stdbool.h>
#include <stdint.h>

#include "user.h"

bool scrollbar_handle_builtin_mouse(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
void scrollbar_handle_builtin_wheel(window_t *win, void *lparam);
// Called from message.c on evTimer for windows with built-in scrollbars.
// Checks whether timer_id matches a pending overlay-hide timer and, if so,
// hides the thumb.  Does not consume the timer event.
void scrollbar_handle_builtin_timer(window_t *win, uint32_t timer_id);
void scrollbar_draw_statusbar_merged_hscroll(window_t *win, irect16_t row, int split_x);

#endif
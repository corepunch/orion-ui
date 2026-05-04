// win_gradient — horizontal gradient bar control.
//
// Draws a left-to-right colour gradient filling the entire client area.
// Default colours are black (left) to white (right).
//
// Messages:
//   grSetColors(wparam=left_rgba, lparam=right_rgba) — change gradient colours.

#include <stdbool.h>
#include <stdint.h>

#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"
#include "commctl.h"

typedef struct {
  uint32_t left_color;
  uint32_t right_color;
} gradient_state_t;

result_t win_gradient(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  gradient_state_t *s = (gradient_state_t *)win->userdata;
  switch (msg) {
    case evCreate: {
      gradient_state_t *ns = allocate_window_data(win, sizeof(gradient_state_t));
      ns->left_color  = 0xFF000000u;
      ns->right_color = 0xFFFFFFFFu;
      win->flags |= WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOTABSTOP;
      return true;
    }

    case evPaint:
      if (!s) return true;
      draw_gradient_rect(get_client_rect(win), s->left_color, s->right_color);
      return true;

    case grSetColors:
      if (!s) return false;
      s->left_color  = wparam;
      s->right_color = (uint32_t)(uintptr_t)lparam;
      invalidate_window(win);
      return true;

    default:
      return false;
  }
}

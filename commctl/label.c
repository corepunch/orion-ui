#include <string.h>
#include <stdio.h>
#include <math.h>

#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"

#define PADDING 3

// Helper function (will be moved to ui/user/window.c later)
extern window_t *_focused;

// Label control window procedure.
// lparam in kWindowMessageCreate is an optional RGBA color (void*)(uintptr_t)col.
// When lparam is NULL the default kColorTextNormal is used.
result_t win_label(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case kWindowMessageCreate:
      win->frame.w = MAX(win->frame.w, strwidth(win->title));
      win->notabstop = true;
      if (lparam) win->userdata = lparam;
      return true;
    case kWindowMessagePaint: {
      // Convention: userdata == 0 → default (kColorTextNormal);
      // 0 < userdata < kColorCount → sys_color_idx_t index resolved at paint time;
      // userdata >= kColorCount → raw RGBA color (top byte is 0xff for any valid RGBA).
      uint32_t col;
      uintptr_t ud = (uintptr_t)win->userdata;
      if (ud == 0)
        col = get_sys_color(kColorTextNormal);
      else if (ud < (uintptr_t)kColorCount)
        col = get_sys_color((sys_color_idx_t)ud);
      else
        col = (uint32_t)ud;
      draw_text_small(win->title, win->frame.x+1, win->frame.y+1+PADDING, get_sys_color(kColorDarkEdge));
      draw_text_small(win->title, win->frame.x, win->frame.y+PADDING, col);
      return true;
    }
  }
  return false;
}

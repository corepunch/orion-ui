#include <string.h>
#include <stdio.h>
#include <math.h>

#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"
#include "../user/rect.h"
#include "../user/theme.h"

// Label control window procedure.
// lparam in evCreate is an optional RGBA color (void*)(uintptr_t)col.
// When lparam is NULL the default kColorTextNormal is used.
result_t win_label(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      win->frame.w = MAX(win->frame.w, strwidth(win->title));
      win->notabstop = true;
      if (lparam) win->userdata = lparam;
      return true;
    case evPaint: {
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
      rect_t text_pos = rect_offset(win->frame, 0, LABEL_TEXT_PADDING);
      draw_text_small(win->title, text_pos.x + TEXT_SHADOW_OFFSET, text_pos.y + TEXT_SHADOW_OFFSET, get_sys_color(kColorDarkEdge));
      draw_text_small(win->title, text_pos.x, text_pos.y, col);
      return true;
    }
  }
  return false;
}

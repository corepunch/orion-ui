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
// When lparam is NULL the default brTextNormal is used.
result_t win_label(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      win->frame.w = MAX(win->frame.w, text_strwidth(FONT_SMALL, win->title) + TEXT_SHADOW_OFFSET);
      win->notabstop = true;
      if (lparam) win->userdata = lparam;
      return true;
    case evPaint: {
      // Convention: userdata == 0 → default (brTextNormal);
      // 0 < userdata < brCount → sys_color_idx_t index resolved at paint time;
      // userdata >= brCount → raw RGBA color (top byte is 0xff for any valid RGBA).
      uint32_t col;
      uintptr_t ud = (uintptr_t)win->userdata;
      if (ud == 0)
        col = get_sys_color(brTextNormal);
      else if (ud < (uintptr_t)brCount)
        col = get_sys_color((sys_color_idx_t)ud);
      else
        col = (uint32_t)ud;
      rect_t text_pos = {0, LABEL_TEXT_PADDING, win->frame.w, win->frame.h};
      draw_text(FONT_SMALL, win->title, text_pos.x + TEXT_SHADOW_OFFSET, text_pos.y + TEXT_SHADOW_OFFSET, get_sys_color(brDarkEdge));
      draw_text(FONT_SMALL, win->title, text_pos.x, text_pos.y, col);
      return true;
    }
  }
  return false;
}

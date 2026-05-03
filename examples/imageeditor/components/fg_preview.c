#include "fg_preview.h"

static void fg_preview_draw_outline(irect16_t r, uint32_t col) {
  fill_rect(col, R(r.x, r.y, r.w, 1));
  fill_rect(col, R(r.x, r.y, 1, r.h));
  fill_rect(col, R(r.x, r.y + r.h - 1, r.w, 1));
  fill_rect(col, R(r.x + r.w - 1, r.y, 1, r.h));
}

result_t fg_preview_component_proc(window_t *win, uint32_t msg,
                                   uint32_t wparam, void *lparam) {
  fg_preview_data_t *st = (fg_preview_data_t *)win->userdata;
  (void)wparam;
  switch (msg) {
    case evCreate: {
      fg_preview_data_t *data = allocate_window_data(win, sizeof(fg_preview_data_t));
      if (lparam)
        *data = *(const fg_preview_data_t *)lparam;
      win->flags |= WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOTABSTOP;
      return true;
    }
    case fgPreviewSetData:
      if (!st || !lparam) return false;
      *st = *(const fg_preview_data_t *)lparam;
      invalidate_window(win);
      return true;
    case evPaint: {
      irect16_t r = R(0, 0, win->frame.w, win->frame.h);
      draw_checkerboard(r, 8);
      if (st && st->texture) {
        if (st->program)
          draw_program_rect((int)st->texture, r, st->program, 1.0f);
        else
          draw_rect((int)st->texture, r);
      } else {
        fill_rect(get_sys_color(brWorkspaceBg), r);
        irect16_t label = R(0, (r.h - CONTROL_HEIGHT) / 2, r.w, CONTROL_HEIGHT);
        draw_text_clipped(FONT_SMALL, "Filter Preview", &label,
                          get_sys_color(brTextDisabled), TEXT_ALIGN_CENTER);
      }
      fg_preview_draw_outline(r, get_sys_color(brBorderActive));
      return true;
    }
    default:
      return false;
  }
}

#include <string.h>
#include <stdio.h>
#include <math.h>

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/rect.h>
#include <orion/user/theme.h>

// Label control window procedure.
// lparam in evCreate may be either:
//   - NULL: default palette text color + FONT_SMALL
//   - a small integer palette index: text color only, FONT_SMALL
//   - label_create_params_t*: explicit palette index + font selection

static ui_font_t label_font(const window_t *win) {
  uint32_t packed = (uint32_t)(uintptr_t)(win ? win->userdata : NULL);
  ui_font_t font = (ui_font_t)((packed >> 8) & 0xffu);
  return font <= FONT_SMALLEST ? font : FONT_SMALL;
}

static uint32_t label_color(const window_t *win) {
  uint32_t packed = (uint32_t)(uintptr_t)(win ? win->userdata : NULL);
  uint8_t idx = (uint8_t)(packed & 0xffu);
  if ((packed & LABEL_PACK_COLOR_SET) == 0)
    return get_sys_color(brTextNormal);
  if (idx == 0)
    return get_sys_color(brTransparent);
  if (idx < (uint8_t)brCount)
    return get_sys_color((sys_color_idx_t)idx);
  return get_sys_color(brTextNormal);
}

static bool label_truncates(const window_t *win) {
  return win && (((uint32_t)(uintptr_t)win->userdata) & LABEL_PACK_TRUNCATE) != 0;
}

result_t win_label(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case lbSetStyle: {
      const label_create_params_t *p = (const label_create_params_t *)lparam;
      if (!p) return false;
      uint32_t packed = label_pack_userdata(p->color_index, p->font, p->color_set);
      if (p->truncate) packed |= LABEL_PACK_TRUNCATE;
      win->userdata = (void *)(uintptr_t)packed;
      invalidate_window(win);
      return true;
    }
    case evCreate:
      if (lparam && (uintptr_t)lparam > 0x1000) {
        const form_ctrl_def_t *cd = (const form_ctrl_def_t *)lparam;
        uint8_t color_idx = cd->color_set ? cd->color : 0;
        uint8_t font = cd->font_set ? cd->font : FONT_SMALL;
        win->userdata = (void *)(uintptr_t)label_pack_userdata(color_idx, font, cd->color_set);
      } else {
        uint32_t color_index = (uint32_t)(uintptr_t)lparam;
        win->userdata = (void *)(uintptr_t)label_pack_userdata(color_index, FONT_SMALL, lparam != NULL);
      }
      // Single-line labels auto-expand to fit text; multiline labels wrap and
      // should be measured from content instead of the previous frame size.
      {
        ui_font_t font = label_font(win);
        if (win->frame.h <= CONTROL_HEIGHT)
          win->frame.w = MAX(win->frame.w, text_strwidth(font, win->title) + TEXT_SHADOW_OFFSET);
      }
      win->flags |= WINDOW_NOTABSTOP;
      return true;
    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (!m) return true;
      ui_font_t font = label_font(win);
      if (label_truncates(win)) {
        // One line; the arranged width decides how much of it is visible.
        m->desired_w = text_strwidth(font, win->title) + TEXT_SHADOW_OFFSET;
        m->desired_h = MAX(CONTROL_HEIGHT, text_char_height(font) + TEXT_SHADOW_OFFSET);   // descenders must fit
        return true;
      }
      int avail_w = m->avail_w > 0 ? m->avail_w : win->frame.w;
      if (avail_w < 1) avail_w = win->frame.w > 0 ? win->frame.w : 1;
      irect16_t wrap_vp = {0, 0, avail_w, 1};
      text_wrap_result_t wrap = text_wrap_layout_font(font, win->title, &wrap_vp, 0, false);
      if (wrap.wrapped) {
        m->desired_w = avail_w;
        m->desired_h = wrap.height;
      } else {
        int text_w = MAX(wrap.width, text_strwidth(font, win->title));
        m->desired_w = text_w + TEXT_SHADOW_OFFSET;
        m->desired_h = CONTROL_HEIGHT;
      }
      return true;
    }
    case evPaint: {
      uint32_t col = label_color(win);
      ui_font_t font = label_font(win);
      irect16_t text_pos = {0, 0, win->frame.w, win->frame.h};
      if (label_truncates(win)) {
        char fitted[512];
        text_ellipsize(font, win->title, win->frame.w - TEXT_SHADOW_OFFSET, fitted, sizeof(fitted));
        irect16_t shadow_pos = rect_offset(text_pos, TEXT_SHADOW_OFFSET, TEXT_SHADOW_OFFSET);
        draw_text_clipped(font, fitted, &shadow_pos, get_sys_color(brDarkEdge), 0);
        draw_text_clipped(font, fitted, &text_pos, col, 0);
        return true;
      }
      // Labels taller than a single control row use draw_text_wrapped so that
      // long text reflows naturally within the available width.
      text_wrap_result_t wrap = text_wrap_layout_font(font, win->title, &text_pos, 0, false);
      if (wrap.wrapped) {
        draw_text_wrapped_font(font, win->title, &text_pos, col);
      } else {
        irect16_t shadow_pos = rect_offset(text_pos, TEXT_SHADOW_OFFSET, TEXT_SHADOW_OFFSET);
        draw_text_clipped(font, win->title, &shadow_pos, get_sys_color(brDarkEdge), 0);
        draw_text_clipped(font, win->title, &text_pos, col, 0);
      }
      return true;
    }
  }
  return false;
}

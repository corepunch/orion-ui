#include <string.h>
#include <stdio.h>
#include <math.h>

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/rect.h>
#include <orion/user/theme.h>
#include <orion/user/ui_style.h>

// Label control window procedure.
// lparam in evCreate may be either:
//   - NULL: default palette text color, LABEL_TEXT
//   - a small integer palette index: text color only, LABEL_TEXT
//   - label_create_params_t*: explicit palette index + font selection

// The label's style is its CSS class; its own classes and #id join it.
static ui_font_t label_font(const window_t *win) {
  static const char *const k_label_selectors[LABEL_STYLE_COUNT] = {
    [LABEL_TEXT] = "Label", [LABEL_CAPTION] = "Label.caption", [LABEL_HEADING] = "Label.heading",
    [LABEL_TITLE] = "Label.title", [LABEL_DISPLAY] = "Label.display",
  };
  uint32_t packed = (uint32_t)(uintptr_t)(win ? win->userdata : NULL);
  uint32_t style = (packed >> 8) & 0xffu;
  return ui_selector(win, k_label_selectors[style < LABEL_STYLE_COUNT ? style : LABEL_TEXT]);
}

static uint32_t label_color(const window_t *win) {
  uint32_t packed = (uint32_t)(uintptr_t)(win ? win->userdata : NULL);
  uint8_t idx = (uint8_t)(packed & 0xffu);
  if ((packed & LABEL_PACK_COLOR_SET) == 0)
    return ui_color(label_font(win), "color");   // the style sheet's, unless color= sets a role
  if (idx == 0)
    return get_sys_color(brTransparent);
  if (idx < (uint8_t)brCount)
    return get_sys_color((sys_color_idx_t)idx);
  return get_sys_color(brTextNormal);
}

// Classic's text-shadow falls one pixel right and down; the label's box makes room for it.
static int label_shadow_inset(void) { return ui_flag("Button", "text-shadow") ? TEXT_SHADOW_OFFSET : 0; }

#define LABEL_MAX_CONTENT 0x4000   // wider than any line

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
        uint8_t font = cd->font_set ? cd->font : LABEL_TEXT;
        win->userdata = (void *)(uintptr_t)label_pack_userdata(color_idx, font, cd->color_set);
      } else {
        uint32_t color_index = (uint32_t)(uintptr_t)lparam;
        win->userdata = (void *)(uintptr_t)label_pack_userdata(color_index, LABEL_TEXT, lparam != NULL);
      }
      // Single-line labels start as wide as their text.
      if (win->frame.h <= CONTROL_HEIGHT)
        win->frame.w = MAX(win->frame.w, text_strwidth(label_font(win), win->title) + label_shadow_inset());
      win->flags |= WINDOW_NOTABSTOP;
      if ((win->flags & LABEL_COLLAPSE_EMPTY) && !win->title[0]) win->frame = R(0, 0, 0, 0);
      return true;
    // A label is the line boxes of its text and nothing else: its width is the widest line, its
    // height the lines times the element's line-height (text_line_height), the first line box is
    // at the top and glyphs sit in the middle of each, as for an inline-level box in CSS.
    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (!m) return true;
      ui_font_t font = label_font(win);
      int inset = label_shadow_inset(), line = text_line_height(font);
      if (label_truncates(win)) {
        // One line; the arranged width decides how much of it is visible.
        m->desired_w = text_strwidth(font, win->title) + inset;
        m->desired_h = line + inset;
        return true;
      }
      // Until the parent has a width the label is as wide as its text, or as its declared width.
      int avail_w = m->avail_w > 0 ? m->avail_w : win->layout.layout_fixed_w > 0 ? win->layout.layout_fixed_w : LABEL_MAX_CONTENT;
      irect16_t wrap_vp = {0, 0, avail_w, 1};
      text_wrap_result_t wrap = text_wrap_layout_font(font, win->title, &wrap_vp, 0, false);
      if (wrap.wrapped) {
        m->desired_w = avail_w;
        m->desired_h = wrap.height + inset;
      } else {
        m->desired_w = MAX(1, wrap.width + inset);   // the message router reads a zero width as "not measured"
        m->desired_h = (wrap.height ? wrap.height : line) + inset;   // an empty label keeps its line
      }
      return true;
    }
    case evPaint: {
      uint32_t col = label_color(win);
      ui_font_t font = label_font(win);
      irect16_t text_pos = {0, 0, win->frame.w, win->frame.h};
      bool shadow = ui_flag("Button", "text-shadow");
      irect16_t shadow_pos = rect_offset(text_pos, TEXT_SHADOW_OFFSET, TEXT_SHADOW_OFFSET);
      if (label_truncates(win)) {
        char fitted[512];
        text_ellipsize(font, win->title, win->frame.w - label_shadow_inset(), fitted, sizeof(fitted));
        text_pos.h = shadow_pos.h = text_line_height(font);   // the top line box; glyphs in its middle
        if (shadow) draw_text_clipped(font, fitted, &shadow_pos, get_sys_color(brDarkEdge), 0);
        draw_text_clipped(font, fitted, &text_pos, col, 0);
        return true;
      }
      if (shadow) draw_text_wrapped_font(font, win->title, &shadow_pos, get_sys_color(brDarkEdge));
      draw_text_wrapped_font(font, win->title, &text_pos, col);
      return true;
    }
  }
  return false;
}

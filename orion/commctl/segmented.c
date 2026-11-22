// SegmentedControl — a row of mutually exclusive segments, in the spirit of
// NSSegmentedControl / a WinAPI auto-radio group. One segment is selected at
// a time; a click or Left/Right selects another and notifies the parent.
// Segments can carry icons from a shared strip (sgSetImageStrip +
// sgSetSegmentIcon, as tcSetImageStrip / tcSetTabIcon on a TabView), or named
// icons that follow the segment's colour (sgSetSegmentGlyph). A control tall
// enough for a large named icon over its label draws its segments that way.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/rect.h>
#include <orion/user/theme.h>
#include <orion/user/svg_icon_loader.h>
#include "commctl.h"

#define SEGMENTED_GLYPH_MAX 48

typedef struct {
  char label[SEGMENTED_MAX_SEGMENTS][SEGMENTED_LABEL_MAX];
  int  icon[SEGMENTED_MAX_SEGMENTS]; // index in strip, -1 = none
  uint32_t dot[SEGMENTED_MAX_SEGMENTS]; // colour dot before the label, 0 = none
  char glyph[SEGMENTED_MAX_SEGMENTS][2][SEGMENTED_GLYPH_MAX]; // named icon, and the one shown while selected
  int  count, selected, pressed, hot;
  uint32_t style;
  bitmap_strip_t strip;
} segmented_state_t;

#define SG_ICON_GAP 4

static const char *sg_sel(const window_t *win) { return ui_selector(win, "SegmentedControl::segment"); }
static bool sg_has_glyph(const segmented_state_t *s, int i) { return s->glyph[i][0][0] != 0; }
static bool sg_has_strip_icon(const segmented_state_t *s, int i) { return s->strip.cols > 0 && s->icon[i] >= 0; }
static bool sg_has_icon(const segmented_state_t *s, int i) { return sg_has_glyph(s, i) || sg_has_strip_icon(s, i); }
static int sg_icon_width(const window_t *win, const segmented_state_t *s, int i) {
  return sg_has_glyph(s, i) ? ui_px(sg_sel(win), "--icon-size") : s->strip.icon_w;
}
// The height the control is laid out for: `h` when a parent is asking, otherwise its own.
static int sg_height(const window_t *win, int h) {
  return h > 0 ? h : win->frame.h > 1 ? win->frame.h : control_predefined_height(win->flags);
}
// Tall: every segment has a named icon, and there is room for the large one over its label.
static bool sg_tall(const window_t *win, const segmented_state_t *s, int h) {
  const char *sel = sg_sel(win);
  for (int i = 0; i < s->count; i++) if (!sg_has_glyph(s, i)) return false;
  return s->count > 0 && sg_height(win, h) >= ui_px(sel, "--large-icon-size") + ui_px(sel, "--caption-gap") + text_char_height(sel) + 4 * SEGMENTED_INSET;
}
static bool sg_shows_label(const segmented_state_t *s, int i) { return !sg_has_icon(s, i) || !(s->style & SEGMENTED_STYLE_ICONS_ONLY); }
// The dot is a small disc on the text's x-height, like a status bullet.
static int sg_dot_size(const window_t *win) { return MAX(4, text_char_height("SegmentedControl::segment") * 3 / 5); }

// Width of the dot, icon and label drawn centred in a segment.
static int sg_content_width(const window_t *win, const segmented_state_t *s, int i) {
  int w = s->dot[i] ? sg_dot_size(win) : 0;
  if (sg_has_icon(s, i)) w += (w ? SG_ICON_GAP : 0) + sg_icon_width(win, s, i);
  if (sg_shows_label(s, i)) w += (w ? SG_ICON_GAP : 0) + text_strwidth(ui_selector(win, "SegmentedControl::segment"), s->label[i]);
  return w;
}

// A segment is never narrower than it is tall, so a short label still forms a
// capsule whose ends are concentric with the track's (taller controls on touch).
static int sg_natural_width(const window_t *win, const segmented_state_t *s, int i, int height) {
  int h = sg_height(win, height);
  if (sg_tall(win, s, height))   // the large icon over the label, as wide as the wider of the two
    return MAX(ui_px(sg_sel(win), "--large-icon-size"), text_strwidth(sg_sel(win), s->label[i])) + 2 * SEGMENTED_PADDING;
  int pad = sg_shows_label(s, i) ? SEGMENTED_PADDING : SEGMENTED_INSET;
  return MAX(sg_content_width(win, s, i) + 2 * pad, h - 2 * SEGMENTED_INSET);
}

static int sg_desired_width(const window_t *win, const segmented_state_t *s, int height) {
  int w = 2 * SEGMENTED_INSET;
  for (int i = 0; i < s->count; i++) w += sg_natural_width(win, s, i, height);
  return w;
}

static void sg_draw_glyph(const segmented_state_t *s, int i, bool selected, irect16_t r, int size, uint32_t ink) {
  sysicon_resolved_t icon;
  const char *name = selected && s->glyph[i][1][0] ? s->glyph[i][1] : s->glyph[i][0];
  if (!sysicon_resolve_size(name, size, &icon)) return;
  draw_sprite_region((int)icon.tex, rect_center(r, size, size), UV_RECT(icon.u0, icon.v0, icon.u1, icon.v1), ink, 0);
}

// Segments keep their natural proportions; spare or missing width is shared.
static irect16_t sg_segment_rect(const window_t *win, const segmented_state_t *s, int index) {
  irect16_t track = rect_inset(R(0, 0, win->frame.w, win->frame.h), SEGMENTED_INSET);
  int natural = MAX(1, sg_desired_width(win, s, 0) - 2 * SEGMENTED_INSET), acc = 0, x0 = 0, x1 = 0;
  for (int i = 0; i <= index; i++) {
    x0 = x1;
    acc += sg_natural_width(win, s, i, 0);
    x1 = (int)((int64_t)acc * track.w / natural);
  }
  return R(track.x + x0, track.y, x1 - x0, track.h);
}

static int sg_hit(const window_t *win, const segmented_state_t *s, int x, int y) {
  for (int i = 0; i < s->count; i++)
    if (rect_contains_point(sg_segment_rect(win, s, i), (ipoint16_t){ (int16_t)x, (int16_t)y })) return i;
  return -1;
}

static bool sg_select(window_t *win, segmented_state_t *s, int index, bool notify) {
  if (index < -1 || index >= s->count) {
    fprintf(stderr, "[sg] selection rejected win=%u index=%d count=%d\n", win->id, index, s->count);
    fflush(stderr);
    return false;
  }
  if (index == s->selected) return true;
  s->selected = index;
  invalidate_window(win);
  if (notify && win->parent)
    send_message(win->parent, evCommand, MAKEDWORD((uint16_t)win->id, sgnSelChange), win);
  return true;
}

static int sg_add(window_t *win, segmented_state_t *s, const char *label, size_t len) {
  if (s->count >= SEGMENTED_MAX_SEGMENTS) {
    fprintf(stderr, "[sg] segment rejected win=%u count=%d: control is full\n", win->id, s->count);
    fflush(stderr);
    return -1;
  }
  size_t n = MIN(len, (size_t)SEGMENTED_LABEL_MAX - 1);
  while (n && n < len && ((unsigned char)label[n] & 0xc0) == 0x80) n--;
  memcpy(s->label[s->count], label, n);
  s->label[s->count][n] = '\0';
  s->icon[s->count] = -1;
  s->dot[s->count] = 0;
  s->glyph[s->count][0][0] = s->glyph[s->count][1][0] = '\0';
  return s->count++;
}

// "All|Dance|Rave" replaces every segment. The selection is kept when it still exists.
static void sg_set_segments(window_t *win, segmented_state_t *s, const char *labels) {
  s->count = 0;
  for (const char *p = labels; p && *p;) {
    const char *end = strchr(p, '|');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (sg_add(win, s, p, len) < 0) break;
    p = end ? end + 1 : p + len;
  }
  s->selected = s->count ? CLAMP(s->selected, 0, s->count - 1) : -1;
  s->pressed = s->hot = -1;
  invalidate_window(win);
}

result_t win_segmented(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  segmented_state_t *s = win->userdata;
  if (!s && msg != evCreate && msg != evDestroy) return false;
  switch (msg) {
    case evCreate:
      if (!(s = allocate_window_data(win, sizeof(*s)))) {
        fprintf(stderr, "[sg] allocation failed win=%u\n", win->id);
        fflush(stderr);
        return false;
      }
      sg_set_segments(win, s, win->title);
      if (win->frame.w <= 1) win->frame.w = sg_desired_width(win, s, 0);
      if (win->frame.h <= 1) control_apply_predefined_height(win, "segmented");
      return true;
    case evMeasure: {
      layout_measure_t *m = lparam;
      if (!m) return true;
      // Offered a height that fits the tall form, the control takes it; otherwise it is one row.
      bool tall = m->avail_h > 0 && sg_tall(win, s, m->avail_h);
      m->desired_h = tall ? m->avail_h : control_predefined_height(win->flags);
      m->desired_w = sg_desired_width(win, s, tall ? m->avail_h : m->avail_h > 0 ? m->desired_h : 0);
      return true;
    }
    case evArrange:
      if (lparam) control_arrange_predefined_height(win, lparam);
      return true;
    case evPaint: {
      bool disabled = window_is_disabled(win);
      ctrl_state_t base = disabled ? CTRL_DISABLED : g_ui_runtime.focused == win ? CTRL_FOCUSED : CTRL_NORMAL;
      theme_draw_in(win, THEME_PART_SEGMENTED_TRACK, R(0, 0, win->frame.w, win->frame.h), base);
      for (int i = 0; i < s->count; i++) {
        irect16_t r = sg_segment_rect(win, s, i);
        ctrl_state_t state = (disabled ? CTRL_DISABLED : CTRL_NORMAL) | (i == s->selected ? CTRL_SELECTED : 0);
        if (!disabled && i == s->pressed) state |= CTRL_PRESSED;
        if (!disabled && i == s->hot)     state |= CTRL_HOVER;
        theme_draw_in(win, THEME_PART_SEGMENT, r, state);
        uint32_t ink = theme_foreground_in(win, THEME_PART_SEGMENT, state);
        if (sg_tall(win, s, 0)) {
          const char *sel = sg_sel(win);
          int size = ui_px(sel, "--large-icon-size"), line = text_char_height(sel);
          irect16_t body = rect_center(r, r.w - 4, size + ui_px(sel, "--caption-gap") + line);
          irect16_t caption = rect_split_bottom(body, line);
          sg_draw_glyph(s, i, i == s->selected, rect_split_top(body, size), size, ink);
          draw_text_clipped(sel, s->label[i], &caption, ink, TEXT_ALIGN_CENTER);
          continue;
        }
        irect16_t content = rect_center(r, MIN(sg_content_width(win, s, i), MAX(0, r.w - 4)), r.h);
        if (s->dot[i]) {
          // The chosen segment already shows its colour in the content it filters; its dot takes the label colour.
          int d = sg_dot_size(win);
          uint32_t color = (i == s->selected) ? theme_foreground_in(win, THEME_PART_SEGMENT, state) : s->dot[i];
          if (disabled) color = color_with_alpha(color, 0x80);
          fill_rounded_rect(color, rect_center(rect_split_left(content, d), d, d), d / 2);
          content = rect_trim_left(content, d + SG_ICON_GAP);
        }
        if (sg_has_glyph(s, i)) {
          int size = sg_icon_width(win, s, i);
          sg_draw_glyph(s, i, i == s->selected, rect_split_left(content, size), size, ink);
          content = rect_trim_left(content, size + SG_ICON_GAP);
        } else if (sg_has_strip_icon(s, i)) {
          const bitmap_strip_t *st = &s->strip;
          int col = s->icon[i] % st->cols, row = s->icon[i] / st->cols;
          float u0 = (float)(col * st->icon_w) / st->sheet_w, v0 = (float)(row * st->icon_h) / st->sheet_h;
          irect16_t icon = rect_center(rect_split_left(content, st->icon_w), st->icon_w, st->icon_h);
          draw_sprite_region((int)st->tex, icon,
                             UV_RECT(u0, v0, u0 + (float)st->icon_w / st->sheet_w, v0 + (float)st->icon_h / st->sheet_h),
                             disabled ? color_with_alpha(0xFFFFFFFFu, 0x80) : 0xFFFFFFFFu, 0);
          content = rect_trim_left(content, st->icon_w + SG_ICON_GAP);
        }
        if (sg_shows_label(s, i) && content.w > 0) {
          irect16_t label = rect_center(content, content.w, text_char_height(ui_selector(win, "SegmentedControl::segment")));
          draw_text_ellipsized(ui_selector(win, "SegmentedControl::segment"), s->label[i], label.x, label.y, label.w, ink);
        }
      }
      return true;
    }
    case evGetTooltipRect: {
      int hit = sg_hit(win, s, (int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam));
      if (hit < 0 || !lparam) return false;
      *(irect16_t *)lparam = sg_segment_rect(win, s, hit);
      return true;
    }
    case evGetTooltipText: {
      int hit = sg_hit(win, s, (int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam));
      if (hit < 0 || sg_shows_label(s, hit) || sg_tall(win, s, 0) || !lparam) return false;
      snprintf(lparam, 256, "%s", s->label[hit]);
      return true;
    }
    case evMouseMove: {
      int hot = sg_hit(win, s, (int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam));
      track_mouse(win);
      if (hot != s->hot) { s->hot = hot; invalidate_window(win); }
      return true;
    }
    case evMouseLeave:
      if (s->hot >= 0) { s->hot = -1; invalidate_window(win); }
      return false;
    case evLeftButtonDown: {
      if (window_has_state(win, WINDOW_STATE_DISABLED)) return true;
      int hit = sg_hit(win, s, (int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam));
      if (hit < 0) return false;
      s->pressed = hit;
      set_capture(win);
      invalidate_window(win);
      return true;
    }
    case evLeftButtonUp: {
      if (s->pressed < 0) return false;
      int pressed = s->pressed;
      s->pressed = -1;
      if (g_ui_runtime.captured == win) set_capture(NULL);
      invalidate_window(win);
      // Radio semantics: releasing on the pressed segment selects it; the selected one stays selected.
      if (sg_hit(win, s, (int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)) == pressed) sg_select(win, s, pressed, true);
      return true;
    }
    case evPointerCancel:
      if (s->pressed < 0) return false;
      s->pressed = -1;
      if (g_ui_runtime.captured == win) set_capture(NULL);
      invalidate_window(win);
      return true;
    case evKeyDown:
      if (window_has_state(win, WINDOW_STATE_DISABLED) || !s->count) return false;
      if (wparam == AX_KEY_LEFTARROW)  return sg_select(win, s, MAX(s->selected - 1, 0), true);
      if (wparam == AX_KEY_RIGHTARROW) return sg_select(win, s, MIN(s->selected + 1, s->count - 1), true);
      return false;
    case evSetFocus:
    case evKillFocus:
      invalidate_window(win);
      return false;
    case sgSetSegments:
      sg_set_segments(win, s, lparam);
      return true;
    case sgAddSegment: {
      const char *label = lparam ? lparam : "";
      int index = sg_add(win, s, label, strlen(label));
      if (index < 0) return (result_t)-1;
      if (s->selected < 0) s->selected = 0;
      invalidate_window(win);
      return (result_t)index;
    }
    case sgGetCount:     return (result_t)s->count;
    case sgGetSelection: return (result_t)s->selected;
    case sgSetSelection: return sg_select(win, s, (int)wparam, false);
    case sgGetSegmentRect:
      if ((int)wparam < 0 || (int)wparam >= s->count || !lparam) {
        fprintf(stderr, "[sg] segment rect rejected win=%u index=%d count=%d out=%p\n", win->id, (int)wparam, s->count, lparam);
        fflush(stderr);
        return false;
      }
      *(irect16_t *)lparam = sg_segment_rect(win, s, (int)wparam);
      return true;
    case sgSetImageStrip:
      if (!lparam) {
        fprintf(stderr, "[sg] image strip rejected win=%u: null strip\n", win->id);
        fflush(stderr);
        return false;
      }
      s->strip = *(const bitmap_strip_t *)lparam;
      invalidate_window(win);
      return true;
    case sgSetSegmentIcon: {
      int index = (int)wparam, icon = (int)(intptr_t)lparam;
      if (index < 0 || index >= s->count || icon < -1) {
        fprintf(stderr, "[sg] segment icon rejected win=%u index=%d count=%d icon=%d\n", win->id, index, s->count, icon);
        fflush(stderr);
        return false;
      }
      s->icon[index] = icon;
      invalidate_window(win);
      return true;
    }
    case sgSetSegmentGlyph: {
      int index = (int)wparam;
      const segment_glyph_t *glyph = lparam;
      if (index < 0 || index >= s->count || (glyph && !glyph->icon)) {
        fprintf(stderr, "[sg] segment glyph rejected win=%u index=%d count=%d icon=%p\n", win->id, index, s->count,
                glyph ? (void *)glyph->icon : NULL);
        fflush(stderr);
        return false;
      }
      snprintf(s->glyph[index][0], SEGMENTED_GLYPH_MAX, "%s", glyph ? glyph->icon : "");
      snprintf(s->glyph[index][1], SEGMENTED_GLYPH_MAX, "%s", glyph && glyph->checked_icon ? glyph->checked_icon : "");
      invalidate_window(win);
      return true;
    }
    case sgSetSegmentColor: {
      int index = (int)wparam;
      if (index < 0 || index >= s->count) {
        fprintf(stderr, "[sg] segment colour rejected win=%u index=%d count=%d\n", win->id, index, s->count);
        fflush(stderr);
        return false;
      }
      s->dot[index] = (uint32_t)(uintptr_t)lparam;
      invalidate_window(win);
      return true;
    }
    case sgSetStyle:
      if (wparam & ~SEGMENTED_STYLE_ICONS_ONLY) {
        fprintf(stderr, "[sg] style rejected win=%u style=0x%x\n", win->id, (unsigned)wparam);
        fflush(stderr);
        return false;
      }
      s->style = wparam;
      invalidate_window(win);
      return true;
    case evDestroy:
      if (s && s->pressed >= 0 && g_ui_runtime.captured == win) set_capture(NULL);
      free(s);
      win->userdata = NULL;
      return true;
    default: return false;
  }
}

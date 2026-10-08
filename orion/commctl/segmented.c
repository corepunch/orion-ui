// SegmentedControl — a row of mutually exclusive segments, in the spirit of
// NSSegmentedControl / a WinAPI auto-radio group. One segment is selected at
// a time; a click or Left/Right selects another and notifies the parent.
// Segments can carry icons from a shared strip (sgSetImageStrip +
// sgSetSegmentIcon, as tcSetImageStrip / tcSetTabIcon on a TabView).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/rect.h>
#include <orion/user/theme.h>
#include "commctl.h"

typedef struct {
  char label[SEGMENTED_MAX_SEGMENTS][SEGMENTED_LABEL_MAX];
  int  icon[SEGMENTED_MAX_SEGMENTS]; // index in strip, -1 = none
  int  count, selected, pressed, hot;
  uint32_t style;
  bitmap_strip_t strip;
} segmented_state_t;

#define SG_ICON_GAP 4

static bool sg_has_icon(const segmented_state_t *s, int i) { return s->strip.cols > 0 && s->icon[i] >= 0; }
static bool sg_shows_label(const segmented_state_t *s, int i) { return !sg_has_icon(s, i) || !(s->style & SEGMENTED_STYLE_ICONS_ONLY); }

// Width of the icon and label drawn centred in a segment.
static int sg_content_width(const segmented_state_t *s, int i) {
  int w = sg_has_icon(s, i) ? s->strip.icon_w : 0;
  if (sg_shows_label(s, i)) w += (w ? SG_ICON_GAP : 0) + text_strwidth(FONT_SMALL, s->label[i]);
  return w;
}

// A segment is never narrower than it is tall, so a short label still forms a
// capsule whose ends are concentric with the track's (taller controls on touch).
static int sg_natural_width(const window_t *win, const segmented_state_t *s, int i) {
  int h = win->frame.h > 1 ? win->frame.h : control_predefined_height(win->flags);
  int pad = sg_shows_label(s, i) ? SEGMENTED_PADDING : SEGMENTED_INSET;
  return MAX(sg_content_width(s, i) + 2 * pad, h - 2 * SEGMENTED_INSET);
}

static int sg_desired_width(const window_t *win, const segmented_state_t *s) {
  int w = 2 * SEGMENTED_INSET;
  for (int i = 0; i < s->count; i++) w += sg_natural_width(win, s, i);
  return w;
}

// Segments keep their natural proportions; spare or missing width is shared.
static irect16_t sg_segment_rect(const window_t *win, const segmented_state_t *s, int index) {
  irect16_t track = rect_inset(R(0, 0, win->frame.w, win->frame.h), SEGMENTED_INSET);
  int natural = MAX(1, sg_desired_width(win, s) - 2 * SEGMENTED_INSET), acc = 0, x0 = 0, x1 = 0;
  for (int i = 0; i <= index; i++) {
    x0 = x1;
    acc += sg_natural_width(win, s, i);
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
  snprintf(s->label[s->count], SEGMENTED_LABEL_MAX, "%.*s", (int)MIN(len, (size_t)SEGMENTED_LABEL_MAX - 1), label);
  s->icon[s->count] = -1;
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
      if (win->frame.w <= 1) win->frame.w = sg_desired_width(win, s);
      if (win->frame.h <= 1) control_apply_predefined_height(win, "segmented");
      return true;
    case evMeasure: {
      layout_measure_t *m = lparam;
      if (m) { m->desired_w = sg_desired_width(win, s); m->desired_h = control_predefined_height(win->flags); }
      return true;
    }
    case evArrange:
      if (lparam) control_arrange_predefined_height(win, lparam);
      return true;
    case evPaint: {
      bool disabled = window_has_state(win, WINDOW_STATE_DISABLED);
      ctrl_state_t base = disabled ? CTRL_DISABLED : g_ui_runtime.focused == win ? CTRL_FOCUSED : CTRL_NORMAL;
      theme_draw(THEME_PART_SEGMENTED_TRACK, R(0, 0, win->frame.w, win->frame.h), base);
      for (int i = 0; i < s->count; i++) {
        irect16_t r = sg_segment_rect(win, s, i);
        ctrl_state_t state = (disabled ? CTRL_DISABLED : CTRL_NORMAL) | (i == s->selected ? CTRL_SELECTED : 0);
        if (!disabled && i == s->pressed) state |= CTRL_PRESSED;
        if (!disabled && i == s->hot)     state |= CTRL_HOVER;
        theme_draw(THEME_PART_SEGMENT, r, state);
        irect16_t content = rect_center(r, MIN(sg_content_width(s, i), MAX(0, r.w - 4)), r.h);
        if (sg_has_icon(s, i)) {
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
          irect16_t label = rect_center(content, content.w, text_char_height(FONT_SMALL));
          draw_text_ellipsized(FONT_SMALL, s->label[i], label.x, label.y, label.w, theme_foreground(THEME_PART_SEGMENT, state));
        }
      }
      return true;
    }
    case evGetTooltipText: {
      int hit = sg_hit(win, s, (int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam));
      if (hit < 0 || sg_shows_label(s, hit) || !lparam) return false;
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

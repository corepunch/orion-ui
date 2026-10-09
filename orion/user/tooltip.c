// Framework tooltip: element-anchored bubble with one shared silhouette.
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "user.h"
#include "messages.h"
#include "draw.h"
#include "text.h"
#include "toolbar.h"
#include "dock.h"

#define TOOLTIP_MAX_WIDTH 360

static window_t *g_tooltip_win;
static window_t *g_tooltip_src;
static char g_tooltip_text[256];
static irect16_t g_tooltip_anchor;
static irect16_t g_tooltip_bar_rect;
static int g_tooltip_tail_offset;
static tooltip_tail_side_t g_tooltip_preferred_side, g_tooltip_tail_side;

static tooltip_tail_side_t tooltip_preferred_side(window_t *src, irect16_t *bounds) {
  for (window_t *win = src; win; win = win->parent) {
    window_t *owner = NULL;
    if (win->parent && win->parent->toolbar == win) owner = win->parent;
    else if (win == src && (win->flags & WINDOW_TOOLBAR)) owner = win;
    else if (win->parent) {
      toolbar_state_t *tb = toolbar_get_state(win->parent);
      for (window_t *child = tb ? tb->children : NULL; child; child = child->next)
        if (child == win) { owner = win->parent; break; }
    }
    if (!owner) continue;
    *bounds = rect_offset(toolbar_band_rect(owner), window_screen_x(owner), window_screen_y(owner));
    dock_side_t side = owner->dock ? owner->dock->side : DOCK_FLOAT;
    if (side == DOCK_FLOAT && owner->dock) side = owner->dock->last_side;
    toolbar_state_t *tb = toolbar_get_state(owner);
    if (side == DOCK_FLOAT || side == DOCK_FILL)
      side = tb && tb->orientation == TOOLBAR_VERTICAL ? DOCK_LEFT : DOCK_TOP;
    switch (side) {
      case DOCK_LEFT:   return TOOLTIP_TAIL_LEFT;
      case DOCK_RIGHT:  return TOOLTIP_TAIL_RIGHT;
      case DOCK_TOP:    return TOOLTIP_TAIL_TOP;
      case DOCK_BOTTOM: return TOOLTIP_TAIL_BOTTOM;
      default:         break;
    }
  }
  return TOOLTIP_TAIL_BOTTOM;
}

static bool tooltip_source_visible(window_t *src) {
  if (!is_window(src)) return false;
  for (window_t *win = src; win; win = win->parent) {
    // Toolbar hosts are hidden dispatch windows; their parent paints the band.
    if (win->parent && win->parent->toolbar == win) continue;
    if (!window_has_state(win, WINDOW_STATE_VISIBLE)) return false;
  }
  return true;
}

static void tooltip_show(window_t *win) {
  if (!g_tooltip_text[0] || !tooltip_source_visible(g_tooltip_src)) return;
  const theme_t *theme = get_theme();
  int pad = theme->tooltip_shadow_size;
  int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
  int sh = ui_get_system_metrics(kSystemMetricScreenHeight);
  bool horizontal = g_tooltip_preferred_side == TOOLTIP_TAIL_LEFT || g_tooltip_preferred_side == TOOLTIP_TAIL_RIGHT;
  int tail = theme->tooltip_tail_size;
  int max_text = MAX(1, MIN(TOOLTIP_MAX_WIDTH, sw - 2 * (pad + theme->tooltip_padding_x) - (horizontal ? tail : 0)));
  int tw = MAX(1, MIN(text_strwidth(FONT_SMALL, g_tooltip_text), max_text));
  int th = calc_text_height_font(FONT_SMALL, g_tooltip_text, tw);
  int margin = pad + theme->tooltip_corner_radius + tail;
  int w = tw + 2 * (pad + theme->tooltip_padding_x) + (horizontal ? tail : 0);
  int h = th + 2 * (pad + theme->tooltip_padding_y) + (horizontal ? 0 : tail);
  if (horizontal) h = MAX(h, 2 * margin);
  else w = MAX(w, 2 * margin);
  ipoint16_t center = {g_tooltip_anchor.x + g_tooltip_anchor.w / 2, g_tooltip_anchor.y + g_tooltip_anchor.h / 2};
  irect16_t bar = g_tooltip_bar_rect;
  int gap = theme->tooltip_gap - pad;
  int x = center.x - w / 2, y = center.y - h / 2;
  g_tooltip_tail_side = g_tooltip_preferred_side;
  if (horizontal) {
    int left = bar.x - gap - w, right = bar.x + bar.w + gap;
    x = g_tooltip_tail_side == TOOLTIP_TAIL_LEFT ? right : left;
    if (x < 0 && right + w <= sw) { x = right; g_tooltip_tail_side = TOOLTIP_TAIL_LEFT; }
    else if (x + w > sw && left >= 0) { x = left; g_tooltip_tail_side = TOOLTIP_TAIL_RIGHT; }
  } else {
    int top = bar.y - gap - h, bottom = bar.y + bar.h + gap;
    y = g_tooltip_tail_side == TOOLTIP_TAIL_TOP ? bottom : top;
    if (y < 0 && bottom + h <= sh) { y = bottom; g_tooltip_tail_side = TOOLTIP_TAIL_TOP; }
    else if (y + h > sh && top >= 0) { y = top; g_tooltip_tail_side = TOOLTIP_TAIL_BOTTOM; }
  }
  x = MAX(0, MIN(x, sw - w));
  y = MAX(0, MIN(y, sh - h));
  g_tooltip_tail_offset = horizontal ? MAX(margin, MIN(center.y - y, h - margin))
                                      : MAX(margin, MIN(center.x - x, w - margin));
  win->frame = R(x, y, w, h);
  snprintf(win->title, sizeof(win->title), "%s", g_tooltip_text);
  show_window(win, true);
  invalidate_window(win);
}

static result_t tooltip_win_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)wparam;
  switch (msg) {
    case evCreate: return true;
    case evDestroy:
      g_tooltip_win = g_tooltip_src = NULL;
      g_tooltip_text[0] = '\0';
      return true;
    case evPaint: {
      const theme_t *theme = get_theme();
      irect16_t bounds = R(0, 0, win->frame.w, win->frame.h);
      draw_tooltip_bubble(bounds, g_tooltip_tail_offset, g_tooltip_tail_side);
      irect16_t text = rect_inset_xy(tooltip_bubble_body_rect(bounds, g_tooltip_tail_side),
                                     theme->tooltip_padding_x, theme->tooltip_padding_y);
      draw_text_wrapped(win->title, &text, get_sys_color(brTextNormal));
      return true;
    }
    // Tooltips do not intercept pointer hit-testing through their shadow.
    case evHitTest:
      if (lparam) *(window_t **)lparam = NULL;
      return true;
  }
  return false;
}

void tooltip_cancel(void) {
  if (is_window(g_tooltip_win) && window_has_state(g_tooltip_win, WINDOW_STATE_VISIBLE))
    show_window(g_tooltip_win, false);
  g_tooltip_text[0] = '\0';
  g_tooltip_src = NULL;
}

void tooltip_update(window_t *src_win, const char *text, int sx, int sy) {
  if (!src_win || !text || !text[0]) { tooltip_cancel(); return; }
  int ox = window_screen_x(src_win), oy = window_screen_y(src_win);
  irect16_t anchor = R(ox, oy, src_win->frame.w, src_win->frame.h), part;
  int cx = sx - ox + (int)src_win->hscroll.pos;
  int cy = sy - oy - titlebar_height(src_win) + (int)src_win->vscroll.pos;
  if (send_message(src_win, evGetTooltipRect, MAKEDWORD(cx, cy), &part))
    anchor = rect_offset(part, ox - (int)src_win->hscroll.pos,
                         oy + titlebar_height(src_win) - (int)src_win->vscroll.pos);
  irect16_t bar = anchor;
  tooltip_tail_side_t side = tooltip_preferred_side(src_win, &bar);
  if (g_tooltip_src == src_win && !memcmp(&anchor, &g_tooltip_anchor, sizeof(anchor)) &&
      !memcmp(&bar, &g_tooltip_bar_rect, sizeof(bar)) && side == g_tooltip_preferred_side &&
      !strcmp(g_tooltip_text, text)) return;
  tooltip_cancel();
  g_tooltip_src = src_win;
  g_tooltip_anchor = anchor;
  g_tooltip_bar_rect = bar;
  g_tooltip_preferred_side = side;
  snprintf(g_tooltip_text, sizeof(g_tooltip_text), "%s", text);
  if (!is_window(g_tooltip_win))
    g_tooltip_win = create_window("", WINDOW_NOTITLE | WINDOW_NORESIZE | WINDOW_ALWAYSONTOP |
      WINDOW_NOTRAYBUTTON | WINDOW_NOFILL | WINDOW_NOACTIVATE | WINDOW_TRANSPARENT | WINDOW_HIDDEN,
      MAKERECT(0, 0, 10, 10), NULL, tooltip_win_proc, 0, NULL);
  if (g_tooltip_win) tooltip_show(g_tooltip_win);
}

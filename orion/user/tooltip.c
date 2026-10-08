// Framework tooltip: element-anchored bubble with one shared silhouette.
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "user.h"
#include "messages.h"
#include "draw.h"
#include "text.h"
#include <platform/platform.h>

#define TOOLTIP_DELAY_MS 600
#define TOOLTIP_MAX_WIDTH 360

static window_t *g_tooltip_win;
static window_t *g_tooltip_src;
static char g_tooltip_pending[256];
static irect16_t g_tooltip_anchor;
static uint32_t g_tooltip_timer_id;
static int g_tooltip_tail_x;
static bool g_tooltip_tail_on_top;

static bool tooltip_source_visible(window_t *src) {
  if (!is_window(src)) return false;
  for (window_t *win = src; win; win = win->parent) {
    // Toolbar hosts are hidden dispatch windows; their parent paints the band.
    if (win->parent && win->parent->toolbar == win) continue;
    if (!window_has_state(win, WINDOW_STATE_VISIBLE)) return false;
  }
  return true;
}

static result_t tooltip_win_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)lparam;
  switch (msg) {
    case evCreate: return true;
    case evDestroy:
      if (g_tooltip_timer_id) axCancelTimer(g_tooltip_timer_id);
      g_tooltip_timer_id = 0;
      g_tooltip_win = g_tooltip_src = NULL;
      g_tooltip_pending[0] = '\0';
      return true;
    case evPaint: {
      const theme_t *theme = get_theme();
      draw_tooltip_bubble(R(0, 0, win->frame.w, win->frame.h), g_tooltip_tail_x, g_tooltip_tail_on_top);
      int pad = theme->tooltip_shadow_size;
      irect16_t text = rect_inset_xy(R(0, 0, win->frame.w, win->frame.h),
                                     pad + theme->tooltip_padding_x, pad + theme->tooltip_padding_y);
      if (g_tooltip_tail_on_top) text = rect_trim_top(text, theme->tooltip_tail_size);
      else text = rect_trim_bottom(text, theme->tooltip_tail_size);
      draw_text_wrapped(win->title, &text, get_sys_color(brTextNormal));
      return true;
    }
    case evTimer: {
      if ((uint32_t)wparam != g_tooltip_timer_id) return true;
      g_tooltip_timer_id = 0;
      if (!g_tooltip_pending[0] || !tooltip_source_visible(g_tooltip_src)) return true;
      const theme_t *theme = get_theme();
      int pad = theme->tooltip_shadow_size;
      int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
      int sh = ui_get_system_metrics(kSystemMetricScreenHeight);
      int max_text = MAX(1, MIN(TOOLTIP_MAX_WIDTH, sw - 2 * (pad + theme->tooltip_padding_x)));
      int tw = MAX(1, MIN(text_strwidth(FONT_SMALL, g_tooltip_pending), max_text));
      int th = calc_text_height_font(FONT_SMALL, g_tooltip_pending, tw);
      int w = tw + 2 * (pad + theme->tooltip_padding_x);
      w = MAX(w, 2 * (pad + theme->tooltip_corner_radius + theme->tooltip_tail_size));
      int h = th + 2 * (pad + theme->tooltip_padding_y) + theme->tooltip_tail_size;
      int center = g_tooltip_anchor.x + g_tooltip_anchor.w / 2;
      int x = MAX(0, MIN(center - w / 2, sw - w));
      int y = g_tooltip_anchor.y - theme->tooltip_gap - h + pad;
      g_tooltip_tail_on_top = y < 0;
      if (g_tooltip_tail_on_top) y = g_tooltip_anchor.y + g_tooltip_anchor.h + theme->tooltip_gap - pad;
      y = MAX(0, MIN(y, sh - h));
      int margin = pad + theme->tooltip_corner_radius + theme->tooltip_tail_size;
      g_tooltip_tail_x = MAX(margin, MIN(center - x, w - margin));
      win->frame = R(x, y, w, h);
      snprintf(win->title, sizeof(win->title), "%s", g_tooltip_pending);
      show_window(win, true);
      invalidate_window(win);
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
  if (g_tooltip_timer_id) axCancelTimer(g_tooltip_timer_id);
  g_tooltip_timer_id = 0;
  if (is_window(g_tooltip_win) && window_has_state(g_tooltip_win, WINDOW_STATE_VISIBLE))
    show_window(g_tooltip_win, false);
  g_tooltip_pending[0] = '\0';
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
  if (g_tooltip_src == src_win && !memcmp(&anchor, &g_tooltip_anchor, sizeof(anchor)) &&
      !strcmp(g_tooltip_pending, text)) return;
  tooltip_cancel();
  g_tooltip_src = src_win;
  g_tooltip_anchor = anchor;
  snprintf(g_tooltip_pending, sizeof(g_tooltip_pending), "%s", text);
  if (!is_window(g_tooltip_win))
    g_tooltip_win = create_window("", WINDOW_NOTITLE | WINDOW_NORESIZE | WINDOW_ALWAYSONTOP |
      WINDOW_NOTRAYBUTTON | WINDOW_NOFILL | WINDOW_NOACTIVATE | WINDOW_TRANSPARENT | WINDOW_HIDDEN,
      MAKERECT(0, 0, 10, 10), NULL, tooltip_win_proc, 0, NULL);
  if (g_tooltip_win) {
    g_tooltip_timer_id = axSetTimer(g_tooltip_win, TOOLTIP_DELAY_MS, NULL, FALSE);
    if (!g_tooltip_timer_id) {
      fprintf(stderr, "[tooltip] window %u: hover timer allocation failed\n", src_win->id);
      fflush(stderr);
    }
  }
}

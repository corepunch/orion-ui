#include <stdio.h>
#include "user.h"
#include "messages.h"
#include "draw.h"
#include "theme.h"

static void draw_theme_icon_in_rect_scrollbar(int id, irect16_t r, uint32_t col);

static int ui_sb_clamp_range(win_sb_t const *sb, int pos) {
  int max_pos = sb->max_val - sb->page;
  if (max_pos < sb->min_val) max_pos = sb->min_val;
  if (pos < sb->min_val) return sb->min_val;
  if (pos > max_pos)     return max_pos;
  return pos;
}

static int ui_sb_thumb_len(win_sb_t const *sb, int track) {
  int range = sb->max_val - sb->min_val;
  if (range <= 0 || sb->page >= range) return track;
  int tl = track * sb->page / range;
  return tl < 8 ? 8 : tl;
}

static int ui_sb_thumb_off(win_sb_t const *sb, int track, int tl) {
  int travel = sb->max_val - sb->min_val - sb->page;
  if (travel <= 0) return 0;
  int tt = track - tl;
  if (tt <= 0) return 0;
  return (sb->pos - sb->min_val) * tt / travel;
}

static void set_scroll_info_one(win_sb_t *sb, scroll_info_t const *info) {
  if (info->fMask & SIF_RANGE) {
    sb->min_val = info->nMin;
    sb->max_val = info->nMax;
  }
  if (info->fMask & SIF_PAGE) {
    sb->page = info->nPage;
  }
  if (info->fMask & SIF_POS) {
    sb->pos = ui_sb_clamp_range(sb, info->nPos);
  }
  if (info->fMask & (SIF_RANGE | SIF_PAGE)) {
    sb->pos = ui_sb_clamp_range(sb, sb->pos);
  }
  if (sb->visible_mode == SB_VIS_HIDE) {
    sb->visible = false;
  } else if (sb->visible_mode == SB_VIS_SHOW) {
    sb->visible = true;
  } else {
    bool should_show = (sb->page < sb->max_val - sb->min_val);
    sb->visible = should_show;
  }
  if (sb->visible && !sb->enabled) {
    sb->enabled = true;
  }
}

void set_scroll_info(window_t *win, int bar, scroll_info_t const *info, bool redraw) {
  if (!win || !info) return;
  if (bar == SB_VERT) {
    set_scroll_info_one(&win->vscroll, info);
  } else if (bar == SB_HORZ) {
    set_scroll_info_one(&win->hscroll, info);
  } else {
    set_scroll_info_one(&win->hscroll, info);
    set_scroll_info_one(&win->vscroll, info);
  }
  if (redraw) invalidate_window(win);
}

void get_scroll_info(window_t *win, int bar, scroll_info_t *info) {
  if (!win || !info) return;
  if (bar == SB_BOTH) bar = SB_HORZ;
  win_sb_t *sb = (bar == SB_VERT) ? &win->vscroll : &win->hscroll;
  if (info->fMask & SIF_RANGE) {
    info->nMin = sb->min_val;
    info->nMax = sb->max_val;
  }
  if (info->fMask & SIF_PAGE) info->nPage = sb->page;
  if (info->fMask & SIF_POS)  info->nPos  = sb->pos;
}

int get_scroll_pos(window_t *win, int bar) {
  if (!win) return 0;
  if (bar == SB_VERT) return win->vscroll.pos;
  return win->hscroll.pos;
}

void enable_scroll_bar(window_t *win, int bar, bool enable) {
  if (!win) return;
  if (bar == SB_HORZ || bar == SB_BOTH) win->hscroll.enabled = enable;
  if (bar == SB_VERT || bar == SB_BOTH) win->vscroll.enabled = enable;
  invalidate_window(win);
}

void show_scroll_bar(window_t *win, int bar, bool show) {
  if (!win) return;
  if (bar == SB_HORZ || bar == SB_BOTH) {
    win->hscroll.visible = show;
    win->hscroll.visible_mode = show ? SB_VIS_SHOW : SB_VIS_HIDE;
  }
  if (bar == SB_VERT || bar == SB_BOTH) {
    win->vscroll.visible = show;
    win->vscroll.visible_mode = show ? SB_VIS_SHOW : SB_VIS_HIDE;
  }
  invalidate_window(win);
}

void reset_scroll_bar_auto(window_t *win, int bar) {
  if (!win) return;
  if (bar == SB_HORZ || bar == SB_BOTH) win->hscroll.visible_mode = SB_VIS_AUTO;
  if (bar == SB_VERT || bar == SB_BOTH) win->vscroll.visible_mode = SB_VIS_AUTO;
}

static int sb_mouse_axis_coord(uint32_t wparam, bool vertical) {
  return vertical ? (int16_t)HIWORD(wparam) : (int16_t)LOWORD(wparam);
}

static int sb_mouse_axis_delta(void *lparam, bool vertical) {
  uint32_t delta = (uint32_t)(uintptr_t)lparam;
  return vertical ? (int16_t)HIWORD(delta) : (int16_t)LOWORD(delta);
}

static bool sb_try_scroll(window_t *win, win_sb_t *sb, uint32_t scroll_msg, int new_pos) {
  new_pos = ui_sb_clamp_range(sb, new_pos);
  if (new_pos == sb->pos) return false;
  sb->pos = new_pos;
  send_message(win, scroll_msg, (uint32_t)new_pos, NULL);
  invalidate_window(win);
  return true;
}

static void sb_handle_drag_move(window_t *win, win_sb_t *sb, uint32_t scroll_msg,
                                 int mouse_delta, int track) {
  sb->drag_mouse += mouse_delta;
  int eff_track = track - 2 * SCROLLBAR_WIDTH;
  int track_len  = (eff_track > 0 ? eff_track : track);
  int pos_eff    = sb->drag_mouse;
  int tl         = ui_sb_thumb_len(sb, track_len);
  int tp         = track_len - tl;
  int tr         = sb->max_val - sb->min_val - sb->page;
  if (tp > 0 && tr > 0) {
    sb_try_scroll(win, sb, scroll_msg,
                  sb->drag_start_pos + (pos_eff - sb->drag_start_mouse) * tr / tp);
  }
}

static void sb_handle_track_click(window_t *win, win_sb_t *sb, uint32_t scroll_msg,
                                   int pos, int track) {
  if (track >= 2 * SCROLLBAR_WIDTH) {
    if (pos < SCROLLBAR_WIDTH) {
      sb_try_scroll(win, sb, scroll_msg, sb->pos - SB_ARROW_STEP);
      return;
    }
    if (pos >= track - SCROLLBAR_WIDTH) {
      sb_try_scroll(win, sb, scroll_msg, sb->pos + SB_ARROW_STEP);
      return;
    }
    int eff_track = track - 2 * SCROLLBAR_WIDTH;
    int pos_eff   = pos - SCROLLBAR_WIDTH;
    if (eff_track > 0) {
      int tl = ui_sb_thumb_len(sb, eff_track);
      int to = ui_sb_thumb_off(sb, eff_track, tl);
      if (pos_eff >= to && pos_eff < to + tl) {
        sb->dragging         = true;
        sb->drag_start_mouse = pos_eff;
        sb->drag_mouse       = pos_eff;
        sb->drag_start_pos   = sb->pos;
        set_capture(win);
      } else {
        sb_try_scroll(win, sb, scroll_msg,
                      sb->pos + (pos_eff < to ? -sb->page : sb->page));
      }
    }
  } else {
    int tl = ui_sb_thumb_len(sb, track);
    int to = ui_sb_thumb_off(sb, track, tl);
    if (pos >= to && pos < to + tl) {
      sb->dragging         = true;
      sb->drag_start_mouse = pos;
      sb->drag_mouse       = pos;
      sb->drag_start_pos   = sb->pos;
      set_capture(win);
    } else {
      sb_try_scroll(win, sb, scroll_msg,
                    sb->pos + (pos < to ? -sb->page : sb->page));
    }
  }
}

bool scrollbar_handle_builtin_mouse(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  bool has_h = (win->flags & WINDOW_HSCROLL) && win->hscroll.visible;
  bool has_v = (win->flags & WINDOW_VSCROLL) && win->vscroll.visible;

  int t = titlebar_height(win);
  int s = statusbar_height(win);
  int content_h = win->frame.h - t - s;

  bool h_merged = has_h && (win->flags & WINDOW_STATUSBAR);
  int h_x_min   = h_merged ? SB_STATUS_SPLIT_X(win->frame.w) : 0;
  int h_y_min   = h_merged ? content_h : content_h - SCROLLBAR_WIDTH;
  int h_y_max   = h_merged ? content_h + STATUSBAR_HEIGHT : content_h;
  int h_track   = (win->frame.w - h_x_min) -
                  (h_merged ? SCROLLBAR_WIDTH : (has_v ? SCROLLBAR_WIDTH : 0));
  int v_track = content_h - (has_h && !h_merged ? SCROLLBAR_WIDTH : 0);

  if (msg == evMouseMove || msg == evLeftButtonUp) {
    if (win->hscroll.dragging) {
      int mouse_delta = sb_mouse_axis_delta(lparam, false);
      if (msg == evMouseMove)
        sb_handle_drag_move(win, &win->hscroll, evHScroll, mouse_delta, h_track);
      else {
        win->hscroll.dragging = false;
        set_capture(NULL);
      }
      return true;
    }
    if (win->vscroll.dragging) {
      int mouse_delta = sb_mouse_axis_delta(lparam, true);
      if (msg == evMouseMove)
        sb_handle_drag_move(win, &win->vscroll, evVScroll, mouse_delta, v_track);
      else {
        win->vscroll.dragging = false;
        set_capture(NULL);
      }
      return true;
    }
    return false;
  }

  if (msg != evLeftButtonDown && msg != evLeftButtonDoubleClick) return false;
  if (!has_h && !has_v) return false;

  // Mouse messages are delivered to window procedures in content space, but
  // built-in scrollbars live in the fixed viewport/non-client area. Convert
  // back before hit-testing so scrolling the client does not move the
  // scrollbar's clickable strip over the content.
  int cx = sb_mouse_axis_coord(wparam, false) - (int)win->hscroll.pos;
  int cy = sb_mouse_axis_coord(wparam, true) - (int)win->vscroll.pos;

  if (has_h && cy >= h_y_min && cy < h_y_max && cx >= h_x_min && cx < win->frame.w) {
    if (!win->hscroll.enabled) return true;
    int lx = cx - h_x_min;
    if (lx >= h_track) return true;
    sb_handle_track_click(win, &win->hscroll, evHScroll, lx, h_track);
    return true;
  }

  if (has_v && cx >= win->frame.w - SCROLLBAR_WIDTH && cx < win->frame.w &&
      cy >= 0 && cy < content_h) {
    if (!win->vscroll.enabled) return true;
    if (cy >= v_track) return true;
    sb_handle_track_click(win, &win->vscroll, evVScroll, cy, v_track);
    return true;
  }

  return false;
}

void scrollbar_handle_builtin_wheel(window_t *win, void *lparam) {
  if (!win) return;
  if ((win->flags & WINDOW_HSCROLL) && win->hscroll.visible && win->hscroll.enabled) {
    int delta = (int16_t)LOWORD((uintptr_t)lparam);
    sb_try_scroll(win, &win->hscroll, evHScroll, win->hscroll.pos + delta);
  }
  if ((win->flags & WINDOW_VSCROLL) && win->vscroll.visible && win->vscroll.enabled) {
    int delta = -(int16_t)HIWORD((uintptr_t)lparam);
    sb_try_scroll(win, &win->vscroll, evVScroll, win->vscroll.pos + delta);
  }
}

void scrollbar_draw_statusbar_merged_hscroll(window_t *win, irect16_t row, int split_x) {
  if (!win) return;
  win_sb_t *sb = &win->hscroll;
  irect16_t corner = rect_split_right(row, SCROLLBAR_WIDTH);
  fill_rect(get_sys_color(brWindowDarkBg), corner);
  draw_theme_icon_in_rect_scrollbar(THEME_ICON_RESIZE, corner, get_sys_color(brTextNormal));

  int bw = row.w - split_x - SCROLLBAR_WIDTH;
  if (bw <= 0)
    return;

  int sx = row.x + split_x;
  fill_rect(get_sys_color(brWindowDarkBg), R(sx, row.y, bw, row.h));
  if (bw >= 2 * SCROLLBAR_WIDTH) {
    irect16_t left_arr  = {sx, row.y, SCROLLBAR_WIDTH, row.h};
    irect16_t right_arr = {sx + bw - SCROLLBAR_WIDTH, row.y, SCROLLBAR_WIDTH, row.h};
    fill_rect(get_sys_color(brControlBg), left_arr);
    draw_theme_icon_in_rect_scrollbar(THEME_ICON_SCROLL_LEFT, left_arr, get_sys_color(brTextNormal));
    fill_rect(get_sys_color(brControlBg), right_arr);
    draw_theme_icon_in_rect_scrollbar(THEME_ICON_SCROLL_RIGHT, right_arr, get_sys_color(brTextNormal));
    int eff_track = bw - 2 * SCROLLBAR_WIDTH;
    if (eff_track > 0) {
      int tl = ui_sb_thumb_len(sb, eff_track);
      int to = ui_sb_thumb_off(sb, eff_track, tl);
      uint32_t thumb_col = sb->enabled ? get_sys_color(brLightEdge) : get_sys_color(brDarkEdge);
      fill_rect(thumb_col, R(left_arr.x + left_arr.w + to, row.y, tl, row.h));
    }
  } else {
    int tl = ui_sb_thumb_len(sb, bw);
    int to = ui_sb_thumb_off(sb, bw, tl);
    uint32_t thumb_col = sb->enabled ? get_sys_color(brLightEdge) : get_sys_color(brDarkEdge);
    fill_rect(thumb_col, R(sx + to, row.y, tl, row.h));
  }
}

static void draw_theme_icon_in_rect_scrollbar(int id, irect16_t r, uint32_t col) {
  draw_theme_icon(id,
                  r.x + (r.w - THEME_ICON_SIZE) / 2,
                  r.y + (r.h - THEME_ICON_SIZE) / 2,
                  THEME_ICON_SIZE, col);
}

void draw_builtin_scrollbars(window_t *win) {
  bool has_h = (win->flags & WINDOW_HSCROLL) && win->hscroll.visible;
  bool has_v = (win->flags & WINDOW_VSCROLL) && win->vscroll.visible;
  if (!has_h && !has_v) return;

  // TODO: overlay — apply theme scrollbar_overlay/scrollbar_width policy here
  // once overlay-thumb support lands; currently only reserved-space bars render.
  static theme_t *s_last_sb_theme = NULL;
  theme_t *th = get_theme();
  if (th != s_last_sb_theme) {
    fprintf(stderr, "[theme] builtin-scrollbar geometry: name=%s overlay=%d width=%d\n",
            th->name, (int)th->scrollbar_overlay, th->scrollbar_width);
    fflush(stderr);
    s_last_sb_theme = th;
  }

  int t = titlebar_height(win);
  int s = statusbar_height(win);

  window_t *root = get_root_window(win);
  int root_t = titlebar_height(root);
  int base_x = window_screen_x(win) - root->frame.x;
  int base_y = window_screen_y(win) - (root->frame.y + root_t);

  bool h_merged = has_h && (win->flags & WINDOW_STATUSBAR);
  int content_h = win->frame.h - t - s;

  if (has_h && !h_merged) {
    win_sb_t *sb = &win->hscroll;
    int bw = win->frame.w - (has_v ? SCROLLBAR_WIDTH : 0);
    irect16_t hbar = {base_x, base_y + content_h - SCROLLBAR_WIDTH, bw, SCROLLBAR_WIDTH};
    fill_rect(get_sys_color(brWindowDarkBg), hbar);
    if (bw >= 2 * SCROLLBAR_WIDTH) {
      irect16_t left_arr  = rect_split_left(hbar, SCROLLBAR_WIDTH);
      irect16_t right_arr = rect_split_right(hbar, SCROLLBAR_WIDTH);
      fill_rect(get_sys_color(brControlBg), left_arr);
      draw_theme_icon_in_rect_scrollbar(THEME_ICON_SCROLL_LEFT, left_arr, get_sys_color(brTextNormal));
      fill_rect(get_sys_color(brControlBg), right_arr);
      draw_theme_icon_in_rect_scrollbar(THEME_ICON_SCROLL_RIGHT, right_arr, get_sys_color(brTextNormal));
      int eff_track = bw - 2 * SCROLLBAR_WIDTH;
      if (eff_track > 0) {
        int tl = ui_sb_thumb_len(sb, eff_track);
        int to = ui_sb_thumb_off(sb, eff_track, tl);
        uint32_t thumb_col = sb->enabled ? get_sys_color(brLightEdge) : get_sys_color(brDarkEdge);
        fill_rect(thumb_col, R(left_arr.x + left_arr.w + to, hbar.y, tl, SCROLLBAR_WIDTH));
      }
    } else {
      int tl = ui_sb_thumb_len(sb, bw);
      int to = ui_sb_thumb_off(sb, bw, tl);
      uint32_t thumb_col = sb->enabled ? get_sys_color(brLightEdge) : get_sys_color(brDarkEdge);
      fill_rect(thumb_col, R(hbar.x + to, hbar.y, tl, SCROLLBAR_WIDTH));
    }
  }

  if (has_v) {
    win_sb_t *sb = &win->vscroll;
    int bh = content_h - (has_h && !h_merged ? SCROLLBAR_WIDTH : 0);
    irect16_t vbar = {base_x + win->frame.w - SCROLLBAR_WIDTH, base_y, SCROLLBAR_WIDTH, bh};
    fill_rect(get_sys_color(brWindowDarkBg), vbar);
    if (bh >= 2 * SCROLLBAR_WIDTH) {
      irect16_t top_arr = rect_split_top(vbar, SCROLLBAR_WIDTH);
      irect16_t bot_arr = rect_split_bottom(vbar, SCROLLBAR_WIDTH);
      fill_rect(get_sys_color(brControlBg), top_arr);
      draw_theme_icon_in_rect_scrollbar(THEME_ICON_SCROLL_UP, top_arr, get_sys_color(brTextNormal));
      fill_rect(get_sys_color(brControlBg), bot_arr);
      draw_theme_icon_in_rect_scrollbar(THEME_ICON_SCROLL_DOWN, bot_arr, get_sys_color(brTextNormal));
      int eff_track = bh - 2 * SCROLLBAR_WIDTH;
      if (eff_track > 0) {
        int tl = ui_sb_thumb_len(sb, eff_track);
        int to = ui_sb_thumb_off(sb, eff_track, tl);
        uint32_t thumb_col = sb->enabled ? get_sys_color(brLightEdge) : get_sys_color(brDarkEdge);
        fill_rect(thumb_col, R(vbar.x, top_arr.y + top_arr.h + to, SCROLLBAR_WIDTH, tl));
      }
    } else {
      int tl = ui_sb_thumb_len(sb, bh);
      int to = ui_sb_thumb_off(sb, bh, tl);
      uint32_t thumb_col = sb->enabled ? get_sys_color(brLightEdge) : get_sys_color(brDarkEdge);
      fill_rect(thumb_col, R(vbar.x, vbar.y + to, SCROLLBAR_WIDTH, tl));
    }
  }

  if (has_h && !h_merged && has_v) {
    irect16_t corner = {base_x + win->frame.w - SCROLLBAR_WIDTH,
                        base_y + content_h - SCROLLBAR_WIDTH,
                        SCROLLBAR_WIDTH, SCROLLBAR_WIDTH};
    fill_rect(get_sys_color(brWindowDarkBg), corner);
    draw_theme_icon_in_rect_scrollbar(THEME_ICON_RESIZE, corner, get_sys_color(brTextNormal));
  }
}

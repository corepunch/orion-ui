#include <stdio.h>
#include "user.h"
#include "messages.h"
#include "draw.h"
#include "theme.h"
#include <platform/platform.h>


// ── Overlay scrollbar constants ───────────────────────────────────────────────
// Visual thumb is 6 px wide; hit zone is 16 px so the target is always
// reachable even though the gutter is invisible.
#define SB_OVERLAY_THUMB_W   6
#define SB_OVERLAY_HIT_W    16
// Thumb auto-hides after this many milliseconds of no activity.
#define SB_OVERLAY_HIDE_MS  1500

// ── Helpers ───────────────────────────────────────────────────────────────────

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

// ── Overlay reveal / hide ─────────────────────────────────────────────────────

// Reveal the overlay thumb and (re)start the auto-hide timer.
// Safe to call repeatedly — cancels any pending timer before starting a new one.
static void sb_overlay_reveal(window_t *win, win_sb_t *sb) {
  bool was_visible = sb->overlay_visible;
  sb->overlay_visible = true;
  // Cancel previous timer if any.
  if (sb->hide_timer_id) {
    axCancelTimer(sb->hide_timer_id);
    sb->hide_timer_id = 0;
  }
  sb->hide_timer_id = axSetTimer(win, SB_OVERLAY_HIDE_MS, NULL, false);
  if (!was_visible)
    invalidate_window(win);
}

// Hide the overlay thumb for a single bar and clear the timer handle.
static void sb_overlay_hide(window_t *win, win_sb_t *sb) {
  sb->overlay_visible = false;
  sb->hide_timer_id   = 0;
  invalidate_window(win);
}

// ── set_scroll_info helpers ───────────────────────────────────────────────────

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

// ── Mouse coordinate helpers ──────────────────────────────────────────────────

static int sb_mouse_axis_coord(uint32_t wparam, bool vertical) {
  return vertical ? (int16_t)HIWORD(wparam) : (int16_t)LOWORD(wparam);
}

static int sb_mouse_axis_delta(void *lparam, bool vertical) {
  uint32_t delta = (uint32_t)(uintptr_t)lparam;
  return vertical ? (int16_t)HIWORD(delta) : (int16_t)LOWORD(delta);
}

// ── Scroll helpers ────────────────────────────────────────────────────────────

// Attempt to scroll to new_pos.  Returns true if the position changed.
// In overlay mode, also reveals the thumb and restarts the hide timer.
static bool sb_try_scroll(window_t *win, win_sb_t *sb, uint32_t scroll_msg, int new_pos) {
  new_pos = ui_sb_clamp_range(sb, new_pos);
  if (new_pos == sb->pos) return false;
  sb->pos = new_pos;
  send_message(win, scroll_msg, (uint32_t)new_pos, NULL);
  invalidate_window(win);
  if (get_theme()->scrollbar_overlay)
    sb_overlay_reveal(win, sb);
  // a. Scroll under stationary pointer: re-evaluate hover after content moves
  // under a stationary cursor.  Synthesise an evMouseMove at the last known
  // pointer position so the window proc can update item-level hover state.
  {
    int sx = g_ui_runtime.last_mouse_sx;
    int sy = g_ui_runtime.last_mouse_sy;
    int abs_x = win->parent ? window_screen_x(win) : win->frame.x;
    int abs_y = win->parent ? window_screen_y(win)
                            : (win->frame.y + titlebar_height(win));
    int lx = sx - abs_x + (int)win->hscroll.pos;
    int ly = sy - abs_y + (int)win->vscroll.pos;
    if (lx >= 0 && lx < win->frame.w && ly >= 0)
      send_message(win, evMouseMove, MAKEDWORD((uint16_t)lx, (uint16_t)ly), NULL);
  }
  return true;
}

// ── Classic drag helpers ──────────────────────────────────────────────────────

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

// ── Overlay drag helpers ──────────────────────────────────────────────────────

// Begin or page-scroll in overlay mode.  track is the full content extent
// along the scroll axis (no gutter deduction, no arrow buttons).
static void sb_handle_track_click_overlay(window_t *win, win_sb_t *sb,
                                          uint32_t scroll_msg, int pos, int track) {
  int tl = ui_sb_thumb_len(sb, track);
  int to = ui_sb_thumb_off(sb, track, tl);
  if (pos >= to && pos < to + tl) {
    // Begin thumb drag.
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

// Update scroll position while dragging an overlay thumb.
// track is the full content extent (no arrow deduction).
static void sb_handle_drag_move_overlay(window_t *win, win_sb_t *sb,
                                        uint32_t scroll_msg,
                                        int mouse_delta, int track) {
  sb->drag_mouse += mouse_delta;
  int tl = ui_sb_thumb_len(sb, track);
  int tp = track - tl;
  int tr = sb->max_val - sb->min_val - sb->page;
  if (tp > 0 && tr > 0) {
    sb_try_scroll(win, sb, scroll_msg,
                  sb->drag_start_pos +
                  (sb->drag_mouse - sb->drag_start_mouse) * tr / tp);
  }
}

// ── Public API ────────────────────────────────────────────────────────────────

bool scrollbar_handle_builtin_mouse(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  bool has_h = (win->flags & WINDOW_HSCROLL) && win->hscroll.visible;
  bool has_v = (win->flags & WINDOW_VSCROLL) && win->vscroll.visible;

  int t = titlebar_height(win);
  int s = statusbar_height(win);
  int content_h = win->frame.h - t - s;

  bool h_merged = has_h && (win->flags & WINDOW_STATUSBAR);

  bool overlay = get_theme()->scrollbar_overlay;

  if (overlay) {
    // ── Overlay mode ──────────────────────────────────────────────────────────
    //
    // In overlay mode the scrollbar gutter has zero reserved width/height; the
    // thumb is drawn over the content.  Hit-testing uses a 16 px zone at the
    // content edge regardless of the 6 px visual thumb.

    // Convert mouse coords from content-space back to window-space.
    int cx = sb_mouse_axis_coord(wparam, false) - (int)win->hscroll.pos;
    int cy = sb_mouse_axis_coord(wparam, true)  - (int)win->vscroll.pos;

    if (msg == evMouseMove || msg == evLeftButtonUp) {
      // Drag continuation / release.
      if (win->hscroll.dragging) {
        int mouse_delta = sb_mouse_axis_delta(lparam, false);
        if (msg == evMouseMove) {
          sb_handle_drag_move_overlay(win, &win->hscroll, evHScroll,
                                      mouse_delta, win->frame.w);
        } else {
          win->hscroll.dragging = false;
          set_capture(NULL);
          // Restart hide timer after drag release.
          sb_overlay_reveal(win, &win->hscroll);
        }
        return true;
      }
      if (win->vscroll.dragging) {
        int mouse_delta = sb_mouse_axis_delta(lparam, true);
        if (msg == evMouseMove) {
          sb_handle_drag_move_overlay(win, &win->vscroll, evVScroll,
                                      mouse_delta, content_h);
        } else {
          win->vscroll.dragging = false;
          set_capture(NULL);
          sb_overlay_reveal(win, &win->vscroll);
        }
        return true;
      }

      if (msg == evMouseMove) {
        // Reveal bars when the pointer is within the hit zone at the edge.
        if (has_v && cx >= win->frame.w - SB_OVERLAY_HIT_W &&
            cy >= 0 && cy < content_h)
          sb_overlay_reveal(win, &win->vscroll);
        if (has_h && !h_merged &&
            cy >= content_h - SB_OVERLAY_HIT_W &&
            cx >= 0 && cx < win->frame.w)
          sb_overlay_reveal(win, &win->hscroll);
      }
      // Don't consume mouse-move/up — content should still receive them.
      return false;
    }

    if (msg != evLeftButtonDown && msg != evLeftButtonDoubleClick) return false;
    if (!has_h && !has_v) return false;

    // Hit-test using the 16 px zone at the window edge.
    if (has_v && cx >= win->frame.w - SB_OVERLAY_HIT_W && cx < win->frame.w &&
        cy >= 0 && cy < content_h) {
      if (!win->vscroll.enabled) return true;
      sb_overlay_reveal(win, &win->vscroll);
      sb_handle_track_click_overlay(win, &win->vscroll, evVScroll, cy, content_h);
      return true;
    }
    if (has_h && !h_merged &&
        cy >= content_h - SB_OVERLAY_HIT_W && cy < content_h &&
        cx >= 0 && cx < win->frame.w) {
      if (!win->hscroll.enabled) return true;
      sb_overlay_reveal(win, &win->hscroll);
      sb_handle_track_click_overlay(win, &win->hscroll, evHScroll, cx, win->frame.w);
      return true;
    }
    return false;

  } else {
    // ── Classic (reserved-gutter) mode ────────────────────────────────────────

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
}

// Called from message.c on evTimer for windows that have built-in scrollbars.
// Checks whether the timer ID matches a pending overlay-hide timer and, if so,
// hides the corresponding thumb.  Does NOT consume the event (the window proc
// may also have timers of its own).
void scrollbar_handle_builtin_timer(window_t *win, uint32_t timer_id) {
  if (!timer_id) return;
  if ((win->flags & WINDOW_HSCROLL) && win->hscroll.hide_timer_id == timer_id) {
    sb_overlay_hide(win, &win->hscroll);
    return;
  }
  if ((win->flags & WINDOW_VSCROLL) && win->vscroll.hide_timer_id == timer_id) {
    sb_overlay_hide(win, &win->vscroll);
  }
}

void scrollbar_handle_builtin_wheel(window_t *win, void *lparam) {
  if (!win) return;
  // sb_try_scroll already calls sb_overlay_reveal in overlay mode when the
  // position changes, so no additional reveal call is needed here.
  if ((win->flags & WINDOW_HSCROLL) && win->hscroll.visible && win->hscroll.enabled) {
    int delta = (int16_t)LOWORD((uintptr_t)lparam);
    sb_try_scroll(win, &win->hscroll, evHScroll, win->hscroll.pos + delta);
  }
  if ((win->flags & WINDOW_VSCROLL) && win->vscroll.visible && win->vscroll.enabled) {
    int delta = -(int16_t)HIWORD((uintptr_t)lparam);
    sb_try_scroll(win, &win->vscroll, evVScroll, win->vscroll.pos + delta);
  }
}

// ── Status-bar merged hscroll ─────────────────────────────────────────────────
// The shared status row retains its geometry in both themes. Each part is
// painted by the theme, using the same bounds as scrollbar hit-testing.

void scrollbar_draw_statusbar_merged_hscroll(window_t *win, irect16_t row, int split_x) {
  if (!win) return;
  win_sb_t *sb = &win->hscroll;
  irect16_t corner = rect_split_right(row, SCROLLBAR_WIDTH);
  theme_draw(THEME_PART_SCROLLBAR_CORNER, corner, CTRL_NORMAL);

  int bw = row.w - split_x - SCROLLBAR_WIDTH;
  if (bw <= 0)
    return;

  int sx = row.x + split_x;
  theme_draw(THEME_PART_SCROLLBAR_TRACK, R(sx, row.y, bw, row.h), CTRL_NORMAL);
  if (bw >= 2 * SCROLLBAR_WIDTH) {
    irect16_t left_arr  = {sx, row.y, SCROLLBAR_WIDTH, row.h};
    irect16_t right_arr = {sx + bw - SCROLLBAR_WIDTH, row.y, SCROLLBAR_WIDTH, row.h};
    theme_draw(THEME_PART_SCROLLBAR_ARROW_LEFT, left_arr, sb->enabled ? CTRL_NORMAL : CTRL_DISABLED);
    theme_draw(THEME_PART_SCROLLBAR_ARROW_RIGHT, right_arr, sb->enabled ? CTRL_NORMAL : CTRL_DISABLED);
    int eff_track = bw - 2 * SCROLLBAR_WIDTH;
    if (eff_track > 0) {
      int tl = ui_sb_thumb_len(sb, eff_track);
      int to = ui_sb_thumb_off(sb, eff_track, tl);
      theme_draw(THEME_PART_SCROLLBAR_THUMB, R(left_arr.x + left_arr.w + to, row.y, tl, row.h), sb->enabled ? CTRL_NORMAL : CTRL_DISABLED);
    }
  } else {
    int tl = ui_sb_thumb_len(sb, bw);
    int to = ui_sb_thumb_off(sb, bw, tl);
    theme_draw(THEME_PART_SCROLLBAR_THUMB, R(sx + to, row.y, tl, row.h), sb->enabled ? CTRL_NORMAL : CTRL_DISABLED);
  }
}


// ── draw_builtin_scrollbars ───────────────────────────────────────────────────

void draw_builtin_scrollbars(window_t *win) {
  bool has_h = (win->flags & WINDOW_HSCROLL) && win->hscroll.visible;
  bool has_v = (win->flags & WINDOW_VSCROLL) && win->vscroll.visible;
  if (!has_h && !has_v) return;

  theme_t *th = get_theme();

  int t = titlebar_height(win);
  int s = statusbar_height(win);

  window_t *root = get_root_window(win);
  int root_t = titlebar_height(root);
  int base_x = window_screen_x(win) - root->frame.x;
  int base_y = window_screen_y(win) - (root->frame.y + root_t);

  bool h_merged = has_h && (win->flags & WINDOW_STATUSBAR);
  int content_h = win->frame.h - t - s;

  if (th->scrollbar_overlay) {
    // ── Overlay mode: draw thin transparent thumbs over content ──────────────
    //
    // No gutter fill, no arrow buttons.  Each thumb is SB_OVERLAY_THUMB_W px
    // wide and is only rendered when overlay_visible is set.  The thumb uses
    // the full content_h / frame.w as its track (no arrow deduction).

    if (has_v && win->vscroll.overlay_visible) {
      win_sb_t *sb = &win->vscroll;
      int track = content_h;
      int tl = ui_sb_thumb_len(sb, track);
      int to = ui_sb_thumb_off(sb, track, tl);
      theme_draw(THEME_PART_SCROLLBAR_THUMB, R(base_x + win->frame.w - SB_OVERLAY_THUMB_W,
                       base_y + to,
                       SB_OVERLAY_THUMB_W, tl), sb->enabled ? CTRL_NORMAL : CTRL_DISABLED);
    }

    if (has_h && !h_merged && win->hscroll.overlay_visible) {
      win_sb_t *sb = &win->hscroll;
      int track = win->frame.w;
      int tl = ui_sb_thumb_len(sb, track);
      int to = ui_sb_thumb_off(sb, track, tl);
      theme_draw(THEME_PART_SCROLLBAR_THUMB, R(base_x + to,
                       base_y + content_h - SB_OVERLAY_THUMB_W,
                       tl, SB_OVERLAY_THUMB_W), sb->enabled ? CTRL_NORMAL : CTRL_DISABLED);
    }
    return;
  }

  // ── Classic mode: reserved-gutter bars with arrow buttons ────────────────

  if (has_h && !h_merged) {
    win_sb_t *sb = &win->hscroll;
    int bw = win->frame.w - (has_v ? SCROLLBAR_WIDTH : 0);
    irect16_t hbar = {base_x, base_y + content_h - SCROLLBAR_WIDTH, bw, SCROLLBAR_WIDTH};
    theme_draw(THEME_PART_SCROLLBAR_TRACK, hbar, CTRL_NORMAL);
    if (bw >= 2 * SCROLLBAR_WIDTH) {
      irect16_t left_arr  = rect_split_left(hbar, SCROLLBAR_WIDTH);
      irect16_t right_arr = rect_split_right(hbar, SCROLLBAR_WIDTH);
      theme_draw(THEME_PART_SCROLLBAR_ARROW_LEFT, left_arr, sb->enabled ? CTRL_NORMAL : CTRL_DISABLED);
      theme_draw(THEME_PART_SCROLLBAR_ARROW_RIGHT, right_arr, sb->enabled ? CTRL_NORMAL : CTRL_DISABLED);
      int eff_track = bw - 2 * SCROLLBAR_WIDTH;
      if (eff_track > 0) {
        int tl = ui_sb_thumb_len(sb, eff_track);
        int to = ui_sb_thumb_off(sb, eff_track, tl);
        theme_draw(THEME_PART_SCROLLBAR_THUMB, R(left_arr.x + left_arr.w + to, hbar.y, tl, SCROLLBAR_WIDTH), sb->enabled ? CTRL_NORMAL : CTRL_DISABLED);
      }
    } else {
      int tl = ui_sb_thumb_len(sb, bw);
      int to = ui_sb_thumb_off(sb, bw, tl);
      theme_draw(THEME_PART_SCROLLBAR_THUMB, R(hbar.x + to, hbar.y, tl, SCROLLBAR_WIDTH), sb->enabled ? CTRL_NORMAL : CTRL_DISABLED);
    }
  }

  if (has_v) {
    win_sb_t *sb = &win->vscroll;
    int bh = content_h - (has_h && !h_merged ? SCROLLBAR_WIDTH : 0);
    irect16_t vbar = {base_x + win->frame.w - SCROLLBAR_WIDTH, base_y, SCROLLBAR_WIDTH, bh};
    theme_draw(THEME_PART_SCROLLBAR_TRACK, vbar, CTRL_NORMAL);
    if (bh >= 2 * SCROLLBAR_WIDTH) {
      irect16_t top_arr = rect_split_top(vbar, SCROLLBAR_WIDTH);
      irect16_t bot_arr = rect_split_bottom(vbar, SCROLLBAR_WIDTH);
      theme_draw(THEME_PART_SCROLLBAR_ARROW_UP, top_arr, sb->enabled ? CTRL_NORMAL : CTRL_DISABLED);
      theme_draw(THEME_PART_SCROLLBAR_ARROW_DOWN, bot_arr, sb->enabled ? CTRL_NORMAL : CTRL_DISABLED);
      int eff_track = bh - 2 * SCROLLBAR_WIDTH;
      if (eff_track > 0) {
        int tl = ui_sb_thumb_len(sb, eff_track);
        int to = ui_sb_thumb_off(sb, eff_track, tl);
        theme_draw(THEME_PART_SCROLLBAR_THUMB, R(vbar.x, top_arr.y + top_arr.h + to, SCROLLBAR_WIDTH, tl), sb->enabled ? CTRL_NORMAL : CTRL_DISABLED);
      }
    } else {
      int tl = ui_sb_thumb_len(sb, bh);
      int to = ui_sb_thumb_off(sb, bh, tl);
      theme_draw(THEME_PART_SCROLLBAR_THUMB, R(vbar.x, vbar.y + to, SCROLLBAR_WIDTH, tl), sb->enabled ? CTRL_NORMAL : CTRL_DISABLED);
    }
  }

  if (has_h && !h_merged && has_v) {
    irect16_t corner = {base_x + win->frame.w - SCROLLBAR_WIDTH,
                        base_y + content_h - SCROLLBAR_WIDTH,
                        SCROLLBAR_WIDTH, SCROLLBAR_WIDTH};
    theme_draw(THEME_PART_SCROLLBAR_CORNER, corner, CTRL_NORMAL);
  }
}

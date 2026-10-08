#include <stdlib.h>
#include <string.h>
#include "dock.h"
#include "toolbar.h"
#include "draw.h"
#include "theme.h"

static struct {
  window_t *win;
  ipoint16_t anchor;
  irect16_t original;
  int extent;
  dock_side_t target;
  bool resize, floating_resize, moved, collapsed;
} dock_drag;

static bool dock_error(window_t *win, const char *reason, int value) {
  fprintf(stderr, "[dock] rejected win=%u: %s value=%d\n", win ? win->id : 0, reason, value);
  fflush(stderr);
  return false;
}

bool dock_is_floating(const window_t *win) { return win && win->dock && win->dock->side == DOCK_FLOAT; }

static bool dock_vertical(dock_side_t side) { return side == DOCK_LEFT || side == DOCK_RIGHT; }

static void dock_place(window_t *win, irect16_t frame) {
  if (!memcmp(&win->frame, &frame, sizeof(frame))) return;
  win->frame = frame;
  send_message(win, evResize, 0, NULL);
  invalidate_window(win);
}

static void dock_refresh(window_t *host) {
  if (!host || (host->dock_host && host->dock_host->busy)) return;
  dock_layout(host, get_client_rect(host));
  send_message(host, evDockChanged, 0, NULL);
  invalidate_window(host);
}

static void dock_orient(window_t *win) {
  dock_state_t *d = win->dock;
  if (d->flags & DOCK_TOOLBAR) {
    bool vertical = dock_vertical(d->side == DOCK_FLOAT ? d->last_side : d->side);
    toolbar_set_dock_hint(win, vertical ? TOOLBAR_DOCK_LEFT : TOOLBAR_DOCK_TOP);
    send_message(win, tbSetOrientation, vertical ? TOOLBAR_VERTICAL : TOOLBAR_HORIZONTAL, NULL);
  }
  if (d->flags & DOCK_MENU) send_message(win, evDockOrient, d->side == DOCK_FLOAT || dock_vertical(d->side), NULL);
}

bool dock_window(window_t *win, dock_side_t side, uint32_t allowed, uint32_t flags, int extent, int minimum) {
  if (!win || !win->parent || side < DOCK_FLOAT || side > DOCK_FILL || extent < 0 || minimum < 0 ||
      (allowed & ~DOCK_ALL_EDGES) || (flags & ~(DOCK_RESIZABLE | DOCK_TOOLBAR | DOCK_MENU | DOCK_NOFLOAT)) ||
      (side > DOCK_FLOAT && side < DOCK_FILL && !(allowed & DOCK_EDGE(side))) ||
      (side == DOCK_FLOAT && (flags & DOCK_NOFLOAT)))
    return dock_error(win, "invalid registration", side);
  if (win->dock) return dock_error(win, "already registered", side);
  dock_state_t *d = calloc(1, sizeof(*d));
  if (!d) return dock_error(win, "allocation failed", side);
  d->side = side; d->last_side = side == DOCK_FLOAT && toolbar_get_state(win) && toolbar_get_state(win)->orientation == TOOLBAR_VERTICAL
    ? DOCK_LEFT : side == DOCK_FLOAT ? DOCK_TOP : side;
  d->allowed = allowed; d->flags = flags; d->extent = extent; d->minimum = minimum;
  d->floating = win->frame;
  for (window_t *c = win->parent->children; c; c = c->next)
    if (c->dock) d->order = MAX(d->order, c->dock->order + 1);
  win->dock = d;
  dock_orient(win);
  dock_refresh(win->parent);
  return true;
}

bool dock_set_side(window_t *win, dock_side_t side) {
  if (!win || !win->dock || side < DOCK_TOP || side > DOCK_RIGHT || !(win->dock->allowed & DOCK_EDGE(side)))
    return dock_error(win, "edge unavailable", side);
  dock_state_t *d = win->dock;
  if (d->side == DOCK_FLOAT && !d->collapsed) d->floating = win->frame;
  d->side = d->last_side = side;
  dock_orient(win);
  dock_refresh(win->parent);
  return true;
}

bool dock_float(window_t *win, irect16_t frame) {
  if (!win || !win->dock || (win->dock->flags & DOCK_NOFLOAT) || win->dock->side == DOCK_FILL || frame.w <= 0 || frame.h <= 0)
    return dock_error(win, "floating unavailable", 0);
  if (win->dock->side != DOCK_FLOAT) win->dock->last_side = win->dock->side;
  if (win->dock->collapsed) frame.h = MAX(frame.h, MAX(win->dock->floating.h, win->dock->extent));
  win->dock->side = DOCK_FLOAT;
  win->dock->floating = frame;
  dock_orient(win);
  dock_place(win, frame);
  dock_refresh(win->parent);
  return true;
}

bool dock_collapse(window_t *win, bool collapsed) {
  if (!win || !win->dock || !(win->dock->flags & DOCK_RESIZABLE)) return dock_error(win, "collapse unavailable", collapsed);
  if (collapsed && (win->flags & WINDOW_NOCOLLAPSE)) return dock_error(win, "collapse disabled by WINDOW_NOCOLLAPSE", collapsed);
  win->dock->collapsed = collapsed;
  dock_refresh(win->parent);
  return true;
}

static int dock_measure(window_t *win, irect16_t area) {
  dock_state_t *d = win->dock;
  if (d->flags & DOCK_MENU) {
    ipoint16_t size = {0};
    send_message(win, evDockMeasure, 0, &size);
    return dock_vertical(d->side) ? size.x : size.y;
  }
  if (d->flags & DOCK_TOOLBAR) {
    if (!dock_vertical(d->side)) return titlebar_height(win);
    win->frame.h = area.h;
    toolbar_handle_message(win, evResize, 0, NULL);
    toolbar_state_t *tb = toolbar_get_state(win);
    int size = toolbar_effective_bsz(win) + 2 * toolbar_effective_padding(win);
    for (int i = 0; tb && tb->item_rects && i < tb->item_count; i++)
      size = MAX(size, tb->item_rects[i].x + tb->item_rects[i].w + toolbar_effective_padding(win));
    return size;
  }
  return d->collapsed ? titlebar_height(win) : MAX(d->extent, MAX(d->minimum, titlebar_height(win)));
}

static int64_t dock_order(window_t *win) {
  return (int64_t)win->dock->order + (dock_vertical(win->dock->side) ? (INT64_C(1) << 32) : 0);
}

irect16_t dock_layout(window_t *host, irect16_t area) {
  if (!host) { dock_error(NULL, "missing host", 0); return area; }
  if (!host->dock_host && !(host->dock_host = calloc(1, sizeof(*host->dock_host)))) {
    dock_error(host, "host allocation failed", 0);
    return area;
  }
  if (host->dock_host->busy) return host->dock_host->content;
  host->dock_host->busy = true;
  area.w = MAX(0, area.w); area.h = MAX(0, area.h);
  int64_t order = -1;
  for (;;) {
    window_t *pane = NULL;
    for (window_t *c = host->children; c; c = c->next)
      if (c->dock && dock_order(c) > order && (!pane || dock_order(c) < dock_order(pane))) pane = c;
    if (!pane) break;
    dock_state_t *d = pane->dock;
    order = dock_order(pane);
    d->splitter = R(0, 0, 0, 0);
    if (!window_has_state(pane, WINDOW_STATE_VISIBLE) || d->side == DOCK_FILL ||
        ((d->flags & DOCK_TOOLBAR) && toolbar_dock_hint(pane) == TOOLBAR_DOCK_MENU)) continue;
    if (d->side == DOCK_FLOAT) {
      irect16_t r = d->floating, bounds = get_client_rect(host);
      if (d->flags & DOCK_MENU) {
        ipoint16_t size = {0}; send_message(pane, evDockMeasure, 0, &size);
        r.w = size.x; r.h = size.y;
      }
      if (d->collapsed) r.h = titlebar_height(pane);
      r.w = MIN(r.w, bounds.w); r.h = MIN(r.h, bounds.h);
      r.x = CLAMP(r.x, 0, MAX(0, bounds.w - r.w));
      r.y = CLAMP(r.y, 0, MAX(0, bounds.h - r.h));
      dock_place(pane, r);
      continue;
    }
    irect16_t old = pane->frame;
    int available = dock_vertical(d->side) ? area.w : area.h;
    int gap = (d->flags & DOCK_RESIZABLE) ? MIN(DOCK_SPLITTER, available) : 0;
    int reserve = 0;
    for (window_t *c = host->children; c; c = c->next)
      if (c->dock && c->dock->side == DOCK_FILL && window_has_state(c, WINDOW_STATE_VISIBLE)) reserve = MAX(reserve, c->dock->minimum);
    reserve = MIN(reserve, MAX(0, available - gap - titlebar_height(pane)));
    int size = MIN(dock_measure(pane, area), MAX(0, available - gap - reserve));
    pane->frame = old;
    irect16_t band = area, splitter = area;
    switch (d->side) {
      case DOCK_TOP:    band = rect_split_top(area, size);    area = rect_trim_top(area, size);    splitter = rect_split_top(area, gap);    area = rect_trim_top(area, gap);    break;
      case DOCK_BOTTOM: band = rect_split_bottom(area, size); area = rect_trim_bottom(area, size); splitter = rect_split_bottom(area, gap); area = rect_trim_bottom(area, gap); break;
      case DOCK_LEFT:   band = rect_split_left(area, size);   area = rect_trim_left(area, size);   splitter = rect_split_left(area, gap);   area = rect_trim_left(area, gap);   break;
      case DOCK_RIGHT:  band = rect_split_right(area, size);  area = rect_trim_right(area, size);  splitter = rect_split_right(area, gap);  area = rect_trim_right(area, gap);  break;
      default: break;
    }
    d->splitter = splitter;
    dock_place(pane, band);
  }
  host->dock_host->content = area;
  for (window_t *c = host->children; c; c = c->next)
    if (c->dock && c->dock->side == DOCK_FILL && window_has_state(c, WINDOW_STATE_VISIBLE)) dock_place(c, area);
  host->dock_host->busy = false;
  return area;
}

irect16_t dock_content_rect(window_t *host) {
  if (!host) { dock_error(NULL, "missing host", 0); return R(0, 0, 0, 0); }
  return dock_layout(host, get_client_rect(host));
}

window_t *dock_hit_test(window_t *host, ipoint16_t point) {
  window_t *hit = NULL;
  for (window_t *c = host->children; c; c = c->next)
    if (dock_is_floating(c) && window_has_state(c, WINDOW_STATE_VISIBLE) && rect_contains_point(c->frame, point)) hit = c;
  return hit;
}

static bool dock_grip(window_t *win, ipoint16_t point) {
  dock_state_t *d = win->dock;
  if (d->side == DOCK_FILL || (d->flags & DOCK_NOFLOAT) || (win->flags & WINDOW_NODRAG)) return false;
  if (d->flags & DOCK_MENU) {
    bool vertical = d->side == DOCK_FLOAT || dock_vertical(d->side);
    return vertical ? point.y < TOOLBAR_GRIP_HEIGHT : point.x < TOOLBAR_GRIP_WIDTH;
  }
  if (point.y >= titlebar_height(win)) return false;
  if (win->flags & WINDOW_TOOLBAR) {
    toolbar_state_t *tb = toolbar_get_state(win);
    if (toolbar_hit_action(tb, point.x, point.y - toolbar_content_offset(win))) return false;
  }
  return true;
}

static dock_side_t dock_target(window_t *win, ipoint16_t screen) {
  window_t *host = win->parent;
  irect16_t r = get_client_rect(host);
  ipoint16_t p = {screen.x - window_screen_x(host), screen.y - window_screen_y(host) - titlebar_height(host)};
  if (!rect_contains_point(r, p)) return DOCK_FLOAT;
  int distances[] = {0, p.y, p.x, r.h - p.y, r.w - p.x};
  dock_side_t side = DOCK_FLOAT;
  int nearest = 32;
  for (int i = DOCK_TOP; i <= DOCK_RIGHT; i++)
    if ((win->dock->allowed & DOCK_EDGE(i)) && distances[i] < nearest) { side = i; nearest = distances[i]; }
  return side;
}

void dock_cancel_window(window_t *win) {
  if (!win || dock_drag.win != win) return;
  win->dock->extent = dock_drag.extent;
  win->dock->collapsed = dock_drag.collapsed;
  if (dock_drag.floating_resize) win->dock->floating = dock_drag.original;
  window_clear_drag_visual(win);
  memset(&dock_drag, 0, sizeof(dock_drag));
  dock_refresh(win->parent);
}

bool dock_handle_event(ui_event_t *event) {
  uint32_t msg = event->message;
  ipoint16_t screen = {event->x / UI_WINDOW_SCALE, event->y / UI_WINDOW_SCALE};
  if (dock_drag.win) {
    window_t *win = dock_drag.win;
    if (msg == kEventPointerCancel || msg == kEventKillFocus || (msg == kEventKeyDown && event->keyCode == AX_KEY_ESCAPE)) {
      dock_cancel_window(win);
      return msg != kEventKillFocus;
    }
    if (msg != kEventLeftButtonDragged && msg != kEventMouseMoved && msg != kEventLeftButtonUp) return false;
    int dx = screen.x - dock_drag.anchor.x, dy = screen.y - dock_drag.anchor.y;
    if (abs(dx) + abs(dy) > 4) dock_drag.moved = true;
    if (dock_drag.floating_resize) {
      irect16_t r = dock_drag.original;
      r.w = MAX(100, r.w + dx); r.h = MAX(MAX(win->dock->minimum, titlebar_height(win)), r.h + dy);
      win->dock->floating = r;
      dock_refresh(win->parent);
    } else if (dock_drag.resize) {
      if (dock_drag.moved) win->dock->collapsed = false;
      int delta = dock_vertical(win->dock->side) ? dx : dy;
      if (win->dock->side == DOCK_BOTTOM || win->dock->side == DOCK_RIGHT) delta = -delta;
      win->dock->extent = MAX(win->dock->minimum, dock_drag.extent + delta);
      dock_refresh(win->parent);
    } else if (dock_drag.moved) {
      dock_drag.target = dock_target(win, screen);
      window_set_drag_visual(win, dx, dy);
    }
    if (msg == kEventLeftButtonUp) {
      bool move = dock_drag.moved && !dock_drag.resize && !dock_drag.floating_resize;
      irect16_t frame = rect_offset(dock_drag.original, dx, dy);
      memset(&dock_drag, 0, sizeof(dock_drag));
      window_clear_drag_visual(win);
      if (move) {
        dock_side_t target = dock_target(win, screen);
        if (target == DOCK_FLOAT) dock_float(win, frame);
        else dock_set_side(win, target);
      }
    }
    return true;
  }
  if ((msg != kEventLeftButtonDown && msg != kEventLeftDoubleClick) || g_ui_runtime.captured) return false;
  window_t *hit = find_window(screen.x, screen.y);
  if (!hit || window_has_state(hit, WINDOW_STATE_DISABLED)) return false;
  for (window_t *pane = hit; pane; pane = pane->parent) {
    if (!dock_is_floating(pane) || !(pane->dock->flags & DOCK_RESIZABLE) || pane->dock->collapsed) continue;
    ipoint16_t p = {screen.x - window_screen_x(pane), screen.y - window_screen_y(pane)};
    irect16_t grip = rect_split_right(rect_split_bottom(R(0, 0, pane->frame.w, pane->frame.h), 16), 16);
    if (rect_contains_point(grip, p)) {
      dock_drag.win = pane; dock_drag.floating_resize = true; dock_drag.anchor = screen;
      dock_drag.original = pane->frame; dock_drag.extent = pane->dock->extent;
      return true;
    }
  }
  for (window_t *pane = hit; pane; pane = pane->parent) {
    if (!dock_is_floating(pane)) continue;
    window_t **link = &pane->parent->children;
    while (*link && *link != pane) link = &(*link)->next;
    if (*link) {
      *link = pane->next;
      while (*link) link = &(*link)->next;
      *link = pane; pane->next = NULL;
      invalidate_window(pane->parent);
    }
    break;
  }
  // Splitters occupy the host client; inspect them before a pane's caption.
  for (window_t *host = hit; host; host = host->parent) {
    ipoint16_t point = {screen.x - window_screen_x(host), screen.y - window_screen_y(host) - titlebar_height(host)};
    for (window_t *c = host->children; c; c = c->next) {
      if (!c->dock || !window_has_state(c, WINDOW_STATE_VISIBLE) || !rect_contains_point(c->dock->splitter, point)) continue;
      dock_drag.win = c; dock_drag.resize = true; dock_drag.extent = c->dock->extent; dock_drag.anchor = screen;
      dock_drag.collapsed = c->dock->collapsed;
      return true;
    }
  }
  if (hit->dock && !window_has_state(hit, WINDOW_STATE_DISABLED)) {
    ipoint16_t point = {screen.x - window_screen_x(hit), screen.y - window_screen_y(hit)};
    if (dock_grip(hit, point)) {
      if (msg == kEventLeftDoubleClick) {
        if (dock_is_floating(hit)) dock_set_side(hit, hit->dock->last_side);
        else dock_float(hit, hit->dock->floating);
        return true;
      }
      dock_drag.win = hit; dock_drag.anchor = screen; dock_drag.original = hit->frame; dock_drag.extent = hit->dock->extent;
      dock_drag.collapsed = hit->dock->collapsed;
      set_focus(hit);
      return true;
    }
  }
  return false;
}

void dock_forget_window(window_t *win) {
  if (dock_drag.win == win) memset(&dock_drag, 0, sizeof(dock_drag));
  free(win->dock);
  win->dock = NULL;
  free(win->dock_host);
  win->dock_host = NULL;
}

void dock_paint(window_t *host) {
  for (window_t *c = host->children; c; c = c->next)
    if (c->dock && window_has_state(c, WINDOW_STATE_VISIBLE) && c->dock->splitter.w && c->dock->splitter.h)
      theme_draw(THEME_PART_SEPARATOR, c->dock->splitter, CTRL_NORMAL);
}

int dock_cursor(window_t *hit, ipoint16_t screen) {
  for (window_t *host = hit; host; host = host->parent) {
    ipoint16_t p = {screen.x - window_screen_x(host), screen.y - window_screen_y(host)};
    if (dock_is_floating(host) && (host->dock->flags & DOCK_RESIZABLE) && !host->dock->collapsed &&
        rect_contains_point(rect_split_right(rect_split_bottom(R(0, 0, host->frame.w, host->frame.h), 16), 16), p))
      return curResizeNWSE;
    p.y -= titlebar_height(host);
    for (window_t *c = host->children; c; c = c->next)
      if (c->dock && window_has_state(c, WINDOW_STATE_VISIBLE) && rect_contains_point(c->dock->splitter, p))
        return dock_vertical(c->dock->side) ? curResizeH : curResizeV;
  }
  return -1;
}

void dock_paint_overlay(window_t *host) {
  if (dock_is_floating(host) && (host->dock->flags & DOCK_RESIZABLE) && !host->dock->collapsed) {
    window_t *root = get_root_window(host);
    int x = window_screen_x(host) - root->frame.x, y = window_screen_y(host) - root->frame.y;
    irect16_t frame = R(x, y, host->frame.w, host->frame.h);
    set_viewport_for_fbo(root);
    set_projection(0, 0, root->frame.w, root->frame.h);
    set_scissor_fbo(root, frame);
    theme_draw(THEME_PART_RESIZE_GRIP, frame, CTRL_NORMAL);
  }
  window_t *pane = dock_drag.win;
  if (!pane || pane->parent != host || !dock_drag.moved || dock_drag.target == DOCK_FLOAT) return;
  irect16_t area = get_client_rect(host), preview = area;
  int size = MAX(pane->dock->minimum, pane->dock->extent);
  switch (dock_drag.target) {
    case DOCK_TOP:    preview = rect_split_top(area, MIN(size, area.h));    break;
    case DOCK_BOTTOM: preview = rect_split_bottom(area, MIN(size, area.h)); break;
    case DOCK_LEFT:   preview = rect_split_left(area, MIN(size, area.w));   break;
    case DOCK_RIGHT:  preview = rect_split_right(area, MIN(size, area.w));  break;
    default: return;
  }
  if (pane->dock->flags & (DOCK_TOOLBAR | DOCK_MENU)) {
    int size = titlebar_height(pane);
    if (size <= 0) size = get_theme()->menubar_height;
    switch (dock_drag.target) {
      case DOCK_TOP:    preview = rect_split_top(area, MIN(size, area.h));    break;
      case DOCK_BOTTOM: preview = rect_split_bottom(area, MIN(size, area.h)); break;
      case DOCK_LEFT:   preview = rect_split_left(area, MIN(size, area.w));   break;
      case DOCK_RIGHT:  preview = rect_split_right(area, MIN(size, area.w));  break;
      default: break;
    }
  }
  window_t *root = get_root_window(host);
  int x = window_screen_x(host) - root->frame.x;
  int y = window_screen_y(host) + titlebar_height(host) - root->frame.y;
  set_viewport_for_fbo(root);
  set_projection(-x, -y, root->frame.w - x, root->frame.h - y);
  set_scissor_fbo(root, rect_offset(area, x, y));
  fill_rect(color_with_alpha(get_sys_color(brTextInfo), 70), preview);
}

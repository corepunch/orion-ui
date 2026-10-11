// A pane tree uses ordinary SplitViews and TabViews; moving a tab keeps its window alive.
#include "commctl.h"
#include <orion/user/draw.h>
#include <stdio.h>

#define PANE_MIN_WIDTH 96
#define PANE_MIN_HEIGHT 64

typedef struct {
  window_t *active, *drag_page, *overlay;
  pane_drop_t drop;
} pane_host_state_t;

static bool pane_contains(window_t *owner, window_t *child) {
  for (; child; child = child->parent) if (child == owner) return true;
  return false;
}

static pane_host_state_t *pane_state(window_t *host) {
  return host && is_window(host) && host->proc == win_pane_host ? host->userdata : NULL;
}

static window_t *pane_tree(window_t *host) {
  for (window_t *c = host ? host->children : NULL; c; c = c->next)
    if (c->proc == win_tabview || c->proc == win_splitview) return c;
  return NULL;
}

static window_t *pane_at(window_t *tree, int *index) {
  if (!tree) return NULL;
  if (tree->proc == win_tabview) return (*index)-- == 0 ? tree : NULL;
  if (tree->proc != win_splitview) return NULL;
  window_t *pane = pane_at(splitview_get_left(tree), index);
  return pane ? pane : pane_at(splitview_get_right(tree), index);
}

int pane_host_count(window_t *host) {
  int index = -1;
  if (pane_state(host)) pane_at(pane_tree(host), &index);
  return -index - 1;
}

window_t *pane_host_pane(window_t *host, int index) {
  return pane_state(host) && index >= 0 ? pane_at(pane_tree(host), &index) : NULL;
}

static bool pane_member(window_t *host, window_t *pane) {
  if (!pane_state(host) || !pane || !is_window(pane) || pane->proc != win_tabview) return false;
  window_t *p = pane->parent;
  while (p && p != host && p->proc == win_splitview) p = p->parent;
  return p == host;
}

window_t *pane_host_active(window_t *host) {
  pane_host_state_t *st = pane_state(host);
  if (!st) return NULL;
  if (!pane_member(host, st->active)) st->active = pane_host_pane(host, 0);
  return st->active;
}

static void pane_configure(window_t *tree) {
  if (!tree) return;
  if (tree->proc == win_tabview) {
    uint32_t style = send_message(tree, tcGetStyle, 0, NULL);
    uint32_t required = TAB_STYLE_RESTORE_FOCUS | TAB_STYLE_DRAG_TABS;
    if ((style & required) != required) send_message(tree, tcSetStyle, style | required, NULL);
  } else if (tree->proc == win_splitview) {
    pane_configure(splitview_get_left(tree)); pane_configure(splitview_get_right(tree));
  }
}

static void pane_arrange(window_t *host) {
  window_t *tree = pane_tree(host);
  if (!tree) return;
  pane_configure(tree);
  layout_arrange_t a = {get_client_rect(host)};
  send_message(tree, evArrange, 0, &a);
  invalidate_window(host);
}

static void pane_cancel(window_t *host) {
  pane_host_state_t *st = pane_state(host);
  if (!st) return;
  st->drag_page = NULL; st->drop = (pane_drop_t){0};
  if (st->overlay) window_set_state(st->overlay, WINDOW_STATE_VISIBLE, false);
  if (g_ui_runtime.captured == host) set_capture(NULL);
  invalidate_window(host);
}

static bool pane_can_split(window_t *pane, pane_edge_t edge) {
  irect16_t r = get_client_rect(pane);
  if (edge == PANE_LEFT || edge == PANE_RIGHT) return r.w >= 2 * PANE_MIN_WIDTH + 6;
  if (edge == PANE_TOP || edge == PANE_BOTTOM) return r.h >= 2 * PANE_MIN_HEIGHT + 6;
  return false;
}

window_t *pane_host_split(window_t *host, window_t *pane, pane_edge_t edge) {
  if (!pane_member(host, pane) || !pane_can_split(pane, edge)) {
    fprintf(stderr, "[pane_host] split rejected host=%p pane=%p edge=%d (invalid pane or insufficient space)\n",
            (void *)host, (void *)pane, edge);
    fflush(stderr); return NULL;
  }
  window_t *parent = pane->parent;
  bool leading = edge == PANE_LEFT || edge == PANE_TOP;
  int orientation = edge == PANE_LEFT || edge == PANE_RIGHT ? SPLIT_VERT : SPLIT_HORZ;
  window_t *split = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOACTIVATE | WINDOW_FLEXSPACE,
                                  &pane->frame, parent, "SplitView", host->hinstance, (void *)(intptr_t)orientation);
  window_t *tabs = split ? create_window("", WINDOW_NOTITLE | WINDOW_NOACTIVATE | WINDOW_FLEXSPACE,
                                        MAKERECT(0, 0, 1, 1), split, "TabView", host->hinstance, NULL) : NULL;
  if (!tabs) {
    fprintf(stderr, "[pane_host] split allocation failed host=%u\n", host->id);
    fflush(stderr); if (split) destroy_window(split); return NULL;
  }
  // Place the new branch in the old leaf's slot before moving the leaf inside it.
  window_reparent(split, parent, pane);
  window_reparent(pane, split, leading ? NULL : tabs);
  pane_arrange(host);
  return tabs;
}

static void pane_collapse_empty(window_t *host, window_t *pane) {
  if (!pane_member(host, pane) || send_message(pane, tcGetCount, 0, NULL)) return;
  window_t *split = pane->parent;
  if (split == host || split->proc != win_splitview) return;
  window_t *sibling = splitview_get_left(split);
  if (sibling == pane) sibling = splitview_get_right(split);
  if (!sibling) return;
  window_reparent(sibling, split->parent, split);
  destroy_window(split);
  pane_arrange(host);
}

static int pane_page_index(window_t *tabs, window_t *page) {
  int count = send_message(tabs, tcGetCount, 0, NULL);
  for (int i = 0; i < count; i++) if ((window_t *)send_message(tabs, tcGetPage, i, NULL) == page) return i;
  return -1;
}

bool pane_host_move_tab(window_t *host, window_t *page, window_t *target, pane_edge_t edge, int before) {
  window_t *source = page && is_window(page) ? page->parent : NULL;
  int index = pane_member(host, source) ? pane_page_index(source, page) : -1;
  if (index < 0 || !pane_member(host, target) || edge < PANE_CENTER || edge > PANE_BOTTOM ||
      (edge != PANE_CENTER && source == target && send_message(source, tcGetCount, 0, NULL) == 1)) {
    fprintf(stderr, "[pane_host] move rejected host=%p page=%p target=%p edge=%d\n",
            (void *)host, (void *)page, (void *)target, edge);
    fflush(stderr); return false;
  }
  window_t *destination = target;
  if (edge != PANE_CENTER) destination = pane_host_split(host, target, edge);
  if (!destination) return false;
  if (!tabview_move_page(source, index, destination, edge == PANE_CENTER ? before : -1)) {
    if (destination != target) pane_collapse_empty(host, destination);
    return false;
  }
  pane_state(host)->active = destination;
  pane_collapse_empty(host, source);
  pane_arrange(host);
  return true;
}

bool pane_host_close_tab(window_t *host, window_t *page) {
  window_t *pane = page && is_window(page) ? page->parent : NULL;
  int index = pane_member(host, pane) ? pane_page_index(pane, page) : -1;
  if (index < 0) {
    fprintf(stderr, "[pane_host] close rejected host=%p page=%p\n", (void *)host, (void *)page);
    fflush(stderr); return false;
  }
  if (!send_message(pane, tcDeletePage, index, NULL)) return false;
  pane_collapse_empty(host, pane);
  window_t *active = pane_host_active(host);
  if (active && send_message(active, tcGetCount, 0, NULL))
    send_message(active, tcSetSelection, send_message(active, tcGetSelection, 0, NULL), NULL);
  return true;
}

static result_t pane_preview_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)wparam; (void)lparam;
  switch (msg) {
    case evCreate: case evDestroy: return true;
    case evPaint: {
      pane_host_state_t *st = pane_state(win->parent);
      if (!st || !st->drop.pane) return true;
      static const char *labels[] = {"Move tab here", "Split left", "Split right", "Split above", "Split below"};
      irect16_t r = get_client_rect(win);
      fill_rect(color_with_alpha(get_sys_color(brTextInfo), 70), r);
      stroke_rounded_rect(get_sys_color(brTextInfo), r, 0, 2);
      if (r.w > 80) {
        irect16_t label = R(8, 8, MIN(r.w - 16, measure_badge(FONT_SYSTEM, labels[st->drop.edge])), 24);
        draw_badge_ex(FONT_SYSTEM, labels[st->drop.edge], label, get_sys_color(brTextNormal), get_sys_color(brControlBg), false);
      }
      return true;
    }
    default: return false;
  }
}

static void pane_preview(window_t *host, ipoint16_t point) {
  pane_host_state_t *st = pane_state(host);
  pane_drop_t drop = {0};
  if (!st || !st->drag_page) return;
  for (int i = 0, count = pane_host_count(host); i < count; i++) {
    window_t *pane = pane_host_pane(host, i);
    ipoint16_t pt = window_map_point(host, pane, point);
    irect16_t r = get_client_rect(pane);
    if (!rect_contains_point(r, pt)) continue;
    drop.pane = pane; drop.index = -1;
    irect16_t body = r;
    send_message(pane, tcAdjustRect, 0, &body);
    if (pt.y < body.y) {
      // The TabBar offers insertion slots; the pane body offers split edges.
      int n = send_message(pane, tcGetCount, 0, NULL);
      drop.index = n;
      for (int t = 0; t < n; t++) {
        irect16_t tab; send_message(pane, tcGetTabRect, t, &tab);
        if (pt.x < tab.x + tab.w / 2) { drop.index = t; break; }
      }
      irect16_t tab = {2, 0, 0, body.y};
      if (n) send_message(pane, tcGetTabRect, MIN(drop.index, n - 1), &tab);
      int x = drop.index == n ? tab.x + tab.w : tab.x;
      r = R(MAX(0, MIN(x, r.w - 3)), 0, 3, body.y);
    } else {
      int x = pt.x - body.x, y = pt.y - body.y;
      // Compare normalized edge distances so corners behave consistently in narrow panes.
      int dx = MIN(x, body.w - x), dy = MIN(y, body.h - y);
      if (dx < body.w / 4 && dx * body.h <= dy * body.w) drop.edge = x < body.w / 2 ? PANE_LEFT : PANE_RIGHT;
      else if (dy < body.h / 4) drop.edge = y < body.h / 2 ? PANE_TOP : PANE_BOTTOM;
      if (drop.edge != PANE_CENTER) {
        if (!pane_can_split(pane, drop.edge) ||
            (st->drag_page->parent == pane && send_message(pane, tcGetCount, 0, NULL) == 1)) {
          drop.pane = NULL; break;
        }
        int width = MAX(0, (r.w - 6) / 2), height = MAX(0, (r.h - 6) / 2);
        switch (drop.edge) {
          case PANE_LEFT:   r = rect_split_left(r, width); break;
          case PANE_RIGHT:  r = rect_split_right(r, width); break;
          case PANE_TOP:    r = rect_split_top(r, height); break;
          case PANE_BOTTOM: r = rect_split_bottom(r, height); break;
          default: break;
        }
      }
    }
    ipoint16_t origin = window_map_point(pane, host, (ipoint16_t){r.x, r.y});
    drop.rect = R(origin.x, origin.y, r.w, r.h);
    break;
  }
  st->drop = drop;
  if (!st->overlay) {
    st->overlay = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOACTIVATE | WINDOW_NOTABSTOP,
                                MAKERECT(0, 0, 1, 1), host, pane_preview_proc, host->hinstance, NULL);
    if (!st->overlay) {
      fprintf(stderr, "[pane_host] preview allocation failed host=%u\n", host->id);
      fflush(stderr); pane_cancel(host); return;
    }
  }
  window_set_state(st->overlay, WINDOW_STATE_VISIBLE, drop.pane != NULL);
  if (drop.pane) st->overlay->frame = drop.rect;
  invalidate_window(host);
}

bool pane_host_get_preview(window_t *host, pane_drop_t *preview) {
  pane_host_state_t *st = pane_state(host);
  if (!st || !st->drag_page || !st->drop.pane || !preview) return false;
  *preview = st->drop; return true;
}

result_t win_pane_host(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  pane_host_state_t *st = win->userdata;
  switch (msg) {
    case evCreate:
      if (!allocate_window_data(win, sizeof(*st))) {
        fprintf(stderr, "[pane_host] state allocation failed host=%u\n", win->id);
        fflush(stderr); return false;
      }
      return true;
    case evDestroy:
      free(st); win->userdata = NULL; return true;
    case evPaint: return false;
    case evMeasure: {
      layout_measure_t *m = lparam;
      if (m) { m->desired_w = 400; m->desired_h = 300; }
      return true;
    }
    case evArrange:
      if (lparam) win->frame = ((layout_arrange_t *)lparam)->rect;
      if (st && st->drag_page) pane_cancel(win);
      pane_arrange(win); return true;
    case evResize:
      if (st && st->drag_page) pane_cancel(win);
      pane_arrange(win); return true;
    case evDescendantFocus:
      for (window_t *p = lparam; st && p && p != win; p = p->parent)
        if (pane_member(win, p)) { st->active = p; break; }
      return true;
    case evDescendantDetach: case evDescendantDestroy:
      if (!st) return true;
      if (pane_contains(lparam, st->drag_page) || pane_contains(lparam, st->drop.pane)) pane_cancel(win);
      if (pane_contains(lparam, st->active)) st->active = NULL;
      if (st->overlay == lparam) st->overlay = NULL;
      return true;
    case evCommand:
      if (st && HIWORD(wparam) == tcnBeginDrag && lparam) {
        tab_drag_t *drag = lparam;
        if (!drag->page || !pane_member(win, drag->page->parent)) return false;
        st->drag_page = drag->page;
        set_capture(win);
        pane_preview(win, window_map_point(drag->page->parent, win, drag->point));
        return true;
      }
      return false;
    case evMouseMove:
      if (!st || !st->drag_page) return false;
      pane_preview(win, (ipoint16_t){(int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)});
      return true;
    case evLeftButtonUp: {
      if (!st || !st->drag_page) return false;
      pane_preview(win, (ipoint16_t){(int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)});
      pane_drop_t drop = st->drop; window_t *page = st->drag_page;
      pane_cancel(win);
      if (drop.pane) pane_host_move_tab(win, page, drop.pane, drop.edge, drop.index);
      return true;
    }
    case evPointerCancel: pane_cancel(win); return true;
    case evGetCursor: return st && st->drag_page && !st->drop.pane ? curNotAllowed : curArrow;
    default: return false;
  }
}

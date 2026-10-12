// A pane tree uses ordinary SplitViews and TabViews; moving a tab keeps its window alive.
#include "page.h"
#include <orion/commctl/commctl.h>
#include <orion/user/draw.h>
#include <stdio.h>

#define PANE_MIN_WIDTH 96
#define PANE_MIN_HEIGHT 64

typedef struct page_docking_s {
  window_t *active, *drag_page;
  pane_drop_t drop;
} page_docking_t;

static bool pane_contains(window_t *owner, window_t *child) {
  for (; child; child = child->parent) if (child == owner) return true;
  return false;
}

static page_docking_t *pane_state(window_t *owner) {
  return owner && is_window(owner) && owner->role == WINDOW_ROLE_PAGE && owner->pages ? owner->pages->docking : NULL;
}

static bool pane_class(window_t *win, const char *name) {
  return win && win->proc == find_window_class_proc(name);
}

static window_t *pane_child(window_t *split, int index) {
  return (window_t *)send_message(split, svGetPane, index, NULL);
}

static window_t *pane_tree(window_t *owner) {
  for (window_t *c = owner ? owner->children : NULL; c; c = c->next)
    if (pane_class(c, "TabView") || pane_class(c, "SplitView")) return c;
  return NULL;
}

static window_t *pane_at(window_t *tree, int *index) {
  if (!tree) return NULL;
  if (pane_class(tree, "TabView")) return (*index)-- == 0 ? tree : NULL;
  if (!pane_class(tree, "SplitView")) return NULL;
  window_t *pane = pane_at(pane_child(tree, 0), index);
  return pane ? pane : pane_at(pane_child(tree, 1), index);
}

int page_pane_count(window_t *owner) {
  int index = -1;
  if (pane_state(owner)) pane_at(pane_tree(owner), &index);
  return -index - 1;
}

window_t *page_pane_at(window_t *owner, int index) {
  return pane_state(owner) && index >= 0 ? pane_at(pane_tree(owner), &index) : NULL;
}

static bool pane_member(window_t *owner, window_t *pane) {
  if (!pane_state(owner) || !pane || !is_window(pane) || !pane_class(pane, "TabView")) return false;
  window_t *tree = pane;
  while (tree->parent && tree->parent != owner && pane_class(tree->parent, "SplitView")) tree = tree->parent;
  return tree->parent == owner && tree == pane_tree(owner);
}

static bool pane_visible(window_t *owner, window_t *pane) {
  for (window_t *p = pane; p; p = p->parent) {
    if (!window_has_state(p, WINDOW_STATE_VISIBLE)) return false;
    if (p == owner) return true;
  }
  return false;
}

window_t *page_active_pane(window_t *owner) {
  page_docking_t *st = pane_state(owner);
  if (!st) return NULL;
  if (!pane_member(owner, st->active) || !pane_visible(owner, st->active)) {
    st->active = NULL;
    for (int i = 0, n = page_pane_count(owner); i < n; i++) {
      window_t *pane = page_pane_at(owner, i);
      if (pane_visible(owner, pane)) { st->active = pane; break; }
    }
  }
  return st->active;
}

static void pane_configure(window_t *tree) {
  if (!tree) return;
  if (pane_class(tree, "TabView")) {
    uint32_t style = send_message(tree, tcGetStyle, 0, NULL);
    uint32_t required = TAB_STYLE_RESTORE_FOCUS | TAB_STYLE_DRAG_TABS;
    if ((style & required) != required) send_message(tree, tcSetStyle, style | required, NULL);
  } else if (pane_class(tree, "SplitView")) {
    pane_configure(pane_child(tree, 0)); pane_configure(pane_child(tree, 1));
  }
}

static void pane_arrange(window_t *owner) {
  window_t *tree = pane_tree(owner);
  if (!tree) return;
  pane_configure(tree);
  irect16_t area = get_client_rect(owner);
  layout_arrange_window(owner, &area);
  invalidate_window(owner);
}

static void pane_cancel(window_t *owner) {
  page_docking_t *st = pane_state(owner);
  if (!st) return;
  st->drag_page = NULL; st->drop = (pane_drop_t){0};
  if (g_ui_runtime.captured == owner) set_capture(NULL);
  invalidate_window(owner);
}

static bool pane_can_split(window_t *pane, pane_edge_t edge) {
  irect16_t r = get_client_rect(pane);
  if (edge == PANE_LEFT || edge == PANE_RIGHT) return r.w >= 2 * PANE_MIN_WIDTH + 6;
  if (edge == PANE_TOP || edge == PANE_BOTTOM) return r.h >= 2 * PANE_MIN_HEIGHT + 6;
  return false;
}

window_t *page_split_pane(window_t *owner, window_t *pane, pane_edge_t edge) {
  if (!pane_member(owner, pane) || !pane_can_split(pane, edge)) {
    fprintf(stderr, "[page] split rejected page=%u pane=%p edge=%d (invalid pane or insufficient space)\n",
            owner && is_window(owner) ? owner->id : 0, (void *)pane, edge);
    fflush(stderr); return NULL;
  }
  window_t *parent = pane->parent;
  bool leading = edge == PANE_LEFT || edge == PANE_TOP;
  int orientation = edge == PANE_LEFT || edge == PANE_RIGHT ? SPLIT_VERT : SPLIT_HORZ;
  window_t *split = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOACTIVATE | WINDOW_FLEXSPACE,
                                  &pane->frame, parent, "SplitView", owner->hinstance, (void *)(intptr_t)orientation);
  window_t *tabs = split ? create_window("", WINDOW_NOTITLE | WINDOW_NOACTIVATE | WINDOW_FLEXSPACE,
                                        MAKERECT(0, 0, 1, 1), split, "TabView", owner->hinstance, NULL) : NULL;
  if (!tabs) {
    fprintf(stderr, "[page] split allocation failed page=%u\n", owner->id);
    fflush(stderr); if (split) destroy_window(split); return NULL;
  }
  if (parent == owner) split->layout = pane->layout;
  // Place the new branch in the old leaf's slot before moving the leaf inside it.
  window_reparent(split, parent, pane);
  window_reparent(pane, split, leading ? NULL : tabs);
  pane_arrange(owner);
  return tabs;
}

static void pane_collapse_empty(window_t *owner, window_t *pane) {
  if (!pane_member(owner, pane) || send_message(pane, tcGetCount, 0, NULL)) return;
  window_t *split = pane->parent;
  if (split == owner || !pane_class(split, "SplitView")) return;
  window_t *sibling = pane_child(split, 0);
  if (sibling == pane) sibling = pane_child(split, 1);
  if (!sibling) return;
  if (split->parent == owner) sibling->layout = split->layout;
  window_reparent(sibling, split->parent, split);
  destroy_window(split);
  pane_arrange(owner);
}

static int pane_page_index(window_t *tabs, window_t *page) {
  int count = send_message(tabs, tcGetCount, 0, NULL);
  for (int i = 0; i < count; i++) if ((window_t *)send_message(tabs, tcGetPage, i, NULL) == page) return i;
  return -1;
}

bool page_move_tab(window_t *owner, window_t *page, window_t *target, pane_edge_t edge, int before) {
  window_t *source = page && is_window(page) ? page->parent : NULL;
  int index = pane_member(owner, source) ? pane_page_index(source, page) : -1;
  if (index < 0 || !pane_member(owner, target) || edge < PANE_CENTER || edge > PANE_BOTTOM ||
      (edge != PANE_CENTER && source == target && send_message(source, tcGetCount, 0, NULL) == 1)) {
    fprintf(stderr, "[page] move rejected page=%u view=%p target=%p edge=%d\n",
            owner && is_window(owner) ? owner->id : 0, (void *)page, (void *)target, edge);
    fflush(stderr); return false;
  }
  window_t *destination = target;
  if (edge != PANE_CENTER) destination = page_split_pane(owner, target, edge);
  if (!destination) return false;
  tab_move_t move = {destination, edge == PANE_CENTER ? before : -1};
  if (!send_message(source, tcMovePage, index, &move)) {
    if (destination != target) pane_collapse_empty(owner, destination);
    return false;
  }
  pane_state(owner)->active = destination;
  pane_collapse_empty(owner, source);
  pane_arrange(owner);
  return true;
}

bool page_close_tab(window_t *owner, window_t *page) {
  window_t *pane = page && is_window(page) ? page->parent : NULL;
  int index = pane_member(owner, pane) ? pane_page_index(pane, page) : -1;
  if (index < 0) {
    fprintf(stderr, "[page] close rejected page=%u view=%p\n", owner && is_window(owner) ? owner->id : 0, (void *)page);
    fflush(stderr); return false;
  }
  if (!send_message(pane, tcDeletePage, index, NULL)) return false;
  pane_collapse_empty(owner, pane);
  window_t *active = page_active_pane(owner);
  if (active && send_message(active, tcGetCount, 0, NULL))
    send_message(active, tcSetSelection, send_message(active, tcGetSelection, 0, NULL), NULL);
  return true;
}

static void pane_preview(window_t *owner, ipoint16_t point) {
  page_docking_t *st = pane_state(owner);
  pane_drop_t drop = {0};
  if (!st || !st->drag_page) return;
  for (int i = 0, count = page_pane_count(owner); i < count; i++) {
    window_t *pane = page_pane_at(owner, i);
    if (!pane_visible(owner, pane)) continue;
    ipoint16_t pt = window_map_point(owner, pane, point);
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
    ipoint16_t origin = window_map_point(pane, owner, (ipoint16_t){r.x, r.y});
    drop.rect = R(origin.x, origin.y, r.w, r.h);
    break;
  }
  st->drop = drop;
  invalidate_window(owner);
}

bool page_get_drop_preview(window_t *owner, pane_drop_t *preview) {
  page_docking_t *st = pane_state(owner);
  if (!st || !st->drag_page || !st->drop.pane || !preview) return false;
  *preview = st->drop; return true;
}

bool page_handle_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam, result_t *result) {
  if (win->role != WINDOW_ROLE_PAGE) return false;
  page_docking_t *st = pane_state(win);
  *result = true;
  switch (msg) {
    case evCreate:
      if (!win->pages) win->pages = calloc(1, sizeof(*win->pages));
      if (win->pages && !st) st = win->pages->docking = calloc(1, sizeof(*st));
      if (!st) {
        fprintf(stderr, "[page] docking state allocation failed page=%u\n", win->id);
        fflush(stderr); return false;
      }
      pane_arrange(win);
      return false;
    case evDestroy:
      if (st) { pane_cancel(win); free(st); win->pages->docking = NULL; }
      return false;
    case evResize:
      if (st && st->drag_page) pane_cancel(win);
      pane_arrange(win); return false;
    case evArrange: case evDeactivate:
      if (st && st->drag_page) pane_cancel(win);
      pane_configure(pane_tree(win));
      return false;
    case evDescendantFocus:
      for (window_t *p = lparam; st && p && p != win; p = p->parent)
        if (pane_member(win, p)) { st->active = p; break; }
      return false;
    case evDescendantDetach: case evDescendantDestroy:
      if (!st) return false;
      if (pane_contains(lparam, st->drag_page) || pane_contains(lparam, st->drop.pane)) pane_cancel(win);
      if (pane_contains(lparam, st->active)) st->active = NULL;
      return false;
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
      if (drop.pane) page_move_tab(win, page, drop.pane, drop.edge, drop.index);
      return true;
    }
    case evPointerCancel:
      if (!st || !st->drag_page) return false;
      pane_cancel(win); return true;
    case evGetCursor:
      if (!st || !st->drag_page) return false;
      *result = st->drop.pane ? curArrow : curNotAllowed; return true;
    default: return false;
  }
}

void page_paint_overlay(window_t *page, irect16_t clip) {
  page_docking_t *st = pane_state(page);
  if (!st || !st->drag_page || !st->drop.pane) return;
  window_t *root = get_root_window(page);
  int x = window_screen_x(page) - root->frame.x;
  int y = window_screen_y(page) + titlebar_height(page) - root->frame.y;
  set_viewport_for_fbo(root);
  set_projection(-x, -y, root->frame.w - x, root->frame.h - y);
  set_scissor_fbo(root, clip);
  fill_rect(color_with_alpha(get_sys_color(brTextInfo), 35), st->drop.rect);
  stroke_rounded_rect(color_with_alpha(get_sys_color(brTextInfo), 128), st->drop.rect, 0, 2);
}

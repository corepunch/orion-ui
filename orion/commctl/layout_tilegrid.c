// TileGrid: an adaptive grid of equal-width tiles (NSCollectionView grid layout / SwiftUI's
// LazyVGrid with an adaptive GridItem).
//
// Every child is a tile. The grid picks as many columns as fit the minimum tile width, stretches the
// tiles evenly to fill the row, measures each tile at that width, and gives every tile in a row the
// height of the tallest, so tiles that reflow their own content (Cards) stay aligned. The grid scrolls
// vertically, owns the selection and keyboard navigation, and reports to its root:
//   evCommand, HIWORD = tgnSelChange | tgnActivate, LOWORD = tile index, lparam = grid.

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/theme.h>
#include "commctl.h"
#include "layout_shared.h"

#define TILEGRID_DEFAULT_MIN_W 232
#define TILEGRID_DEFAULT_GAP   6
#define TILEGRID_MEASURE_H     4000

typedef struct {
  int  min_w;
  int  selected;
  int  content_w, content_h;   // last extent published to the scrollbars
  int  page_h;                 // client height it was published for
} tilegrid_state_t;

static int tilegrid_index_of(window_t *grid, const window_t *child) {
  int i = 0;
  for (window_t *c = grid->children; c; c = c->next, i++) if (c == child) return i;
  return -1;
}

static void tilegrid_notify(window_t *grid, int index, uint16_t code) {
  window_t *root = get_root_window(grid);
  if (root) send_message(root, evCommand, MAKEDWORD((uint16_t)index, code), grid);
}

// Lays the tiles out for a client width; with apply=false only computes the content height.
static int tilegrid_layout(window_t *win, int client_w, bool apply) {
  irect16_t pad = layout_padding_for(win);
  int gap = layout_spacing_for(win), count = layout_child_count(win);
  tilegrid_state_t *g = (tilegrid_state_t *)win->userdata;
  int content_w = MAX(1, client_w - pad.x - pad.w);
  int cols = MAX(1, (content_w + gap) / (g->min_w + gap));
  int tile_w = MAX(1, (content_w - (cols - 1) * gap) / cols);
  int y = pad.y;
  for (int first = 0; first < count; first += cols) {
    int row_h = 1;
    for (int i = first; i < MIN(count, first + cols); i++)
      row_h = MAX(row_h, layout_measure_child(layout_child_at(win, i), tile_w, TILEGRID_MEASURE_H).desired_h);
    if (apply)
      for (int i = first; i < MIN(count, first + cols); i++)
        layout_arrange_child(layout_child_at(win, i),
                             R(pad.x + (i - first) * (tile_w + gap), y - (int)win->vscroll.pos, tile_w, row_h));
    y += row_h + gap;
  }
  return (count ? y - gap : y) + pad.h;
}

static void tilegrid_relayout(window_t *win) {
  tilegrid_state_t *g = (tilegrid_state_t *)win->userdata;
  if (!g) return;
  irect16_t cr = get_client_rect(win);
  // Keep the scroll position inside the content (content may have shrunk, or the grid may not be sized yet).
  int max_pos = MAX(0, tilegrid_layout(win, cr.w, false) - cr.h);
  if ((int)win->vscroll.pos > max_pos) win->vscroll.pos = (uint32_t)max_pos;
  int height = tilegrid_layout(win, cr.w, true);
  // Publishing the extent may add or remove the scrollbar gutter, which changes the client width and
  // re-enters through evResize; the size check makes that converge.
  int w = cr.w;
  if (height != g->content_h || w != g->content_w || cr.h != g->page_h) {
    g->content_h = height;
    g->content_w = w;
    g->page_h = cr.h;
    set_scroll_content(win, w, height, 0, (int)win->vscroll.pos);
  }
  invalidate_window(win);
}

static void tilegrid_sync_states(window_t *win) {
  tilegrid_state_t *g = (tilegrid_state_t *)win->userdata;
  int i = 0;
  for (window_t *c = win->children; c; c = c->next, i++) {
    uint32_t state = i == g->selected ? CTRL_SELECTED | (window_has_focus(win) ? CTRL_FOCUSED : 0) : 0;
    send_message(c, cdSetState, state, NULL);
  }
}

static void tilegrid_scroll_to(window_t *win, window_t *tile) {
  irect16_t cr = get_client_rect(win), pad = layout_padding_for(win);
  if (cr.h <= 0) return;   // not sized yet
  int pos = (int)win->vscroll.pos;
  if (tile->frame.y < pad.y) pos += tile->frame.y - pad.y;
  else if (tile->frame.y + tile->frame.h > cr.h - pad.h) pos += tile->frame.y + tile->frame.h - (cr.h - pad.h);
  win->vscroll.pos = (uint32_t)MAX(0, pos);
  tilegrid_relayout(win);
}

static void tilegrid_select(window_t *win, int index, bool announce) {
  tilegrid_state_t *g = (tilegrid_state_t *)win->userdata;
  int count = layout_child_count(win);
  if (index < 0 || index >= count) {
    fprintf(stderr, "[tg] win=%u select index=%d out of range (count=%d)\n", (unsigned)win->id, index, count);
    fflush(stderr);
    return;
  }
  bool changed = index != g->selected;
  g->selected = index;
  tilegrid_sync_states(win);
  tilegrid_scroll_to(win, layout_child_at(win, index));
  if (announce && changed) tilegrid_notify(win, index, tgnSelChange);
}

static void tilegrid_move(window_t *win, int dx, int dy) {
  tilegrid_state_t *g = (tilegrid_state_t *)win->userdata;
  int count = layout_child_count(win);
  if (!count) return;
  if (g->selected < 0) { tilegrid_select(win, 0, true); return; }
  window_t *cur = layout_child_at(win, g->selected);
  int best = -1, best_score = INT_MAX;
  for (int i = 0; i < count; i++) {
    if (i == g->selected) continue;
    window_t *c = layout_child_at(win, i);
    int score;
    if (dx) { if ((dx > 0) != (i > g->selected)) continue; score = abs(i - g->selected); }
    else { int dist = (c->frame.y - cur->frame.y) * dy; if (dist <= 0) continue; score = dist * 4 + abs(c->frame.x - cur->frame.x); }
    if (score < best_score) { best_score = score; best = i; }
  }
  if (best >= 0) tilegrid_select(win, best, true);
}

result_t win_tilegrid(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  tilegrid_state_t *g = (tilegrid_state_t *)win->userdata;
  switch (msg) {
    case evCreate: {
      g = calloc(1, sizeof(*g));
      if (!g) {
        fprintf(stderr, "[tg] win=%u state allocation failed\n", (unsigned)win->id);
        fflush(stderr);
        return false;
      }
      g->min_w = TILEGRID_DEFAULT_MIN_W;
      g->selected = -1;
      win->userdata = g;
      win->flags |= WINDOW_AUTO_LAYOUT | WINDOW_LAYOUT_CONTAINER | WINDOW_VSCROLL;
      win->vscroll.visible_mode = SB_VIS_AUTO;
      win->layout.layout_spacing = TILEGRID_DEFAULT_GAP;
      win->layout.layout_padding = (irect16_t){10, 6, 10, 10};
      if (lparam && (uintptr_t)lparam > 0x1000000) {
        const form_ctrl_def_t *cd = (const form_ctrl_def_t *)lparam;
        if (cd->layout_spacing > 0) win->layout.layout_spacing = cd->layout_spacing;
        if (cd->padding.x || cd->padding.y || cd->padding.w || cd->padding.h) win->layout.layout_padding = cd->padding;
      }
      return true;
    }
    case evDestroy:
      free(g);
      win->userdata = NULL;
      return false;

    case tgSetMinTileWidth:
      if (!g || wparam < 1) {
        fprintf(stderr, "[tg] win=%u tgSetMinTileWidth rejected value=%u\n", (unsigned)win->id, (unsigned)wparam);
        fflush(stderr);
        return false;
      }
      g->min_w = (int)wparam;
      tilegrid_relayout(win);
      return true;
    case tgGetSelection: return g ? (result_t)g->selected : (result_t)-1;
    case tgSetSelection:
      if (g && (int)wparam < layout_child_count(win)) {
        g->selected = (int)wparam;
        tilegrid_sync_states(win);
        tilegrid_scroll_to(win, layout_child_at(win, (int)wparam));
      }
      return true;
    case tgClear:
      while (win->children) destroy_window(win->children);
      if (g) { g->selected = -1; g->content_h = g->content_w = g->page_h = 0; }
      win->vscroll.pos = 0;
      tilegrid_relayout(win);
      return true;

    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (!m || !g) return true;
      int w = m->avail_w > 0 ? m->avail_w : get_client_rect(win).w;
      m->desired_w = w;
      m->desired_h = tilegrid_layout(win, w, false);
      return true;
    }
    case evArrange: {
      layout_arrange_t *a = (layout_arrange_t *)lparam;
      if (a) { win->frame = a->rect; tilegrid_relayout(win); }
      return true;
    }
    case evResize:
      tilegrid_relayout(win);
      return true;
    case evVScroll:
      win->vscroll.pos = wparam;
      tilegrid_relayout(win);
      return true;
    case evPaint:
      fill_rect(get_sys_color(brPanelDark), get_client_rect(win));
      layout_paint_children(win);
      return true;
    case evParentNotify:
      return false;

    case evSetFocus:
    case evKillFocus:
      if (g) tilegrid_sync_states(win);
      return false;
    case evLeftButtonDown:
      set_focus(win);
      return true;
    case evCommand: {
      window_t *tile = (window_t *)lparam;
      uint16_t code = HIWORD(wparam);
      int index = tile && tile->parent == win ? tilegrid_index_of(win, tile) : -1;
      if (index < 0 || (code != cdnClicked && code != cdnActivated)) return false;
      set_focus(win);
      tilegrid_select(win, index, true);
      // A click opens the tile, including a second click on the tile already selected.
      if (code == cdnClicked || code == cdnActivated) tilegrid_notify(win, index, tgnActivate);
      return true;
    }
    case evKeyDown:
      if (!g) return false;
      switch (wparam) {
        case AX_KEY_LEFTARROW:  tilegrid_move(win, -1, 0); return true;
        case AX_KEY_RIGHTARROW: tilegrid_move(win,  1, 0); return true;
        case AX_KEY_UPARROW:    tilegrid_move(win, 0, -1); return true;
        case AX_KEY_DOWNARROW:  tilegrid_move(win, 0,  1); return true;
        case AX_KEY_ENTER:      if (g->selected >= 0) tilegrid_notify(win, g->selected, tgnActivate); return true;
        default: return false;
      }
    default:
      return false;
  }
}

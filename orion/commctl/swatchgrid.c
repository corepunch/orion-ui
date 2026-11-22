// SwatchGrid — a palette as a grid of colour swatches, one of them selected.
//
// The grid fills its height with rows and takes as many columns as its colours need,
// up to the sheet's --max-columns; further colours are on later pages, reached from the
// pager at its trailing edge or with the wheel.
//
// Messages:
//   swSetColors(wparam=count, lparam=const uint32_t*) — the palette (copied).
//   swSetSel(wparam=index, -1 for none), swGetSel — the selected swatch; selecting shows its page.
// Notifies its parent with evCommand(MAKEDWORD(id, swnSelChange), self) when the user picks one.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/theme.h>
#include <orion/user/ui_style.h>
#include "commctl_internal.h"

typedef enum { SW_NONE, SW_SWATCH, SW_PREV, SW_NEXT } sw_target_t;
typedef struct { sw_target_t kind; int index; } sw_hit_t;
typedef struct {
  uint32_t *colors;
  int       count, selected, page;
  sw_hit_t  hot;
} swatchgrid_state_t;

typedef struct { int rows, columns, pages, cell_w, cell_h, gap, pager_w; } sw_grid_t;

// The grid for a control `height` tall and at most `width` wide (0 = as wide as the palette needs).
static sw_grid_t sw_grid_in(const window_t *win, const swatchgrid_state_t *st, int height, int width) {
  const char *swatch = ui_selector(win, "SwatchGrid::swatch");
  sw_grid_t g = {.cell_w = MAX(1, ui_px(swatch, "width")), .cell_h = MAX(1, ui_px(swatch, "height")),
                 .gap = ui_px(ui_selector(win, "SwatchGrid"), "gap")};
  g.rows = MAX(1, (height + g.gap) / (g.cell_h + g.gap));
  int limit = MAX(1, ui_px(ui_selector(win, "SwatchGrid"), "--max-columns"));
  int pager = ui_px(ui_selector(win, "SwatchGrid::pager"), "width") + g.gap;
  g.columns = CLAMP((st->count + g.rows - 1) / g.rows, 1, limit);
  if (width > 0 && g.columns * (g.cell_w + g.gap) - g.gap > width)   // too wide: page, and leave room for the pager
    g.columns = MAX(1, MIN(g.columns - 1, (width - pager + g.gap) / (g.cell_w + g.gap)));
  g.pages = MAX(1, (st->count + g.rows * g.columns - 1) / (g.rows * g.columns));
  g.pager_w = g.pages > 1 ? pager : 0;
  return g;
}

static sw_grid_t sw_grid(const window_t *win, const swatchgrid_state_t *st, int height) {
  (void)height;
  return sw_grid_in(win, st, win->frame.h, win->frame.w);
}

// The grid is centred in the control's height; row-major, so a palette reads left to right.
static irect16_t sw_cell_rect(const window_t *win, const sw_grid_t *g, int slot) {
  int grid_h = g->rows * (g->cell_h + g->gap) - g->gap;
  int y = (win->frame.h - grid_h) / 2;
  return R((slot % g->columns) * (g->cell_w + g->gap), y + (slot / g->columns) * (g->cell_h + g->gap), g->cell_w, g->cell_h);
}

static irect16_t sw_pager_rect(const window_t *win, const sw_grid_t *g, bool next) {
  int x = g->columns * (g->cell_w + g->gap), grid_h = g->rows * (g->cell_h + g->gap) - g->gap;
  irect16_t strip = R(x, (win->frame.h - grid_h) / 2, g->pager_w - g->gap, grid_h);
  return next ? rect_trim_top(strip, strip.h / 2) : rect_split_top(strip, strip.h / 2);
}

static sw_hit_t sw_hit(const window_t *win, const swatchgrid_state_t *st, ipoint16_t p) {
  sw_grid_t g = sw_grid(win, st, win->frame.h);
  int per_page = g.rows * g.columns;
  for (int slot = 0; slot < per_page && st->page * per_page + slot < st->count; slot++)
    if (rect_contains_point(sw_cell_rect(win, &g, slot), p)) return (sw_hit_t){SW_SWATCH, st->page * per_page + slot};
  if (g.pages > 1 && rect_contains_point(sw_pager_rect(win, &g, false), p)) return (sw_hit_t){SW_PREV, 0};
  if (g.pages > 1 && rect_contains_point(sw_pager_rect(win, &g, true), p)) return (sw_hit_t){SW_NEXT, 0};
  return (sw_hit_t){SW_NONE, 0};
}

static void sw_set_page(window_t *win, swatchgrid_state_t *st, int page) {
  sw_grid_t g = sw_grid(win, st, win->frame.h);
  page = CLAMP(page, 0, g.pages - 1);
  if (page == st->page) return;
  st->page = page;
  invalidate_window(win);
}

static void sw_paint(window_t *win, swatchgrid_state_t *st) {
  const char *swatch = ui_selector(win, "SwatchGrid::swatch");
  sw_grid_t g = sw_grid(win, st, win->frame.h);
  int per_page = g.rows * g.columns, radius = ui_px(swatch, "border-radius");
  bool disabled = window_has_state(win, WINDOW_STATE_DISABLED);
  for (int slot = 0; slot < per_page && st->page * per_page + slot < st->count; slot++) {
    int index = st->page * per_page + slot;
    irect16_t r = sw_cell_rect(win, &g, slot);
    bool selected = index == st->selected && !disabled;
    ctrl_state_t state = disabled ? CTRL_DISABLED : selected ? CTRL_SELECTED : CTRL_NORMAL;
    if (!disabled && st->hot.kind == SW_SWATCH && st->hot.index == index) state |= CTRL_HOVER;
    // The selected swatch stands clear of its ring by --checked-inset.
    irect16_t ink = selected ? rect_inset(r, ui_px(swatch, "--checked-inset")) : r;
    fill_rounded_rect(disabled ? color_with_alpha(st->colors[index], 96) : st->colors[index], ink, radius);
    theme_draw_in(win, THEME_PART_SWATCH, r, state);
  }
  for (int next = 0; g.pages > 1 && next < 2; next++) {
    bool off = disabled || (next ? st->page >= g.pages - 1 : st->page <= 0);
    ctrl_state_t state = off ? CTRL_DISABLED : st->hot.kind == (next ? SW_NEXT : SW_PREV) ? CTRL_HOVER : CTRL_NORMAL;
    irect16_t r = sw_pager_rect(win, &g, next);
    theme_draw_in(win, THEME_PART_SWATCH_PAGER, r, state);
    draw_theme_icon_in_rect(next ? THEME_ICON_SCROLL_DOWN : THEME_ICON_SCROLL_UP, r, theme_foreground_in(win, THEME_PART_SWATCH_PAGER, state));
  }
}

result_t win_swatchgrid(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  swatchgrid_state_t *st = win->userdata;
  ipoint16_t point = {(int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)};
  if (!st && msg != evCreate) return false;
  switch (msg) {
    case evCreate:
      if (!(st = allocate_window_data(win, sizeof(*st)))) {
        fprintf(stderr, "[swatchgrid] allocation failed win=%u\n", win->id);
        fflush(stderr);
        return false;
      }
      st->selected = -1;
      win->flags |= WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOTABSTOP;
      return true;
    case evDestroy:
      free(st->colors); free(st);
      win->userdata = NULL;
      return true;
    case evPaint:
      sw_paint(win, st);
      return true;
    case evMeasure: {
      layout_measure_t *m = lparam;
      if (!m) return false;
      sw_grid_t g = sw_grid_in(win, st, m->avail_h > 0 ? m->avail_h : win->frame.h, m->avail_w);
      m->desired_w = g.columns * (g.cell_w + g.gap) - g.gap + g.pager_w;
      m->desired_h = g.rows * (g.cell_h + g.gap) - g.gap;
      return true;
    }
    case evMouseMove: {
      sw_hit_t hot = sw_hit(win, st, point);
      track_mouse(win);
      if (hot.kind != st->hot.kind || hot.index != st->hot.index) { st->hot = hot; invalidate_window(win); }
      return true;
    }
    case evMouseLeave:
      if (st->hot.kind != SW_NONE) { st->hot = (sw_hit_t){SW_NONE, 0}; invalidate_window(win); }
      return false;
    case evLeftButtonDown: {
      sw_hit_t hit = sw_hit(win, st, point);
      if (window_has_state(win, WINDOW_STATE_DISABLED)) return true;
      if (hit.kind == SW_PREV || hit.kind == SW_NEXT) sw_set_page(win, st, st->page + (hit.kind == SW_NEXT ? 1 : -1));
      if (hit.kind != SW_SWATCH) return true;
      st->selected = hit.index;
      invalidate_window(win);
      if (win->parent) send_message(win->parent, evCommand, MAKEDWORD(win->id, swnSelChange), win);
      return true;
    }
    case evWheel:
      if (window_has_state(win, WINDOW_STATE_DISABLED)) return false;
      sw_set_page(win, st, st->page + ((int16_t)HIWORD((uint32_t)(uintptr_t)lparam) < 0 ? 1 : -1));   // lparam = MAKEDWORD(dx, dy)
      return true;
    case evGetTooltipRect: {
      sw_hit_t hit = sw_hit(win, st, point);
      if (hit.kind != SW_SWATCH || !lparam) return false;
      sw_grid_t g = sw_grid(win, st, win->frame.h);
      *(irect16_t *)lparam = sw_cell_rect(win, &g, hit.index - st->page * g.rows * g.columns);
      return true;
    }
    case evGetTooltipText: {
      sw_hit_t hit = sw_hit(win, st, point);
      if (hit.kind != SW_SWATCH || !lparam) return false;
      uint32_t c = st->colors[hit.index];
      snprintf(lparam, 256, "#%02X%02X%02X", c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);   // a colour's own notation: data, not text
      return true;
    }
    case swSetColors: {
      int count = (int)wparam;
      uint32_t *colors = count > 0 && lparam ? malloc((size_t)count * sizeof(*colors)) : NULL;
      if (count < 0 || (count > 0 && !colors)) {
        fprintf(stderr, "[swatchgrid] colours rejected win=%u count=%d colors=%p\n", win->id, count, lparam);
        fflush(stderr);
        return false;
      }
      if (colors) memcpy(colors, lparam, (size_t)count * sizeof(*colors));
      free(st->colors);
      st->colors = colors; st->count = colors ? count : 0;
      if (st->selected >= st->count) st->selected = -1;
      st->page = 0; st->hot = (sw_hit_t){SW_NONE, 0};
      invalidate_window(win);
      return true;
    }
    case swGetSel: return st->selected;
    case swSetSel: {
      int index = (int)wparam;
      if (index < -1 || index >= st->count) {
        fprintf(stderr, "[swatchgrid] selection rejected win=%u index=%d count=%d\n", win->id, index, st->count);
        fflush(stderr);
        return false;
      }
      st->selected = index;
      if (index >= 0) {
        sw_grid_t g = sw_grid(win, st, win->frame.h);
        st->page = index / (g.rows * g.columns);
      }
      invalidate_window(win);
      return true;
    }
    default:
      return false;
  }
}

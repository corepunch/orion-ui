#include <stdlib.h>
#include <string.h>

#include "columnview_internal.h"
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/theme.h>

// RVM_VIEW_LARGE_ICON: fixed-width cells with a label under each icon, spread across the row.
// RVM_VIEW_THUMBNAIL: a photo grid. As many columns as fit icon_size (the minimum tile), stretched
// to fill the width with the theme's thumbnail.gap between square tiles and no labels.
typedef struct {
  int ncol;
  int cell_w;    // thumbnail: nominal tile side; columns absorb the remainder
  int cell_h;    // row pitch
  int usable_w;
  int origin_x;
  int gap;
  bool thumbnail;
} icon_grid_geom_t;

static icon_grid_geom_t grid_geom_for(reportview_data_t *data, int origin_x, int width) {
  int usable = MAX(1, width);
  if (data->view_mode == RVM_VIEW_THUMBNAIL) {
    int gap = get_theme()->thumbnail.gap;
    int ncol = data->fixed_large_icon_cols > 0 ? data->fixed_large_icon_cols
                                               : MAX(1, (usable + gap) / (data->icon_size + gap));
    int tile = MAX(1, (usable - (ncol - 1) * gap) / ncol);
    return (icon_grid_geom_t){ncol, tile, tile + gap, usable, origin_x, gap, true};
  }
  int cell_w = data->column_width > 0 ? data->column_width : DEFAULT_COLUMN_WIDTH;
  int ncol = data->fixed_large_icon_cols > 0 ? data->fixed_large_icon_cols : MAX(1, usable / cell_w);
  return (icon_grid_geom_t){ncol, cell_w, rv_large_icon_cell_h(data), usable, origin_x, 0, false};
}

static icon_grid_geom_t grid_geom(window_t *win, reportview_data_t *data) {
  irect16_t content = window_scroll_content_rect(win);
  return grid_geom_for(data, content.x, content.w);
}

static int grid_content_h(const icon_grid_geom_t *g, int count) {
  return count ? (count + g->ncol - 1) / g->ncol * g->cell_h - g->gap : 0;
}

// Thumbnail columns split usable_w + gap evenly, so the row ends flush with the right edge.
static int grid_thumb_x(const icon_grid_geom_t *g, int col) {
  return g->origin_x + col * (g->usable_w + g->gap) / g->ncol;
}

static int grid_col_x(const icon_grid_geom_t *g, int col) {
  if (g->thumbnail) return grid_thumb_x(g, col);
  if (col <= 0 || g->ncol <= 1)
    return g->origin_x + (g->ncol == 1 ? MAX(0, (g->usable_w - g->cell_w) / 2) : 0);
  int min_span = (g->ncol - 1) * g->cell_w;
  int fill_span = g->usable_w - g->cell_w;
  int span = MAX(min_span, fill_span);
  return g->origin_x
       + (span * col + (g->ncol - 1) / 2) / (g->ncol - 1);
}

static void grid_sync_scroll(window_t *win) {
  window_set_scroll_content(win, 0, 0, win->scroll_content.padding, (irect16_t){0});
}

static irect16_t grid_item_rect(window_t *win, reportview_data_t *data, int index) {
  icon_grid_geom_t g = grid_geom(win, data);
  irect16_t content = window_scroll_content_rect(win);
  int col = index % g.ncol, x = grid_col_x(&g, col), y = content.y + index / g.ncol * g.cell_h;
  if (g.thumbnail) return R(x, y, grid_thumb_x(&g, col + 1) - g.gap - x, g.cell_w);
  return R(x, y, g.cell_w, g.cell_h);
}

static int grid_hit_index(window_t *win, reportview_data_t *data, uint32_t wparam) {
  int mx = (int)(int16_t)LOWORD(wparam);
  int my = (int)(int16_t)HIWORD(wparam);
  icon_grid_geom_t g = grid_geom(win, data);
  int local_y = my - window_scroll_content_rect(win).y;
  if (local_y < 0)
    return RV_INVALID_SELECTION;
  int row = local_y / g.cell_h;
  for (int col = 0; col < g.ncol; col++) {
    int index = row * g.ncol + col;
    if (rv_valid_index(data, index) && rect_contains_point(grid_item_rect(win, data, index), (ipoint16_t){mx, my}))
      return index;
  }
  return RV_INVALID_SELECTION;
}

static void grid_paint_thumbnails(window_t *win, reportview_data_t *data) {
  irect16_t cr = get_client_rect(win);
  int ring = get_theme()->thumbnail.ring_width;
  fill_rect(get_sys_color(brColumnViewBg), R(0, 0, cr.w, cr.h));
  for (uint32_t i = 0; i < data->count; i++) {
    irect16_t tile = window_scroll_rect_to_client(win, grid_item_rect(win, data, i));
    if (tile.y + tile.h <= 0) continue;
    if (tile.y >= cr.h) break;
    rv_draw_item_icon(data->icon_strip, data->items[i].icon, &tile, 0xFFFFFFFF);
    if ((int)i == data->selected && ring > 0)
      stroke_rounded_rect(get_sys_color(brAccent), tile, 0, ring);
  }
}

static void grid_paint(window_t *win, reportview_data_t *data) {
  if (data->view_mode == RVM_VIEW_THUMBNAIL) { grid_paint_thumbnails(win, data); return; }
  irect16_t cr = get_client_rect(win);
  icon_grid_geom_t g = grid_geom(win, data);
  int icon_sz = data->icon_size;
  int label_h = text_char_height(FONT_SMALLEST) + 2;
  int clip_bot = cr.h;
  bitmap_strip_t *strip = data->icon_strip;
  uint32_t bg_col = get_sys_color(brColumnViewBg);

  fill_rect(bg_col, R(0, 0, cr.w, cr.h));

  for (uint32_t i = 0; i < data->count; i++) {
    irect16_t item = window_scroll_rect_to_client(win, grid_item_rect(win, data, i));
    int cx = item.x, cy = item.y;
    if (cy + g.cell_h <= 0)
      continue;
    if (cy >= clip_bot)
      break;

    bool selected = (int)i == data->selected;
    irect16_t icon_r = R(cx + (g.cell_w - icon_sz) / 2,
                         cy + RV_LARGE_ICON_TOP_PAD,
                         icon_sz, icon_sz);
    irect16_t label_r = R(cx + 2,
                          icon_r.y + icon_r.h + RV_LARGE_ICON_LABEL_GAP,
                          g.cell_w - 4, label_h);

    if (selected) {
      int sel_h = icon_sz + RV_LARGE_ICON_LABEL_GAP + label_h + 4;
      fill_rect(get_sys_color(brActiveTitlebar),
                rect_inset(R(cx + 2, icon_r.y - 2, g.cell_w - 4, sel_h), -1));
    }

    uint32_t icon_col = data->preserve_icon_colors ? 0xFFFFFFFF :
                        selected ? 0xFFFFFFFF : data->items[i].color;
    rv_draw_item_icon(strip, data->items[i].icon, &icon_r, icon_col);

    uint32_t txt_col = selected ? get_sys_color(brAccentText)
                                : get_sys_color(brTextNormal);
    draw_text_clipped(FONT_SMALLEST, data->items[i].text, &label_r,
                      txt_col, TEXT_ALIGN_CENTER);
  }
}

result_t win_icongrid(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  reportview_data_t *data = (reportview_data_t *)win->userdata2;

  switch (msg) {
    case evCreate: {
      data = calloc(1, sizeof(reportview_data_t));
      if (!data) return false;
      win->userdata2 = data;
      win->flags |= WINDOW_VSCROLL;
      win->flags |= WINDOW_FLEXSPACE;
      win->vscroll.visible_mode = SB_VIS_AUTO;
      data->selected = -1;
      data->column_width = DEFAULT_COLUMN_WIDTH;
      data->icon_size = DEFAULT_ICON_SIZE;
      data->icon_text_gap = DEFAULT_ICON_TEXT_GAP;
      data->redraw_enabled = true;
      data->redraw_dirty = false;
      data->column_titles_visible = true;
      data->preserve_icon_colors = false;
      data->view_mode = RVM_VIEW_LARGE_ICON;
      window_set_scroll_content(win, 0, 0,
                                (irect16_t){RV_LARGE_ICON_PAD, RV_LARGE_ICON_PAD, RV_LARGE_ICON_PAD, RV_LARGE_ICON_PAD},
                                (irect16_t){0});
      return true;
    }
    case evMeasureScrollContent: {
      layout_measure_t *m = lparam;
      icon_grid_geom_t g = grid_geom_for(data, 0, m->avail_w);
      m->desired_w = 0;
      m->desired_h = grid_content_h(&g, (int)data->count);
      return true;
    }
    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (!m) return true;
      icon_grid_geom_t g = grid_geom(win, data);
      irect16_t pad = win->scroll_content.padding;
      int min_h = pad.y + pad.h + (g.thumbnail ? data->icon_size : g.cell_h);
      int min_w = MAX(1, pad.x + pad.w + (g.thumbnail ? data->icon_size : g.ncol * g.cell_w));
      m->desired_w = MAX(m->desired_w, min_w);
      m->desired_h = MAX(m->desired_h, min_h);
      return true;
    }
    case evPaint:
      grid_paint(win, data);
      return false;
    case evLeftButtonDown: {
      int index = grid_hit_index(win, data, wparam);
      if (rv_valid_index(data, index)) {
        int old_selection = data->selected;
        data->selected = index;
        if (old_selection != data->selected)
          rv_notify(win, data, index, RVN_SELCHANGE);
        rv_invalidate(win, data);
      }
      return true;
    }
    case evLeftButtonDoubleClick: {
      int index = grid_hit_index(win, data, wparam);
      if (rv_valid_index(data, index)) {
        rv_notify(win, data, index, RVN_DBLCLK);
      }
      return true;
    }
    case RVM_ADDITEM: {
      reportview_item_t *item = (reportview_item_t *)lparam;
      if (!item || data->count >= MAX_COLUMNVIEW_ITEMS)
        return -1;
      uint32_t i = data->count;
      if (!rv_store_item(data, i, item))
        return -1;
      data->count++;
      grid_sync_scroll(win);
      rv_invalidate(win, data);
      return (result_t)i;
    }
    case RVM_DELETEITEM: {
      if (wparam >= data->count)
        return false;
      memmove(data->items + wparam, data->items + wparam + 1, (data->count - wparam - 1) * sizeof(data->items[0]));
      memmove(data->names + wparam, data->names + wparam + 1, (data->count - wparam - 1) * sizeof(data->names[0]));
      memmove(data->subnames + wparam, data->subnames + wparam + 1, (data->count - wparam - 1) * sizeof(data->subnames[0]));
      data->count--;
      rv_rebind_item_refs(data, (uint32_t)wparam);
      if (data->selected == (int)wparam) data->selected = RV_INVALID_SELECTION;
      else if (data->selected > (int)wparam) data->selected--;
      grid_sync_scroll(win);
      rv_invalidate(win, data);
      return true;
    }
    case RVM_GETITEMCOUNT:
      return (result_t)data->count;
    case RVM_GETSELECTION:
      return (result_t)data->selected;
    case RVM_SETSELECTION:
      if ((int)wparam >= 0 && wparam < data->count) {
        data->selected = (int)wparam;
        window_ensure_rect_visible(win, grid_item_rect(win, data, data->selected), SB_VERT);
        rv_invalidate(win, data);
        return true;
      }
      return false;
    case RVM_CLEAR:
      data->count = 0;
      rv_reset_view_state(win, data);
      grid_sync_scroll(win);
      rv_invalidate(win, data);
      return true;
    case RVM_SETCOLUMNWIDTH:
      if (wparam > 0) {
        data->column_width = (int)wparam;
        grid_sync_scroll(win);
        rv_invalidate(win, data);
        return true;
      }
      return false;
    case RVM_GETCOLUMNWIDTH:
      return (result_t)data->column_width;
    case RVM_GETITEMDATA:
      if (wparam < data->count && lparam) {
        *(reportview_item_t *)lparam = data->items[wparam];
        return true;
      }
      return false;
    case RVM_HITTEST:
      return (result_t)grid_hit_index(win, data, wparam);
    case RVM_SETITEMDATA: {
      reportview_item_t *item = (reportview_item_t *)lparam;
      if (!item || wparam >= data->count)
        return false;
      if (!rv_store_item(data, (uint32_t)wparam, item))
        return false;
      rv_invalidate(win, data);
      return true;
    }
    case RVM_SETVIEWMODE: {
      if (wparam != RVM_VIEW_LARGE_ICON && wparam != RVM_VIEW_THUMBNAIL) return false;
      int pad = wparam == RVM_VIEW_THUMBNAIL ? 0 : RV_LARGE_ICON_PAD;
      data->view_mode = (int)wparam;
      window_set_scroll_content(win, 0, 0, (irect16_t){pad, pad, pad, pad}, (irect16_t){0});
      rv_invalidate(win, data);
      return true;
    }
    case RVM_SETREDRAW:
      if (wparam) {
        data->redraw_enabled = true;
        if (data->redraw_dirty) {
          data->redraw_dirty = false;
          invalidate_window(win);
        }
      } else {
        data->redraw_enabled = false;
      }
      return true;
    case RVM_SETICONSTRIP:
      data->icon_strip = (bitmap_strip_t *)lparam;
      rv_invalidate(win, data);
      return true;
    case RVM_SETLARGEICONCOLS:
      data->fixed_large_icon_cols = (int)wparam;
      grid_sync_scroll(win);
      rv_invalidate(win, data);
      return true;
    case RVM_SETICONSIZE:
      if ((int)wparam > 0) {
        data->icon_size = (int)wparam;
        grid_sync_scroll(win);
        rv_invalidate(win, data);
        return true;
      }
      return false;
    case RVM_SETCOLUMNTITLESVISIBLE:
      data->column_titles_visible = (wparam != 0);
      grid_sync_scroll(win);
      rv_invalidate(win, data);
      return true;
    case RVM_GETCOLUMNTITLESVISIBLE:
      return data->column_titles_visible ? true : false;
    case RVM_SETPRESERVEICONCOLORS:
      data->preserve_icon_colors = (wparam != 0);
      rv_invalidate(win, data);
      return true;
    case RVM_SETICONTEXTGAP:
      data->icon_text_gap = (int)wparam;
      if (data->icon_text_gap < 0) data->icon_text_gap = 0;
      rv_invalidate(win, data);
      return true;
    case evVScroll:
      rv_invalidate(win, data);
      return true;
    case evArrange: {
      layout_arrange_t const *a = (layout_arrange_t const *)lparam;
      if (a) {
        irect16_t r = a->rect;
        if (r.w < 1) r.w = 1;
        if (r.h < 1) r.h = 1;
        win->frame = r;
      }
      grid_sync_scroll(win);
      return true;
    }
    case evResize:
      grid_sync_scroll(win);
      return false;
    case evKeyDown: {
      if (!data || data->count == 0)
        return false;
      int count = (int)data->count;
      int cur = data->selected;
      int next = cur;
      icon_grid_geom_t g = grid_geom(win, data);
      switch (wparam) {
        case AX_KEY_UPARROW:
          next = (cur < 0) ? 0 : (cur - g.ncol >= 0 ? cur - g.ncol : cur);
          break;
        case AX_KEY_DOWNARROW:
          next = (cur < 0) ? 0 : (cur + g.ncol < count ? cur + g.ncol : cur);
          break;
        case AX_KEY_LEFTARROW:
          next = (cur < 0) ? 0 : (cur > 0 ? cur - 1 : 0);
          break;
        case AX_KEY_RIGHTARROW:
          next = (cur < 0) ? 0 : (cur + 1 < count ? cur + 1 : cur);
          break;
        case AX_KEY_ENTER:
          if (cur < 0) return false;
          rv_notify(win, data, cur, RVN_DBLCLK);
          return true;
        case AX_KEY_DEL:
          if (cur < 0) return false;
          rv_notify(win, data, cur, RVN_DELETE);
          return true;
        default:
          return false;
      }
      if (next != cur && next >= 0) {
        data->selected = next;
        window_ensure_rect_visible(win, grid_item_rect(win, data, next), SB_VERT);
        rv_notify(win, data, next, RVN_SELCHANGE);
        rv_invalidate(win, data);
      }
      return true;
    }
    case evDestroy:
      free(data);
      win->userdata2 = NULL;
      return true;
    default:
      return false;
  }
}

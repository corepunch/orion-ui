#include <stdlib.h>
#include <string.h>

#include "columnview_internal.h"
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/theme.h>

typedef struct {
  int ncol;
  int cell_w;
  int cell_h;
  int usable_w;
} icon_grid_geom_t;

static icon_grid_geom_t grid_geom(window_t *win, reportview_data_t *data) {
  int eff_w = rv_content_width(win);
  int cell_w = data->column_width > 0 ? data->column_width : DEFAULT_COLUMN_WIDTH;
  return (icon_grid_geom_t){
    .ncol = rv_large_icon_ncol(data, eff_w, cell_w),
    .cell_w = cell_w,
    .cell_h = rv_large_icon_cell_h(data),
    .usable_w = MAX(1, eff_w - 2 * RV_LARGE_ICON_PAD),
  };
}

static int grid_col_x(const icon_grid_geom_t *g, int col) {
  if (!g || col <= 0) {
    int usable = g ? g->usable_w : 0;
    int cell_w = g ? g->cell_w : 0;
    if (g && g->ncol == 1)
      return RV_LARGE_ICON_PAD + MAX(0, (usable - cell_w) / 2);
    return RV_LARGE_ICON_PAD;
  }
  if (g->ncol <= 1)
    return RV_LARGE_ICON_PAD + MAX(0, (g->usable_w - g->cell_w) / 2);

  int min_span = (g->ncol - 1) * g->cell_w;
  int fill_span = g->usable_w - g->cell_w;
  int span = MAX(min_span, fill_span);
  return RV_LARGE_ICON_PAD
       + (span * col + (g->ncol - 1) / 2) / (g->ncol - 1);
}

static int grid_content_height(window_t *win, reportview_data_t *data) {
  icon_grid_geom_t g = grid_geom(win, data);
  int rows = (data->count == 0) ? 0 : ((int)data->count + g.ncol - 1) / g.ncol;
  return 2 * RV_LARGE_ICON_PAD + rows * g.cell_h;
}

static void grid_sync_scroll(window_t *win, reportview_data_t *data) {
  if (!win || !data)
    return;
  irect16_t cr = get_client_rect(win);
  if (cr.h <= 0)
    return;
  int total_h = grid_content_height(win, data);
  int max_scroll_px = total_h - cr.h;
  if (max_scroll_px < 0)
    max_scroll_px = 0;
  if ((int)win->vscroll.pos > max_scroll_px)
    win->vscroll.pos = (uint32_t)max_scroll_px;
  scroll_info_t si;
  si.fMask = SIF_ALL;
  si.nMin = 0;
  si.nMax = total_h;
  si.nPage = (uint32_t)cr.h;
  si.nPos = (int)win->vscroll.pos;
  set_scroll_info(win, SB_VERT, &si, false);
}

static int grid_hit_index(window_t *win, reportview_data_t *data, uint32_t wparam) {
  int mx = (int)(int16_t)LOWORD(wparam);
  int my = (int)(int16_t)HIWORD(wparam);
  icon_grid_geom_t g = grid_geom(win, data);
  int local_y = my - RV_LARGE_ICON_PAD;
  if (local_y < 0)
    return RV_INVALID_SELECTION;
  int col = RV_INVALID_SELECTION;
  for (int i = 0; i < g.ncol; i++) {
    int cx = grid_col_x(&g, i);
    if (mx >= cx && mx < cx + g.cell_w) {
      col = i;
      break;
    }
  }
  if (col < 0)
    return RV_INVALID_SELECTION;
  int row = local_y / g.cell_h;
  int index = row * g.ncol + col;
  return rv_valid_index(data, index) ? index : RV_INVALID_SELECTION;
}

static void grid_scroll_to_item(window_t *win, reportview_data_t *data, int index) {
  int scroll_y = (int)win->vscroll.pos;
  int visible_h = get_client_rect(win).h;
  icon_grid_geom_t g = grid_geom(win, data);
  int row = index / g.ncol;
  int item_y_top = RV_LARGE_ICON_PAD + row * g.cell_h;
  int item_y_bottom = item_y_top + g.cell_h;

  if (item_y_top - scroll_y < 0)
    win->vscroll.pos = (uint32_t)(item_y_top > 0 ? item_y_top : 0);
  else if (item_y_bottom - scroll_y > visible_h)
    win->vscroll.pos = (uint32_t)(item_y_bottom - visible_h);
}

static void grid_paint(window_t *win, reportview_data_t *data) {
  irect16_t cr = get_client_rect(win);
  int scroll_y = (int)win->vscroll.pos;
  icon_grid_geom_t g = grid_geom(win, data);
  int icon_sz = data->icon_size;
  int label_h = text_char_height(FONT_SMALLEST) + 2;
  int clip_bot = cr.h;
  bitmap_strip_t *strip = data->icon_strip;
  uint32_t bg_col = get_sys_color(brColumnViewBg);

  fill_rect(bg_col, R(0, 0, cr.w, cr.h));

  for (uint32_t i = 0; i < data->count; i++) {
    int icol = (int)i % g.ncol;
    int irow = (int)i / g.ncol;
    int cx = grid_col_x(&g, icol);
    int cy = RV_LARGE_ICON_PAD + irow * g.cell_h - scroll_y;
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

    uint32_t txt_col = selected ? get_sys_color(brActiveTitlebarText)
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
      data->last_click_index = RV_INVALID_SELECTION;
      data->column_width = DEFAULT_COLUMN_WIDTH;
      data->icon_size = DEFAULT_ICON_SIZE;
      data->icon_text_gap = DEFAULT_ICON_TEXT_GAP;
      data->redraw_enabled = true;
      data->redraw_dirty = false;
      data->column_titles_visible = true;
      data->preserve_icon_colors = false;
      grid_sync_scroll(win, data);
      return true;
    }
    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (!m) return true;
      icon_grid_geom_t g = grid_geom(win, data);
      int min_h = 2 * RV_LARGE_ICON_PAD + rv_large_icon_cell_h(data);
      int min_w = MAX(1, 2 * RV_LARGE_ICON_PAD + g.ncol * g.cell_w);
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
        uint32_t now = axGetMilliseconds();
        if (data->last_click_index == index && (now - data->last_click_time) < RV_DOUBLE_CLICK_MS) {
          rv_notify(win, data, index, RVN_DBLCLK);
          rv_reset_click_state(data);
        } else {
          int old_selection = data->selected;
          data->selected = index;
          data->last_click_time = now;
          data->last_click_index = index;
          if (old_selection != data->selected)
            rv_notify(win, data, index, RVN_SELCHANGE);
          rv_invalidate(win, data);
        }
      }
      return true;
    }
    case evLeftButtonDoubleClick: {
      int index = grid_hit_index(win, data, wparam);
      if (rv_valid_index(data, index)) {
        rv_reset_click_state(data);
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
      grid_sync_scroll(win, data);
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
      grid_sync_scroll(win, data);
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
        grid_scroll_to_item(win, data, data->selected);
        grid_sync_scroll(win, data);
        rv_invalidate(win, data);
        return true;
      }
      return false;
    case RVM_CLEAR:
      data->count = 0;
      rv_reset_view_state(win, data);
      grid_sync_scroll(win, data);
      rv_invalidate(win, data);
      return true;
    case RVM_SETCOLUMNWIDTH:
      if (wparam > 0) {
        data->column_width = (int)wparam;
        grid_sync_scroll(win, data);
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
    case RVM_SETVIEWMODE:
      return wparam == RVM_VIEW_LARGE_ICON;
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
      grid_sync_scroll(win, data);
      rv_invalidate(win, data);
      return true;
    case RVM_SETICONSIZE:
      if ((int)wparam > 0) {
        data->icon_size = (int)wparam;
        grid_sync_scroll(win, data);
        rv_invalidate(win, data);
        return true;
      }
      return false;
    case RVM_SETCOLUMNTITLESVISIBLE:
      data->column_titles_visible = (wparam != 0);
      grid_sync_scroll(win, data);
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
      win->vscroll.pos = (uint32_t)wparam;
      grid_sync_scroll(win, data);
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
      grid_sync_scroll(win, data);
      return true;
    }
    case evResize:
      grid_sync_scroll(win, data);
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
        grid_scroll_to_item(win, data, next);
        grid_sync_scroll(win, data);
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

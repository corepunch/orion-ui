#include <string.h>
#include <stdlib.h>

#include "columnview.h"
#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"
#include "../user/theme.h"

#define MAX_COLUMNVIEW_ITEM_NAME 256
#define MAX_COLUMNVIEW_ITEMS 256
#define MAX_REPORTVIEW_COLUMNS 16
#define MAX_REPORTVIEW_TITLE 64
#define ENTRY_HEIGHT  COLUMNVIEW_ENTRY_HEIGHT
#define HEADER_HEIGHT COLUMNVIEW_HEADER_HEIGHT
#define DEFAULT_COLUMN_WIDTH 160
#define ICON_OFFSET 16
#define WIN_PADDING 4
#define RV_DOUBLE_CLICK_MS 500u
#define RV_INVALID_SELECTION (-1)

// ReportView/ListView/ColumnView shared data structure.
typedef struct {
  reportview_item_t items[MAX_COLUMNVIEW_ITEMS];
  char names[MAX_COLUMNVIEW_ITEMS][MAX_COLUMNVIEW_ITEM_NAME];
  char subnames[MAX_COLUMNVIEW_ITEMS][REPORTVIEW_MAX_SUBITEMS][MAX_COLUMNVIEW_ITEM_NAME];

  struct {
    char title[MAX_REPORTVIEW_TITLE];
    uint32_t width;
  } columns[MAX_REPORTVIEW_COLUMNS];

  uint32_t count;
  int selected;
  int column_width;
  uint32_t last_click_time;
  int last_click_index;

  uint32_t view_mode;
  uint32_t column_count;
  bool redraw_enabled;
  bool redraw_dirty;

  // Per-instance icon strip for icon-view rendering.
  // Set via RVM_SETICONSTRIP (lparam = bitmap_strip_t*; NULL to clear).
  // The strip is owned by the caller — win_reportview never frees it.
  bitmap_strip_t *icon_strip;
} reportview_data_t;

static inline void rv_invalidate(window_t *win, reportview_data_t *data) {
  if (!win || !data)
    return;
  if (data->redraw_enabled) {
    invalidate_window(win);
  } else {
    data->redraw_dirty = true;
  }
}

static inline bool rv_valid_index(const reportview_data_t *data, int index) {
  return data && index >= 0 && index < (int)data->count;
}

// Centralized command notification helper.
//
// Follows WinAPI WM_COMMAND convention for control notifications:
//   wparam LOWORD = item index (row that triggered the notification)
//   wparam HIWORD = notification code (RVN_SELCHANGE, RVN_DBLCLK, …)
//   lparam        = source control window  ← WinAPI: lParam = hWnd of control
//
// Sending to get_root_window() mirrors how win_reportview is typically used:
// the control is a child of the root (or a child of a child), and root-window
// procs handle RVN_* by examining lparam to identify the source control.
static void rv_notify(window_t *win, reportview_data_t *data, int index, uint16_t code) {
  if (!rv_valid_index(data, index))
    return;
  send_message(get_root_window(win), evCommand,
               MAKEDWORD(index, code), (void *)win);
}

static inline void rv_reset_click_state(reportview_data_t *data) {
  data->last_click_time = 0;
  data->last_click_index = RV_INVALID_SELECTION;
}

// Keep contiguous pointer-backed storage valid after insert/update/delete.
static bool rv_store_item(reportview_data_t *data, uint32_t i,
                          const reportview_item_t *item) {
  if (!data || !item || i >= MAX_COLUMNVIEW_ITEMS)
    return false;

  char *name = data->names[i];
  strncpy(name, item->text ? item->text : "", MAX_COLUMNVIEW_ITEM_NAME - 1);
  name[MAX_COLUMNVIEW_ITEM_NAME - 1] = '\0';

  reportview_item_t dst = *item;
  dst.text = name;
  if (dst.subitem_count > REPORTVIEW_MAX_SUBITEMS)
    dst.subitem_count = REPORTVIEW_MAX_SUBITEMS;

  for (uint32_t s = 0; s < REPORTVIEW_MAX_SUBITEMS; s++) {
    char *sub = data->subnames[i][s];
    const char *src = (s < dst.subitem_count && item->subitems[s])
                    ? item->subitems[s]
                    : "";
    strncpy(sub, src, MAX_COLUMNVIEW_ITEM_NAME - 1);
    sub[MAX_COLUMNVIEW_ITEM_NAME - 1] = '\0';
    dst.subitems[s] = sub;
  }

  data->items[i] = dst;
  return true;
}

static void rv_rebind_item_refs(reportview_data_t *data, uint32_t start) {
  if (!data || start >= data->count)
    return;

  for (uint32_t i = start; i < data->count; i++) {
    data->items[i].text = data->names[i];
    if (data->items[i].subitem_count > REPORTVIEW_MAX_SUBITEMS)
      data->items[i].subitem_count = REPORTVIEW_MAX_SUBITEMS;
    for (uint32_t s = 0; s < REPORTVIEW_MAX_SUBITEMS; s++)
      data->items[i].subitems[s] = data->subnames[i][s];
  }
}

static void rv_reset_view_state(window_t *win, reportview_data_t *data) {
  data->selected = RV_INVALID_SELECTION;
  rv_reset_click_state(data);
  win->scroll[1] = 0;
}

static inline int get_column_count(int window_width, int column_width) {
  if (window_width <= 0 || column_width <= 0)
    return 1;
  int ncol = window_width / column_width;
  return (ncol > 0) ? ncol : 1;
}

static inline int rv_content_width(window_t *win) {
  return win->frame.w - (win->vscroll.visible ? SCROLLBAR_WIDTH : 0);
}

static int rv_get_report_column_width(reportview_data_t *data, int col, int avail_w) {
  if (!data || col < 0 || col >= (int)data->column_count)
    return 80;
  if (data->columns[col].width != 0)
    return (int)data->columns[col].width;
  if (data->column_count == 0)
    return avail_w;
  return avail_w / (int)data->column_count;
}

static int rv_report_total_width(reportview_data_t *data, int avail_w) {
  int total = 0;
  if (!data || data->column_count == 0)
    return avail_w;
  for (uint32_t i = 0; i < data->column_count; i++)
    total += rv_get_report_column_width(data, (int)i, avail_w);
  return total;
}

static int rv_content_height(window_t *win, reportview_data_t *data) {
  int eff_w = rv_content_width(win);
  if (data->view_mode == RVM_VIEW_REPORT) {
    return HEADER_HEIGHT + (int)data->count * ENTRY_HEIGHT;
  }

  int ncol = get_column_count(eff_w, data->column_width);
  int total_rows = (data->count == 0) ? 0
                 : (int)((data->count + (unsigned)ncol - 1) / (unsigned)ncol);
  return total_rows * ENTRY_HEIGHT;
}

// Coordinate space notes:
//   event.c computes LOCAL_X/LOCAL_Y using the active window's scroll and
//   absolute position.  For the active (root) window those coords are in
//   content space already.  For child windows event.c adds the child's own
//   scroll before calling send_message, so the wparam received here is also
//   in content space.  Draw code uses content-space coords with the same
//   origin, so no further adjustment is needed.
//
// Common mistake (historical): an older version added win->scroll[] inside
// this function, which caused double-counting when event.c already included
// the scroll.  The fix is to use wparam directly.
//   See tests/columnview_keyboard_test.c for regression tests.
static int rv_hit_index(window_t *win, reportview_data_t *data, uint32_t wparam) {
  int mx = (int)(int16_t)LOWORD(wparam);
  int my = (int)(int16_t)HIWORD(wparam);

  if (data->view_mode == RVM_VIEW_REPORT) {
    (void)mx;
    if (my < HEADER_HEIGHT)
      return -1;
    int row = (my - HEADER_HEIGHT) / ENTRY_HEIGHT;
    return rv_valid_index(data, row) ? row : RV_INVALID_SELECTION;
  }

  int eff_w = rv_content_width(win);
  int ncol = get_column_count(eff_w, data->column_width);
  int col = mx / data->column_width;
  int row = (my - WIN_PADDING) / ENTRY_HEIGHT;
  int index = row * ncol + col;
  return rv_valid_index(data, index) ? index : RV_INVALID_SELECTION;
}

static void rv_scroll_to_item(window_t *win, reportview_data_t *data, int index) {
  if (!win || !data || index < 0 || index >= (int)data->count)
    return;

  int scroll_y = (int)win->scroll[1];
  int visible_h = win->frame.h;

  if (data->view_mode == RVM_VIEW_REPORT) {
    int item_y_top = HEADER_HEIGHT + index * ENTRY_HEIGHT;
    int item_y_bottom = item_y_top + ENTRY_HEIGHT;

    if (item_y_top - scroll_y < HEADER_HEIGHT) {
      win->scroll[1] = (uint32_t)(item_y_top - HEADER_HEIGHT);
    } else if (item_y_bottom - scroll_y > visible_h) {
      win->scroll[1] = (uint32_t)(item_y_bottom - visible_h);
    }
    return;
  }

  int eff_w = rv_content_width(win);
  int ncol = get_column_count(eff_w, data->column_width);
  int row = index / ncol;
  int item_y_top = row * ENTRY_HEIGHT + WIN_PADDING;
  int item_y_bottom = item_y_top + ENTRY_HEIGHT;

  if (item_y_top - scroll_y < 0)
    win->scroll[1] = (uint32_t)(item_y_top > 0 ? item_y_top : 0);
  else if (item_y_bottom - scroll_y > visible_h)
    win->scroll[1] = (uint32_t)(item_y_bottom - visible_h);
}

static void rv_sync_scroll(window_t *win, reportview_data_t *data) {
  if (!win || !data || win->frame.h <= 0)
    return;

  int total_h = rv_content_height(win, data);
  int max_scroll_px = total_h - win->frame.h;
  if (max_scroll_px < 0) max_scroll_px = 0;

  if ((int)win->scroll[1] > max_scroll_px)
    win->scroll[1] = (uint32_t)max_scroll_px;

  scroll_info_t si;
  si.fMask = SIF_ALL;
  si.nMin = 0;
  si.nMax = total_h;
  si.nPage = (uint32_t)win->frame.h;
  si.nPos = (int)win->scroll[1];
  set_scroll_info(win, SB_VERT, &si, false);
}

// Draw one icon from the per-instance strip centred inside icon_rect.
// If no strip is assigned (or icon_id is out of range) a small placeholder
// rectangle is drawn so the icon slot is always visually occupied.
static void rv_draw_item_icon(bitmap_strip_t *strip, int icon_id,
                              rect_t const *icon_rect, uint32_t col) {
  if (strip && strip->tex != 0 && strip->cols > 0) {
    int total = strip->cols * (strip->sheet_h / strip->icon_h);
    if (icon_id >= 0 && icon_id < total) {
      int scol   = icon_id % strip->cols;
      int srow   = icon_id / strip->cols;
      int icon_sz = strip->icon_w;
      int ix = icon_rect->x + (icon_rect->w - icon_sz) / 2;
      int iy = icon_rect->y + (icon_rect->h - icon_sz) / 2;
      float u0 = (float)(scol * strip->icon_w) / (float)strip->sheet_w;
      float v0 = (float)(srow * strip->icon_h) / (float)strip->sheet_h;
      float u1 = u0 + (float)strip->icon_w / (float)strip->sheet_w;
      float v1 = v0 + (float)strip->icon_h / (float)strip->sheet_h;
      draw_sprite_region((int)strip->tex, R(ix, iy, icon_sz, icon_sz),
                         u0, v0, u1, v1, col);
      return;
    }
  }
  // Fallback: draw a small placeholder square so the icon slot is not blank.
  // (draw_icon8_clipped is not used here because it renders theme icons, which
  // use a different index space than file-picker / custom icon_id_t values.)
  {
    int ph = 8;
    int px = icon_rect->x + (icon_rect->w - ph) / 2;
    int py = icon_rect->y + (icon_rect->h - ph) / 2;
    uint32_t dim = (col & 0x00FFFFFFu) | 0x60000000u;  // 38% opacity
    fill_rect(dim, R(px,        py,        ph, 1));
    fill_rect(dim, R(px,        py + ph-1, ph, 1));
    fill_rect(dim, R(px,        py + 1,    1,  ph - 2));
    fill_rect(dim, R(px + ph-1, py + 1,    1,  ph - 2));
  }
}

static void rv_paint_icon_view(window_t *win, reportview_data_t *data) {
  int eff_w = rv_content_width(win);
  int ncol = get_column_count(eff_w, data->column_width);
  int scroll_y = (int)win->scroll[1];
  uint32_t bg_col = get_sys_color(brColumnViewBg);

  fill_rect(bg_col, R(0, 0, win->frame.w, win->frame.h));

  // With the child-relative projection applied by send_message before evPaint,
  // y=0 in draw space is always the window's own client top regardless of whether
  // this is a root or child window.  Clip items to [0, frame.h].
  int clip_top = 0;
  int clip_bottom = win->frame.h;

  bitmap_strip_t *strip = data->icon_strip;

  for (uint32_t i = 0; i < data->count; i++) {
    int col = i % ncol;
    int x = col * data->column_width + WIN_PADDING;
    int y = (int)(i / (uint32_t)ncol) * ENTRY_HEIGHT + WIN_PADDING - scroll_y;

    if (y + ENTRY_HEIGHT <= clip_top) continue;
    if (y >= clip_bottom) break;

    int item_w  = data->column_width - 6;
    int item_h  = ENTRY_HEIGHT - 1;
    rect_t icon_rect = {x,               y, ICON_OFFSET,              item_h};
    rect_t text_rect = {x + ICON_OFFSET, y, item_w - ICON_OFFSET - 2, item_h};

    int icon_id = data->items[i].icon;

    if ((int)i == data->selected) {
      uint32_t icon_col = get_sys_color(brWindowBg);
      fill_rect(get_sys_color(brTextNormal), R(x - 2, y, item_w, item_h));
      rv_draw_item_icon(strip, icon_id, &icon_rect, icon_col);
      draw_text_clipped(FONT_SMALL, data->items[i].text, &text_rect, icon_col, 0);
    } else {
      uint32_t icon_col = data->items[i].color;
      fill_rect(bg_col, R(x - 2, y, item_w, item_h));
      rv_draw_item_icon(strip, icon_id, &icon_rect, icon_col);
      draw_text_clipped(FONT_SMALL, data->items[i].text, &text_rect, icon_col, 0);
    }
  }
}

static void rv_paint_report_view(window_t *win, reportview_data_t *data) {
  int eff_w = rv_content_width(win);
  int row_w = rv_report_total_width(data, eff_w);
  int body_h = win->frame.h - HEADER_HEIGHT;
  int scroll_y = (int)win->scroll[1];
  uint32_t bg_col = get_sys_color(brColumnViewBg);

  int first_row = (body_h > 0) ? (scroll_y / ENTRY_HEIGHT) : 0;
  int last_row = (body_h > 0) ? ((scroll_y + body_h + ENTRY_HEIGHT - 1) / ENTRY_HEIGHT) : 0;
  if (first_row < 0) first_row = 0;
  if (last_row > (int)data->count) last_row = (int)data->count;

  uint32_t hdr_fg = get_sys_color(brTextNormal);
  uint32_t sep_col = get_sys_color(brDarkEdge);

  // Background and full-row selection highlight painted once (no per-column clip).
  fill_rect(bg_col, R(0, HEADER_HEIGHT, row_w, body_h));

  if (data->selected >= first_row && data->selected < last_row) {
    int y = HEADER_HEIGHT + data->selected * ENTRY_HEIGHT - scroll_y;
    if (y < HEADER_HEIGHT) y = HEADER_HEIGHT;
    fill_rect(get_sys_color(brTextNormal), R(0, y, row_w, ENTRY_HEIGHT - 1));
  }

  // Compute screen-space origin of this window's client top-left.
  // Needed so we can pass screen-absolute rects to set_clip_rect(NULL, ...).
  // win_frame_in_screen returns win->frame directly for root windows.
  window_t *root = get_root_window(win);
  int root_t = titlebar_height(root);
  int scr_x = (win == root) ? win->frame.x : root->frame.x + win->frame.x;
  int scr_y = (win == root) ? win->frame.y : root->frame.y + root_t + win->frame.y;

  // Draw per-column: use two scissor rects per column — one for the header
  // band and one for the body — so that scrolled row text cannot overdraw
  // the header and text cannot bleed into adjacent columns.
  int col_x = 0;
  for (uint32_t col = 0; col < data->column_count; col++) {
    int col_w = rv_get_report_column_width(data, (int)col, eff_w);

    // Header scissor: column width, header height only.
    set_clip_rect(NULL, &(rect_t){scr_x + col_x, scr_y, col_w, HEADER_HEIGHT});
    draw_button(&(rect_t){col_x, 0, col_w, HEADER_HEIGHT}, 1, 1, false);
    draw_text_small_clipped(data->columns[col].title, &(rect_t){col_x, 0, col_w, HEADER_HEIGHT}, hdr_fg, TEXT_PADDING_LEFT);

    // Body scissor: column width, everything below the header.
    int body_h = win->frame.h - HEADER_HEIGHT;
    set_clip_rect(NULL, &(rect_t){scr_x + col_x, scr_y + HEADER_HEIGHT, col_w, body_h});

    for (int row = first_row; row < last_row; row++) {
      reportview_item_t *it = &data->items[row];
      uint32_t fg = (row == data->selected) ? get_sys_color(brWindowBg)
                  : it->color              ? it->color
                                           : get_sys_color(brTextNormal);
      int y = HEADER_HEIGHT + row * ENTRY_HEIGHT - scroll_y;
      const char *src = "";

      if (col == 0) {
        src = it->text ? it->text : "";
      } else {
        uint32_t idx = col - 1;
        src = (idx < it->subitem_count && it->subitems[idx]) ? it->subitems[idx] : "";
      }

      draw_text_clipped(FONT_SMALL, src, &(rect_t){col_x, y, col_w, ENTRY_HEIGHT}, fg, TEXT_PADDING_LEFT);
    }

    col_x += col_w;
  }

  // Restore scissor to the window client area before drawing separators,
  // which span the full column height and must not be column-clipped.
  set_clip_rect(NULL, &(rect_t){scr_x, scr_y, eff_w, win->frame.h});

  // Column separator lines.
  col_x = 0;
  for (uint32_t col = 0; col < data->column_count; col++) {
    int col_w = rv_get_report_column_width(data, (int)col, eff_w);
    col_x += col_w;
    fill_rect(sep_col, R(col_x, HEADER_HEIGHT, 1, win->frame.h - HEADER_HEIGHT));
  }
}

result_t win_reportview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  reportview_data_t *data = (reportview_data_t *)win->userdata2;

  switch (msg) {
    case evCreate: {
      data = calloc(1, sizeof(reportview_data_t));
      if (!data) return false;

      win->userdata2 = data;
      win->flags |= WINDOW_VSCROLL;
      win->vscroll.visible_mode = SB_VIS_AUTO;

      data->selected = -1;
      data->last_click_index = RV_INVALID_SELECTION;
      data->column_width = DEFAULT_COLUMN_WIDTH;
      data->view_mode = RVM_VIEW_ICON;
      data->redraw_enabled = true;
      data->redraw_dirty = false;

      rv_sync_scroll(win, data);
      return true;
    }

    case evPaint:
      if (data->view_mode == RVM_VIEW_REPORT)
        rv_paint_report_view(win, data);
      else
        rv_paint_icon_view(win, data);
      return false;

    case evLeftButtonDown: {
      int index = rv_hit_index(win, data, wparam);
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

          if (old_selection != data->selected) {
            rv_notify(win, data, index, RVN_SELCHANGE);
          }
          rv_invalidate(win, data);
        }
      }
      return true;
    }

    case evLeftButtonDoubleClick: {
      int index = rv_hit_index(win, data, wparam);
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
      rv_sync_scroll(win, data);
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

      if (data->selected == (int)wparam) {
        data->selected = RV_INVALID_SELECTION;
      } else if (data->selected > (int)wparam) {
        data->selected--;
      }

      rv_sync_scroll(win, data);
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
        rv_scroll_to_item(win, data, data->selected);
        rv_sync_scroll(win, data);
        rv_invalidate(win, data);
        return true;
      }
      return false;

    case RVM_CLEAR:
      data->count = 0;
      rv_reset_view_state(win, data);
      rv_sync_scroll(win, data);
      rv_invalidate(win, data);
      return true;

    case RVM_SETCOLUMNWIDTH:
      if (wparam > 0) {
        data->column_width = (int)wparam;
        rv_sync_scroll(win, data);
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

    case RVM_SETITEMDATA: {
      reportview_item_t *item = (reportview_item_t *)lparam;
      if (!item || wparam >= data->count)
        return false;

      uint32_t i = (uint32_t)wparam;
      if (!rv_store_item(data, i, item))
        return false;
      rv_invalidate(win, data);
      return true;
    }

    case RVM_SETVIEWMODE:
      if (wparam == RVM_VIEW_ICON || wparam == RVM_VIEW_REPORT) {
        data->view_mode = wparam;
        rv_reset_view_state(win, data);
        rv_sync_scroll(win, data);
        rv_invalidate(win, data);
        return true;
      }
      return false;

    case RVM_ADDCOLUMN: {
      reportview_column_t *col = (reportview_column_t *)lparam;
      if (!col || data->column_count >= MAX_REPORTVIEW_COLUMNS)
        return -1;

      uint32_t i = data->column_count;
      strncpy(data->columns[i].title, col->title ? col->title : "", MAX_REPORTVIEW_TITLE - 1);
      data->columns[i].title[MAX_REPORTVIEW_TITLE - 1] = '\0';
      data->columns[i].width = col->width;
      data->column_count++;
      rv_invalidate(win, data);
      return (result_t)i;
    }

    case RVM_CLEARCOLUMNS:
      data->column_count = 0;
      rv_invalidate(win, data);
      return true;

    case RVM_GETCOLUMNCOUNT:
      return (result_t)data->column_count;

    case RVM_SETREPORTCOLUMNWIDTH: {
      uint32_t ci = (uint32_t)wparam;
      if (ci >= data->column_count)
        return false;
      data->columns[ci].width = (uint32_t)(uintptr_t)lparam;
      rv_invalidate(win, data);
      return true;

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
    }

    case RVM_SETICONSTRIP:
      data->icon_strip = (bitmap_strip_t *)lparam;
      rv_invalidate(win, data);
      return true;

    case evVScroll: {
      int total_h = rv_content_height(win, data);
      int max_scroll_px = total_h - win->frame.h;
      if (max_scroll_px < 0) max_scroll_px = 0;

      int new_scroll = (int)wparam;
      if (new_scroll > max_scroll_px) new_scroll = max_scroll_px;
      win->scroll[1] = (uint32_t)new_scroll;

      rv_sync_scroll(win, data);
      rv_invalidate(win, data);
      return true;
    }

    case evResize:
      rv_sync_scroll(win, data);
      return false;

    case evKeyDown: {
      if (!data || data->count == 0)
        return false;

      int count = (int)data->count;
      int cur = data->selected;
      int next = cur;

      if (data->view_mode == RVM_VIEW_REPORT) {
        switch (wparam) {
          case AX_KEY_UPARROW:
            next = (cur <= 0) ? 0 : cur - 1;
            break;
          case AX_KEY_DOWNARROW:
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
      } else {
        int eff_w = rv_content_width(win);
        int ncol = get_column_count(eff_w, data->column_width);

        switch (wparam) {
          case AX_KEY_UPARROW:
            next = (cur < 0) ? 0 : (cur - ncol >= 0 ? cur - ncol : cur);
            break;
          case AX_KEY_DOWNARROW:
            next = (cur < 0) ? 0 : (cur + ncol < count ? cur + ncol : cur);
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
      }

      if (next != cur && next >= 0) {
        data->selected = next;
        rv_scroll_to_item(win, data, next);
        rv_sync_scroll(win, data);
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

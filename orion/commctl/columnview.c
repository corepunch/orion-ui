#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "columnview_internal.h"
#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/theme.h>

void tableview_handle_master_selection(window_t *root, window_t *master);

// Shared helper implementation used by the dedicated view controls.

void rv_invalidate(window_t *win, reportview_data_t *data) {
  if (!win || !data)
    return;
  if (data->redraw_enabled) {
    invalidate_window(win);
  } else {
    data->redraw_dirty = true;
  }
}

bool rv_valid_index(const reportview_data_t *data, int index) {
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
void rv_notify(window_t *win, reportview_data_t *data, int index, uint16_t code) {
  if (!rv_valid_index(data, index))
    return;
  if (code == RVN_DBLCLK && tableview_handle_activate(win))
    return;
  window_t *target = get_root_window(win);
  if (win->parent && win->parent->proc == win_column_browser)
    target = win->parent;
  send_message(target, evCommand,
               MAKEDWORD(index, code), (void *)win);
  if (code == RVN_SELCHANGE)
    tableview_handle_master_selection(get_root_window(win), win);
}

// Keep contiguous pointer-backed storage valid after insert/update/delete.
bool rv_store_item(reportview_data_t *data, uint32_t i,
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
  snprintf(data->sections[i], sizeof(data->sections[i]), "%s", item->section ? item->section : "");
  dst.section = data->sections[i][0] ? data->sections[i] : NULL;

  data->items[i] = dst;
  return true;
}

void rv_rebind_item_refs(reportview_data_t *data, uint32_t start) {
  if (!data || start >= data->count)
    return;

  for (uint32_t i = start; i < data->count; i++) {
    data->items[i].text = data->names[i];
    if (data->items[i].subitem_count > REPORTVIEW_MAX_SUBITEMS)
      data->items[i].subitem_count = REPORTVIEW_MAX_SUBITEMS;
    for (uint32_t s = 0; s < REPORTVIEW_MAX_SUBITEMS; s++)
      data->items[i].subitems[s] = data->subnames[i][s];
    data->items[i].section = data->sections[i][0] ? data->sections[i] : NULL;
  }
}

void rv_reset_view_state(window_t *win, reportview_data_t *data) {
  data->selected = RV_INVALID_SELECTION;
  win->vscroll.pos = 0;
}

int rv_content_width(window_t *win) {
  irect16_t cr = get_client_rect(win);
  return cr.w;
}

int rv_report_header_height(const reportview_data_t *data) {
  return (!data || data->column_titles_visible) ? HEADER_HEIGHT : 0;
}

int rv_report_footer_height(const reportview_data_t *data) {
  return data && data->footer_visible ? HEADER_HEIGHT : 0;
}

int rv_get_report_column_width(reportview_data_t *data, int col, int avail_w) {
  if (!data || col < 0 || col >= (int)data->column_count)
    return 80;
  if (data->cell_style == REPORTVIEW_CELL_TWO_LINE)
    return col == 0 ? avail_w : 0;
  // If column has a fixed width spec (>0), return it
  if (data->columns[col].width_spec != 0)
    return (int)data->columns[col].width_spec;
  // Flex column (width_spec == 0): calculate from available width
  // Count how many flex columns there are
  int flex_count = 0;
  int fixed_total = 0;
  for (uint32_t i = 0; i < data->column_count; i++) {
    if (data->columns[i].width_spec == 0)
      flex_count++;
    else
      fixed_total += (int)data->columns[i].width_spec;
  }
  if (flex_count == 0)
    return avail_w;
  int flex_avail = avail_w - fixed_total;
  if (flex_avail < flex_count) flex_avail = flex_count; // Minimum 1px per flex column
  int share = flex_avail / flex_count;
  int min_w = data->columns[col].min_width > 0 ? (int)data->columns[col].min_width : 0;
  return share > min_w ? share : min_w;
}

int rv_report_total_width(reportview_data_t *data, int avail_w) {
  int total = 0;
  if (!data || data->column_count == 0)
    return avail_w;
  if (data->cell_style == REPORTVIEW_CELL_TWO_LINE)
    return avail_w;
  for (uint32_t i = 0; i < data->column_count; i++)
    total += rv_get_report_column_width(data, (int)i, avail_w);
  return total;
}

// Large-icon geometry helpers.
// cell_h is derived from the icon size and font height so that the label
// always fits neatly below the thumbnail.
int rv_large_icon_cell_h(const reportview_data_t *data) {
  return data->icon_size
       + RV_LARGE_ICON_TOP_PAD
       + RV_LARGE_ICON_LABEL_GAP
      + text_char_height(FONT_SMALLEST)
       + RV_LARGE_ICON_BOT_PAD;
}

// Draw one icon from the per-instance strip centred inside icon_rect.
// If no strip is assigned (or icon_id is out of range) a small placeholder
// rectangle is drawn so the icon slot is always visually occupied.
void rv_draw_item_icon(bitmap_strip_t *strip, int icon_id,
                       irect16_t const *icon_rect, uint32_t col) {
  if (strip && strip->tex != 0 && strip->cols > 0) {
    int total = strip->cols * (strip->sheet_h / strip->icon_h);
    if (icon_id >= 0 && icon_id < total) {
      int scol = icon_id % strip->cols;
      int srow = icon_id / strip->cols;
      float u0 = (float)(scol * strip->icon_w) / (float)strip->sheet_w;
      float v0 = (float)(srow * strip->icon_h) / (float)strip->sheet_h;
      float u1 = u0 + (float)strip->icon_w / (float)strip->sheet_w;
      float v1 = v0 + (float)strip->icon_h / (float)strip->sheet_h;
      draw_sprite_region((int)strip->tex, *icon_rect,
                         UV_RECT(u0, v0, u1, v1), col, 0);
      return;
    }
  }
  // Fallback: draw a small placeholder square so the icon slot is not blank.
  // (draw_icon8_clipped is not used here because it renders theme icons, which
  // use a different index space than file-picker / custom icon_id_t values.)
  {
    const int ph = THEME_ICON_SIZE;  // 8 px — matches the smallest tile unit
    int px = icon_rect->x + (icon_rect->w - ph) / 2;
    int py = icon_rect->y + (icon_rect->h - ph) / 2;
    uint32_t dim = (col & 0x00FFFFFFu) | 0x60000000u;  // 38% opacity
    fill_rect(dim, R(px,        py,        ph, 1));
    fill_rect(dim, R(px,        py + ph-1, ph, 1));
    fill_rect(dim, R(px,        py + 1,    1,  ph - 2));
    fill_rect(dim, R(px + ph-1, py + 1,    1,  ph - 2));
  }
}

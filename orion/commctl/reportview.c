#include <stdlib.h>
#include <string.h>

#include "columnview_internal.h"
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/theme.h>
#include <orion/user/svg_icon_loader.h>

// Rows of a grouped list sit below their section captions, each caption one
// row tall; row_tops holds every row's top within the content.
static void report_layout_rows(reportview_data_t *data) {
  int entry_h = rv_entry_height(data), top = 0;
  data->has_sections = false;
  for (uint32_t i = 0; i < data->count; i++) {
    if (data->items[i].section) { data->has_sections = true; top += entry_h; }
    data->row_tops[i] = top;
    top += entry_h;
  }
  data->row_tops[data->count] = top;
}

static int report_row_top(reportview_data_t *data, int index) {
  return data->has_sections ? data->row_tops[index] : index * rv_entry_height(data);
}

static void report_sync_scroll(window_t *win, reportview_data_t *data) {
  report_layout_rows(data);
  window_set_scroll_content(win, 0, 0, (irect16_t){0},
                            (irect16_t){0, rv_report_header_height(data), 0, rv_report_footer_height(data)});
}

static irect16_t report_item_rect(window_t *win, reportview_data_t *data, int index) {
  irect16_t content = window_scroll_content_rect(win);
  return R(content.x, content.y + report_row_top(data, index), content.w, rv_entry_height(data));
}

// A click on a section caption hits no row.
static int report_hit_index(window_t *win, reportview_data_t *data, uint32_t wparam) {
  int my = (int)(int16_t)HIWORD(wparam);
  int origin = window_scroll_content_rect(win).y;
  irect16_t point = window_scroll_rect_to_client(win, R(0, my, 0, 0)), viewport = window_scroll_viewport(win);
  if (my < origin || point.y < viewport.y || point.y >= viewport.y + viewport.h) return -1;
  int y = my - origin, entry_h = rv_entry_height(data);
  if (!data->has_sections) {
    int row = y / entry_h;
    return rv_valid_index(data, row) ? row : RV_INVALID_SELECTION;
  }
  for (uint32_t i = 0; i < data->count; i++)
    if (y >= data->row_tops[i] && y < data->row_tops[i] + entry_h) return (int)i;
  return RV_INVALID_SELECTION;
}

static bool report_hit_checkbox(window_t *win, reportview_data_t *data,
                                uint32_t wparam, int *index_out) {
  if (!data || !(data->extended_style & RVS_EX_CHECKBOXES)) return false;
  int row = report_hit_index(win, data, wparam);
  if (!rv_valid_index(data, row)) return false;
  int mx = (int16_t)LOWORD(wparam);
  int first_x = window_scroll_content_rect(win).x + WIN_PADDING;
  if (mx < first_x || mx >= first_x + CHECKBOX_BOX_SIZE + CHECKBOX_GAP) return false;
  if (index_out) *index_out = row;
  return true;
}

static bool report_set_item_state(window_t *win, reportview_data_t *data,
                                  int index, uint32_t state, uint32_t mask,
                                  bool notify) {
  if (!rv_valid_index(data, index)) return false;
  uint32_t old = data->items[index].state;
  data->items[index].state = (old & ~mask) | (state & mask);
  if (old != data->items[index].state) {
    rv_invalidate(win, data);
    if (notify) rv_notify(win, data, index, RVN_ITEMCHECK);
  }
  return true;
}

static bool report_toggle_check(window_t *win, reportview_data_t *data, int index) {
  if (!rv_valid_index(data, index) || !(data->extended_style & RVS_EX_CHECKBOXES)) return false;
  bool checked = RV_STATEIMAGEINDEX(data->items[index].state) == 2;
  return report_set_item_state(win, data, index,
    RV_INDEXTOSTATEIMAGEMASK(checked ? 1 : 2), RVIS_STATEIMAGEMASK, true);
}

static int report_hit_column_edge(window_t *win, reportview_data_t *data,
                                  int eff_w, int mx, int my) {
#if REPORTVIEW_RESIZE_FULL_HEIGHT
  (void)win;
#else
  int header_h = rv_report_header_height(data);
  int vy = window_scroll_rect_to_client(win, R(mx, my, 0, 0)).y;
  if (vy < 0 || vy >= header_h) return -1;
#endif
  int col_x = 0;
  for (uint32_t col = 0; col < data->column_count; col++) {
    int col_w = rv_get_report_column_width(data, (int)col, eff_w);
    col_x += col_w;
    int dist = mx - col_x;
    if (dist < 0) dist = -dist;
    if (dist <= REPORTVIEW_RESIZE_HOT_ZONE)
      return (int)col;
  }
  return -1;
}

// The selection is strong while the list (or the table holding it, or a field
// driving it with edSetList) has the keyboard focus, and quiet otherwise, so
// the window shows where typing goes.
static ctrl_state_t report_selection_state(window_t *win) {
  for (window_t *f = g_ui_runtime.focused; f; f = f->parent)
    if (f == win) return CTRL_SELECTED;
  window_t *focused = g_ui_runtime.focused;
  if (focused && win->id && get_root_window(focused) == get_root_window(win) &&
      (uint32_t)send_message(focused, edGetList, 0, NULL) == win->id) return CTRL_SELECTED;
  return g_ui_runtime.focused && g_ui_runtime.focused == win->parent ? CTRL_SELECTED : CTRL_SELECTED | CTRL_INACTIVE;
}

static void report_paint(window_t *win, reportview_data_t *data) {
  irect16_t cr = get_client_rect(win);
  int eff_w = cr.w;
  int header_h = rv_report_header_height(data);
  int footer_h = rv_report_footer_height(data);
  int body_bottom = cr.h - footer_h;
  int body_h = window_scroll_viewport(win).h;
  int entry_h = rv_entry_height(data);
  int scroll_y = (int)win->vscroll.pos;
  uint32_t bg_col = get_sys_color(brColumnViewBg);

  report_layout_rows(data);
  int first_row = 0, last_row = 0;
  if (body_h > 0 && data->has_sections) {
    while (first_row < (int)data->count && report_row_top(data, first_row) + entry_h <= scroll_y) first_row++;
    last_row = first_row;
    while (last_row < (int)data->count && report_row_top(data, last_row) < scroll_y + body_h) last_row++;
  } else if (body_h > 0) {
    first_row = scroll_y / entry_h;
    last_row = (scroll_y + body_h + entry_h - 1) / entry_h;
  }
  if (first_row < 0) first_row = 0;
  if (last_row > (int)data->count) last_row = (int)data->count;

  uint32_t hdr_fg = get_sys_color(brTextNormal);
#if !REPORTVIEW_RESIZE_FULL_HEIGHT
  uint32_t sep_col = get_sys_color(brDarkEdge);
#endif

  // A framed list clips everything it draws to the frame's silhouette, so
  // the header and the rows that scroll under the corners stay inside it.
  int frame_radius = (data->extended_style & RVS_EX_FRAME) ? get_theme()->table.corner_radius : -1;
  if (frame_radius > 0) set_clip_rounded_rect(win, cr, frame_radius);
  fill_rect(bg_col, rect_trim_top(cr, header_h));
  ctrl_state_t selection = report_selection_state(win);
  // Section captions span the columns, in secondary text above their rows.
  for (int row = first_row; data->has_sections && row <= last_row && row < (int)data->count; row++) {
    if (!data->items[row].section) continue;
    int y = window_scroll_rect_to_client(win, report_item_rect(win, data, row)).y - entry_h;
    if (y + entry_h <= header_h || y >= body_bottom) continue;
    set_clip_rect(win, (irect16_t){0, header_h, eff_w, MAX(0, body_bottom - header_h)});
    irect16_t caption = {WIN_PADDING * 2, y, eff_w - WIN_PADDING * 4, entry_h};
    draw_text_clipped(FONT_SMALLEST, data->items[row].section, &caption, get_sys_color(brTextSecondary), 0);
  }

  if (data->selected >= first_row && data->selected < last_row) {
    int y = window_scroll_rect_to_client(win, report_item_rect(win, data, data->selected)).y;
    int top = MAX(y, header_h), bottom = MIN(y + entry_h, body_bottom);
    theme_draw(THEME_PART_LIST_ITEM, R(0, top, eff_w, bottom - top), selection);
  }

  int col_x = 0;
  uint32_t draw_columns = data->cell_style == REPORTVIEW_CELL_TWO_LINE ? 1 : data->column_count;
  for (uint32_t col = 0; col < draw_columns; col++) {
    int col_w = data->cell_style == REPORTVIEW_CELL_TWO_LINE
              ? eff_w : rv_get_report_column_width(data, (int)col, eff_w);
    int draw_x = window_scroll_rect_to_client(win, R(col_x, 0, 0, 0)).x;
    int clip_x = MAX(0, draw_x);
    int clip_r = MIN(eff_w, draw_x + col_w);
    int clip_w = clip_r - clip_x;
    if (clip_w <= 0) {
      col_x += col_w;
      continue;
    }
    if (header_h > 0) {
      set_clip_rect(win, (irect16_t){clip_x, 0, clip_w, header_h});
      theme_draw(THEME_PART_HEADER, R(draw_x, 0, col_w, header_h), CTRL_NORMAL);
      char title[MAX_REPORTVIEW_TITLE + 4];
      const char *shown = data->columns[col].title;
      int title_w = col_w - 2 * WIN_PADDING;
      if (strwidth(shown) > title_w) { text_ellipsize(FONT_SYSTEM, shown, MAX(0, title_w), title, sizeof(title)); shown = title; }
      draw_text_small_clipped(shown,
                              &(irect16_t){draw_x, 0, col_w - (data->columns[col].align ? WIN_PADDING : 0), header_h},
                              hdr_fg, data->columns[col].align ? data->columns[col].align : TEXT_PADDING_LEFT);
    }

    set_clip_rect(win, (irect16_t){clip_x, header_h, clip_w, MAX(0, body_bottom - header_h)});
    for (int row = first_row; row < last_row; row++) {
      reportview_item_t *it = &data->items[row];
      bool strong = row == data->selected && !(selection & CTRL_INACTIVE);
      uint32_t fg = strong ? theme_foreground(THEME_PART_LIST_ITEM, selection)
                  : it->color ? it->color : get_sys_color(brTextNormal);
      int y = window_scroll_rect_to_client(win, report_item_rect(win, data, row)).y;
      const char *src = "";
      if (col == 0) {
        src = it->text ? it->text : "";
      } else {
        uint32_t idx = col - 1;
        src = (idx < it->subitem_count && it->subitems[idx]) ? it->subitems[idx] : "";
      }
      int text_x = draw_x + WIN_PADDING;
      if (col == 0 && it->icon_name && data->cell_style != REPORTVIEW_CELL_TWO_LINE) {
        sysicon_resolved_t res;
        irect16_t icon = {text_x, y + (entry_h - ICON_OFFSET) / 2, ICON_OFFSET, ICON_OFFSET};
        if (sysicon_resolve(it->icon_name, &res))
          draw_sprite_region((int)res.tex, icon, UV_RECT(res.u0, res.v0, res.u1, res.v1),
                             !strong && it->icon_color ? it->icon_color : fg, 0);
        text_x += ICON_OFFSET + WIN_PADDING;
      }
      if (col == 0 && (data->extended_style & RVS_EX_CHECKBOXES)) {
        irect16_t box = {text_x,
                         y + MAX(0, (entry_h - CHECKBOX_BOX_SIZE) / 2),
                         CHECKBOX_BOX_SIZE, CHECKBOX_BOX_SIZE};
        bool cb_checked = RV_STATEIMAGEINDEX(it->state) == 2;
        ctrl_state_t cb_state = cb_checked ? CTRL_SELECTED : CTRL_NORMAL;
        theme_draw(THEME_PART_CHECKBOX, box, cb_state);
        text_x += CHECKBOX_BOX_SIZE + CHECKBOX_GAP;
      }
      int text_w = MAX(0, draw_x + col_w - text_x);
      if (data->cell_style == REPORTVIEW_CELL_TWO_LINE) {
        irect16_t title_rect = {text_x, y + 1, text_w, FONT_SIZE_SMALL};
        irect16_t subtitle_rect = {text_x, y + FONT_SIZE_SMALL + 3,
                                   text_w, FONT_SIZE_SMALL};
        draw_text_clipped(FONT_SMALL, src, &title_rect, fg, 0);
        char subtitle[512] = {0};
        for (uint32_t sub = 0; sub < data->items[row].subitem_count; sub++) {
          const char *part = data->items[row].subitems[sub];
          if (!part || !part[0]) continue;
          size_t used = strlen(subtitle);
          snprintf(subtitle + used, sizeof(subtitle) - used, "%s%s",
                   used ? " - " : "", part);
        }
        draw_text_clipped(FONT_SMALLEST, subtitle, &subtitle_rect,
                          row == data->selected ? fg : get_sys_color(brTextDisabled), 0);
      } else {
        // Text that does not fit ends in an ellipsis; numbers align right.
        char fitted[MAX_COLUMNVIEW_ITEM_NAME];
        int pad = data->columns[col].align ? WIN_PADDING : 0;
        if (text_strwidth(FONT_SMALL, src) > text_w - pad) {
          text_ellipsize(FONT_SMALL, src, MAX(0, text_w - pad), fitted, sizeof(fitted));
          src = fitted;
        }
        irect16_t text_rect = {text_x, y, MAX(0, text_w - pad), entry_h};
        draw_text_clipped(FONT_SMALL, src, &text_rect, fg, data->columns[col].align);
      }
    }

    if (footer_h > 0) {  // totals band: a header face pinned under the rows
      set_clip_rect(win, (irect16_t){clip_x, body_bottom, clip_w, footer_h});
      theme_draw(THEME_PART_HEADER, R(draw_x, body_bottom, col_w, footer_h), CTRL_NORMAL);
    }

    col_x += col_w;
  }

  set_clip_rect(win, (irect16_t){0, 0, eff_w, cr.h});
  col_x = 0;
  int divider_w = get_theme()->report.divider_width;
  for (uint32_t col = 0; col < data->column_count; col++) {
    int col_w = data->cell_style == REPORTVIEW_CELL_TWO_LINE
              ? eff_w : rv_get_report_column_width(data, (int)col, eff_w);
    col_x += col_w;
    int sep_x = window_scroll_rect_to_client(win, R(col_x, 0, 0, 0)).x;
    ctrl_state_t divider_state = (int)col == data->resize_hot_col && data->resize_col < 0 ? CTRL_HOVER : CTRL_NORMAL;
#if REPORTVIEW_RESIZE_FULL_HEIGHT
    if (sep_x >= 0 && sep_x <= eff_w && !data->has_sections)   // a grouped list reads as one column of rows
      theme_draw(THEME_PART_COLUMN_DIVIDER, R(sep_x - divider_w / 2, 0, divider_w, cr.h), divider_state);
#else
    if (sep_x >= 0 && sep_x <= eff_w && !data->has_sections)   // a grouped list reads as one column of rows
      fill_rect(sep_col, R(sep_x, header_h, 1, cr.h - header_h));
    if (header_h > 0 && sep_x >= 0 && sep_x <= eff_w)
      theme_draw(THEME_PART_COLUMN_DIVIDER, R(sep_x - divider_w / 2, 0, divider_w, header_h), divider_state);
#endif
  }

  // Totals text, aligned like its column. A left-aligned caption runs on
  // into the empty cells after it, as in a spreadsheet, over their dividers.
  col_x = 0;
  for (uint32_t col = 0; footer_h > 0 && col < draw_columns; col++) {
    int col_w = data->cell_style == REPORTVIEW_CELL_TWO_LINE ? eff_w : rv_get_report_column_width(data, (int)col, eff_w);
    int span_w = col_w;
    for (uint32_t next = col + 1; !data->columns[col].align && next < draw_columns && !data->footer[next][0]; next++)
      span_w += rv_get_report_column_width(data, (int)next, eff_w);
    int draw_x = window_scroll_rect_to_client(win, R(col_x, 0, 0, 0)).x;
    col_x += col_w;
    if (!data->footer[col][0]) continue;
    int clip_x = MAX(0, draw_x), clip_r = MIN(eff_w, draw_x + span_w);
    if (clip_r <= clip_x) continue;
    set_clip_rect(win, (irect16_t){clip_x, body_bottom, clip_r - clip_x, footer_h});
    if (span_w > col_w) theme_draw(THEME_PART_HEADER, R(draw_x, body_bottom, span_w - 1, footer_h), CTRL_NORMAL);
    char total[MAX_REPORTVIEW_TITLE + 4];
    const char *shown = data->footer[col];
    int pad = data->columns[col].align ? WIN_PADDING : 0, total_w = span_w - 2 * WIN_PADDING;
    if (text_strwidth(FONT_SMALL, shown) > total_w) { text_ellipsize(FONT_SMALL, shown, MAX(0, total_w), total, sizeof(total)); shown = total; }
    irect16_t total_rect = {draw_x + WIN_PADDING, body_bottom, MAX(0, span_w - WIN_PADDING - pad), footer_h};
    draw_text_clipped(FONT_SMALL, shown, &total_rect, hdr_fg, data->columns[col].align);
  }
  set_clip_rect(win, (irect16_t){0, 0, eff_w, cr.h});
  if (frame_radius >= 0) {
    set_clip_rounded_rect(win, cr, 0);
    stroke_rounded_rect(get_sys_color(brLightEdge), cr, frame_radius, 1);
  }
}

result_t win_reportview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  reportview_data_t *data = (reportview_data_t *)win->userdata2;

  switch (msg) {
    case evCreate: {
      data = calloc(1, sizeof(reportview_data_t));
    if (!data) return false;
      win->userdata2 = data;
      win->flags |= WINDOW_HSCROLL;
      win->flags |= WINDOW_VSCROLL;
      win->flags |= WINDOW_FLEXSPACE;
      win->hscroll.visible_mode = SB_VIS_AUTO;
      win->vscroll.visible_mode = SB_VIS_AUTO;
      data->selected = -1;
      data->last_click_index = RV_INVALID_SELECTION;
      data->resize_col = -1;
      data->resize_hot_col = -1;
      data->column_width = DEFAULT_COLUMN_WIDTH;
      data->icon_size = DEFAULT_ICON_SIZE;
      data->icon_text_gap = DEFAULT_ICON_TEXT_GAP;
      data->redraw_enabled = true;
      data->redraw_dirty = false;
      data->column_titles_visible = true;
      data->preserve_icon_colors = false;
      report_sync_scroll(win, data);
      return true;
    }
    case evMeasureScrollContent: {
      layout_measure_t *m = lparam;
      m->desired_w = rv_report_total_width(data, m->avail_w);
      report_layout_rows(data);
      m->desired_h = report_row_top(data, (int)data->count);
      return true;
    }
    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (!m) return true;
      int min_h = rv_report_header_height(data) + rv_entry_height(data) + rv_report_footer_height(data);
      int min_w = 1;
      if (data && data->column_count > 0) {
        int cols_w = 0;
        for (uint32_t i = 0; i < data->column_count; i++) {
          if (i > 0) cols_w += 1;
          cols_w += data->columns[i].width > 0 ? (int)data->columns[i].width : (int)data->column_width;
        }
        if (cols_w > 0) min_w = MAX(min_w, cols_w);
      }
      m->desired_w = MAX(m->desired_w, min_w);
      m->desired_h = MAX(m->desired_h, min_h);
      if (data && (data->extended_style & RVS_EX_FITROWS)) {   // exactly its rows, as a popover shows them
        report_layout_rows(data);
        m->desired_h = rv_report_header_height(data) + report_row_top(data, (int)data->count) + rv_report_footer_height(data);
      }
      return true;
    }
    case evPaint:
      report_paint(win, data);
      return false;
    case evMouseMove: {
      if (!data) return false;
      irect16_t cr = get_client_rect(win);
      int mx = (int16_t)LOWORD(wparam), my = (int16_t)HIWORD(wparam);
      // Drag: update column width
      if (data->resize_col >= 0) {
        int delta = mx - data->resize_start_x;
        int new_w = data->resize_start_w + delta;
        int min_w = data->columns[data->resize_col].min_width > 0
                      ? (int)data->columns[data->resize_col].min_width : 20;
        if (new_w < min_w) new_w = min_w;
        data->columns[data->resize_col].width_spec = (uint32_t)new_w;
        data->columns[data->resize_col].width = (uint32_t)new_w;
        report_sync_scroll(win, data);
        rv_invalidate(win, data);
        return true;
      }
      // Hover: track column edge
      int hot = report_hit_column_edge(win, data, cr.w, mx, my);
      if (hot != data->resize_hot_col) {
        data->resize_hot_col = hot;
        rv_invalidate(win, data);
      }
      axSetCursor((hot >= 0 || data->resize_col >= 0) ? curResizeH : curArrow);
      track_mouse(win);
      return false;
    }
    case evMouseLeave: {
      if (data && data->resize_hot_col >= 0) {
        data->resize_hot_col = -1;
        rv_invalidate(win, data);
      }
      return false;
    }
    case evQueryDrag: // column edges resize; rows scroll
      return data && report_hit_column_edge(win, data, get_client_rect(win).w,
                                            (int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)) >= 0 ? DRAG_NOW : DRAG_NONE;
    case evLeftButtonDown: {
      // Start column resize drag if on a column edge
      irect16_t cr = get_client_rect(win);
      int mx = (int16_t)LOWORD(wparam);
      int my = (int16_t)HIWORD(wparam);
      int col = report_hit_column_edge(win, data, cr.w, mx, my);
      if (col >= 0) {
        data->resize_col = col;
        data->resize_start_x = mx;
        data->resize_start_w = (int)data->columns[col].width;
        set_capture(win);
        return true;
      }
      // Otherwise select row; the state image toggles independently of row selection.
      int index = report_hit_index(win, data, wparam);
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
        int checked_index = -1;
        if (report_hit_checkbox(win, data, wparam, &checked_index)) {
          report_toggle_check(win, data, checked_index);
        }
      }
      return true;
    }
    case evLeftButtonDoubleClick: {
      int index = report_hit_index(win, data, wparam);
      if (rv_valid_index(data, index)) {
        rv_reset_click_state(data);
        rv_notify(win, data, index, RVN_DBLCLK);
      }
      return true;
    }
    case evLeftButtonUp: {
      if (data && data->resize_col >= 0) {
        data->resize_col = -1;
        data->resize_hot_col = -1;
        set_capture(NULL);
        return true;
      }
      return false;
    }
    case RVM_ADDITEM: {
      reportview_item_t *item = (reportview_item_t *)lparam;
      if (!item || data->count >= MAX_COLUMNVIEW_ITEMS)
        return -1;
      uint32_t i = data->count;
      if (!rv_store_item(data, i, item))
        return -1;
      data->count++;
      report_sync_scroll(win, data);
      rv_invalidate(win, data);
      return (result_t)i;
    }
    case RVM_DELETEITEM: {
      if (wparam >= data->count)
        return false;
      memmove(data->items + wparam, data->items + wparam + 1, (data->count - wparam - 1) * sizeof(data->items[0]));
      memmove(data->names + wparam, data->names + wparam + 1, (data->count - wparam - 1) * sizeof(data->names[0]));
      memmove(data->subnames + wparam, data->subnames + wparam + 1, (data->count - wparam - 1) * sizeof(data->subnames[0]));
      memmove(data->sections + wparam, data->sections + wparam + 1, (data->count - wparam - 1) * sizeof(data->sections[0]));
      data->count--;
      rv_rebind_item_refs(data, (uint32_t)wparam);
      if (data->selected == (int)wparam) data->selected = RV_INVALID_SELECTION;
      else if (data->selected > (int)wparam) data->selected--;
      report_sync_scroll(win, data);
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
        window_ensure_rect_visible(win, report_item_rect(win, data, data->selected), SB_VERT);
        rv_invalidate(win, data);
        return true;
      }
      return false;
    case RVM_CLEAR:
      data->count = 0;
      rv_reset_view_state(win, data);
      report_sync_scroll(win, data);
      rv_invalidate(win, data);
      return true;
    case RVM_SETCOLUMNWIDTH:
      if (wparam > 0) {
        data->column_width = (int)wparam;
        report_sync_scroll(win, data);
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
      return (result_t)report_hit_index(win, data, wparam);
    case RVM_SETITEMDATA: {
      reportview_item_t *item = (reportview_item_t *)lparam;
      if (!item || wparam >= data->count)
        return false;
      if (!rv_store_item(data, (uint32_t)wparam, item))
        return false;
      rv_invalidate(win, data);
      return true;
    }
    case RVM_SETEXTENDEDSTYLE: {
      uint32_t mask = wparam ? wparam : UINT32_MAX;
      uint32_t style = (uint32_t)(uintptr_t)lparam;
      uint32_t old = data->extended_style;
      data->extended_style = (old & ~mask) | (style & mask);
      if (!(old & RVS_EX_CHECKBOXES) && (data->extended_style & RVS_EX_CHECKBOXES)) {
        for (uint32_t i = 0; i < data->count; i++)
          data->items[i].state = RV_INDEXTOSTATEIMAGEMASK(1);
      }
      rv_invalidate(win, data);
      return (result_t)old;
    }
    case RVM_GETEXTENDEDSTYLE:
      return (result_t)data->extended_style;
    case RVM_SETCELLSTYLE:
      if (wparam != REPORTVIEW_CELL_COLUMNS && wparam != REPORTVIEW_CELL_TWO_LINE)
        return false;
      data->cell_style = (reportview_cell_style_t)wparam;
      if (data->cell_style == REPORTVIEW_CELL_TWO_LINE)
        data->column_titles_visible = false;
      report_sync_scroll(win, data);
      rv_invalidate(win, data);
      return true;
    case RVM_GETCELLSTYLE:
      return (result_t)data->cell_style;
    case RVM_SETFOOTER: {
      const char *const *texts = lparam;
      if (texts && wparam > MAX_REPORTVIEW_COLUMNS) {
        fprintf(stderr, "[reportview] footer texts beyond the column limit are dropped win=%u count=%u max=%d\n",
                win->id, (unsigned)wparam, MAX_REPORTVIEW_COLUMNS);
        fflush(stderr);
      }
      memset(data->footer, 0, sizeof(data->footer));
      for (uint32_t i = 0; texts && i < wparam && i < MAX_REPORTVIEW_COLUMNS; i++)
        snprintf(data->footer[i], sizeof(data->footer[i]), "%s", texts[i] ? texts[i] : "");
      data->footer_visible = texts != NULL;
      report_sync_scroll(win, data);
      rv_invalidate(win, data);
      return true;
    }
    case RVM_SETITEMSTATE: {
      reportview_item_state_t *st = (reportview_item_state_t *)lparam;
      if (!st) return false;
      if ((int32_t)wparam == -1) {
        for (uint32_t i = 0; i < data->count; i++)
          report_set_item_state(win, data, (int)i, st->state, st->state_mask, false);
        return true;
      }
      return report_set_item_state(win, data, (int)wparam,
                                   st->state, st->state_mask, false);
    }
    case RVM_GETITEMSTATE:
      return wparam < data->count
        ? (result_t)(data->items[wparam].state & (uint32_t)(uintptr_t)lparam) : 0;
    case RVM_SETVIEWMODE:
      return wparam == RVM_VIEW_REPORT;
    case RVM_ADDCOLUMN: {
      reportview_column_t *col = (reportview_column_t *)lparam;
      if (!col || data->column_count >= MAX_REPORTVIEW_COLUMNS) {
        fprintf(stderr, "[reportview] add column rejected win=%u count=%u max=%d title=%s\n", win->id,
                data->column_count, MAX_REPORTVIEW_COLUMNS, col && col->title ? col->title : "");
        fflush(stderr);
        return -1;
      }
      uint32_t i = data->column_count;
      strncpy(data->columns[i].title, col->title ? col->title : "", MAX_REPORTVIEW_TITLE - 1);
      data->columns[i].title[MAX_REPORTVIEW_TITLE - 1] = '\0';
      data->columns[i].width_spec = col->width;  // Store original spec
      data->columns[i].width = col->width;        // Initial effective width
      data->columns[i].min_width = col->min_width;
      data->columns[i].align = col->align;
      data->column_count++;

      // Keep effective widths in sync when columns are added after layout.
      // TableView populates columns during refresh, which can happen after the
      // initial evArrange/evResize pass.
      irect16_t cr = get_client_rect(win);
      if (cr.w > 0) {
        for (uint32_t ci = 0; ci < data->column_count; ci++) {
          int w = rv_get_report_column_width(data, (int)ci, cr.w);
          data->columns[ci].width = (uint32_t)w;
        }
      }
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
    }
    case RVM_GETREPORTCOLUMNWIDTH: {
      uint32_t ci = (uint32_t)wparam;
      if (ci >= data->column_count)
        return 0;
      return (result_t)data->columns[ci].width;
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
      rv_invalidate(win, data);
      return true;
    case RVM_SETICONSIZE:
      data->icon_size = (int)wparam;
      rv_invalidate(win, data);
      return true;
    case RVM_SETCOLUMNTITLESVISIBLE:
      data->column_titles_visible = (wparam != 0);
      report_sync_scroll(win, data);
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
    case evHScroll:
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
      // Recalculate flex column widths after arranging
      irect16_t cr = get_client_rect(win);
      if (cr.w > 0 && data->column_count > 0) {
        for (uint32_t i = 0; i < data->column_count; i++) {
          int w = rv_get_report_column_width(data, (int)i, cr.w);
          data->columns[i].width = (uint32_t)w;
        }
      }
      report_sync_scroll(win, data);
      return true;
    }
    case evResize: {
      // Recalculate flex column widths when window resizes
      irect16_t cr = get_client_rect(win);
      if (cr.w <= 0) {
        // Window not sized yet - skip column width calculation
        report_sync_scroll(win, data);
        return false;
      }
      int avail_w = cr.w;
      for (uint32_t i = 0; i < data->column_count; i++) {
        int w = rv_get_report_column_width(data, (int)i, avail_w);
        data->columns[i].width = (uint32_t)w;
      }
      report_sync_scroll(win, data);
      rv_invalidate(win, data);
      return false;
    }
    case evKeyDown: {
      if (!data || data->count == 0)
        return false;
      int count = (int)data->count;
      int cur = data->selected;
      int next = cur;
      switch (wparam) {
        case AX_KEY_UPARROW:
        case AX_KEY_LEFTARROW:
          next = (cur <= 0) ? 0 : cur - 1;
          break;
        case AX_KEY_DOWNARROW:
        case AX_KEY_RIGHTARROW:
          next = (cur < 0) ? 0 : (cur + 1 < count ? cur + 1 : cur);
          break;
        case AX_KEY_ENTER:
          if (cur < 0) return false;
          rv_notify(win, data, cur, RVN_DBLCLK);
          return true;
        case AX_KEY_SPACE:
          if (cur < 0 || !(data->extended_style & RVS_EX_CHECKBOXES)) return false;
          return report_toggle_check(win, data, cur);
        case AX_KEY_DEL:
          if (cur < 0) return false;
          rv_notify(win, data, cur, RVN_DELETE);
          return true;
        default:
          return false;
      }
      if (next != cur && next >= 0) {
        data->selected = next;
        window_ensure_rect_visible(win, report_item_rect(win, data, next), SB_VERT);
        rv_notify(win, data, next, RVN_SELCHANGE);
        rv_invalidate(win, data);
      }
      return true;
    }
    case evGetCursor: {
      if (!data)
        return curArrow;
      if (data->resize_col >= 0)
        return curResizeH;
      irect16_t cr = get_client_rect(win);
      int mx = (int16_t)LOWORD(wparam);
      int my = (int16_t)HIWORD(wparam);
      if (report_hit_column_edge(win, data, cr.w, mx, my) >= 0)
        return curResizeH;
      return curArrow;
    }
    case evDestroy:
      free(data);
      win->userdata2 = NULL;
      return true;
    default:
      return false;
  }
}

#include <stdlib.h>
#include <string.h>

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include "commctl.h"
#include "layout_shared.h"

extern void layout_stack_measure_window(window_t *win, layout_measure_t *m);
extern void layout_stack_arrange_window(window_t *win, const irect16_t *rect);
extern void window_layout_sync(window_t *win);

void layout_grid_measure_window(window_t *win, layout_measure_t *m) {
  if (!win || !m) return;
  irect16_t cr = get_client_rect(win);
  if (m->avail_w > 0) cr.w = m->avail_w;
  if (m->avail_h > 0) cr.h = m->avail_h;
  irect16_t content = layout_content_rect(win, cr);
  int content_w = content.w;
  int content_h = content.h;
  int cols = layout_child_count(win);
  if (cols == 0) {
    irect16_t pad = layout_padding_for(win);
    m->desired_w = content.w + pad.x + pad.w;
    m->desired_h = content.h + pad.y + pad.h;
    return;
  }
  int gap = layout_spacing_for(win);
  int *col_w = calloc((size_t)cols, sizeof(int));
  int *col_counts = calloc((size_t)cols, sizeof(int));
  bool *col_auto = calloc((size_t)cols, sizeof(bool));
  if (!col_w || !col_counts || !col_auto) {
    free(col_w);
    free(col_counts);
    free(col_auto);
    irect16_t pad = layout_padding_for(win);
    m->desired_w = content.w + pad.x + pad.w;
    m->desired_h = content.h + pad.y + pad.h;
    return;
  }
  int rows = 0;
  for (int col = 0; col < cols; col++) {
    window_t *column = layout_child_at(win, col);
    int row_count = layout_child_count(column);
    col_counts[col] = row_count;
    if (row_count > rows) rows = row_count;
    if (column && column->layout.layout_fixed_w > 0) {
      col_w[col] = column->layout.layout_fixed_w;
      col_auto[col] = false;
    } else {
      col_auto[col] = !column || column->layout.layout_fixed_w == 0;
    }
    for (int row = 0; row < row_count; row++) {
      window_t *cell = layout_child_at(column, row);
      if (!cell) continue;
      layout_measure_t cm = layout_measure_child(cell, content_w, content_h);
      if ((col_auto[col] || column->layout.layout_fixed_w < 0) && cm.desired_w > col_w[col]) col_w[col] = cm.desired_w;
    }
  }
  int total_fixed_w = 0;
  int auto_cols = 0;
  for (int col = 0; col < cols; col++) {
    if (col_auto[col]) auto_cols++;
    else total_fixed_w += col_w[col];
  }
  int total_gap = (cols > 0) ? gap * (cols - 1) : 0;
  int available_for_auto = content.w - total_fixed_w - total_gap;
  if (available_for_auto < 0) available_for_auto = 0;
  int star_width = auto_cols > 0 ? available_for_auto / auto_cols : 0;
  int extra = auto_cols > 0 ? available_for_auto % auto_cols : 0;
  for (int col = 0; col < cols; col++) {
    if (!col_auto[col]) continue;
    col_w[col] = star_width;
    if (extra > 0) {
      col_w[col]++;
      extra--;
    }
  }
  int *row_h = rows > 0 ? calloc((size_t)rows, sizeof(int)) : NULL;
  bool *row_flex = rows > 0 ? calloc((size_t)rows, sizeof(bool)) : NULL;
  if (rows > 0 && (!row_h || !row_flex)) {
    free(col_w);
    free(col_counts);
    free(col_auto);
    free(row_h);
    free(row_flex);
    irect16_t pad = layout_padding_for(win);
    m->desired_w = content.w + pad.x + pad.w;
    m->desired_h = content.h + pad.y + pad.h;
    return;
  }
  for (int col = 0; col < cols; col++) {
    window_t *column = layout_child_at(win, col);
    int row_count = col_counts[col];
    for (int row = 0; row < row_count; row++) {
      window_t *cell = layout_child_at(column, row);
      if (!cell) continue;
      layout_measure_t cm = layout_measure_child(cell, col_w[col], content_h);
      if (cm.desired_h > row_h[row]) row_h[row] = cm.desired_h;
      if (cell->layout.layout_fixed_h > row_h[row]) row_h[row] = cell->layout.layout_fixed_h;
      if (layout_child_is_flex(cell))
        row_flex[row] = true;
    }
  }
  int total_h = 0;
  int flex_rows = 0;
  for (int row = 0; row < rows; row++) {
    if (row > 0) total_h += gap;
    total_h += row_h[row];
    if (row_flex[row]) flex_rows++;
  }
  int desired_total_h = total_h;
  int remaining_h = content.h - total_h;
  if (remaining_h < 0) remaining_h = 0;
  if (flex_rows > 0 && remaining_h > 0) {
    int base = remaining_h / flex_rows;
    int extra_h = remaining_h % flex_rows;
    for (int row = 0; row < rows; row++) {
      if (!row_flex[row]) continue;
      row_h[row] += base;
      if (extra_h > 0) {
        row_h[row]++;
        extra_h--;
      }
    }
    total_h = 0;
    for (int row = 0; row < rows; row++) {
      if (row > 0) total_h += gap;
      total_h += row_h[row];
    }
  }
  int total_w = 0;
  for (int col = 0; col < cols; col++) {
    if (col > 0) total_w += gap;
    total_w += col_w[col];
  }
  irect16_t pad = layout_padding_for(win);
  m->desired_w = total_w + pad.x + pad.w;
  m->desired_h = desired_total_h + pad.y + pad.h;
  free(col_w);
  free(col_counts);
  free(col_auto);
  free(row_h);
  free(row_flex);
}

void layout_grid_arrange_window(window_t *win, const irect16_t *rect) {
  if (!win) return;
  irect16_t cr = rect ? *rect : get_client_rect(win);
  irect16_t content = layout_content_rect(win, cr);
  int cols = layout_child_count(win);
  if (cols == 0) return;
  int gap = layout_spacing_for(win);
  int rows = 0;
  int *col_counts = calloc((size_t)cols, sizeof(int));
  int *col_w = calloc((size_t)cols, sizeof(int));
  bool *col_auto = calloc((size_t)cols, sizeof(bool));
  if (!col_counts || !col_w || !col_auto) {
    free(col_counts);
    free(col_w);
    free(col_auto);
    return;
  }
  for (int col = 0; col < cols; col++) {
    window_t *column = layout_child_at(win, col);
    int row_count = layout_child_count(column);
    col_counts[col] = row_count;
    if (row_count > rows) rows = row_count;
    if (column && column->layout.layout_fixed_w > 0) {
      col_w[col] = column->layout.layout_fixed_w;
      col_auto[col] = false;
    } else {
      col_auto[col] = !column || column->layout.layout_fixed_w == 0;
    }
    for (int row = 0; row < row_count; row++) {
      window_t *cell = layout_child_at(column, row);
      if (!cell) continue;
      layout_measure_t cm = layout_measure_child(cell, content.w, content.h);
      if ((col_auto[col] || column->layout.layout_fixed_w < 0) && cm.desired_w > col_w[col]) col_w[col] = cm.desired_w;
    }
  }
  int total_fixed = 0;
  int auto_count = 0;
  for (int col = 0; col < cols; col++) {
    if (col_auto[col]) auto_count++;
    else total_fixed += col_w[col];
  }
  int total_gap = (cols > 0) ? gap * (cols - 1) : 0;
  int available_for_auto = content.w - total_fixed - total_gap;
  if (available_for_auto < 0) available_for_auto = 0;
  int star_width = auto_count > 0 ? available_for_auto / auto_count : 0;
  int extra = auto_count > 0 ? available_for_auto % auto_count : 0;
  for (int col = 0; col < cols; col++) {
    if (!col_auto[col]) continue;
    col_w[col] = star_width;
    if (extra > 0) {
      col_w[col]++;
      extra--;
    }
  }
  layout_measure_t *cellm = rows > 0 ? calloc((size_t)cols * (size_t)rows, sizeof(layout_measure_t)) : NULL;
  int *row_h = rows > 0 ? calloc((size_t)rows, sizeof(int)) : NULL;
  bool *row_flex = rows > 0 ? calloc((size_t)rows, sizeof(bool)) : NULL;
  if (rows > 0 && (!cellm || !row_h || !row_flex)) {
    free(col_counts);
    free(col_w);
    free(col_auto);
    free(cellm);
    free(row_h);
    free(row_flex);
    return;
  }
  for (int col = 0; col < cols; col++) {
    window_t *column = layout_child_at(win, col);
    int row_count = col_counts[col];
    for (int row = 0; row < row_count; row++) {
      window_t *cell = layout_child_at(column, row);
      if (!cell) continue;
      layout_measure_t cm = layout_measure_child(cell, col_w[col], content.h);
      cellm[(size_t)col * (size_t)rows + (size_t)row] = cm;
      if (cm.desired_h > row_h[row]) row_h[row] = cm.desired_h;
      if (cell->layout.layout_fixed_h > row_h[row]) row_h[row] = cell->layout.layout_fixed_h;
      if (layout_child_is_flex(cell))
        row_flex[row] = true;
    }
  }
  int total_h = 0;
  int flex_rows = 0;
  for (int row = 0; row < rows; row++) {
    if (row > 0) total_h += gap;
    total_h += row_h[row];
    if (row_flex[row]) flex_rows++;
  }
  int remaining_h = content.h - total_h;
  if (remaining_h < 0) remaining_h = 0;
  if (flex_rows > 0 && remaining_h > 0) {
    int base = remaining_h / flex_rows;
    int extra_h = remaining_h % flex_rows;
    for (int row = 0; row < rows; row++) {
      if (!row_flex[row]) continue;
      row_h[row] += base;
      if (extra_h > 0) {
        row_h[row]++;
        extra_h--;
      }
    }
  }
  int *row_y = rows > 0 ? calloc((size_t)rows, sizeof(int)) : NULL;
  if (rows > 0 && !row_y) {
    free(col_counts);
    free(col_w);
    free(col_auto);
    free(cellm);
    free(row_h);
    free(row_flex);
    return;
  }
  int y = content.y;
  for (int row = 0; row < rows; row++) {
    row_y[row] = y;
    y += row_h[row];
    if (row + 1 < rows) y += gap;
  }
  int x = content.x;
  for (int col = 0; col < cols; col++) {
    window_t *column = layout_child_at(win, col);
    int cw = col_w[col];
    if (!column) {
      x += cw;
      if (col + 1 < cols)
        x += gap;
      continue;
    }
    column->frame = R(x, content.y, cw, content.h);
    int row_count = col_counts[col];
    for (int row = 0; row < row_count; row++) {
      window_t *cell = layout_child_at(column, row);
      if (!cell) continue;
      layout_measure_t cm = cellm[(size_t)col * (size_t)rows + (size_t)row];
      int ch = row_h[row];
      int cell_w = layout_apply_alignment(cw, cm.desired_w, cell->layout.h_align);
      int cell_h = layout_apply_alignment(ch, cm.desired_h, cell->layout.v_align);
      int cx = x;
      int cy = row_y[row];
      if (cell->layout.h_align == LAYOUT_ALIGN_CENTER)
        cx += (cw - cell_w) / 2;
      else if (cell->layout.h_align == LAYOUT_ALIGN_END)
        cx += cw - cell_w;
      if (cell->layout.v_align == LAYOUT_ALIGN_CENTER)
        cy += (ch - cell_h) / 2;
      else if (cell->layout.v_align == LAYOUT_ALIGN_END)
        cy += ch - cell_h;
      layout_arrange_child(cell, R(cx - x, cy - content.y, cell_w, cell_h));
    }
    x += cw;
    if (col + 1 < cols)
      x += gap;
  }
  free(col_counts);
  free(col_w);
  free(col_auto);
  free(cellm);
  free(row_h);
  free(row_flex);
  free(row_y);
}

static result_t layout_init_default_grid_columns(window_t *win) {
  if (!win)
    return true;
  if (!win->children) {
    layout_view_config_t cfg = {
      .orientation = WINDOW_STACK_VERTICAL,
      .spacing = 4,
      .padding = (irect16_t){0, 0, 0, 0},
      .margin = (irect16_t){0, 0, 0, 0},
    };
    for (int i = 0; i < 2; i++) {
      irect16_t frame = {0, 0, 0, 0};
      window_t *col = create_window("",
          WINDOW_NOTITLE | WINDOW_NOFILL,
          &frame,
          win, win_column, win->hinstance, &cfg);
      if (col)
        col->id = (uint32_t)(i + 1);
    }
  }
  send_message(win, evResize, 0, NULL);
  return true;
}

result_t win_column(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCanParent: {
      (void)wparam;
      window_t *target = (window_t *)lparam;
      if (!target) return true;
      return !(target->proc == win_grid || target->proc == win_gridview);
    }
    case evCreate: {
      win->flags |= WINDOW_AUTO_LAYOUT;
      win->flags &= ~WINDOW_STACK_HORIZONTAL;
      win->layout.layout_spacing = 4;
      win->layout.layout_padding = (irect16_t){0, 0, 0, 0};
      win->layout.layout_margin = (irect16_t){0, 0, 0, 0};
      return true;
    }
    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (!m)
        return MAKEDWORD(1, 1);
      layout_stack_measure_window(win, m);
      if (win->layout.layout_fixed_w > m->desired_w) m->desired_w = win->layout.layout_fixed_w;
      if (win->layout.layout_fixed_h > m->desired_h) m->desired_h = win->layout.layout_fixed_h;
      if (m->desired_w < 1) m->desired_w = 1;
      if (m->desired_h < 1) m->desired_h = 1;
      return MAKEDWORD((uint16_t)m->desired_w, (uint16_t)m->desired_h);
    }
    case evArrange: {
      layout_arrange_t *a = (layout_arrange_t *)lparam;
      if (a) {
        win->frame = a->rect;
        irect16_t cr = get_client_rect(win);
        layout_stack_arrange_window(win, &cr);
      }
      return MAKEDWORD((uint16_t)MAX(1, win->frame.w),
                       (uint16_t)MAX(1, win->frame.h));
    }
    case evResize:
      {
        irect16_t cr = get_client_rect(win);
        layout_stack_arrange_window(win, &cr);
      }
      return true;
    case evPaint:
      layout_paint_children(win);
      return true;
    case evParentNotify:
      return win->parent ? send_message(win->parent, msg, wparam, lparam) : false;
    default:
      return false;
  }
}

result_t win_grid(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      win->flags |= WINDOW_AUTO_LAYOUT;
      win->flags &= ~WINDOW_STACK_HORIZONTAL;
      win->layout.layout_spacing = 0;
      win->layout.layout_padding = (irect16_t){0, 0, 0, 0};
      win->layout.layout_margin = (irect16_t){0, 0, 0, 0};
      
      if (lparam) {
        const form_ctrl_def_t *cd = (const form_ctrl_def_t *)lparam;
        if (cd->class_name && (uintptr_t)cd->class_name > 0x1000000) {
          if (cd->layout_spacing > 0)
            win->layout.layout_spacing = cd->layout_spacing;
          win->layout.layout_padding = cd->padding;
          win->layout.layout_margin = cd->margin;
        } else {
          const layout_view_config_t *cfg = (const layout_view_config_t *)lparam;
          if (cfg->spacing > 0)
            win->layout.layout_spacing = cfg->spacing;
          win->layout.layout_padding = cfg->padding;
          win->layout.layout_margin = cfg->margin;
        }
      }
      return true;
    }
    case evInitChildren:
      return layout_init_default_grid_columns(win);
    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (!m)
        return MAKEDWORD(1, 1);
      layout_grid_measure_window(win, m);
      if (m->desired_w < 1) m->desired_w = 1;
      if (m->desired_h < 1) m->desired_h = 1;
      return MAKEDWORD((uint16_t)m->desired_w, (uint16_t)m->desired_h);
    }
    case evArrange: {
      layout_arrange_t *a = (layout_arrange_t *)lparam;
      if (a) {
        win->frame = a->rect;
        irect16_t cr = get_client_rect(win);
        layout_grid_arrange_window(win, &cr);
      }
      return MAKEDWORD((uint16_t)MAX(1, win->frame.w),
                       (uint16_t)MAX(1, win->frame.h));
    }
    case evResize:
      {
        irect16_t cr = get_client_rect(win);
        layout_grid_arrange_window(win, &cr);
      }
      return true;
    case evPaint:
      layout_paint_children(win);
      return true;
    default:
      return false;
  }
}

result_t win_gridview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  return win_grid(win, msg, wparam, lparam);
}

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include "commctl.h"
#include "layout_shared.h"

extern void window_layout_sync(window_t *win);

static bool layout_flow_collect(window_t *win, int content_w, int content_h,
                                int **row_of_out, int **row_w_out, int **row_h_out,
                                layout_measure_t **cellm_out, int *rows_out) {
  int count = layout_child_count(win);
  int *row_of = count > 0 ? calloc((size_t)count, sizeof(int)) : NULL;
  int *row_w = count > 0 ? calloc((size_t)count, sizeof(int)) : NULL;
  int *row_h = count > 0 ? calloc((size_t)count, sizeof(int)) : NULL;
  layout_measure_t *cellm = count > 0 ? calloc((size_t)count, sizeof(layout_measure_t)) : NULL;
  if ((count > 0) && (!row_of || !row_w || !row_h || !cellm)) {
    free(row_of);
    free(row_w);
    free(row_h);
    free(cellm);
    return false;
  }

  int gap = layout_spacing_for(win);
  int wrap_w = content_w > 0 ? content_w : INT_MAX;
  int row = 0;
  int cur_w = 0;
  int cur_h = 0;
  bool row_empty = true;

  for (int i = 0; i < count; i++) {
    window_t *child = layout_child_at(win, i);
    if (!child) continue;
    layout_measure_t cm = layout_measure_child(child, content_w, content_h);
    if (cellm) cellm[i] = cm;
    int item_w = cm.desired_w;
    int item_h = cm.desired_h;
    if (!row_empty && cur_w + gap + item_w > wrap_w) {
      row_w[row] = cur_w;
      row_h[row] = cur_h;
      row++;
      cur_w = item_w;
      cur_h = item_h;
      row_empty = false;
    } else {
      if (!row_empty) cur_w += gap;
      cur_w += item_w;
      if (item_h > cur_h) cur_h = item_h;
      row_empty = false;
    }
    if (row_of) row_of[i] = row;
  }

  if (count > 0) {
    row_w[row] = cur_w;
    row_h[row] = cur_h;
  }

  if (rows_out) *rows_out = count > 0 ? row + 1 : 0;
  if (row_of_out) *row_of_out = row_of; else free(row_of);
  if (row_w_out) *row_w_out = row_w; else free(row_w);
  if (row_h_out) *row_h_out = row_h; else free(row_h);
  if (cellm_out) *cellm_out = cellm; else free(cellm);
  return true;
}

void layout_flow_measure_window(window_t *win, layout_measure_t *m) {
  if (!win || !m) return;
  irect16_t cr = get_client_rect(win);
  if (m->avail_w > 0) cr.w = m->avail_w;
  if (m->avail_h > 0) cr.h = m->avail_h;
  irect16_t content = layout_content_rect(win, cr);
  int *row_w = NULL;
  int *row_h = NULL;
  layout_measure_t *cellm = NULL;
  int rows = 0;
  if (!layout_flow_collect(win, content.w, content.h, NULL, &row_w, &row_h, &cellm, &rows)) {
    irect16_t pad = layout_padding_for(win);
    m->desired_w = content.w + pad.x + pad.w;
    m->desired_h = content.h + pad.y + pad.h;
    return;
  }

  int total_w = 0;
  int total_h = 0;
  int gap = layout_spacing_for(win);
  for (int row = 0; row < rows; row++) {
    if (row > 0) total_h += gap;
    total_h += row_h[row];
    if (row_w[row] > total_w) total_w = row_w[row];
  }
  irect16_t pad = layout_padding_for(win);
  m->desired_w = total_w + pad.x + pad.w;
  m->desired_h = total_h + pad.y + pad.h;
  free(row_w);
  free(row_h);
  free(cellm);
}

void layout_flow_arrange_window(window_t *win, const irect16_t *rect) {
  if (!win) return;
  irect16_t cr = rect ? *rect : get_client_rect(win);
  irect16_t content = layout_content_rect(win, cr);
  int *row_of = NULL;
  int *row_w = NULL;
  int *row_h = NULL;
  layout_measure_t *cellm = NULL;
  int rows = 0;
  int count = layout_child_count(win);
  if (!layout_flow_collect(win, content.w, content.h, &row_of, &row_w, &row_h, &cellm, &rows)) {
    return;
  }

  int gap = layout_spacing_for(win);
  int *row_y = rows > 0 ? calloc((size_t)rows, sizeof(int)) : NULL;
  if (rows > 0 && !row_y) {
    free(row_of);
    free(row_w);
    free(row_h);
    free(cellm);
    return;
  }

  int y = content.y;
  for (int row = 0; row < rows; row++) {
    row_y[row] = y;
    y += row_h[row];
    if (row + 1 < rows) y += gap;
  }

  int cur_row = -1;
  int x = content.x;
  for (int i = 0; i < count; i++) {
    window_t *child = layout_child_at(win, i);
    if (!child) continue;
    int row = row_of ? row_of[i] : 0;
    if (row != cur_row) {
      cur_row = row;
      x = content.x;
    } else if (x > content.x) {
      x += gap;
    }
    layout_measure_t cm = cellm[i];
    int cw = cm.desired_w;
    int ch = layout_apply_alignment(row_h[row], cm.desired_h, child->layout.v_align);
    int cx = x;
    int cy = row_y[row];
    if (child->layout.h_align == LAYOUT_ALIGN_CENTER)
      cx += (row_w[row] - cw) / 2;
    else if (child->layout.h_align == LAYOUT_ALIGN_END)
      cx += row_w[row] - cw;
    if (child->layout.v_align == LAYOUT_ALIGN_CENTER)
      cy += (row_h[row] - ch) / 2;
    else if (child->layout.v_align == LAYOUT_ALIGN_END)
      cy += row_h[row] - ch;
    layout_arrange_child(child, R(cx, cy, cw, ch));
    x += cw;
  }

  free(row_of);
  free(row_w);
  free(row_h);
  free(cellm);
  free(row_y);
}

result_t win_flow(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      win->flags |= WINDOW_AUTO_LAYOUT | WINDOW_LAYOUT_CONTAINER;
      win->flags |= WINDOW_STACK_HORIZONTAL;
      win->layout.layout_spacing = 0;
      win->layout.layout_padding = (irect16_t){0, 0, 0, 0};
      win->layout.layout_margin = (irect16_t){0, 0, 0, 0};
      
      if (lparam) {
        const form_ctrl_def_t *cd = (const form_ctrl_def_t *)lparam;
        if (cd->class_name && (uintptr_t)cd->class_name > 0x1000000) {
          if (cd->flags & WINDOW_STACK_HORIZONTAL)
            win->flags |= WINDOW_STACK_HORIZONTAL;
          else
            win->flags &= ~WINDOW_STACK_HORIZONTAL;
          if (cd->layout_spacing > 0)
            win->layout.layout_spacing = cd->layout_spacing;
          win->layout.layout_padding = cd->padding;
          win->layout.layout_margin = cd->margin;
        } else {
          const layout_view_config_t *cfg = (const layout_view_config_t *)lparam;
          if (cfg->orientation & WINDOW_STACK_HORIZONTAL)
            win->flags |= WINDOW_STACK_HORIZONTAL;
          else
            win->flags &= ~WINDOW_STACK_HORIZONTAL;
          if (cfg->spacing > 0)
            win->layout.layout_spacing = cfg->spacing;
          win->layout.layout_padding = cfg->padding;
          win->layout.layout_margin = cfg->margin;
        }
      }
      return true;
    }
    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (!m)
        return MAKEDWORD(1, 1);
      layout_flow_measure_window(win, m);
      if (m->desired_w < 1) m->desired_w = 1;
      if (m->desired_h < 1) m->desired_h = 1;
      return MAKEDWORD((uint16_t)m->desired_w, (uint16_t)m->desired_h);
    }
    case evArrange: {
      layout_arrange_t *a = (layout_arrange_t *)lparam;
      if (a) {
        win->frame = a->rect;
        irect16_t cr = get_client_rect(win);
        layout_flow_arrange_window(win, &cr);
      }
      return MAKEDWORD((uint16_t)MAX(1, win->frame.w),
                       (uint16_t)MAX(1, win->frame.h));
    }
    case evResize:
      {
        irect16_t cr = get_client_rect(win);
        layout_flow_arrange_window(win, &cr);
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

result_t win_flowview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  return win_flow(win, msg, wparam, lparam);
}

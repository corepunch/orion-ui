// Tool options palette for the image editor.
// Displays per-tool controls in a small floating window below the tool palette.
//
// Three panels (selected by current tool):
//   OPTS_BRUSH  — pencil / brush / spray / eraser:
//                 a vertical list of 5 MacPaint-style horizontal stroke previews
//                 used to pick the brush radius (0, 1, 2, 3, 4).
//   OPTS_SHAPE  — line / rect / ellipse / rounded rect / polygon:
//                 an Outline / Filled toggle matching the old toolbox widget.
//   OPTS_NONE   — all other tools: empty / blank panel.
//
// The window invalidates itself whenever handle_menu_command switches tools.
// It does so through a direct invalidate_window() call because the tool
// options window is not a child of the tool palette.

#include "imageeditor.h"

// Brush radii — 5 MacPaint-style line widths.
const int kBrushSizes[NUM_BRUSH_SIZES] = {0, 1, 2, 3, 4};

// ── Panel type helpers ──────────────────────────────────────────────────────

typedef enum {
  OPTS_NONE = 0,
  OPTS_BRUSH,
  OPTS_SHAPE,
} tool_opts_panel_t;

static tool_opts_panel_t panel_for_tool(int tool) {
  switch (tool) {
    case ID_TOOL_PENCIL:
    case ID_TOOL_BRUSH:
    case ID_TOOL_ERASER:
    case ID_TOOL_SPRAY:
      return OPTS_BRUSH;
    case ID_TOOL_LINE:
    case ID_TOOL_RECT:
    case ID_TOOL_ELLIPSE:
    case ID_TOOL_ROUNDED_RECT:
    case ID_TOOL_POLYGON:
      return OPTS_SHAPE;
    default:
      return OPTS_NONE;
  }
}

// ── Layout helpers ──────────────────────────────────────────────────────────

// Label row at the top of the panel (y=1, h=9).
#define OPTS_LABEL_H  9
// Gap between label and content rows.
#define OPTS_GAP_H    2
// Height of the shape-mode toggle row (shorter than the brush list).
#define OPTS_ROW_H    14

static rect_t opts_label_rect(void) {
  return (rect_t){ 2, 1, TOOL_OPTIONS_WIN_W - 4, OPTS_LABEL_H };
}

// The first content row starts just below the label + gap.
static int opts_content_y(void) {
  return OPTS_LABEL_H + OPTS_GAP_H;
}

// Returns the rect for brush-size row i in the vertical list.
static rect_t brush_cell_rect(int idx) {
  return (rect_t){ 1, opts_content_y() + idx * OPTS_BRUSH_CELL_H,
                   TOOL_OPTIONS_WIN_W - 2, OPTS_BRUSH_CELL_H };
}

// Returns the single-row rect used by the shape-mode toggle.
static rect_t shape_row_rect(void) {
  return (rect_t){ 1, opts_content_y(), TOOL_OPTIONS_WIN_W - 2, OPTS_ROW_H };
}

// ── Brush-size panel ────────────────────────────────────────────────────────

static void draw_brush_panel(int selected_idx) {
  rect_t lbl = opts_label_rect();
  draw_text_small("Size:", lbl.x, lbl.y, get_sys_color(brTextDisabled));

  for (int i = 0; i < NUM_BRUSH_SIZES; i++) {
    rect_t cell = brush_cell_rect(i);
    bool active = (i == selected_idx);
    uint32_t bg = active ? get_sys_color(brFocusRing) : get_sys_color(brButtonBg);
    rect_t outer = rect_inset_xy(cell, 1, 0);
    rect_t inner = rect_inset(outer, 1);

    fill_rect(get_sys_color(brDarkEdge), &outer);
    fill_rect(bg, &inner);

    // Horizontal stroke centred in the cell; thickness = 2*radius+1, clamped.
    int radius = kBrushSizes[i];
    int thickness = 2 * radius + 1;
    if (thickness > inner.h) thickness = inner.h;
    int stroke_y = inner.y + (inner.h - thickness) / 2;
    uint32_t stroke_col = active
        ? get_sys_color(brWindowBg)
        : get_sys_color(brTextNormal);
    rect_t stroke = { inner.x + 2, stroke_y, inner.w - 4, thickness };
    fill_rect(stroke_col, &stroke);
  }
}

// ── Shape-mode panel ────────────────────────────────────────────────────────

static void draw_shape_panel(bool filled) {
  rect_t lbl = opts_label_rect();
  draw_text_small("Fill:", lbl.x, lbl.y, get_sys_color(brTextDisabled));

  rect_t row = shape_row_rect();
  rect_t outline_outer = rect_inset_xy(rect_split_left(row, row.w / 2), 1, 0);
  rect_t filled_outer  = rect_inset_xy(rect_split_right(row, row.w / 2), 1, 0);
  rect_t outline_inner = rect_inset(outline_outer, 1);
  rect_t filled_inner  = rect_inset(filled_outer, 1);
  rect_t outline_text  = rect_center(outline_inner, strwidth("O"), 8);
  rect_t filled_text   = rect_center(filled_inner,  strwidth("F"), 8);

  uint32_t outline_col = filled ? get_sys_color(brButtonBg) : get_sys_color(brFocusRing);
  uint32_t filled_col  = filled ? get_sys_color(brFocusRing) : get_sys_color(brButtonBg);

  fill_rect(get_sys_color(brDarkEdge), &outline_outer);
  fill_rect(outline_col, &outline_inner);
  draw_text_small("O", outline_text.x, outline_text.y, get_sys_color(brTextNormal));

  fill_rect(get_sys_color(brDarkEdge), &filled_outer);
  fill_rect(filled_col, &filled_inner);
  draw_text_small("F", filled_text.x, filled_text.y, get_sys_color(brTextNormal));
}

// ── Hit-testing ─────────────────────────────────────────────────────────────

// Returns the brush-size index at client position (mx, my), or -1 if outside.
static int brush_hit(int mx, int my) {
  for (int i = 0; i < NUM_BRUSH_SIZES; i++) {
    rect_t cell = brush_cell_rect(i);
    if (mx >= cell.x && mx < cell.x + cell.w &&
        my >= cell.y && my < cell.y + cell.h)
      return i;
  }
  return -1;
}

// Returns true if the click lands in the "Filled" (right) half of the row.
static bool shape_filled_hit(int mx) {
  rect_t row = shape_row_rect();
  rect_t filled_half = rect_split_right(row, row.w / 2);
  return mx >= filled_half.x;
}

static bool shape_row_hit(int mx, int my) {
  rect_t row = shape_row_rect();
  return mx >= row.x && mx < row.x + row.w &&
         my >= row.y && my < row.y + row.h;
}

// ── Window procedure ────────────────────────────────────────────────────────

result_t win_tool_options_proc(window_t *win, uint32_t msg,
                               uint32_t wparam, void *lparam) {
  (void)lparam;
  switch (msg) {
    case evCreate:
      return true;

    case evPaint: {
      rect_t cr = get_client_rect(win);
      fill_rect(get_sys_color(brWindowBg), &cr);
      if (!g_app) return true;
      tool_opts_panel_t panel = panel_for_tool(g_app->current_tool);
      if (panel == OPTS_BRUSH) {
        draw_brush_panel(g_app->brush_size);
      } else if (panel == OPTS_SHAPE) {
        draw_shape_panel(g_app->shape_filled);
      }
      return true;
    }

    case evLeftButtonDown: {
      if (!g_app) return true;
      int mx = (int)(int16_t)LOWORD(wparam);
      int my = (int)(int16_t)HIWORD(wparam);
      tool_opts_panel_t panel = panel_for_tool(g_app->current_tool);

      if (panel == OPTS_BRUSH) {
        int idx = brush_hit(mx, my);
        if (idx >= 0 && idx != g_app->brush_size) {
          g_app->brush_size = idx;
          invalidate_window(win);
        }
      } else if (panel == OPTS_SHAPE) {
        if (shape_row_hit(mx, my)) {
          bool new_filled = shape_filled_hit(mx);
          if (new_filled != g_app->shape_filled) {
            g_app->shape_filled = new_filled;
            invalidate_window(win);
          }
        }
      }
      return true;
    }

    case evDestroy:
      if (g_app && g_app->tool_options_win == win)
        g_app->tool_options_win = NULL;
      return true;

    default:
      return false;
  }
}

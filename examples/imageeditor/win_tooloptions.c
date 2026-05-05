// Tool options palette for the image editor.
// Displays per-tool controls in a small floating window below the tool palette.
//
// Three panels (selected by current tool):
//   OPTS_BRUSH  — pencil / brush / spray / eraser:
//                 a vertical list of 5 MacPaint-style horizontal stroke previews
//                 used to pick the brush radius (0, 1, 2, 3, 4).
//   OPTS_SHAPE  — line / rect / ellipse / rounded rect / polygon:
//                 an Outline / Filled toggle matching the old toolbox widget.
//   OPTS_WAND   — magic-wand tolerance and overlay display controls.
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
  OPTS_WAND,
} tool_opts_panel_t;

enum {
  WAND_OPT_ID_AA = 9001,
  WAND_OPT_ID_SPREAD_LABEL,
  WAND_OPT_ID_SPREAD,
  WAND_OPT_ID_COLOR_LABEL,
  WAND_OPT_ID_COLOR,
};

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
    case ID_TOOL_MAGIC_WAND:
      return OPTS_WAND;
    default:
      return OPTS_NONE;
  }
}

// ── Layout helpers ──────────────────────────────────────────────────────────

// Gap between label and content rows.
#define OPTS_GAP_H    2
// Height of the shape-mode toggle row (shorter than the brush list).
#define OPTS_ROW_H    18

// The first content row starts just below the label + gap.
static int opts_content_y(void) {
  return OPTS_GAP_H;
}

// Returns the rect for brush-size row i in the vertical list.
static irect16_t brush_cell_rect(int idx) {
  return (irect16_t){ 1, opts_content_y() + idx * OPTS_BRUSH_CELL_H,
                   TOOL_OPTIONS_WIN_W - 2, OPTS_BRUSH_CELL_H };
}

// Returns the single-row rect used by the shape-mode toggle.
static irect16_t shape_row_rect(void) {
  return (irect16_t){ 1, opts_content_y(), TOOL_OPTIONS_WIN_W - 2, OPTS_ROW_H };
}

static irect16_t wand_aa_rect(void) {
  return (irect16_t){ 3, 4, TOOL_OPTIONS_WIN_W - 6, CONTROL_HEIGHT };
}

static irect16_t wand_spread_label_rect(void) {
  return (irect16_t){ 4, 25, 54, CONTROL_HEIGHT };
}

static irect16_t wand_spread_rect(void) {
  return (irect16_t){ 62, 24, 42, CONTROL_HEIGHT };
}

static irect16_t wand_color_label_rect(void) {
  return (irect16_t){ 4, 48, 54, CONTROL_HEIGHT };
}

static irect16_t wand_color_rect(void) {
  return (irect16_t){ 62, 47, 24, CONTROL_HEIGHT };
}

// ── Brush-size panel ────────────────────────────────────────────────────────

static void draw_brush_panel(int selected_idx) {
  for (int i = 0; i < NUM_BRUSH_SIZES; i++) {
    irect16_t cell = brush_cell_rect(i);
    bool active = (i == selected_idx);
    uint32_t bg = active ? get_sys_color(brFocusRing) : get_sys_color(brButtonBg);
    irect16_t outer = rect_inset_xy(cell, 1, 0);
    irect16_t inner = rect_inset(outer, 1);

    // fill_rect(get_sys_color(brDarkEdge), &outer);
    fill_rect(bg, inner);

    // Horizontal stroke centred in the cell; thickness = 2*radius+1, clamped.
    int radius = kBrushSizes[i];
    int thickness = 2 * radius + 1;
    if (thickness > inner.h) thickness = inner.h;
    int stroke_y = inner.y + (inner.h - thickness) / 2;
    uint32_t stroke_col = active
        ? get_sys_color(brWindowBg)
        : get_sys_color(brTextNormal);
    irect16_t stroke = { inner.x + 2, stroke_y, inner.w - 4, thickness };
    fill_rect(stroke_col, stroke);
  }
}

// ── Shape-mode panel ────────────────────────────────────────────────────────

static void draw_shape_panel(bool filled) {
  irect16_t row = shape_row_rect();
  irect16_t outline_outer = rect_inset_xy(rect_split_left(row, row.w / 2), 1, 0);
  irect16_t filled_outer  = rect_inset_xy(rect_split_right(row, row.w / 2), 1, 0);
  irect16_t outline_inner = rect_inset(outline_outer, 1);
  irect16_t filled_inner  = rect_inset(filled_outer, 1);
  // irect16_t outline_text  = rect_center(outline_inner, strwidth("O"), 8);
  // irect16_t filled_text   = rect_center(filled_inner,  strwidth("F"), 8);

  uint32_t outline_col = filled ? get_sys_color(brButtonBg) : get_sys_color(brFocusRing);
  uint32_t filled_col  = filled ? get_sys_color(brFocusRing) : get_sys_color(brButtonBg);

  fill_rect(get_sys_color(brDarkEdge), outline_outer);
  fill_rect(outline_col, outline_inner);
  draw_text_small_clipped("O", &outline_inner, get_sys_color(brTextNormal), TEXT_ALIGN_CENTER);

  fill_rect(get_sys_color(brDarkEdge), filled_outer);
  fill_rect(filled_col, filled_inner);
  draw_text_small_clipped("F", &filled_inner, get_sys_color(brTextNormal), TEXT_ALIGN_CENTER);
}

static void wand_sync_spread_from_edit(window_t *win) {
  if (!win || !g_app) return;
  window_t *ed = get_window_item(win, WAND_OPT_ID_SPREAD);
  if (!ed) return;
  int spread = atoi(ed->title);
  g_app->wand.spread = CLAMP(spread, 0, 255);
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", g_app->wand.spread);
  send_message(ed, edSetText, 0, buf);
}

static void wand_sync_controls(window_t *win);

static void wand_create_controls(window_t *win) {
  if (!win || get_window_item(win, WAND_OPT_ID_AA)) return;

  window_t *prev_focus = g_ui_runtime.focused;
  irect16_t aa = wand_aa_rect();
  window_t *aa_win = create_window("Antialias", 0, &aa, win, win_checkbox, 0, NULL);
  if (aa_win) aa_win->id = WAND_OPT_ID_AA;

  irect16_t spread_label = wand_spread_label_rect();
  window_t *spread_label_win = create_window("Spread", 0, &spread_label,
                                             win, win_label, 0, NULL);
  if (spread_label_win) spread_label_win->id = WAND_OPT_ID_SPREAD_LABEL;

  char spread_text[16];
  snprintf(spread_text, sizeof(spread_text), "%d", g_app ? g_app->wand.spread : 0);
  irect16_t spread = wand_spread_rect();
  window_t *spread_win = create_window(spread_text, 0, &spread, win, win_textedit, 0, NULL);
  if (spread_win) spread_win->id = WAND_OPT_ID_SPREAD;

  irect16_t color_label = wand_color_label_rect();
  window_t *color_label_win = create_window("Overlay", 0, &color_label,
                                            win, win_label, 0, NULL);
  if (color_label_win) color_label_win->id = WAND_OPT_ID_COLOR_LABEL;

  wand_sync_controls(win);
  if (prev_focus && is_window(prev_focus))
    g_ui_runtime.focused = prev_focus;
}

static void wand_destroy_controls(window_t *win) {
  if (!win) return;
  window_t *aa = get_window_item(win, WAND_OPT_ID_AA);
  window_t *spread_label = get_window_item(win, WAND_OPT_ID_SPREAD_LABEL);
  window_t *spread = get_window_item(win, WAND_OPT_ID_SPREAD);
  window_t *color_label = get_window_item(win, WAND_OPT_ID_COLOR_LABEL);
  if (aa) destroy_window(aa);
  if (spread_label) destroy_window(spread_label);
  if (spread) destroy_window(spread);
  if (color_label) destroy_window(color_label);
}

static void wand_sync_controls(window_t *win) {
  if (!win || !g_app) return;
  window_t *aa = get_window_item(win, WAND_OPT_ID_AA);
  if (aa) send_message(aa, btnSetCheck,
                       g_app->wand.antialias ? btnStateChecked : btnStateUnchecked,
                       NULL);
  window_t *spread = get_window_item(win, WAND_OPT_ID_SPREAD);
  if (spread && !spread->editing) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", g_app->wand.spread);
    send_message(spread, edSetText, 0, buf);
  }
}

static void draw_wand_panel(window_t *win) {
  (void)win;
  irect16_t sw = wand_color_rect();
  fill_rect(get_sys_color(brDarkEdge), R(sw.x - 1, sw.y - 1, sw.w + 2, sw.h + 2));
  fill_rect(g_app ? g_app->wand.overlay_color : MAKE_COLOR(0x40, 0xA0, 0xFF, 0x55), sw);
}

// ── Hit-testing ─────────────────────────────────────────────────────────────

// Returns the brush-size index at client position (mx, my), or -1 if outside.
static int brush_hit(int mx, int my) {
  for (int i = 0; i < NUM_BRUSH_SIZES; i++) {
    irect16_t cell = brush_cell_rect(i);
    if (mx >= cell.x && mx < cell.x + cell.w &&
        my >= cell.y && my < cell.y + cell.h)
      return i;
  }
  return -1;
}

// Returns true if the click lands in the "Filled" (right) half of the row.
static bool shape_filled_hit(int mx) {
  irect16_t row = shape_row_rect();
  irect16_t filled_half = rect_split_right(row, row.w / 2);
  return mx >= filled_half.x;
}

static bool shape_row_hit(int mx, int my) {
  irect16_t row = shape_row_rect();
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
      irect16_t cr = get_client_rect(win);
      fill_rect(get_sys_color(brWindowBg), cr);
      if (!g_app) return true;
      tool_opts_panel_t panel = panel_for_tool(g_app->current_tool);
      if (panel == OPTS_WAND)
        wand_create_controls(win);
      else
        wand_destroy_controls(win);
      if (panel == OPTS_BRUSH) {
        draw_brush_panel(g_app->brush_size);
      } else if (panel == OPTS_SHAPE) {
        draw_shape_panel(g_app->shape_filled);
      } else if (panel == OPTS_WAND) {
        draw_wand_panel(win);
      }
      return panel != OPTS_WAND;
    }

    case evCommand: {
      if (!g_app) return false;
      uint16_t notif = HIWORD(wparam);
      window_t *src = (window_t *)lparam;
      if (!src) return false;

      if (src->id == WAND_OPT_ID_AA && notif == btnClicked) {
        g_app->wand.antialias = send_message(src, btnGetCheck, 0, NULL) != 0;
        invalidate_window(win);
        return true;
      }
      if (src->id == WAND_OPT_ID_SPREAD && notif == edUpdate) {
        wand_sync_spread_from_edit(win);
        return true;
      }
      return false;
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
      } else if (panel == OPTS_WAND) {
        irect16_t sw = wand_color_rect();
        if (mx >= sw.x && mx < sw.x + sw.w &&
            my >= sw.y && my < sw.y + sw.h) {
          uint32_t new_col = g_app->wand.overlay_color;
          if (show_color_picker(win, g_app->wand.overlay_color, &new_col)) {
            g_app->wand.overlay_color = new_col;
            invalidate_window(win);
            if (g_app->active_doc && g_app->active_doc->canvas_win)
              invalidate_window(g_app->active_doc->canvas_win);
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

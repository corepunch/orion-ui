// Tool palette window for the image editor.
// The palette is a floating window containing:
//   - A 2-column toolbox grid (win_toolbox) occupying the top of the client area.
//   - A FG/BG colour swatch row below the grid.
//   - An Outline/Filled shape-mode toggle row below the swatches.
//
// This proc wraps win_toolbox: all messages are passed through to win_toolbox
// first (or after custom handling where the custom code needs priority).

#include "imageeditor.h"
#include "../../commctl/commctl.h"

// tools.png tile size (all icons are square).
#define ICON_W  16

// Tool palette layout with ident and icon index.
static const toolbox_item_t k_tools[NUM_TOOLS] = {
  { ID_TOOL_SELECT,        0 },      // Select
  { ID_TOOL_HAND,          4 },      // Hand
  { ID_TOOL_EYEDROPPER,   11 },      // Eyedropper
  { ID_TOOL_ZOOM,          5 },      // Zoom
  { ID_TOOL_PENCIL,       13 },      // Pencil
  { ID_TOOL_BRUSH,        15 },      // Brush
  { ID_TOOL_SPRAY,        18 },      // Spray
  { ID_TOOL_FILL,          8 },      // Fill
  { ID_TOOL_ERASER,       12 },      // Eraser
  { ID_TOOL_LINE,         10 },      // Line
  { ID_TOOL_TEXT,          7 },      // Text
  { ID_TOOL_RECT,         21 },      // Rect
  { ID_TOOL_ELLIPSE,      23 },      // Ellipse
  { ID_TOOL_ROUNDED_RECT, 22 },      // Rounded Rect
  { ID_TOOL_POLYGON,      24 },      // Polygon
  { ID_TOOL_MAGNIFIER,     6 },      // Magnifier
};

typedef struct {
  rect_t swatch_box;
  rect_t fill_box;
} tool_palette_layout_t;

static tool_palette_layout_t tool_palette_layout(window_t *win) {
  tool_palette_layout_t layout;
  rect_t panel = { 0, toolbox_grid_height(win), PALETTE_WIN_W,
                   SWATCH_CLIENT_H + FILL_MODE_H };
  layout.swatch_box = rect_split_top(panel, TOOL_SWATCH_BOX_H);
  panel = rect_trim_top(panel, TOOL_SWATCH_BOX_H);
  layout.fill_box = rect_split_top(panel, FILL_MODE_H);
  return layout;
}

static void draw_palette_swatch(const tool_palette_layout_t *layout,
                                uint32_t fg_color, uint32_t bg_color) {
  rect_t inner_box = rect_inset(layout->swatch_box, 2);
  int chip_side = inner_box.w - inner_box.w / 3;
  int reset_side = inner_box.w / 3;
  rect_t fg_outer = rect_split_left(rect_split_top(inner_box, chip_side), chip_side);
  rect_t bg_outer = rect_split_right(rect_split_bottom(inner_box, chip_side), chip_side);
  rect_t reset_outer = rect_split_left(rect_split_bottom(inner_box, reset_side), reset_side);
  rect_t bg_inner = rect_inset(bg_outer, 1);
  rect_t fg_inner = rect_inset(fg_outer, 1);
  rect_t reset_inner = rect_inset(reset_outer, 1);
  rect_t reset_black = rect_inset(rect_offset(reset_inner, 1, 1), 1);

  fill_rect(get_sys_color(brDarkEdge), &bg_outer);
  fill_rect(bg_color, &bg_inner);

  fill_rect(get_sys_color(brDarkEdge), &fg_outer);
  fill_rect(fg_color, &fg_inner);

  // Small black/white reset chip in the lower-left corner, like classic editors.
  fill_rect(get_sys_color(brDarkEdge), &reset_outer);
  fill_rect(0xFFFFFFFF, &reset_inner);
  fill_rect(0xFF000000, &reset_black);
}

static void draw_fill_widget(const tool_palette_layout_t *layout, bool filled) {
  rect_t fill_label = rect_split_top(layout->fill_box, TOOL_FILL_LABEL_H);
  rect_t fill_row = rect_split_bottom(layout->fill_box, TOOL_FILL_ROW_H);
  rect_t outline_outer = rect_inset_xy(rect_split_left(fill_row, fill_row.w / 2), 1, 0);
  rect_t filled_outer = rect_inset_xy(rect_split_right(fill_row, fill_row.w / 2), 1, 0);
  rect_t outline_inner = rect_inset(outline_outer, 1);
  rect_t filled_inner = rect_inset(filled_outer, 1);
  rect_t outline_text = rect_center(outline_inner, strwidth("O"), 8);
  rect_t filled_text = rect_center(filled_inner, strwidth("F"), 8);
  uint32_t outline_col = filled ? get_sys_color(brButtonBg) : get_sys_color(brFocusRing);
  uint32_t filled_col = filled ? get_sys_color(brFocusRing) : get_sys_color(brButtonBg);

  draw_text_small("Fill:", fill_label.x + 2, fill_label.y, get_sys_color(brTextDisabled));

  fill_rect(get_sys_color(brDarkEdge), &outline_outer);
  fill_rect(outline_col, &outline_inner);
  draw_text_small("O", outline_text.x, outline_text.y, get_sys_color(brTextNormal));

  fill_rect(get_sys_color(brDarkEdge), &filled_outer);
  fill_rect(filled_col, &filled_inner);
  draw_text_small("F", filled_text.x, filled_text.y, get_sys_color(brTextNormal));
}

result_t win_tool_palette_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      // First let win_toolbox initialise its state.
      win_toolbox(win, msg, wparam, lparam);
      send_message(win, bxSetButtonSize, TOOL_PALETTE_BTN_SIZE, NULL);

      // Load tools.png icon strip.
#ifdef SHAREDIR
      char path[4096];
      snprintf(path, sizeof(path), "%s/" SHAREDIR "/tools.png", ui_get_exe_dir());
      send_message(win, bxLoadStrip, ICON_W, path);
#endif

      // Set tools from unified array.
      send_message(win, bxSetItems, NUM_TOOLS, (void *)k_tools);
      send_message(win, bxSetIconTintBrush, brTextNormal, NULL);
      send_message(win, bxSetActiveItem, ID_TOOL_SELECT, NULL);
      return true;
    }

    case evPaint: {
      // Let win_toolbox paint the button grid (also fills the background).
      win_toolbox(win, msg, wparam, lparam);

      // Paint FG/BG swatches and shape-mode toggles below the grid.
      tool_palette_layout_t layout = tool_palette_layout(win);

      if (g_app) {
        draw_palette_swatch(&layout, g_app->fg_color, g_app->bg_color);
        draw_fill_widget(&layout, g_app->shape_filled);
      }
      return true;
    }

    case evLeftButtonDown: {
      int mx = (int)(int16_t)LOWORD(wparam);
      int my = (int)(int16_t)HIWORD(wparam);
      tool_palette_layout_t layout = tool_palette_layout(win);
      rect_t fill_row = rect_split_bottom(layout.fill_box, TOOL_FILL_ROW_H);

      // Check if the click is in the shape-mode toggle row.
      if (g_app
          && mx >= fill_row.x
          && mx < fill_row.x + fill_row.w
          && my >= fill_row.y
          && my < fill_row.y + fill_row.h) {
        bool was_filled = g_app->shape_filled;
        rect_t filled_half = rect_split_right(fill_row, fill_row.w / 2);
        g_app->shape_filled = (mx >= filled_half.x);
        if (g_app->shape_filled != was_filled)
          invalidate_window(win);
        return true;
      }

      // Otherwise let the toolbox handle it (button grid or swatches ignored).
      return win_toolbox(win, msg, wparam, lparam);
    }

    case evCommand:
      // bxClicked: a tool button was pressed.
      if (HIWORD(wparam) == bxClicked) {
        int clicked_ident = (int)(int16_t)LOWORD(wparam);
        if (g_app) {
          if (g_app->menubar_win) {
            send_message(g_app->menubar_win, evCommand,
                         MAKEDWORD((uint16_t)clicked_ident,
                                   btnClicked),
                         lparam);
          } else {
            handle_menu_command((uint16_t)clicked_ident);
          }
        }
        return true;
      }
      return false;

    case evDestroy:
      if (g_app && g_app->tool_win == win) g_app->tool_win = NULL;
      return win_toolbox(win, msg, wparam, lparam);

    default:
      return win_toolbox(win, msg, wparam, lparam);
  }
}

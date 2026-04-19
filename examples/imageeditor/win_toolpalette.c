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

// Layout constants for client content below the toolbox grid.
// These are relative to y=0 of the client area; the actual offset from
// the grid is added at paint/hit-test time via toolbox_grid_height(win).
#define SWATCH_LABEL_Y   2   // top of FG/BG labels (relative to grid bottom)
#define SWATCH_LABEL_H   8   // height of small text
#define SWATCH_BOX_H    16   // height of colour swatch boxes
#define FILL_LABEL_H     9   // height of "Fill:" label row
#define FILL_ROW_H      12   // height of Outline/Filled toggle buttons

// Display order for the tool palette – mirrors the Photoshop 1.0 toolbox layout.
static const int k_tool_order[NUM_TOOLS] = {
  ID_TOOL_SELECT,
  ID_TOOL_HAND,
  ID_TOOL_EYEDROPPER,
  ID_TOOL_ZOOM,
  ID_TOOL_PENCIL,
  ID_TOOL_BRUSH,
  ID_TOOL_SPRAY,
  ID_TOOL_FILL,
  ID_TOOL_ERASER,
  ID_TOOL_LINE,
  ID_TOOL_TEXT,
  ID_TOOL_RECT,
  ID_TOOL_ELLIPSE,
  ID_TOOL_ROUNDED_RECT,
  ID_TOOL_POLYGON,
  ID_TOOL_MAGNIFIER,
};

// Icon index in tools.png for each tool in k_tool_order.
static const int k_tool_icon_idx[NUM_TOOLS] = {
  0,    // Select
  4,    // Hand
  11,   // Eyedropper
  5,    // Zoom
  13,   // Pencil
  15,   // Brush
  18,   // Spray
  8,    // Fill
  12,   // Eraser
  10,   // Line
  7,    // Text
  21,   // Rect
  23,   // Ellipse
  22,   // Rounded Rect
  24,   // Polygon
  6,    // Magnifier
};

result_t win_tool_palette_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      // First let win_toolbox initialise its state.
      win_toolbox(win, msg, wparam, lparam);

      // Load tools.png icon strip.
#ifdef SHAREDIR
      char path[4096];
      snprintf(path, sizeof(path), "%s/" SHAREDIR "/tools.png", ui_get_exe_dir());
      send_message(win, bxLoadStrip, ICON_W, path);
#endif

      // Build item list in display order.
      toolbox_item_t items[NUM_TOOLS];
      for (int i = 0; i < NUM_TOOLS; i++) {
        items[i].ident = k_tool_order[i];
        items[i].icon  = k_tool_icon_idx[i];
      }
      send_message(win, bxSetItems, NUM_TOOLS, items);
      send_message(win, bxSetIconTintBrush, brTextNormal, NULL);
      send_message(win, bxSetActiveItem, ID_TOOL_SELECT, NULL);
      return true;
    }

    case evPaint: {
      // Let win_toolbox paint the button grid (also fills the background).
      win_toolbox(win, msg, wparam, lparam);

      // Paint FG/BG swatches and shape-mode toggles below the grid.
      int gy = toolbox_grid_height(win);  // y offset where our content starts
      int sy = gy + SWATCH_LABEL_Y;

      draw_text_small("FG", 2, sy, get_sys_color(brTextDisabled));
      draw_text_small("BG", TB_SPACING + 2, sy, get_sys_color(brTextDisabled));
      sy += SWATCH_LABEL_H;

      if (g_app) {
#define DrawSwatch(border_col, x, color) \
  fill_rect((border_col), R((x) + 1, sy - 1, TB_SPACING - 2, SWATCH_BOX_H)); \
  fill_rect((color),      R((x) + 2, sy,     TB_SPACING - 4, SWATCH_BOX_H - 2));
        DrawSwatch(get_sys_color(brDarkEdge), 0,          g_app->fg_color);
        DrawSwatch(get_sys_color(brDarkEdge), TB_SPACING, g_app->bg_color);
#undef DrawSwatch

        // Shape-mode row.
        int fy = sy + SWATCH_BOX_H;
        draw_text_small("Fill:", 2, fy, get_sys_color(brTextDisabled));
        fy += FILL_LABEL_H;

        uint32_t outline_col = g_app->shape_filled
            ? get_sys_color(brButtonBg) : get_sys_color(brFocusRing);
        fill_rect(get_sys_color(brDarkEdge),
              R(1,           fy,     TB_SPACING - 2, FILL_ROW_H));
        fill_rect(outline_col, R(2,           fy + 1, TB_SPACING - 4, FILL_ROW_H - 2));
        draw_text_small("O", 5, fy + 2, get_sys_color(brTextNormal));

        uint32_t filled_col = g_app->shape_filled
            ? get_sys_color(brFocusRing) : get_sys_color(brButtonBg);
        fill_rect(get_sys_color(brDarkEdge),
              R(TB_SPACING + 1, fy,     TB_SPACING - 2, FILL_ROW_H));
        fill_rect(filled_col,
              R(TB_SPACING + 2, fy + 1, TB_SPACING - 4, FILL_ROW_H - 2));
        draw_text_small("F", TB_SPACING + 5, fy + 2, get_sys_color(brTextNormal));
      }
      return true;
    }

    case evLeftButtonDown: {
      int mx = (int)(int16_t)LOWORD(wparam);
      int my = (int)(int16_t)HIWORD(wparam);
      int gy = toolbox_grid_height(win);

      // Check if the click is in the shape-mode toggle row.
      int fill_row_y = gy + SWATCH_LABEL_Y + SWATCH_LABEL_H
                       + SWATCH_BOX_H + FILL_LABEL_H;
      if (g_app && my >= fill_row_y && my < fill_row_y + FILL_ROW_H) {
        bool was_filled = g_app->shape_filled;
        g_app->shape_filled = (mx >= TB_SPACING);
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

    default:
      return win_toolbox(win, msg, wparam, lparam);
  }
}

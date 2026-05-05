// Tool palette window for the image editor.
// The palette is a floating window containing:
//   - A 2-column toolbox grid (win_toolbox) occupying the top of the client area.
//   - A FG/BG colour swatch row below the grid.
//
// Per-tool options (brush size, shape fill mode) live in the separate tool
// options palette (win_tool_options.c).
//
// This proc wraps win_toolbox: all messages are passed through to win_toolbox
// first (or after custom handling where the custom code needs priority).

#include "imageeditor.h"
#include "../../commctl/commctl.h"

// Image editor atlas tile size (all icons are square).
#define ICON_W  TOOL_ICON_W

// Tool palette layout with ident, icon index, and tooltip text (hotkey in parentheses).
// Uses the bundled 24x24 image-editor.png atlas.
static const toolbox_item_t k_tools[NUM_TOOLS] = {
  { ID_TOOL_SELECT,        IE_RECTANGULAR_MARQUEE,           "Select (S)"         },
  { ID_TOOL_MOVE,          IE_MOVE,                          "Move (V)"           },
  { ID_TOOL_MAGIC_WAND,    IE_MAGIC_WAND,                    "Magic Wand (W)"     },
  { ID_TOOL_CROP,          IE_CROP,                          "Crop (C)"           },
  { ID_TOOL_HAND,          IE_HAND,                          "Hand"               },
  { ID_TOOL_EYEDROPPER,    IE_EYEDROPPER,                    "Eyedropper (I)"     },
  { ID_TOOL_ZOOM,          IE_ZOOM_IN,                       "Zoom"               },
  { ID_TOOL_PENCIL,        IE_PENCIL,                        "Pencil (P)"         },
  { ID_TOOL_BRUSH,         IE_BRUSH,                         "Brush (B)"          },
  { ID_TOOL_SPRAY,         IE_SPRAY_CAN,                     "Spray (A)"          },
  { ID_TOOL_FILL,          IE_PAINT_BUCKET,                  "Fill (K)"           },
  { ID_TOOL_ERASER,        IE_ERASER,                        "Eraser (E)"         },
  { ID_TOOL_LINE,          IE_LINE,                          "Line"               },
  { ID_TOOL_TEXT,          IE_TEXT,                          "Text (T)"           },
  { ID_TOOL_RECT,          IE_RECTANGLE,                     "Rectangle"          },
  { ID_TOOL_ELLIPSE,       IE_ELLIPSE,                       "Ellipse"            },
  { ID_TOOL_ROUNDED_RECT,  IE_ROUNDED_RECTANGLE,             "Rounded Rect"       },
  { ID_TOOL_POLYGON,       IE_POLYGON_LASSO,                 "Polygon"            },
  { ID_TOOL_MAGNIFIER,     IE_ZOOM_OUT,                      "Magnifier (G)"      },
};

static void draw_palette_swatch(window_t *win,
                                uint32_t fg_color, uint32_t bg_color) {
  irect16_t swatch_box = { 0, toolbox_grid_height(win), PALETTE_WIN_W, SWATCH_CLIENT_H };
  irect16_t inner_box = rect_inset(swatch_box, 2);
  int chip_side = inner_box.w - inner_box.w / 3;
  int reset_side = inner_box.w / 3;
  irect16_t fg_outer = rect_split_left(rect_split_top(inner_box, chip_side), chip_side);
  irect16_t bg_outer = rect_split_right(rect_split_bottom(inner_box, chip_side), chip_side);
  irect16_t reset_outer = rect_split_left(rect_split_bottom(inner_box, reset_side), reset_side);
  irect16_t bg_inner = rect_inset(bg_outer, 1);
  irect16_t fg_inner = rect_inset(fg_outer, 1);
  irect16_t reset_inner = rect_inset(reset_outer, 1);
  irect16_t reset_black = rect_inset(rect_offset(reset_inner, 1, 1), 1);

  fill_rect(get_sys_color(brDarkEdge), bg_outer);
  fill_rect(bg_color, bg_inner);

  fill_rect(get_sys_color(brDarkEdge), fg_outer);
  fill_rect(fg_color, fg_inner);

  // Small black/white reset chip in the lower-left corner, like classic editors.
  fill_rect(get_sys_color(brDarkEdge), reset_outer);
  fill_rect(0xFFFFFFFF, reset_inner);
  fill_rect(0xFF000000, reset_black);
}

result_t win_tool_palette_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      // First let win_toolbox initialise its state.
      win_toolbox(win, msg, wparam, lparam);
      send_message(win, bxSetButtonSize, TOOL_PALETTE_BTN_SIZE, NULL);

#ifdef SHAREDIR
      char icon_path[512];
      int n = snprintf(icon_path, sizeof(icon_path), "%s/" SHAREDIR "/image-editor.png",
                       ui_get_exe_dir());
      if (n > 0 && (size_t)n < sizeof(icon_path)) {
        send_message(win, bxLoadStrip, ICON_W, icon_path);
      }
#endif

      // Set tools from unified array.
      send_message(win, bxSetItems, NUM_TOOLS, (void *)k_tools);
      send_message(win, bxSetActiveItem, ID_TOOL_SELECT, NULL);
      return true;
    }

    case evPaint: {
      // Let win_toolbox paint the button grid (also fills the background).
      win_toolbox(win, msg, wparam, lparam);

      // Paint FG/BG swatches below the grid.
      if (g_app)
        draw_palette_swatch(win, g_app->fg_color, g_app->bg_color);
      return true;
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

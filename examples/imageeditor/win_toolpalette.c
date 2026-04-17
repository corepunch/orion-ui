// Tool palette window
// Uses WINDOW_TOOLBAR for tool buttons (PNG icons from tools.png with wrapping).
// The client area shows FG/BG color swatches and a filled/outline shape mode selector.
// Follows the WinAPI TB_ADDBITMAP / TBBUTTON pattern: one bitmap_strip_t
// shared across all toolbar buttons; each button stores only an icon index (iBitmap).

#include "imageeditor.h"
#include "../../commctl/commctl.h"

// tools.png tile size (all icons are the same size in the strip)
#define ICON_W    16
#define ICON_H    16

// Layout constants for the client-area swatches and fill-mode row.
// Both paint and hit-test must use these same values to stay in sync.
#define PALETTE_LABEL_Y   2   // top of FG/BG labels
#define PALETTE_LABEL_H   8   // height of small text
#define PALETTE_SWATCH_H  16  // height of color swatch boxes
#define PALETTE_FILL_LABEL_H 9 // height of "Fill:" label row
#define PALETTE_FILL_ROW_H   12 // height of Outline/Filled toggle buttons
// Derived: y of the toggle buttons = LABEL_Y + LABEL_H + SWATCH_H + FILL_LABEL_H
#define PALETTE_FILL_ROW_Y \
  (PALETTE_LABEL_Y + PALETTE_LABEL_H + PALETTE_SWATCH_H + PALETTE_FILL_LABEL_H)
// tools.png: 320×16 = 20 columns × 1 row of 16×16 icons.
// Icon assignments (from visual inspection of tools.png):
//   0=select, 4=hand, 14=eyedropper, 5=zoom,
//   13=pencil, 15=brush, 17=spray (airbrush), 8=fill (paint bucket),
//   12=eraser, 10=line (diagonal line), 7=text,
//   20=rect, 22=ellipse, 21=rounded-rect, 23=polygon (wavy outline),
//   16=magnifier (loupe circle)

// Display order for the tool palette – mirrors the Photoshop 1.0 toolbox layout:
// selection/navigation tools first, then painting tools, then shape tools.
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
    case kWindowMessageCreate: {
      // Load tools.png into the toolbar strip.  The framework owns the GL
      // texture and frees it on destroy; no manual glDeleteTextures needed.
#ifdef SHAREDIR
      char path[4096];
      snprintf(path, sizeof(path), "%s/" SHAREDIR "/tools.png", ui_get_exe_dir());
      send_message(win, kToolBarMessageLoadStrip, ICON_W, path);
#endif

      // Add one toolbar button per tool.
      // icon = PNG strip index (iBitmap); ident = tool command ID.
      toolbar_button_t buttons[NUM_TOOLS];
      for (int i = 0; i < NUM_TOOLS; i++) {
        buttons[i].icon   = k_tool_icon_idx[i];
        buttons[i].ident  = k_tool_order[i];
        buttons[i].flags  = (i == 0) ? TOOLBAR_BUTTON_FLAG_ACTIVE : 0;  // Select is the default tool
      }
      send_message(win, kToolBarMessageAddButtons, NUM_TOOLS, buttons);
      return true;
    }

    case kWindowMessagePaint: {
      // Client area: FG/BG color swatches + fill mode selector.
      fill_rect(get_sys_color(kColorWindowDarkBg), 0, 0, win->frame.w, win->frame.h);
      fill_rect(get_sys_color(kColorDarkEdge), win->frame.w - 1, 0, 1, win->frame.h);
      fill_rect(get_sys_color(kColorDarkEdge), 0, win->frame.h - 1, win->frame.w, 1);

      int sy = PALETTE_LABEL_Y;
      draw_text_small("FG", 2, sy, get_sys_color(kColorTextDisabled));
      draw_text_small("BG", TB_SPACING+2, sy, get_sys_color(kColorTextDisabled));
      sy += PALETTE_LABEL_H;
      if (g_app) {
        #define DrawSwatch(swatch_col, x, color) \
          fill_rect(swatch_col, x+1,  sy - 1, TB_SPACING-2, PALETTE_SWATCH_H); \
          fill_rect(color, x+2,  sy, TB_SPACING-4, PALETTE_SWATCH_H-2); 

        DrawSwatch(get_sys_color(kColorDarkEdge), 0, g_app->fg_color);
        DrawSwatch(get_sys_color(kColorDarkEdge), TB_SPACING, g_app->bg_color);

        // Fill mode row: show "Outline" / "Filled" mini toggles
        int fy = sy + PALETTE_SWATCH_H;
        draw_text_small("Fill:", 2, fy, get_sys_color(kColorTextDisabled));
        fy += PALETTE_FILL_LABEL_H;
        // Outline button (active when !shape_filled)
        uint32_t outline_col = g_app->shape_filled ? get_sys_color(kColorButtonBg) : get_sys_color(kColorFocusRing);
        fill_rect(get_sys_color(kColorDarkEdge),  1,           fy,   TB_SPACING-2, PALETTE_FILL_ROW_H);
        fill_rect(outline_col,      2,           fy+1, TB_SPACING-4, PALETTE_FILL_ROW_H-2);
        draw_text_small("O", 5,                 fy+2, get_sys_color(kColorTextNormal));
        // Filled button (active when shape_filled)
        uint32_t filled_col = g_app->shape_filled ? get_sys_color(kColorFocusRing) : get_sys_color(kColorButtonBg);
        fill_rect(get_sys_color(kColorDarkEdge),  TB_SPACING+1, fy, TB_SPACING-2, PALETTE_FILL_ROW_H);
        fill_rect(filled_col,       TB_SPACING+2, fy+1, TB_SPACING-4, PALETTE_FILL_ROW_H-2);
        draw_text_small("F", TB_SPACING+5,       fy+2, get_sys_color(kColorTextNormal));
      }
      return true;
    }

    case kWindowMessageLeftButtonDown: {
      // Check if click is in the fill mode row
      if (!g_app) return false;
      int mx = (int16_t)LOWORD(wparam);
      int my = (int16_t)HIWORD(wparam);
      if (my >= PALETTE_FILL_ROW_Y && my < PALETTE_FILL_ROW_Y + PALETTE_FILL_ROW_H) {
        bool was_filled = g_app->shape_filled;
        g_app->shape_filled = (mx >= TB_SPACING);
        if (g_app->shape_filled != was_filled) {
          invalidate_window(win);
        }
        return true;
      }
      return false;
    }

    case kToolBarMessageButtonClick: {
      // A toolbar button was clicked: update the active button state and
      // forward the command to the menubar (same path as keyboard accelerators).
      int clicked_ident = (int)wparam;
      send_message(win, kToolBarMessageSetActiveButton, (uint32_t)clicked_ident, NULL);
      if (g_app) {
        if (g_app->menubar_win) {
          send_message(g_app->menubar_win, kWindowMessageCommand,
                       MAKEDWORD((uint16_t)clicked_ident, kButtonNotificationClicked), lparam);
        } else {
          // In gem mode menubar_win is NULL; dispatch directly.
          handle_menu_command((uint16_t)clicked_ident);
        }
      }
      return true;
    }

    default:
      return false;
  }
}

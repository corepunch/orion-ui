// Tool palette for the form editor.
// Uses win_toolbox directly — no extra client content below the grid.
// toolbox.png is a 21×21-px-per-tile sprite sheet in VB3 toolbox order.

#include "formeditor.h"
#include "../../commctl/commctl.h"

// Icon indices in toolbox.png for each tool, in display order.
// Strip layout: 3 columns × N rows of 21×21 tiles.
static const int k_tool_order[NUM_TOOLS] = {
  ID_TOOL_SELECT,
  ID_TOOL_LABEL,
  ID_TOOL_TEXTEDIT,
  ID_TOOL_BUTTON,
  ID_TOOL_CHECKBOX,
  ID_TOOL_COMBOBOX,
  ID_TOOL_LIST,
};

static const int k_tool_icon[NUM_TOOLS] = {
  0,   // Pointer / Select
  2,   // Label
  3,   // TextBox
  5,   // CommandButton
  6,   // CheckBox
  8,   // ComboBox
  9,   // ListBox
};

result_t win_tool_palette_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      // Delegate to win_toolbox first so its state is ready.
      win_toolbox(win, msg, wparam, lparam);

      // Use a larger button size to fit the 21px icons with comfortable margin.
      send_message(win, toolSetButtonSize, FE_TOOLBOX_BTN_SIZE, NULL);

      // Load the VB3-style icon strip.
#ifdef SHAREDIR
      char path[4096];
      snprintf(path, sizeof(path), "%s/" SHAREDIR "/toolbox.png", ui_get_exe_dir());
      send_message(win, toolLoadStrip, FE_TOOLBOX_ICON_W, path);
#endif

      toolbox_item_t items[NUM_TOOLS];
      for (int i = 0; i < NUM_TOOLS; i++) {
        items[i].ident = k_tool_order[i];
        items[i].icon  = k_tool_icon[i];
      }
      send_message(win, toolSetItems, NUM_TOOLS, items);
      send_message(win, toolSetActiveItem, ID_TOOL_SELECT, NULL);
      return true;
    }

    case evCommand:
      // kToolboxNotificationClicked: a tool button was pressed.
      if (HIWORD(wparam) == kToolboxNotificationClicked) {
        int ident = (int)(int16_t)LOWORD(wparam);
        if (g_app) {
          g_app->current_tool = ident;
          if (g_app->doc && g_app->doc->canvas_win)
            invalidate_window(g_app->doc->canvas_win);
          if (g_app->menubar_win)
            send_message(g_app->menubar_win, evCommand,
                         MAKEDWORD((uint16_t)ident, kButtonNotificationClicked),
                         lparam);
          else
            handle_menu_command((uint16_t)ident);
        }
        return true;
      }
      return false;

    default:
      return win_toolbox(win, msg, wparam, lparam);
  }
}

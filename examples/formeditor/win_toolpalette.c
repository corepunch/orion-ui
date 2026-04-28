// Tool palette for the form editor.
// Uses win_toolbox directly — no extra client content below the grid.
// toolbox.png is a 21x21-px-per-tile sprite sheet in VB3 toolbox order.

#include "formeditor.h"
#include "../../commctl/commctl.h"

// Tool palette definition: ident and icon index pairs in display order.
// Strip layout: 3 columns x N rows of 21x21 tiles.
static const toolbox_item_t k_tools[NUM_TOOLS] = {
  { ID_TOOL_SELECT,    0 },   // Pointer / Select
  { ID_TOOL_LABEL,     2 },   // Label
  { ID_TOOL_TEXTEDIT,  3 },   // TextBox
  { ID_TOOL_BUTTON,    5 },   // CommandButton
  { ID_TOOL_CHECKBOX,  6 },   // CheckBox
  { ID_TOOL_COMBOBOX,  8 },   // ComboBox
  { ID_TOOL_LIST,      9 },   // ListBox
};

result_t win_tool_palette_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      // Delegate to win_toolbox first so its state is ready.
      win_toolbox(win, msg, wparam, lparam);

      // Use a larger button size to fit the 21px icons with comfortable margin.
      send_message(win, bxSetButtonSize, FE_TOOLBOX_BTN_SIZE, NULL);

      // Load the VB3-style icon strip.
#ifdef SHAREDIR
      char path[4096];
      snprintf(path, sizeof(path), "%s/" SHAREDIR "/toolbox.png", ui_get_exe_dir());
      send_message(win, bxLoadStrip, FE_TOOLBOX_ICON_W, path);
#endif

      // Set tools from unified array.
      send_message(win, bxSetItems, NUM_TOOLS, (void *)k_tools);
      send_message(win, bxSetIconTintBrush, brTextNormal, NULL);
      send_message(win, bxSetActiveItem, ID_TOOL_SELECT, NULL);
      return true;
    }

    case evCommand:
      // bxClicked: a tool button was pressed.
      if (HIWORD(wparam) == bxClicked) {
        int ident = (int)(int16_t)LOWORD(wparam);
        if (g_app) {
          g_app->current_tool = ident;
          if (g_app->doc && g_app->doc->canvas_win)
            invalidate_window(g_app->doc->canvas_win);
          if (g_app->menubar_win)
            send_message(g_app->menubar_win, evCommand,
                         MAKEDWORD((uint16_t)ident, btnClicked),
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

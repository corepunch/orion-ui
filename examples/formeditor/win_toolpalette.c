// Tool palette — loads toolbox.png (23x23 icons, VB3 toolbox order)
// and presents them via WINDOW_TOOLBAR with a custom TOOLBOX_BTN_SIZE.

#include "formeditor.h"
#include "../../commctl/commctl.h"

// VB3 toolbox strip indices for each tool in display order.
// Strip layout (3 cols x 13 rows, 23x23): index = row*3+col.
// VB3 canonical order: 0=Pointer, 1=PictureBox, 2=Label, 3=TextBox,
//   4=Frame, 5=CommandButton, 6=CheckBox, 7=OptionButton,
//   8=ComboBox, 9=ListBox, ...
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
  0,   // Pointer/Select
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
    case kWindowMessageCreate: {
      // Set button size before adding buttons so titlebar_height() is correct.
      send_message(win, kToolBarMessageSetButtonSize, TOOLBOX_BTN_SIZE, NULL);

      // Load toolbox.png into the toolbar strip.  The framework owns the GL
      // texture; no manual glDeleteTextures needed.
#ifdef SHAREDIR
      char path[4096];
      snprintf(path, sizeof(path), "%s/" SHAREDIR "/toolbox.png", ui_get_exe_dir());
      send_message(win, kToolBarMessageLoadStrip, TOOLBOX_ICON_W, path);
#endif

      toolbar_button_t buttons[NUM_TOOLS];
      for (int i = 0; i < NUM_TOOLS; i++) {
        buttons[i].icon   = k_tool_icon[i];
        buttons[i].ident  = k_tool_order[i];
        buttons[i].active = (k_tool_order[i] == ID_TOOL_SELECT);
      }
      send_message(win, kToolBarMessageAddButtons, NUM_TOOLS, buttons);
      return true;
    }
    case kWindowMessagePaint:
      fill_rect(get_sys_color(kColorWindowDarkBg), 0, 0,
                win->frame.w, win->frame.h);
      return true;
    case kToolBarMessageButtonClick: {
      int ident = (int)wparam;
      send_message(win, kToolBarMessageSetActiveButton, (uint32_t)ident, NULL);
      if (g_app) {
        g_app->current_tool = ident;
        if (g_app->doc && g_app->doc->canvas_win)
          invalidate_window(g_app->doc->canvas_win);
        if (g_app->menubar_win)
          send_message(g_app->menubar_win, kWindowMessageCommand,
                       MAKEDWORD((uint16_t)ident, kButtonNotificationClicked), lparam);
        else
          handle_menu_command((uint16_t)ident);
      }
      return true;
    }
    default:
      return false;
  }
}

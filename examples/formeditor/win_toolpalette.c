// Tool palette for the form editor.
// Uses win_toolbox directly — no extra client content below the grid.
// toolbox.png is a 21x21-px-per-tile sprite sheet in VB3 toolbox order.

#include "formeditor.h"
#include "../../commctl/commctl.h"

static toolbox_item_t g_tools[FE_MAX_COMPONENTS + 1];
static int g_tool_count = 0;

static int palette_win_y(void) {
  return MENUBAR_HEIGHT + 4;
}

static int palette_win_h(void) {
  int items = 1;  // Select tool
  for (int i = 0; i < fe_component_count(); i++) {
    const fe_component_desc_t *c = fe_component_at(i);
    if (!c) continue;
    if ((c->capabilities & (FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBOX)) ==
        (FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBOX))
      items++;
  }
  int rows = (items + TOOLBOX_COLS - 1) / TOOLBOX_COLS;
  return TITLEBAR_HEIGHT + rows * FE_TOOLBOX_BTN_SIZE + 4;
}

static window_t *create_tool_palette(hinstance_t hinstance) {
  window_t *tp = create_window(
      "Tools",
      WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
      MAKERECT(PALETTE_WIN_X, palette_win_y(), PALETTE_WIN_W, palette_win_h()),
      NULL, win_tool_palette_proc, hinstance, NULL);
  if (tp) show_window(tp, true);
  return tp;
}

void formeditor_rebuild_tool_palette(void) {
  if (!g_app) return;
  if (g_app->tool_win) {
    destroy_window(g_app->tool_win);
    g_app->tool_win = NULL;
  }
  g_app->current_tool = ID_TOOL_SELECT;
  g_app->tool_win = create_tool_palette(g_app->hinstance);
}

static void build_tool_items(void) {
  g_tool_count = 0;
  g_tools[g_tool_count++] = (toolbox_item_t){ ID_TOOL_SELECT, 0, "Select" };
  for (int i = 0; i < fe_component_count() && g_tool_count < FE_MAX_COMPONENTS + 1; i++) {
    const fe_component_desc_t *c = fe_component_at(i);
    if (!c) continue;
    if ((c->capabilities & (FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBOX)) !=
        (FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBOX))
      continue;
    g_tools[g_tool_count++] = (toolbox_item_t){ c->toolbox_ident, c->toolbox_icon, c->display_name };
  }
}

result_t win_tool_palette_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      // Delegate to win_toolbox first so its state is ready.
      win_toolbox(win, msg, wparam, lparam);

      // Use a larger button size to fit the 21px icons with comfortable margin.
      send_message(win, bxSetButtonSize, FE_TOOLBOX_BTN_SIZE, NULL);

      // Prefer the loaded component plugin's embedded icon strip; fall back to
      // the historical loose toolbox.png while developing old plugins.
      fe_icon_strip_t strip;
      if (fe_component_toolbox_icon_strip(&strip)) {
        toolbox_load_strip_rgba(win, strip.icon_size, strip.rgba, strip.width, strip.height);
      } else {
#ifdef SHAREDIR
        char path[4096];
        snprintf(path, sizeof(path), "%s/" SHAREDIR "/toolbox.png", ui_get_exe_dir());
        send_message(win, bxLoadStrip, FE_TOOLBOX_ICON_W, path);
#endif
      }

      build_tool_items();
      send_message(win, bxSetItems, g_tool_count, (void *)g_tools);
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

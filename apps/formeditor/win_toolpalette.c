#include "formeditor.h"
#include <orion/commctl/commctl.h>
static toolbar_item_t g_tools[FE_MAX_COMPONENTS + 1];
static int g_tool_count = 0;

static void build_tool_items(void) {
  g_tool_count = 0;
  g_tools[g_tool_count++] = (toolbar_item_t){
      .type = TOOLBAR_ITEM_BUTTON, .ident = ID_TOOL_SELECT,
      .icon = "cursor-pointer",
      .tooltip = "Select",
  };

  for (int i = 0; i < fe_component_count() && g_tool_count < FE_MAX_COMPONENTS + 1; i++) {
    const fe_component_desc_t *c = fe_component_at(i);
    if (!c) continue;
    if ((c->capabilities & (FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBAR)) !=
        (FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBAR))
      continue;
    g_tools[g_tool_count++] = (toolbar_item_t){
        .type = TOOLBAR_ITEM_BUTTON, .ident = i,
        .icon = c->toolbar_icon,
        .tooltip = c->class_name,
    };
  }
}

static void select_tool_by_ident(window_t *win, int ident) {
  if (g_app) {
    g_app->current_tool = ident;
    window_t *doc = g_app->active_form;
    if (doc && doc->children)
      invalidate_window(doc->children);
    if (g_app->windows[FE_WIN_MENUBAR])
      send_message(g_app->windows[FE_WIN_MENUBAR], evCommand,
                   MAKEDWORD((uint16_t)ident, btnClicked),
                   win);
    else
      handle_menu_command((uint16_t)ident);
  }
}

static void populate_toolbar(window_t *win) {
  if (!win)
    return;

  build_tool_items();
  send_message(win, tbSetButtonSize, FE_TOOLBAR_BTN_SIZE, NULL);
  send_message(win, tbSetOrientation, TOOLBAR_VERTICAL, NULL);
  send_message(win, tbSetItems, (uint32_t)g_tool_count, g_tools);

  int current = g_app ? g_app->current_tool : ID_TOOL_SELECT;
  send_message(win, tbSetActiveButton, (uint32_t)current, NULL);
}

window_t *formeditor_create_tool_toolbar(hinstance_t hinstance) {
  build_tool_items();
  int padding = 2 * get_theme()->toolbar_padding;
  window_t *tp = create_window(
      "Tools",
      WINDOW_TOOLBAR | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
      MAKERECT(PALETTE_WIN_X, get_theme()->menubar_height + 4,
               FE_TOOLBAR_BTN_SIZE + padding,
               get_theme()->caption_height + g_tool_count * FE_TOOLBAR_BTN_SIZE +
               (g_tool_count - 1) * TOOLBAR_SPACING + padding),
      NULL, win_tool_palette_proc, hinstance, NULL);
  if (tp) show_window(tp, true);
  return tp;
}

lresult_t win_tool_palette_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      populate_toolbar(win);
      return true;

    case tbButtonClick:
      fprintf(stderr, "[fe] tool click win=%p ident=%u current=%d\n", (void *)win,
              wparam, g_app ? g_app->current_tool : -1);
      send_message(win, tbSetActiveButton, wparam, NULL);
      select_tool_by_ident(win, (int)wparam);
      return true;
    case evPaint:   return true;
    case evDestroy:
      if (g_app && g_app->windows[FE_WIN_TOOL] == win) g_app->windows[FE_WIN_TOOL] = NULL;
      return true;
    default:       return false;
  }
}

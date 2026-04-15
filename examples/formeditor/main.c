#include "formeditor.h"
#include "../../gem_magic.h"

app_state_t *g_app = NULL;

static const accel_t kAccelEntries[] = {
  { FCONTROL|FVIRTKEY, AX_KEY_N, ID_FILE_NEW  },
  { FCONTROL|FVIRTKEY, AX_KEY_O, ID_FILE_OPEN },
  { FCONTROL|FVIRTKEY, AX_KEY_S, ID_FILE_SAVE },
  { FVIRTKEY,          AX_KEY_DEL,       ID_EDIT_DELETE },
  { FVIRTKEY,          AX_KEY_BACKSPACE, ID_EDIT_DELETE },
};

// Compute palette window Y so its title bar sits 4px below the menu bar.
// Toolbar rows = ceil(NUM_TOOLS / 2), each row is TOOLBOX_BTN_SIZE tall.
static int palette_win_y(void) {
  int rows = (NUM_TOOLS + 1) / 2;
  return MENUBAR_HEIGHT + 4 + TITLEBAR_HEIGHT + rows * TOOLBOX_BTN_SIZE;
}

static void create_app_windows(hinstance_t hinstance) {
#ifndef BUILD_AS_GEM
  int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
  window_t *mb = create_window(
      "menubar",
      WINDOW_NOTITLE | WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
      MAKERECT(0, 0, sw, MENUBAR_HEIGHT),
      NULL, editor_menubar_proc, hinstance, NULL);
  send_message(mb, kMenuBarMessageSetMenus, (uint32_t)kNumMenus, (void *)kMenus);
  show_window(mb, true);
  g_app->menubar_win = mb;
#endif

  window_t *tp = create_window(
      "Tools",
      WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE | WINDOW_TOOLBAR,
      MAKERECT(PALETTE_WIN_X, palette_win_y(), PALETTE_WIN_W, PALETTE_WIN_H),
      NULL, win_tool_palette_proc, hinstance, NULL);
  show_window(tp, true);
  g_app->tool_win = tp;
}

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  (void)argc; (void)argv;
  g_app = calloc(1, sizeof(app_state_t));
  if (!g_app) return false;

  g_app->current_tool = ID_TOOL_SELECT;
  g_app->hinstance = hinstance;
  create_app_windows(hinstance);

  g_app->accel = load_accelerators(kAccelEntries,
      (int)(sizeof(kAccelEntries)/sizeof(kAccelEntries[0])));
  if (g_app->menubar_win)
    send_message(g_app->menubar_win, kMenuBarMessageSetAccelerators, 0, g_app->accel);

#ifdef BUILD_AS_GEM
  gem_interface_t *iface = gem_get_interface();
  iface->menus          = kMenus;
  iface->menu_count     = kNumMenus;
  iface->handle_command = handle_menu_command;
#endif

  create_form_doc(FORM_DEFAULT_W, FORM_DEFAULT_H);
  return true;
}

void gem_shutdown(void) {
  if (!g_app) return;
  free_accelerators(g_app->accel);
  g_app->accel = NULL;
  if (g_app->doc)
    close_form_doc(g_app->doc);
  free(g_app);
  g_app = NULL;
}

GEM_DEFINE("Form Editor", "1.0", gem_init, gem_shutdown, NULL)

#ifndef BUILD_AS_GEM
int main(int argc, char *argv[]) {
  if (!ui_init_graphics(UI_INIT_DESKTOP, "Orion Form Editor", SCREEN_W, SCREEN_H))
    return 1;
  if (!gem_init(argc, argv, 0)) {
    ui_shutdown_graphics();
    return 1;
  }
  while (ui_is_running()) {
    ui_event_t e;
    while (get_message(&e)) {
      if (!translate_accelerator(g_app->menubar_win, &e, g_app->accel))
        dispatch_message(&e);
    }
    repost_messages();
  }
  gem_shutdown();
  ui_shutdown_graphics();
  return 0;
}
#endif

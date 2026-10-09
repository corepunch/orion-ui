// Winamp — the classic Winamp 2 player, for iPhone first.

#include "winamp.h"
#include <orion/gem.h>

static const fe_component_desc_t kWinampComponents[] = {
  { .class_name = "WinampPlayer", .name_prefix = "IDC_WAP", .toolbar_icon = "Card",
    .default_size = { WA_W, WA_MAIN_H }, .capabilities = FE_COMPONENT_PLACEABLE,
    .proc = win_winamp_player, .default_flags = WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOTABSTOP },
  { .class_name = "WinampEqualizer", .name_prefix = "IDC_WAE", .toolbar_icon = "Card",
    .default_size = { WA_W, WA_EQ_H }, .capabilities = FE_COMPONENT_PLACEABLE,
    .proc = win_winamp_equalizer, .default_flags = WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOTABSTOP },
  { .class_name = "WinampPlaylist", .name_prefix = "IDC_WAL", .toolbar_icon = "Card",
    .default_size = { WA_W, WA_PL_MIN_H }, .capabilities = FE_COMPONENT_PLACEABLE,
    .proc = win_winamp_playlist, .default_flags = WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOTABSTOP | WINDOW_FLEXSPACE },
};

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  register_commctl_classes();
  for (int i = 0; i < (int)ARRAY_LEN(kWinampComponents); i++) register_window_class(&kWinampComponents[i]);
  if (!app_init()) return false;
  g_app->hinstance = hinstance;
  for (int i = 1; i < argc; i++) app_add_path(argv[i]);
  window_t *win = create_window_from_form(&winamp_main_window_form, 0, 0, NULL, win_winamp_main, hinstance, g_app);
  if (!win) { app_shutdown(g_app); return false; }
  show_window(win, true);
#ifdef AX_PLATFORM_IOS
  maximize_window(win);
#endif
  app_relayout();
  return true;
}

void gem_shutdown(void) {
  if (!g_app) return;
  if (g_app->win && is_window(g_app->win)) destroy_window(g_app->win);
  app_shutdown(g_app);
}

GEM_DEFINE("Winamp", "0.1", gem_init, gem_shutdown, NULL)

GEM_STANDALONE_MAIN("Winamp", 0, 550, 900, g_app->win, g_app->accel)

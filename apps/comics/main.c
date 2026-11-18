#include "comics.h"
#include <orion/gem.h>

static window_t *comics_main_win;
static bool comics_plugin_loaded;

static bool comics_open_file(const char *path) {
  return comics_open_cbz(comics_main_win, path);
}

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  register_commctl_classes();
  comics_plugin_loaded = comics_register_components();
  if (!comics_plugin_loaded) return false;

  char language[32] = {0};
#ifndef BUILD_AS_GEM
  ui_system_language(language, sizeof(language));
#endif
  for (int i = 1; i + 1 < argc; i++)
    if (!strcmp(argv[i], "--lang")) snprintf(language, sizeof(language), "%s", argv[++i]);
  if (language[0]) ui_set_language(language);
  char locale[4096];
  int n = snprintf(locale, sizeof(locale), "%s/../share/comics/locale", ui_get_exe_dir());
  if (n > 0 && (size_t)n < sizeof(locale)) ui_add_string_catalog_dir(locale);

  comics_main_win = comics_open_app(hinstance, ui_get_system_metrics(kSystemMetricScreenWidth),
                                   ui_get_system_metrics(kSystemMetricScreenHeight), true);
  if (!comics_main_win) return false;
  show_window(comics_main_win, true);
  maximize_window(comics_main_win);
#ifndef BUILD_AS_GEM
  ui_register_open_file_handler(comics_open_file);
#endif
  for (int i = 1; i < argc; i++)
    if (!strcmp(argv[i], "--lang")) i++;
    else if (comics_open_file(argv[i])) break;
  return true;
}

void gem_shutdown(void) {
#ifndef BUILD_AS_GEM
  ui_register_open_file_handler(NULL);
#endif
  if (is_window(comics_main_win)) destroy_window(comics_main_win);
  comics_main_win = NULL;
#ifndef AX_PLATFORM_IOS
  if (comics_plugin_loaded) fe_unload_component_plugins();
#endif
  comics_plugin_loaded = false;
}

static const char *comics_types[] = { ".cbz", NULL };
GEM_DEFINE("Comics", "1.0", gem_init, gem_shutdown, comics_types)
GEM_STANDALONE_MAIN("Comics", 0, 1080, 810, comics_main_win, NULL)

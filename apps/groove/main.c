// Groove — a block-based music sequencer in the spirit of Music 2000 and
// Dance eDJay: drag pre-made musical blocks from the bin onto the sheet.

#include "groove.h"
#include <orion/gem.h>
#include <orion/commctl/commctl.h>
#include <orion/user/svg_icon_loader.h>

static const fe_component_desc_t kGrooveComponents[] = {
  { .class_name = "GrooveArrangement", .name_prefix = "IDC_GSH", .toolbar_icon = "FlowView",
    .default_size = { 640, 420 }, .capabilities = FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBAR,
    .proc = win_sheet, .default_layout_size = { 0, 0 },
    .default_flags = WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_HSCROLL | WINDOW_FLEXSPACE,
    .default_h_align = LAYOUT_ALIGN_STRETCH, .default_v_align = LAYOUT_ALIGN_STRETCH },
  { .class_name = "GrooveBlockBin", .name_prefix = "IDC_GBN", .toolbar_icon = "FlowView",
    .default_size = { 800, 220 }, .capabilities = FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBAR,
    .proc = win_bin, .default_layout_size = { 0, -1 },
    .default_flags = WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL | WINDOW_NOACTIVATE,
    .default_h_align = LAYOUT_ALIGN_STRETCH, .default_v_align = LAYOUT_ALIGN_STRETCH },
  { .class_name = "GrooveBlockCard", .name_prefix = "IDC_GBC", .toolbar_icon = "Card",
    .default_size = { 128, 44 }, .capabilities = FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBAR,
    .proc = win_block_card, .default_layout_size = { 0, 0 },
    .default_flags = GR_CARD_FLAGS,
    .default_h_align = LAYOUT_ALIGN_STRETCH, .default_v_align = LAYOUT_ALIGN_STRETCH },
  { .class_name = "GrooveSongPreview", .name_prefix = "IDC_GSP", .toolbar_icon = "Card",
    .default_size = { 220, 84 }, .capabilities = FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBAR,
    .proc = win_song_preview, .default_layout_size = { 0, 0 },
    .default_flags = WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOACTIVATE | WINDOW_NOTABSTOP,
    .default_h_align = LAYOUT_ALIGN_STRETCH, .default_v_align = LAYOUT_ALIGN_STRETCH },
};

static void register_groove_components(void) {
  for (int i = 0; i < ARRAY_LEN(kGrooveComponents); i++) register_window_class(&kGrooveComponents[i]);
}

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  register_commctl_classes();
  register_groove_components();
  DB_CLASS(groove_library_db);
  DB_CLASS(groove_songs_db);
  char icons_path[1024];
  int n = snprintf(icons_path, sizeof(icons_path), "%s/../share/groove/icons", ui_get_exe_dir());
  if (n > 0 && (size_t)n < sizeof(icons_path)) svg_add_icons_dir(hinstance, icons_path);
  char language[32] = {0};
#ifndef BUILD_AS_GEM
  ui_system_language(language, sizeof(language));
#endif
  for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "--lang")) snprintf(language, sizeof(language), "%s", argv[i + 1]);
  if (language[0]) ui_set_language(language);
  char locale_path[1024];
  int locale_n = snprintf(locale_path, sizeof(locale_path), "%s/../share/groove/locale", ui_get_exe_dir());
  if (locale_n > 0 && (size_t)locale_n < sizeof(locale_path)) ui_add_string_catalog_dir(locale_path);
  ui_add_string_catalog_dir("apps/groove/share/locale");
  g_app = app_init();
  if (!g_app) return false;
  for (int i = 1; i < argc; i++) {
    int demo = -1;
    if (!strcmp(argv[i], "--demo") || !strcmp(argv[i], "--demo=skyline")) demo = GR_DEMO_SKYLINE;
    else if (!strcmp(argv[i], "--demo=afterglow")) demo = GR_DEMO_AFTERGLOW;
    else if (!strcmp(argv[i], "--demo=victory")) demo = GR_DEMO_VICTORY;
    if (demo >= 0 && !app_load_demo_song(demo)) { app_shutdown(g_app); return false; }
  }
  g_app->hinstance = hinstance;
  g_app->library_db = create_database("library", "groove_library_db", NULL);
  if (!g_app->library_db) { app_shutdown(g_app); g_app = NULL; return false; }
  ui_set_database(g_app->library_db);
  register_database("library", g_app->library_db);
  if (g_app->songs_dir[0] && (g_app->songs_db = create_database("songs", "groove_songs_db", g_app->songs_dir)))
    register_database("songs", g_app->songs_db);
  window_t *win = create_window_from_form(&groove_main_window_form, 0, 0, NULL, main_win_proc, hinstance, g_app);
  if (!win) { app_shutdown(g_app); return false; }
  show_window(win, true);
  maximize_window(win);
  return true;
}

void gem_shutdown(void) {
  if (!g_app) return;
  if (g_app->win && is_window(g_app->win)) destroy_window(g_app->win); // before the state its proc reads goes away
  app_shutdown(g_app);
}

GEM_DEFINE("Groove", "0.1", gem_init, gem_shutdown, NULL)

GEM_STANDALONE_MAIN("Orion Groove", UI_INIT_DESKTOP, SCREEN_W, SCREEN_H, g_app->menubar_win, g_app->accel)

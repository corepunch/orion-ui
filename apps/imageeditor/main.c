// Image Editor – MacPaint-inspired with color support
// MDI architecture: docked toolbars, floating color palette,
// menu bar, and multiple document windows.
// PNG open/save via libpng.

#include "imageeditor.h"
#include <orion/gem.h>
#include <orion/user/svg_icon_loader.h>

// Global application state
app_state_t *g_app = NULL;
static bool g_loaded_component_plugins = false;

#ifdef IMAGEEDITOR_BW_RETINA
int g_bw_retina_scale = 1;
#endif

// Application accelerators are generated from menu shortcuts in
// imageeditor.orion and loaded during application initialization.

// Resurrect 64 palette from LoSpec.com
static const uint32_t kDefaultPalette[NUM_COLORS] = {
  0xff2f222e, 0xff46353e, 0xff655562, 0xff624f69, 0xff8a707f, 0xffb2ab9b, 0xffd0dcc7, 0xffffffff,
  0xff27276e, 0xff3423ae, 0xff3138b3, 0xff3b3be8, 0xff364fea, 0xff4a7df5, 0xff1d6bfb, 0xff1796f7,
  0xff45307a, 0xff39459e, 0xff6c6c96, 0xff3d68cd, 0xff4e90e6, 0xff7a94ab, 0xff54b9fb, 0xff2bc2f9,
  0xff243e4c, 0xff336667, 0xff47a9a2, 0xff4be0d5, 0xff86fffb, 0xff6cdfcd, 0xff69db91, 0xff73bc1e,
  0xff4c5a16, 0xff639023, 0xff383631, 0xff4a4e37, 0xff647e54, 0xff84a992, 0xff90bab2, 0xff655e0b,
  0xff8f8a0b, 0xff9baf0e, 0xffb9e130, 0xffe2f88f, 0xff533332, 0xff774a48, 0xffb4654d, 0xffe69b4d,
  0xffffd38f, 0xff3f2945, 0xff753e6b, 0xffa95e90, 0xfff384a8, 0xffedadea, 0xff543c75, 0xff6f4ba2,
  0xff7f65cf, 0xff9980ed, 0xff5d1c83, 0xff5424c3, 0xff784ff0, 0xff8181f6, 0xff90a7fc, 0xffb0cbfd,
};

#ifndef BUILD_AS_GEM
static bool image_editor_open_file_handler(const char *path) {
  return imageeditor_open_file_path(path);
}
#endif

// ============================================================
// Application init
// ============================================================

static void create_app_windows(hinstance_t hinstance) {
#ifdef BUILD_AS_GEM
  g_app->menubar_win = set_app_menu(editor_menubar_proc, kMenus, kNumMenus,
                                    handle_menu_command, hinstance);
  g_app->chrome_win = create_app_chrome("Image Editor Chrome", NULL, NULL, 0,
                                        main_toolbar_proc, hinstance);
  g_app->main_toolbar_win = app_chrome_toolbar(g_app->chrome_win);
  imageeditor_sync_main_toolbar();
#else
  g_app->chrome_win = create_app_chrome("Image Editor Chrome", editor_menubar_proc,
                                        kMenus, kNumMenus, main_toolbar_proc,
                                        hinstance);
  g_app->menubar_win      = app_chrome_menubar(g_app->chrome_win);
  g_app->main_toolbar_win = app_chrome_toolbar(g_app->chrome_win);
  imageeditor_sync_main_toolbar();
#endif

  create_tool_palette_window();
  create_tool_options_window();
#if !IMAGEEDITOR_BW
  create_color_palette_window();
  create_layers_window();
#endif
  create_timeline_window();
}

// ============================================================
// .gem entry points
// ============================================================

#if IMAGEEDITOR_BW
static const char *image_editor_types[] = { ".pcx", ".bmp", NULL };
#elif IMAGEEDITOR_INDEXED
static const char *image_editor_types[] = { ".pcx", ".bmp", NULL };
#else
static const char *image_editor_types[] = { ".png", ".bmp", ".jpg", ".jpeg", NULL };
#endif

static bool has_ext(const char *path, const char *ext) {
  if (!path || !ext) return false;
  size_t path_len = strlen(path);
  size_t ext_len = strlen(ext);
  if (path_len < ext_len) return false;
  return strcmp(path + path_len - ext_len, ext) == 0;
}

static bool is_gem_module_path(const char *path) {
  return has_ext(path, ".gem");
}

static int open_startup_documents(int argc, char *argv[]) {
  int opened = 0;
  for (int i = 1; i < argc; i++) {
    const char *path = argv[i];
    if (!path || !path[0]) continue;
    // In gem mode argv[0] is the gem itself and argv[1..] are payload files.
    // In standalone mode argv[0] is the executable, but callers may still
    // pass image paths on the command line. Skip any .gem module paths.
    if (is_gem_module_path(path)) continue;
    if (imageeditor_open_file_path(path))
      opened++;
  }
  return opened;
}

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  g_app = calloc(1, sizeof(app_state_t));
  if (!g_app) return false;

  {
    char icons_path[4096];
    int n = snprintf(icons_path, sizeof(icons_path), "%s/../share/imageeditor/icons",
                     ui_get_exe_dir());
    if (n > 0 && (size_t)n < sizeof(icons_path))
      svg_add_icons_dir(icons_path);
  }

#if IMAGEEDITOR_DEBUG
  {
    char log_path[1024];
    int path_len = snprintf(log_path, sizeof(log_path), "%s/imageeditor.log",
                            axSettingsDirectory());
    if (path_len >= 0 && (size_t)path_len < sizeof(log_path)) {
      if (axSetLogFile(log_path))
        axLog("[imageeditor] logging initialized: %s", axGetLogFile());
    }
  }
#endif

  g_app->current_tool = ID_TOOL_PENCIL;
  g_app->hinstance    = hinstance;
#if IMAGEEDITOR_BW
  // BW mode: simple black/white palette, use pencil tool by default.
  // The ipal will be initialized in create_document() with just 3 entries:
  // 0=transparent, 1=black, 2=white.
  g_app->fg_color = MAKE_COLOR(0x00, 0x00, 0x00, 0xFF); // black
  g_app->bg_color = MAKE_COLOR(0xFF, 0xFF, 0xFF, 0xFF); // white
  g_app->fg_palette_idx = 1; // index 1 = black (foreground draws black)
  #ifdef IMAGEEDITOR_BW_RETINA
  g_bw_retina_scale = MAX(1, (int)(axGetScaling() + 0.5f));
  #endif
#else
  memcpy(g_app->palette, kDefaultPalette, sizeof(kDefaultPalette));
  g_app->fg_color = g_app->palette[4];
  g_app->bg_color = g_app->palette[0];
#endif
  g_app->brush_size = 1;  // default: radius 1 (3px diameter)
  g_app->text_tool.font_size = 16;
  g_app->text_tool.antialias = true;
  g_app->wand.antialias = true;
  g_app->wand.spread = 24;
  g_app->wand.overlay_color = MAKE_COLOR(0x40, 0xA0, 0xFF, 0x55);
  g_app->grid.spacing.x = 16;
  g_app->grid.spacing.y = 16;
  {
    static const float kDefaultPrev[ONION_SKIN_MAX_STEPS] = { 50.0f, 25.0f, 12.5f, 0.0f };
    static const float kDefaultNext[ONION_SKIN_MAX_STEPS] = { 50.0f, 25.0f, 12.5f, 0.0f };
    g_app->anim_trace_enabled = true;
    memcpy(g_app->anim_trace_prev_opacity, kDefaultPrev, sizeof(kDefaultPrev));
    memcpy(g_app->anim_trace_next_opacity, kDefaultNext, sizeof(kDefaultNext));
    g_app->anim_trace_frames = 0;
    for (int i = 0; i < ONION_SKIN_MAX_STEPS; i++) {
      if (kDefaultPrev[i] > 0 || kDefaultNext[i] > 0)
        g_app->anim_trace_frames = i + 1;
    }
  }

#ifndef BUILD_AS_GEM
  ui_register_open_file_handler(image_editor_open_file_handler);
#endif

  srand((unsigned int)time(NULL));

  register_commctl_classes();
  
  // Register tool handlers for the new dispatch system (Phase 3)
  register_builtin_tools();

  if (!imageeditor_render_effects_init()) {
    free(g_app);
    g_app = NULL;
    return false;
  }

  create_app_windows(hinstance);
#if !IMAGEEDITOR_INDEXED
  imageeditor_load_filters();
#endif

#ifdef AX_PLATFORM_IOS
  extern int fe_plugin_class_count(void);
  extern const fe_component_desc_t *fe_plugin_class_desc(int);
  for (int i = 0; i < fe_plugin_class_count(); i++) register_window_class(fe_plugin_class_desc(i));
#else
  {
    char path[4096];
    int n = snprintf(path, sizeof(path), "%s/../lib/imageeditor_components%s",
                     ui_get_exe_dir(), AX_DYNLIB_EXT);
    if (n > 0 && (size_t)n < sizeof(path)) {
      g_loaded_component_plugins = fe_load_component_plugin(path);
      if (!g_loaded_component_plugins)
        IE_DEBUG("failed to load component plugin: %s", path);
    } else {
      IE_DEBUG("component plugin path was truncated");
    }
  }

#endif

  g_app->accel = load_accelerators(imageeditor_default_accels,
                                   imageeditor_default_accel_count);
  if (g_app->menubar_win)
    send_message(g_app->menubar_win, kMenuBarMessageSetAccelerators, 0, g_app->accel);

  if (open_startup_documents(argc, argv) == 0)
    create_document(NULL, CANVAS_W, CANVAS_H);

  /* Splash screen disabled for image editor startup.
#ifdef SHAREDIR
  {
    char splash_path[4096];
    int path_len = snprintf(splash_path, sizeof(splash_path), "%s/" SHAREDIR "/splash.jpg",
             ui_get_exe_dir());
    if (path_len >= 0 && (size_t)path_len < sizeof(splash_path))
      show_splash_screen(splash_path, hinstance);
  }
#endif
  */

  return true;
}

void gem_shutdown(void) {
  if (!g_app) return;

  imageeditor_render_effects_shutdown();

#if IMAGEEDITOR_DEBUG
  if (axGetLogFile()[0])
    axLog("[imageeditor] shutting down");
  axSetLogFile(NULL);
#endif

  free_accelerators(g_app->accel);
  g_app->accel = NULL;

  free(g_app->clipboard);
  g_app->clipboard = NULL;

  if (g_app->chrome_win && is_window(g_app->chrome_win)) {
    destroy_window(g_app->chrome_win);
  } else if (g_app->main_toolbar_win && is_window(g_app->main_toolbar_win)) {
    destroy_window(g_app->main_toolbar_win);
  }
  g_app->chrome_win = g_app->menubar_win = g_app->main_toolbar_win = NULL;

#if !IMAGEEDITOR_INDEXED
  imageeditor_free_filters();
#endif // !IMAGEEDITOR_INDEXED

  if (g_loaded_component_plugins) {
    fe_unload_component_plugins();
    g_loaded_component_plugins = false;
  }

  while (g_app->docs)
    close_document(g_app->docs);
  free(g_app);
  g_app = NULL;
}

#if IMAGEEDITOR_BW
GEM_DEFINE("Pencil Test", "1.0", gem_init, gem_shutdown, image_editor_types)

GEM_STANDALONE_MAIN("Orion Pencil Test", UI_INIT_DESKTOP, SCREEN_W, SCREEN_H,
                    g_app->menubar_win, g_app->accel)
#elif IMAGEEDITOR_INDEXED
GEM_DEFINE("Image Editor 256", "1.0", gem_init, gem_shutdown, image_editor_types)

GEM_STANDALONE_MAIN("Orion Image Editor 256", UI_INIT_DESKTOP, SCREEN_W, SCREEN_H,
                    g_app->menubar_win, g_app->accel)
#else
GEM_DEFINE("Image Editor", "1.0", gem_init, gem_shutdown, image_editor_types)

GEM_STANDALONE_MAIN("Orion Image Editor", UI_INIT_DESKTOP, SCREEN_W, SCREEN_H,
                    g_app->menubar_win, g_app->accel)
#endif

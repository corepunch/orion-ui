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
  g_app->chrome_win = create_application_chrome("Image Editor Chrome", NULL, NULL, 0,
                                        main_toolbar_proc, &imageeditor_application_toolbar, hinstance);
  g_app->main_toolbar_win = app_chrome_toolbar(g_app->chrome_win);
  imageeditor_sync_main_toolbar();
#else
  g_app->chrome_win = create_application_chrome("Image Editor Chrome", editor_menubar_proc,
                                        kMenus, kNumMenus, main_toolbar_proc,
                                        &imageeditor_application_toolbar, hinstance);
  g_app->menubar_win      = app_chrome_menubar(g_app->chrome_win);
  g_app->main_toolbar_win = app_chrome_toolbar(g_app->chrome_win);
  imageeditor_sync_main_toolbar();
#endif

  create_tool_palette_window();
  create_tool_options_window();
#if !IMAGEEDITOR_BW
  create_color_palette_window();
  create_layers_window();
  create_timeline_window();
#endif
}

// ============================================================
// .gem entry points
// ============================================================

#if IMAGEEDITOR_BW
static const char *image_editor_types[] = { ".ptf", ".flc", ".fli", ".pcx", ".bmp", NULL };
#elif IMAGEEDITOR_INDEXED
static const char *image_editor_types[] = { ".flc", ".fli", ".pcx", ".bmp", NULL };
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
  memcpy(g_app->palette, pencil_default_colors(), sizeof(g_app->palette));

  {
    char icons_path[4096];
    int n = snprintf(icons_path, sizeof(icons_path), "%s/../share/imageeditor/icons",
                     ui_get_exe_dir());
    if (n > 0 && (size_t)n < sizeof(icons_path))
      svg_add_icons_dir(hinstance, icons_path);
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
  g_app->shape_tool = ID_TOOL_RECT;
  g_app->hinstance    = hinstance;
#if IMAGEEDITOR_BW
  // Pencil Test starts with ink; coloring swatches share the indexed palette.
  // ipal is initialized in create_document(): 0=ink, 1=paper, 255=transparent.
  g_app->fg_color = pencil_configured_color();
  g_app->bg_color = IE_PAPER_COLOR;
  g_app->fg_palette_idx = 0;
  #ifdef IMAGEEDITOR_BW_RETINA
  g_bw_retina_scale = MAX(1, (int)(axGetScaling() + 0.5f));
  #endif
#else
  g_app->fg_color = g_app->palette[0];
  g_app->bg_color = g_app->palette[1];
#endif
  // Keep the initial drawing weight here so pencil, brush, and shape tools
  // start consistently. Change this index if the preferred default evolves.
  g_app->brush_size = 2;  // default: radius 2 (5px diameter)
  g_app->text_tool.font_size = 16;
  g_app->text_tool.antialias = true;
  g_app->wand.antialias = true;
  g_app->wand.spread = 24;
  g_app->wand.overlay_color = MAKE_COLOR(0x40, 0xA0, 0xFF, 0x55);
#if IMAGEEDITOR_BW
  g_app->fill.gap = IE_FILL_GAP_SMALL;
#else
  g_app->fill.gap = IE_FILL_GAP_OFF;
#endif
  g_app->grid.spacing.x = 16;
  g_app->grid.spacing.y = 16;
  {
    static const float kDefaultPrev[ONION_SKIN_MAX_STEPS] = { 25.0f, 12.5f, 6.25f, 0.0f };
    static const float kDefaultNext[ONION_SKIN_MAX_STEPS] = { 25.0f, 12.5f, 6.25f, 0.0f };
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

  if (open_startup_documents(argc, argv) == 0) {
    int w, h;
    imageeditor_default_canvas_size(&w, &h);
    create_document(NULL, w, h);
  }

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

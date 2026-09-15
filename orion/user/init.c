// Graphics context initialization and management
// Abstraction layer over the platform library / OpenGL

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#else
#  include <unistd.h>
#endif

#include <platform/platform.h>
#include "gl_compat.h"
#include "user.h"
#include "draw.h"
#include "image.h"
#include "theme.h"
#include "svg_icon_loader.h"
#include <orion/commctl/commctl.h>
#include <orion/kernel/kernel.h>

bool ui_init_prog(void);
void ui_shutdown_prog(void);
result_t win_tray(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

// Set to true after ui_init_graphics() succeeds; guards begin/end frame.
static bool g_graphics_initialized = false;

// Internal white texture for drawing solid colors
uint32_t ui_white_texture = 0;

// Internal 4x4 checker texture for drawing selection outlines
uint32_t ui_checker_texture = 0;

// Internal 2x2 checker texture for transparency backgrounds.
uint32_t ui_transparency_checker_texture = 0;

// Theme icon strip loaded from share/orion/theme.png (144x18 px grayscale,
// 9x9 tiles).  Indexed by theme_icon_t (user/theme.h).
static bitmap_strip_t g_theme_strip = {0};
static uint32_t g_theme_tex = 0;

// Initialize the internal white texture
void init_ui_white_texture(void) {
  if (ui_white_texture == 0) {
    uint32_t white_pixel = 0xFFFFFFFF;
    ui_white_texture = R_CreateTextureRGBA(1, 1, &white_pixel,
                                           R_FILTER_NEAREST, R_WRAP_CLAMP);
  }
}

// Initialize the 4x4 checker texture used for dashed selection outlines.
void init_ui_checker_texture(void) {
  if (ui_checker_texture == 0) {
    static const uint8_t pixels[4 * 4 * 4] = {
      /* row 0 */   0,  0,  0,255,   0,  0,  0,255, 255,255,255,255, 255,255,255,255,
      /* row 1 */   0,  0,  0,255,   0,  0,  0,255, 255,255,255,255, 255,255,255,255,
      /* row 2 */ 255,255,255,255, 255,255,255,255,   0,  0,  0,255,   0,  0,  0,255,
      /* row 3 */ 255,255,255,255, 255,255,255,255,   0,  0,  0,255,   0,  0,  0,255,
    };
    ui_checker_texture = R_CreateTextureRGBA(4, 4, pixels,
                                             R_FILTER_NEAREST, R_WRAP_REPEAT);
  }
}

// Initialize the 2x2 checker texture used for transparency backgrounds.
void init_ui_transparency_checker_texture(void) {
  if (ui_transparency_checker_texture == 0) {
    static const uint8_t pixels[2 * 2 * 4] = {
      /* row 0 */ 208, 208, 208, 255, 176, 176, 176, 255,
      /* row 1 */ 176, 176, 176, 255, 208, 208, 208, 255,
    };
    ui_transparency_checker_texture = R_CreateTextureRGBA(2, 2, pixels,
                                                          R_FILTER_NEAREST,
                                                          R_WRAP_REPEAT);
  }
}

// Register the built-in icon directory for on-demand sysicon_resolve() lookups.
static void init_icons_dir(void) {
  char icons_dir[4096];
  snprintf(icons_dir, sizeof(icons_dir), "%s/../share/orion/icons",
           ui_get_exe_dir());
  svg_set_icons_dir(icons_dir);
}

// Load the theme icon sheet from <exe_dir>/../share/orion/theme.png.
// theme.png is a grayscale PNG.  load_image() expands it to RGBA with
// R=G=B=gray, A=255.  We convert in-place to R=G=B=255, A=gray so
// the icons can be tinted at draw time (white pixels opaque, black transparent).
static void init_theme_strip(void) {
  if (g_theme_tex != 0) return;
  char path[4096];
  snprintf(path, sizeof(path), "%s/../share/orion/theme.png",
           ui_get_exe_dir());
  int w = 0, h = 0;
  uint8_t *src = load_image(path, &w, &h);
  if (!src) return;
  if (w < THEME_ICON_SIZE || h < THEME_ICON_SIZE ||
      (w % THEME_ICON_SIZE) != 0 || (h % THEME_ICON_SIZE) != 0) {
    image_free(src);
    return;
  }
  // Convert in-place: use the red channel (= grayscale) as alpha, set RGB=255.
  int n = w * h;
  for (int i = 0; i < n; i++) {
    uint8_t gray = src[i * 4];   // R == G == B for grayscale images
    src[i * 4 + 0] = 255;
    src[i * 4 + 1] = 255;
    src[i * 4 + 2] = 255;
    src[i * 4 + 3] = gray;
  }
  g_theme_tex = R_CreateTextureRGBA(w, h, src, R_FILTER_NEAREST, R_WRAP_CLAMP);
  image_free(src);
  g_theme_strip.tex     = g_theme_tex;
  g_theme_strip.icon_w  = THEME_ICON_SIZE;
  g_theme_strip.icon_h  = THEME_ICON_SIZE;
  g_theme_strip.cols    = w / THEME_ICON_SIZE;
  g_theme_strip.sheet_w = w;
  g_theme_strip.sheet_h = h;
}

static void shutdown_theme_strip(void) {
  SAFE_DELETE(g_theme_tex, R_DeleteTexture);
  g_theme_strip = (bitmap_strip_t){0};
}

// Return the theme icon strip (theme.png), or NULL if not loaded.
bitmap_strip_t *ui_get_theme_strip(void) {
  return (g_theme_strip.tex != 0) ? &g_theme_strip : NULL;
}

void shutdown_ui_textures(void) {
  SAFE_DELETE(ui_white_texture, R_DeleteTexture);
  SAFE_DELETE(ui_checker_texture, R_DeleteTexture);
  SAFE_DELETE(ui_transparency_checker_texture, R_DeleteTexture);
}

void shutdown_white_texture(void) {
  shutdown_ui_textures();
}

static window_t *g_desktop_window;
static bool g_desktop_enabled;
static bool g_syncing_desktop;

window_t *get_desktop_window(void) {
  return g_desktop_window && is_window(g_desktop_window) ? g_desktop_window : NULL;
}

static result_t win_desktop(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win;
  (void)wparam;
  (void)lparam;
  switch (msg) {
    case evPaint:
      fill_rect(0xff6B3529,
                R(0, 0,
                  ui_get_system_metrics(kSystemMetricScreenWidth),
                  ui_get_system_metrics(kSystemMetricScreenHeight)));
      return false; // continue painting desktop children such as icons
    case evDestroy:
      if (g_desktop_window == win) g_desktop_window = NULL;
      return false;
  }
  return false;
}

void sync_desktop_window(void) {
  if (g_syncing_desktop) return;
  g_syncing_desktop = true;
  bool replaced = false;
  for (window_t *win = g_ui_runtime.windows; win; win = win->next)
    if (win->maximized && window_has_state(win, WINDOW_STATE_VISIBLE)) replaced = true;
  if ((!g_desktop_enabled || replaced) && get_desktop_window()) {
    fprintf(stderr, "[desktop] release win=%u replaced=%d\n", g_desktop_window->id, replaced);
    fflush(stderr);
    destroy_window(g_desktop_window);
  } else if (g_desktop_enabled && !replaced && !get_desktop_window()) {
    g_desktop_window = create_window("Desktop",
      WINDOW_NOTITLE | WINDOW_ALWAYSINBACK | WINDOW_NOTRAYBUTTON | WINDOW_NOACTIVATE,
      MAKERECT(0, 0, ui_get_system_metrics(kSystemMetricScreenWidth),
                     ui_get_system_metrics(kSystemMetricScreenHeight)), NULL, win_desktop, 0, NULL);
    fprintf(stderr, "[desktop] recreate win=%p\n", (void *)g_desktop_window);
    fflush(stderr);
  }
  for (window_t *win = g_ui_runtime.windows; win; win = win->next)
    invalidate_window(win);
  g_syncing_desktop = false;
}

void enable_desktop_window(bool enabled) {
  g_desktop_enabled = enabled;
  sync_desktop_window();
}

// Initialize graphics context (platform + OpenGL)
bool ui_init_graphics(int flags, const char *title, int width, int height) {
  // Guard against double-initialization (e.g. when a gem calls this
  // after the shell has already initialized the context).
  if (g_graphics_initialized) return true;

  axInit();

  uint32_t pixel_w = (uint32_t)(width * UI_WINDOW_SCALE);
  uint32_t pixel_h = (uint32_t)(height * UI_WINDOW_SCALE);
  uint32_t window_flags = AX_WINDOW_RESIZABLE | (ORION_ALLOW_HIGHDPI ? AX_WINDOW_HIGHDPI : 0);
  if (flags & UI_INIT_HIDDEN) {
    if (!axCreateSurface(pixel_w, pixel_h) &&
        !axCreateWindow(title, pixel_w, pixel_h, window_flags | AX_WINDOW_HIDDEN)) {
      fprintf(stderr, "[ui] hidden graphics context could not be created\n");
      fflush(stderr);
      axShutdown();
      return false;
    }
  } else if (!axCreateWindow(title, pixel_w, pixel_h, window_flags)) {
    printf("Window could not be created!\n");
    axShutdown();
    return false;
  }

  axBeginPaint();

#ifdef _WIN32
  /* GLEW must be initialized after the OpenGL context is made current. */
  glewExperimental = GL_TRUE;
  GLenum glew_err = glewInit();
  if (glew_err != GLEW_OK) {
    printf("GLEW init failed: %s\n", (const char *)glewGetErrorString(glew_err));
    axShutdown();
    return false;
  }
#endif

  printf("GL_VERSION  : %s\n", glGetString(GL_VERSION));
  printf("GLSL_VERSION: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

  if (!(flags & UI_INIT_HIDDEN))
    axSetSwapInterval(1);

  if (!ui_init_prog()) {
    axShutdown();
    return false;
  }

  init_ui_white_texture();
  init_ui_checker_texture();
  init_ui_transparency_checker_texture();
  init_icons_dir();
  init_theme_strip();

  init_console();

  enable_desktop_window((flags & UI_INIT_DESKTOP) != 0);
  if ((flags & UI_INIT_DESKTOP) && !get_desktop_window()) {
    fprintf(stderr, "[ui] desktop window could not be created\n");
    fflush(stderr);
    ui_shutdown_graphics();
    return false;
  }

  if (flags & UI_INIT_TRAY) {
    window_t *tray = create_window("Tray",
                                   WINDOW_NOTITLE|WINDOW_NOTRAYBUTTON,
                                   MAKERECT(0, 0, 0, 0),
                                   NULL, win_tray, 0, NULL);
    if (!tray) {
      fprintf(stderr, "[ui] tray window could not be created\n");
      fflush(stderr);
      ui_shutdown_graphics();
      return false;
    }
    show_window(tray, true);
  }

  g_ui_runtime.running = true;
  g_graphics_initialized = true;

  return true;
}

// Cleanup all windows
static void cleanup_all_windows(void) {
  while (g_ui_runtime.windows) {
    destroy_window(g_ui_runtime.windows);
  }
}

// Shutdown graphics context
void ui_shutdown_graphics(void) {
  g_desktop_enabled = false;
  g_desktop_window = NULL;
  cleanup_all_windows();

  extern void cleanup_all_hooks(void);
  cleanup_all_hooks();

  if (ui_joystick_available()) {
    ui_joystick_shutdown();
  }

  ui_shutdown_prog();

  shutdown_theme_strip();
  shutdown_white_texture();

  shutdown_console();

  axShutdown();
  g_graphics_initialized = false;
}

// Application lifecycle accessors.
bool ui_is_running(void) {
  return g_ui_runtime.running;
}

void ui_request_quit(void) {
  g_ui_runtime.running = false;
}

// Shell-execute hook — analogous to Win32 ShellExecute().
static ui_open_file_handler_t g_open_file_handler = NULL;

void ui_register_open_file_handler(ui_open_file_handler_t handler) {
  g_open_file_handler = handler;
}

bool ui_open_file(const char *path) {
  if (g_open_file_handler)
    return g_open_file_handler(path);
  return false;
}

// Begin a render frame: make GL context current and bind platform framebuffer.
// Must be called once per frame before any OpenGL drawing.
// No-op when graphics have not been initialized (e.g. headless unit tests).
void ui_begin_frame(void) {
  if (!g_graphics_initialized) return;
  axBeginPaint();
}

// End a render frame: present the rendered content through the platform backend.
// No-op when graphics have not been initialized.
void ui_end_frame(void) {
  if (!g_graphics_initialized) return;
  axEndPaint();
}

static bool screenshot_path_is(const char *path, const char *extension) {
  size_t path_len = path ? strlen(path) : 0;
  size_t extension_len = strlen(extension);
  return path_len >= extension_len &&
         strcmp(path + path_len - extension_len, extension) == 0;
}

// Take a screenshot of the current framebuffer. PNG is lossless; JPEG uses
// quality 1-100. The filename extension selects the encoder.
bool ui_save_screenshot(const char *path, int quality) {
  struct AXsize sz;
  axGetSize(&sz);
  float scale = axGetScaling();
  int pw = (int)((float)sz.width * scale);
  int ph = (int)((float)sz.height * scale);
  if (pw <= 0 || ph <= 0) return false;
  uint8_t *rgba = malloc((size_t)pw * ph * 4);
  if (!rgba) return false;
  bool ok = capture_framebuffer_rgba(pw, ph, rgba);
  if (!ok) { free(rgba); return false; }
  if (screenshot_path_is(path, ".png"))
    ok = save_image_png(path, rgba, pw, ph);
  else if (screenshot_path_is(path, ".jpg") || screenshot_path_is(path, ".jpeg"))
    ok = save_image_jpg(path, rgba, pw, ph, quality);
  else {
    fprintf(stderr, "[ui] screenshot path requires .png, .jpg, or .jpeg: %s\n", path);
    fflush(stderr);
    ok = false;
  }
  free(rgba);
  return ok;
}

#define UI_SCREENSHOT_PATH_MAX 1024

static struct {
  char path[UI_SCREENSHOT_PATH_MAX];
  int quality;
  bool pending, quit_after;
} g_screenshot_request;

bool ui_request_screenshot(const char *path, int quality, bool quit_after) {
  if (!path || !*path || quality < 1 || quality > 100) return false;
  if (!screenshot_path_is(path, ".png") && !screenshot_path_is(path, ".jpg") &&
      !screenshot_path_is(path, ".jpeg"))
    return false;
  int n = snprintf(g_screenshot_request.path, sizeof(g_screenshot_request.path), "%s", path);
  if (n < 0 || (size_t)n >= sizeof(g_screenshot_request.path)) return false;
  g_screenshot_request.quality = quality;
  g_screenshot_request.quit_after = quit_after;
  g_screenshot_request.pending = true;
  for (window_t *win = g_ui_runtime.windows; win; win = win->next) {
    if (window_has_state(win, WINDOW_STATE_VISIBLE)) invalidate_window(win);
  }
  return true;
}

bool ui_dequeue_screenshot_for_frame(bool frame_had_paint, char *path,
                                     size_t path_size, int *quality,
                                     bool *quit_after) {
  if (!frame_had_paint || !g_screenshot_request.pending || !path || path_size == 0)
    return false;
  int n = snprintf(path, path_size, "%s", g_screenshot_request.path);
  if (n < 0 || (size_t)n >= path_size) return false;
  if (quality) *quality = g_screenshot_request.quality;
  if (quit_after) *quit_after = g_screenshot_request.quit_after;
  g_screenshot_request.pending = false;
  return true;
}

// Delay execution
void ui_delay(unsigned int milliseconds) {
  axSleep(milliseconds);
}

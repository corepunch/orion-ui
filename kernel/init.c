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

#include "../platform/platform.h"
#include "../user/gl_compat.h"
#include "../user/user.h"
#include "../user/image.h"
#include "../user/icons.h"
#include "../commctl/commctl.h"
#include "kernel.h"

bool ui_init_prog(void);
void ui_shutdown_prog(void);

// Set to true after ui_init_graphics() succeeds; guards begin/end frame.
static bool g_graphics_initialized = false;

// Internal white texture for drawing solid colors
uint32_t ui_white_texture = 0;

// Internal 4×4 checker texture for drawing selection outlines
uint32_t ui_checker_texture = 0;

// Built-in system icon strip loaded from share/orion/icon_sheet_16x16.png.
// Accessible from draw_impl.c via extern.  Icons are indexed starting at
// SYSICON_BASE (0x10000); subtract SYSICON_BASE to get the strip index.
bitmap_strip_t g_sysicon_strip = {0};
static uint32_t g_sysicon_tex = 0;

// Initialize the internal white texture
void init_ui_white_texture(void) {
  if (ui_white_texture == 0) {
    uint32_t white_pixel = 0xFFFFFFFF;
    ui_white_texture = R_CreateTextureRGBA(1, 1, &white_pixel,
                                           R_FILTER_NEAREST, R_WRAP_CLAMP);
  }
}

// Initialize the 4×4 checker texture used for dashed selection outlines.
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

// Load the built-in icon sheet from <exe_dir>/../share/orion/icon_sheet_16x16.png.
// Safe to call multiple times; subsequent calls are no-ops if already loaded.
static void init_sysicon_strip(void) {
  if (g_sysicon_tex != 0) return;
  char path[4096];
  snprintf(path, sizeof(path), "%s/../share/orion/icon_sheet_16x16.png",
           ui_get_exe_dir());
  int w = 0, h = 0;
  uint8_t *src = load_image(path, &w, &h);
  if (!src) return;
  if (w < 16 || h < 16 || (w % 16) != 0 || (h % 16) != 0) {
    image_free(src);
    return;
  }
  g_sysicon_tex = R_CreateTextureRGBA(w, h, src, R_FILTER_NEAREST, R_WRAP_CLAMP);
  image_free(src);
  g_sysicon_strip.tex     = g_sysicon_tex;
  g_sysicon_strip.icon_w  = 16;
  g_sysicon_strip.icon_h  = 16;
  g_sysicon_strip.cols    = w / 16;
  g_sysicon_strip.sheet_w = w;
  g_sysicon_strip.sheet_h = h;
}

static void shutdown_sysicon_strip(void) {
  R_DeleteTexture(g_sysicon_tex);
  g_sysicon_tex = 0;
  g_sysicon_strip = (bitmap_strip_t){0};
}

void shutdown_ui_textures(void) {
  R_DeleteTexture(ui_white_texture);
  ui_white_texture = 0;
  R_DeleteTexture(ui_checker_texture);
  ui_checker_texture = 0;
}

void shutdown_white_texture(void) {
  shutdown_ui_textures();
}

// Return the global built-in icon strip, or NULL if it has not been loaded.
bitmap_strip_t *ui_get_sysicon_strip(void) {
  return (g_sysicon_strip.tex != 0) ? &g_sysicon_strip : NULL;
}

static result_t win_desktop(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  extern void fill_rect(uint32_t color, int x, int y, int w, int h);
  switch (msg) {
    case kWindowMessagePaint:
      fill_rect(0xff6B3529, 0, 0, ui_get_system_metrics(kSystemMetricScreenWidth), ui_get_system_metrics(kSystemMetricScreenHeight));
      return true;
  }
  return false;
}

result_t win_tray(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

// Initialize graphics context (platform + OpenGL)
bool ui_init_graphics(int flags, const char *title, int width, int height) {
  // Guard against double-initialization (e.g. when a gem calls this
  // after the shell has already initialized the context).
  if (g_graphics_initialized) return true;

  axInit();

  if (!axCreateWindow(title, width * UI_WINDOW_SCALE, height * UI_WINDOW_SCALE, 0)) {
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

  axSetSwapInterval(1);

  ui_init_prog();

  init_ui_white_texture();
  init_ui_checker_texture();
  init_sysicon_strip();

  init_console();

  if (flags & UI_INIT_DESKTOP) {
    show_window(create_window("Desktop",
                              WINDOW_NOTITLE|WINDOW_ALWAYSINBACK|WINDOW_NOTRAYBUTTON,
                              MAKERECT(0, 0, ui_get_system_metrics(kSystemMetricScreenWidth), ui_get_system_metrics(kSystemMetricScreenHeight)),
                              NULL, win_desktop, 0, NULL), true);
  }

  if (flags & UI_INIT_TRAY) {
    show_window(create_window("Tray",
                              WINDOW_NOTITLE|WINDOW_NOTRAYBUTTON,
                              MAKERECT(0, 0, 0, 0),
                              NULL, win_tray, 0, NULL), true);
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
  cleanup_all_windows();

  extern void cleanup_all_hooks(void);
  cleanup_all_hooks();

  if (ui_joystick_available()) {
    ui_joystick_shutdown();
  }

  ui_shutdown_prog();

  shutdown_sysicon_strip();
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

// End a render frame: present the rendered content to the screen.
// Replaces the old SDL_GL_SwapWindow call.
// No-op when graphics have not been initialized.
void ui_end_frame(void) {
  if (!g_graphics_initialized) return;
  axEndPaint();
}

// Delay execution
void ui_delay(unsigned int milliseconds) {
  axSleep(milliseconds);
}

// Return the directory that contains the running executable (no trailing slash).
// The returned pointer is to a static buffer valid until the next call.
// Returns "" on any error.
const char *ui_get_exe_dir(void) {
  static char buf[4096];
  buf[0] = '\0';

#if defined(_WIN32) || defined(_WIN64)
  DWORD len = GetModuleFileNameA(NULL, buf, (DWORD)sizeof(buf));
  if (len == 0 || len >= (DWORD)sizeof(buf)) { buf[0] = '\0'; return buf; }
  char *last = strrchr(buf, '\\');
  if (last) *last = '\0';
#elif defined(__APPLE__)
  uint32_t size = (uint32_t)sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) != 0) { buf[0] = '\0'; return buf; }
  char *last = strrchr(buf, '/');
  if (last) *last = '\0';
#else
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len <= 0) { buf[0] = '\0'; return buf; }
  buf[len] = '\0';
  char *last = strrchr(buf, '/');
  if (last) *last = '\0';
#endif

  return buf;
}


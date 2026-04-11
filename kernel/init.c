// Graphics context initialization and management
// Abstraction layer over the platform library / OpenGL

#include <stdio.h>
#include <stdbool.h>

#include "../platform/platform.h"
#include "../user/gl_compat.h"
#include "../user/user.h"
#include "../commctl/commctl.h"
#include "kernel.h"

bool ui_init_prog(void);
void ui_shutdown_prog(void);

// Internal white texture for drawing solid colors
GLuint ui_white_texture = 0;

// Internal 4×4 checker texture for drawing selection outlines
GLuint ui_checker_texture = 0;

// Initialize the internal white texture
void init_ui_white_texture(void) {
  if (ui_white_texture == 0) {
    glGenTextures(1, &ui_white_texture);
    glBindTexture(GL_TEXTURE_2D, ui_white_texture);
    uint32_t white_pixel = 0xFFFFFFFF;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white_pixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
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
    glGenTextures(1, &ui_checker_texture);
    glBindTexture(GL_TEXTURE_2D, ui_checker_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  }
}

void shutdown_ui_textures(void) {
  SAFE_DELETE_N(ui_white_texture, glDeleteTextures);
  SAFE_DELETE_N(ui_checker_texture, glDeleteTextures);
}

void shutdown_white_texture(void) {
  shutdown_ui_textures();
}

static result_t win_desktop(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  extern void fill_rect(int color, int x, int y, int w, int h);
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
  WI_Init();

  if (!WI_CreateWindow(title, width * UI_WINDOW_SCALE, height * UI_WINDOW_SCALE, 0)) {
    printf("Window could not be created!\n");
    WI_Shutdown();
    return false;
  }

  WI_BeginPaint();

  printf("GL_VERSION  : %s\n", glGetString(GL_VERSION));
  printf("GLSL_VERSION: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

  WI_SetSwapInterval(1);

  ui_init_prog();

  init_ui_white_texture();
  init_ui_checker_texture();

  init_console();

  if (flags & UI_INIT_DESKTOP) {
    show_window(create_window("Desktop",
                              WINDOW_NOTITLE|WINDOW_ALWAYSINBACK|WINDOW_NOTRAYBUTTON,
                              MAKERECT(0, 0, ui_get_system_metrics(kSystemMetricScreenWidth), ui_get_system_metrics(kSystemMetricScreenHeight)),
                              NULL, win_desktop, NULL), true);
  }

  if (flags & UI_INIT_TRAY) {
    show_window(create_window("Tray",
                              WINDOW_NOTITLE|WINDOW_NOTRAYBUTTON,
                              MAKERECT(0, 0, 0, 0),
                              NULL, win_tray, NULL), true);
  }

  running = true;

  return true;
}

// Cleanup all windows
static void cleanup_all_windows(void) {
  extern window_t *windows;

  while (windows) {
    destroy_window(windows);
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

  shutdown_white_texture();

  shutdown_console();

  WI_Shutdown();
}

// Begin a render frame: make GL context current and bind platform framebuffer.
// Must be called once per frame before any OpenGL drawing.
void ui_begin_frame(void) {
  WI_BeginPaint();
}

// End a render frame: present the rendered content to the screen.
// Replaces the old SDL_GL_SwapWindow call.
void ui_end_frame(void) {
  WI_EndPaint();
}

// Delay execution
void ui_delay(unsigned int milliseconds) {
  WI_Sleep(milliseconds);
}


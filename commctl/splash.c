// Splash screen — a borderless, always-on-top image window that closes on click.
// Analogous to the startup splash seen in Blender, GIMP, Photoshop, etc.
//
// Usage:
//   window_t *sp = show_splash_screen(path_to_image, hinstance);
//
// The window is non-modal: the caller's event loop renders it normally.
// A left-click anywhere on the window destroys it.

#include <stdlib.h>
#include "../user/user.h"
#include "../user/draw.h"
#include "../user/image.h"
#include "../kernel/renderer.h"

typedef struct {
  uint32_t tex;
} splash_state_t;

static result_t splash_proc(window_t *win, uint32_t msg,
                             uint32_t wparam, void *lparam) {
  switch (msg) {
    case kWindowMessageCreate: {
      splash_state_t *s = allocate_window_data(win, sizeof(splash_state_t));
      s->tex = (uint32_t)(uintptr_t)lparam;
      win->notabstop = true;
      return true;
    }
    case kWindowMessagePaint: {
      splash_state_t *s = (splash_state_t *)win->userdata;
      if (s && s->tex)
        draw_rect((int)s->tex, 0, 0, win->frame.w, win->frame.h);
      return true;
    }
    case kWindowMessageLeftButtonDown:
      destroy_window(win);
      return true;
    case kWindowMessageDestroy: {
      splash_state_t *s = (splash_state_t *)win->userdata;
      if (s && s->tex) {
        R_DeleteTexture(s->tex);
        s->tex = 0;
      }
      return false;
    }
    default:
      return false;
  }
}

// Show a splash screen window for the given image path.
// The image type is determined by its content (magic bytes), not its extension,
// so both .jpg and .jpeg files are accepted alongside .png and .bmp.
// The window is centered on screen, borderless, and always on top.
// It is destroyed when the user clicks anywhere on it.
// Returns the window pointer, or NULL if the image could not be loaded.
window_t *show_splash_screen(const char *path, hinstance_t hinstance) {
  if (!path) return NULL;

  int w = 0, h = 0;
  uint8_t *pixels = load_image(path, &w, &h);
  if (!pixels || w <= 0 || h <= 0) {
    if (pixels) image_free(pixels);
    return NULL;
  }

  uint32_t tex = R_CreateTextureRGBA(w, h, pixels, R_FILTER_LINEAR, R_WRAP_CLAMP);
  image_free(pixels);
  if (!tex) return NULL;

  int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
  int sh = ui_get_system_metrics(kSystemMetricScreenHeight);
  // Clamp to screen size so an oversized image does not overflow.
  if (w > sw) w = sw;
  if (h > sh) h = sh;
  int x = (sw - w) / 2;
  int y = (sh - h) / 2;

  window_t *win = create_window(
      "",
      WINDOW_NOTITLE | WINDOW_NORESIZE | WINDOW_ALWAYSONTOP |
      WINDOW_NOTRAYBUTTON | WINDOW_NOFILL,
      MAKERECT(x, y, w, h),
      NULL, splash_proc, hinstance, (void *)(uintptr_t)tex);

  if (!win) {
    R_DeleteTexture(tex);
    return NULL;
  }

  show_window(win, true);
  return win;
}

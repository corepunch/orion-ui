// Splash screen — a borderless, always-on-top image window that closes on click.
// Analogous to the startup splash seen in Blender, GIMP, Photoshop, etc.
//
// Usage:
//   window_t *sp = show_splash_screen(path_to_image, hinstance);
//
// The window is non-modal: the caller's event loop renders it normally.
// Moving the mouse over the splash screen and then away from it destroys it,
// as does clicking anywhere on the window.

#include <stdio.h>
#include <stdlib.h>
#include "../user/user.h"
#include "../user/draw.h"
#include "../user/image.h"
#include "../kernel/renderer.h"

typedef struct {
  uint32_t tex;
  bool mouse_entered;
} splash_state_t;

static result_t splash_proc(window_t *win, uint32_t msg,
                             uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      splash_state_t *s = allocate_window_data(win, sizeof(splash_state_t));
      s->tex = (uint32_t)(uintptr_t)lparam;
      s->mouse_entered = false;
      win->notabstop = true;
      return true;
    }
    case evMouseMove: {
      splash_state_t *s = (splash_state_t *)win->userdata;
      if (s && !s->mouse_entered) {
        s->mouse_entered = true;
        track_mouse(win);
      }
      return false;
    }
    case evMouseLeave: {
      splash_state_t *s = (splash_state_t *)win->userdata;
      if (s && s->mouse_entered)
        destroy_window(win);
      return true;
    }
    case evPaint: {
      splash_state_t *s = (splash_state_t *)win->userdata;
      if (s && s->tex)
        draw_rect((int)s->tex, R(0, 0, win->frame.w, win->frame.h));
      return true;
    }
    case evLeftButtonDown:
      destroy_window(win);
      return true;
    case evDestroy: {
      splash_state_t *s = (splash_state_t *)win->userdata;
      if (s) {
        if (s->tex) {
          R_DeleteTexture(s->tex);
          s->tex = 0;
        }
        free(s);
        win->userdata = NULL;
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
// It is destroyed when the user clicks anywhere on it or moves the mouse
// into it and then away.
// Returns the window pointer, or NULL if the image could not be loaded.
window_t *show_splash_screen(const char *path, hinstance_t hinstance) {
  if (!path || !g_ui_runtime.running) return NULL;

  int w = 0, h = 0;
  uint8_t *pixels = load_image(path, &w, &h);
  if (!pixels || w <= 0 || h <= 0) {
    if (pixels) image_free(pixels);
    return NULL;
  }

  uint32_t tex = R_CreateTextureRGBA(w, h, pixels, R_FILTER_LINEAR, R_WRAP_CLAMP);
  image_free(pixels);
  if (!tex) return NULL;

  // Scale down large images to a maximum of 1/4 screen size, preserving aspect ratio.
  w /= 4;
  h /= 4;

  // Center the window on screen, clamping to screen size if the image is oversized.
  int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
  int sh = ui_get_system_metrics(kSystemMetricScreenHeight);
  // Clamp to screen size so an oversized image does not overflow.
  if (w > sw) w = sw;
  if (h > sh) h = sh;
    rect_t wr = center_window_rect((rect_t){0, 0, w, h}, NULL);

  window_t *win = create_window(
      "",
      WINDOW_NOTITLE | WINDOW_NORESIZE | WINDOW_ALWAYSONTOP |
      WINDOW_NOTRAYBUTTON | WINDOW_NOFILL | WINDOW_TRANSPARENT,
      MAKERECT(wr.x, wr.y, wr.w, wr.h),
      NULL, splash_proc, hinstance, (void *)(uintptr_t)tex);

  if (!win) {
    R_DeleteTexture(tex);
    return NULL;
  }

  show_window(win, true);
  return win;
}

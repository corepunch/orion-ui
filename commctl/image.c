#include <SDL2/SDL.h>
#include <stdint.h>

#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"

// Static image control — displays a GL texture.
// lparam in kWindowMessageCreate is (void*)(uintptr_t)GLuint texture ID.
// The texture is NOT owned by the control; the caller must free it.
result_t win_image(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case kWindowMessageCreate:
      win->userdata = lparam;
      win->notabstop = true;
      return true;
    case kWindowMessagePaint: {
      int tex = (int)(uintptr_t)win->userdata;
      if (tex) {
        draw_rect(tex, win->frame.x, win->frame.y, win->frame.w, win->frame.h);
      } else {
        fill_rect(COLOR_PANEL_DARK_BG, win->frame.x, win->frame.y, win->frame.w, win->frame.h);
      }
      return true;
    }
  }
  return false;
}

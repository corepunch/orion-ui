#include <stdint.h>

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>

// Static image control — displays a GL texture.
// lparam in evCreate is (void*)(uintptr_t)GLuint texture ID.
// The texture is NOT owned by the control; the caller must free it.
result_t win_image(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      win->userdata = lparam;
      win->flags |= WINDOW_NOTABSTOP;
      return true;
    case evPaint: {
      int tex = (int)(uintptr_t)win->userdata;
      if (tex) {
        draw_rect(tex, R(0, 0, win->frame.w, win->frame.h));
      } else {
        theme_draw_in(win, THEME_PART_WORKSPACE, R(0, 0, win->frame.w, win->frame.h), CTRL_NORMAL);
      }
      return true;
    }
  }
  return false;
}

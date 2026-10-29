#include "commctl.h"
#include <orion/user/gdi.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct { bitmap_t *bitmap; char *path; } image_view_t;
result_t win_imageview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  image_view_t *state = win->userdata;
  switch (msg) {
    case evCreate:
      state = allocate_window_data(win, sizeof(*state));
      win->flags |= WINDOW_NOTABSTOP;
      return state != NULL;
    case imageSetPath: {
      const char *path = lparam ? lparam : "";
      if (!state) return false;
      if (state->path && !strcmp(state->path, path)) return true;
      bitmap_t *bitmap = *path ? bitmap_load(path) : NULL;
      if (*path && !bitmap) { fprintf(stderr, "[ImageView] window=%u cannot load '%s'\n", win->id, path); fflush(stderr); }
      char *copy = malloc(strlen(path) + 1);
      if (!copy) return false;
      strcpy(copy, path);
      bitmap_free(state->bitmap); free(state->path);
      state->bitmap = bitmap; state->path = copy;
      invalidate_window(win);
      return !*path || bitmap != NULL;
    }
    case evPaint:
      fill_rect(get_sys_color(brWorkspaceBg), R(0, 0, win->frame.w, win->frame.h));
      if (state && state->bitmap) {
        isize16_t size = bitmap_size(state->bitmap);
        if (size.w > 0 && size.h > 0) {
          float scale = fminf((float)win->frame.w / size.w, (float)win->frame.h / size.h);
          stretch_blt(rect_center(R(0, 0, win->frame.w, win->frame.h), size.w * scale, size.h * scale), state->bitmap, R(0, 0, size.w, size.h));
        }
      }
      return true;
    case evDestroy:
      if (state) { bitmap_free(state->bitmap); free(state->path); free(state); win->userdata = NULL; }
      return true;
  }
  return false;
}

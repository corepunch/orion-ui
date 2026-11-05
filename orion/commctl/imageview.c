#include "commctl.h"
#include <orion/user/gdi.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct { bitmap_t *bitmap; char *path; char icon[64]; uint32_t background; } image_view_t;
result_t win_imageview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  image_view_t *state = win->userdata;
  switch (msg) {
    case evCreate: {
      state = allocate_window_data(win, sizeof(*state));
      win->flags |= WINDOW_NOTABSTOP;
      if (state) {
        const form_ctrl_def_t *cd = (const form_ctrl_def_t *)lparam;
        state->background = cd && (uintptr_t)cd > 0x1000 && cd->color_set && cd->color < brCount ? cd->color : brWorkspaceBg;
      }
      return state != NULL;
    }
    case btnSetIconName:
      if (!state) return false;
      snprintf(state->icon, sizeof(state->icon), "%s", lparam ? (const char *)lparam : "");
      invalidate_window(win);
      return true;
    case evHitTest:
      if (!state || !state->icon[0] || !lparam || !win->parent) return false;
      *(window_t **)lparam = win->parent;
      return true;
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
      fill_rect(get_sys_color(state ? state->background : brWorkspaceBg), R(0, 0, win->frame.w, win->frame.h));
      if (state && state->icon[0]) {
        int size = MIN(48, MIN(win->frame.w, win->frame.h));
        irect16_t r = rect_center(get_client_rect(win), size, size);
        draw_sysicon(state->icon, r.x, r.y, size, get_sys_color(brTextSecondary));
      }
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

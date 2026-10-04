#include <stdio.h>
#include <stdlib.h>
#include "image_background.h"
#include "image.h"
#include "draw.h"
#include <orion/kernel/kernel.h>

bool image_atlas_load(image_atlas_t *atlas, const char *path) {
  if (!atlas || !path || !path[0] || !g_ui_runtime.running) {
    fprintf(stderr, "[skin] atlas load rejected atlas=%p path=%s running=%d\n", (void *)atlas, path ? path : "(null)", g_ui_runtime.running);
    fflush(stderr);
    return false;
  }
  int w = 0, h = 0;
  uint8_t *pixels = load_image(path, &w, &h);
  if (!pixels || w <= 0 || h <= 0 || w > INT16_MAX || h > INT16_MAX) {
    fprintf(stderr, "[skin] atlas image unavailable path=%s size=%dx%d\n", path, w, h);
    fflush(stderr);
    image_free(pixels);
    return false;
  }
  uint32_t tex = R_CreateTextureSRGBA8(w, h, pixels, R_FILTER_LINEAR, R_WRAP_CLAMP);
  image_free(pixels);
  if (!tex) {
    fprintf(stderr, "[skin] atlas texture allocation failed path=%s size=%dx%d\n", path, w, h);
    fflush(stderr);
    return false;
  }
  image_atlas_free(atlas);
  *atlas = (image_atlas_t){tex, w, h};
  return true;
}

void image_atlas_free(image_atlas_t *atlas) {
  if (!atlas) return;
  if (atlas->tex) R_DeleteTexture(atlas->tex);
  *atlas = (image_atlas_t){0};
}

bool image_background_validate(const image_background_t *bg) {
  if (!bg || !bg->atlas || !bg->atlas->tex || bg->atlas->width <= 0 || bg->atlas->height <= 0) {
    fprintf(stderr, "[skin] invalid background atlas bg=%p\n", (const void *)bg);
    fflush(stderr);
    return false;
  }
  irect16_t s = bg->source_border, d = bg->border;
  if (s.x < 0 || s.y < 0 || s.w < 0 || s.h < 0 || d.x < 0 || d.y < 0 || d.w < 0 || d.h < 0) {
    fprintf(stderr, "[skin] negative nine-slice borders source=%d,%d,%d,%d destination=%d,%d,%d,%d\n", s.x, s.y, s.w, s.h, d.x, d.y, d.w, d.h);
    fflush(stderr);
    return false;
  }
  for (int i = 0; i < IMAGE_BG_COUNT; i++) {
    irect16_t r = bg->states[i];
    if (i && !r.x && !r.y && !r.w && !r.h) continue;
    if (r.x < 0 || r.y < 0 || r.w <= s.x + s.w || r.h <= s.y + s.h ||
        r.x + r.w > bg->atlas->width || r.y + r.h > bg->atlas->height) {
      fprintf(stderr, "[skin] invalid state region state=%d rect=%d,%d,%d,%d atlas=%dx%d borders=%d,%d,%d,%d\n",
              i, r.x, r.y, r.w, r.h, bg->atlas->width, bg->atlas->height, s.x, s.y, s.w, s.h);
      fflush(stderr);
      return false;
    }
  }
  return true;
}

static void fit_borders(int size, int *first, int *last) {
  int total = *first + *last;
  if (total > size) {
    *first = (int)((int64_t)*first * size / total);
    *last = size - *first;
  }
}

bool draw_image_background(irect16_t r, const image_background_t *bg, ctrl_state_t state) {
  if (!bg || !g_ui_runtime.running || r.w <= 0 || r.h <= 0) return false;
  if (!image_background_validate(bg)) return false;
  int index = state & CTRL_DISABLED ? IMAGE_BG_DISABLED : state & CTRL_PRESSED ? IMAGE_BG_PRESSED :
              state & CTRL_SELECTED ? IMAGE_BG_SELECTED : state & CTRL_HOVER ? IMAGE_BG_HOVER : IMAGE_BG_NORMAL;
  irect16_t src = bg->states[index];
  if (!src.w) src = bg->states[IMAGE_BG_NORMAL];
  int left = bg->border.x, top = bg->border.y, right = bg->border.w, bottom = bg->border.h;
  fit_borders(r.w, &left, &right);
  fit_borders(r.h, &top, &bottom);
  int dx[] = {r.x, r.x + left, r.x + r.w - right, r.x + r.w};
  int dy[] = {r.y, r.y + top, r.y + r.h - bottom, r.y + r.h};
  int sx[] = {src.x, src.x + bg->source_border.x, src.x + src.w - bg->source_border.w, src.x + src.w};
  int sy[] = {src.y, src.y + bg->source_border.y, src.y + src.h - bg->source_border.h, src.y + src.h};
  for (int y = 0; y < 3; y++) for (int x = 0; x < 3; x++) {
    if (dx[x + 1] == dx[x] || dy[y + 1] == dy[y] || sx[x + 1] == sx[x] || sy[y + 1] == sy[y]) continue;
    frect_t uv = {(float)sx[x] / bg->atlas->width, (float)sy[y] / bg->atlas->height,
                  (float)sx[x + 1] / bg->atlas->width, (float)sy[y + 1] / bg->atlas->height};
    draw_sprite_region(bg->atlas->tex, R(dx[x], dy[y], dx[x + 1] - dx[x], dy[y + 1] - dy[y]), &uv, 0xffffffffu, 0);
  }
  return true;
}

bool window_set_image_background(window_t *win, const image_background_t *bg) {
  if (!win) { fprintf(stderr, "[skin] background setter rejected null window\n"); fflush(stderr); return false; }
  image_background_t *copy = NULL;
  if (bg) {
    if (!image_background_validate(bg)) {
      fprintf(stderr, "[skin] background rejected win=%u\n", (unsigned)win->id);
      fflush(stderr);
      return false;
    }
    copy = malloc(sizeof(*copy));
    if (!copy) { fprintf(stderr, "[skin] background allocation failed win=%u\n", (unsigned)win->id); fflush(stderr); return false; }
    *copy = *bg;
  }
  free(win->image_background);
  win->image_background = copy;
  invalidate_window(win);
  return true;
}

bool draw_window_image_background(window_t *win, irect16_t r, ctrl_state_t state) {
  return win && draw_image_background(r, win->image_background, state);
}

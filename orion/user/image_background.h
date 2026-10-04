#ifndef __IMAGE_BACKGROUND_H__
#define __IMAGE_BACKGROUND_H__

#include "user.h"
#include "theme.h"

typedef struct {
  uint32_t tex;
  int width, height;
} image_atlas_t;

enum { IMAGE_BG_NORMAL, IMAGE_BG_SELECTED, IMAGE_BG_PRESSED, IMAGE_BG_HOVER, IMAGE_BG_DISABLED, IMAGE_BG_COUNT };

typedef struct image_background_s {
  const image_atlas_t *atlas;
  irect16_t states[IMAGE_BG_COUNT]; // source pixels; empty optional states reuse normal
  irect16_t source_border;         // left, top, right, bottom in source pixels
  irect16_t border;                // left, top, right, bottom in logical pixels
} image_background_t;

// Zero-initialize atlases. Failed loads preserve the previous texture.
bool image_atlas_load(image_atlas_t *atlas, const char *path);
void image_atlas_free(image_atlas_t *atlas);
bool image_background_validate(const image_background_t *background);
bool draw_image_background(irect16_t r, const image_background_t *background, ctrl_state_t state);
// Copies the descriptor; its atlas remains caller-owned and must outlive the window.
// NULL restores the themed background. Window destruction frees the copy.
bool window_set_image_background(window_t *win, const image_background_t *background);
bool draw_window_image_background(window_t *win, irect16_t r, ctrl_state_t state);

#endif

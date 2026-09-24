#ifndef __IMAGEEDITOR_RENDER_EFFECTS_H__
#define __IMAGEEDITOR_RENDER_EFFECTS_H__

#include <stdint.h>
#include <orion/ui.h>

typedef enum {
  IE_RENDER_EFFECT_COPY = 0,
  IE_RENDER_EFFECT_MASK_GRAYSCALE = 1,
  IE_RENDER_EFFECT_LEVELS = 2,
  IE_RENDER_EFFECT_INVERT = 3,
  IE_RENDER_EFFECT_THRESHOLD = 4,
  IE_RENDER_EFFECT_GRADIENT = 5,
  IE_RENDER_EFFECT_BLUR = 6,
  IE_RENDER_EFFECT_SHARPEN = 7,
  IE_RENDER_EFFECT_EDGE = 8,
  IE_RENDER_EFFECT_ALPHA_THRESHOLD = 9,
  IE_RENDER_EFFECT_SELECTION_MASK = 10,
  IE_RENDER_EFFECT_COUNT
} imageeditor_render_effect_t;

bool imageeditor_render_effects_init(void);
void imageeditor_render_effects_shutdown(void);
char *imageeditor_prepare_effect_shader(const char *source);

void imageeditor_draw_rect_effect(int tex, int x, int y, int w, int h,
                                  imageeditor_render_effect_t effect,
                                  const ui_render_effect_params_t *params);

void imageeditor_draw_rect_effect_blend(int tex, int x, int y, int w, int h,
                                        float alpha, ui_layer_blend_t blend,
                                        imageeditor_render_effect_t effect,
                                        const ui_render_effect_params_t *params);

bool imageeditor_bake_texture_effect(int src_tex, int w, int h,
                                     imageeditor_render_effect_t effect,
                                     const ui_render_effect_params_t *params,
                                     uint32_t *out_tex);

bool imageeditor_bake_texture_blur(int src_tex, int w, int h, int radius,
                                   uint32_t *out_tex);

#endif

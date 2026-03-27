#ifndef __UI_DRAW_H__
#define __UI_DRAW_H__

#include <stdint.h>
#include "../user/user.h"
#include "text.h"

// Rectangle drawing functions
void fill_rect(int color, int x, int y, int w, int h);
void draw_rect(int tex, int x, int y, int w, int h);
void draw_rect_ex(int tex, int x, int y, int w, int h, int type, float alpha);
void draw_sprite_region(int tex, int x, int y, int w, int h,
                        float u0, float v0, float u1, float v1, float alpha);
// Draw a dashed selection-outline rectangle (2–4 GL draw calls depending on dimensions, O(1) regardless of size)
void draw_sel_rect(int x, int y, int w, int h);

// Icon drawing functions
void draw_icon8(int icon, int x, int y, uint32_t col);
void draw_icon16(int icon, int x, int y, uint32_t col);

// Viewport and projection
void set_viewport(rect_t const *frame);
void set_projection(int x, int y, int w, int h);
void set_clip_rect(window_t const *, rect_t const *r);

// Stencil management (internal use)
void ui_set_stencil_for_window(uint32_t window_id);
void ui_set_stencil_for_root_window(uint32_t window_id);

#endif

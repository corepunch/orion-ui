#ifndef __UI_DRAW_H__
#define __UI_DRAW_H__

#include <stdint.h>
#include "../user/user.h"
#include "rect.h"
#include "text.h"

// Rectangle drawing functions
void fill_rect(uint32_t color, rect_t const *r);
void draw_rect(int tex, rect_t const *r);
void draw_rect_ex(int tex, rect_t const *r, int type, float alpha);
void draw_sprite_region(int tex, rect_t const *r,
                        float u0, float v0, float u1, float v1,
                        uint32_t color);
// Draw a dashed selection-outline rectangle (2–4 GL draw calls depending on dimensions, O(1) regardless of size)
void draw_sel_rect(rect_t const *r);

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

// Draw built-in scrollbars for a window on top of its painted content.
// Called by send_message() after evPaint when WINDOW_HSCROLL or
// WINDOW_VSCROLL is set.  Safe to call when neither bar is visible (no-op).
void draw_builtin_scrollbars(window_t *win);

#endif

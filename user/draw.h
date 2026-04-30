#ifndef __UI_DRAW_H__
#define __UI_DRAW_H__

#include <stdint.h>
#include "../user/user.h"
#include "rect.h"
#include "text.h"

// Rectangle drawing functions
void fill_rect(uint32_t color, rect_t r);
void draw_rect(int tex, rect_t r);
void draw_rect_ex(int tex, rect_t r, int type, float alpha);

// UVs for draw_sprite_region() are packed into frect_t as normalized floats:
// x=u0, y=v0, w=u1, h=v1.
#define UV_RECT(U0, V0, U1, V1) \
    (&(frect_t){ (U0), (V0), (U1), (V1) })

enum {
    DRAW_SPRITE_NO_ALPHA = 1u << 0
};

// Short alias for callers that prefer terse draw flags.
#define NO_ALPHA DRAW_SPRITE_NO_ALPHA

void draw_sprite_region(int tex, rect_t r,
                        frect_t const *uv,
                        uint32_t color, uint32_t flags);
// Draw a dashed selection-outline rectangle (2–4 GL draw calls depending on dimensions, O(1) regardless of size)
void draw_sel_rect(rect_t r);

// Icon drawing functions
void draw_theme_icon(int id, int x, int y, int size, uint32_t col);
void draw_theme_icon_in_rect(int id, rect_t r, uint32_t col);
void draw_icon(int id, int x, int y, int size, uint32_t col);
void draw_icon8(int icon, int x, int y, uint32_t col);
void draw_icon8_clipped(int icon, rect_t rect, uint32_t col);
void draw_icon16(int icon, int x, int y, uint32_t col);
void draw_checkerboard(rect_t r, int square_px);

// Viewport and projection
void set_viewport(rect_t frame);
void set_projection(int x, int y, int w, int h);
void set_clip_rect(window_t const *, rect_t r);

// Stencil management (internal use)
void ui_set_stencil_for_window(uint32_t window_id);
void ui_set_stencil_for_root_window(uint32_t window_id);

// Draw built-in scrollbars for a window on top of its painted content.
// Called by send_message() after evPaint when WINDOW_HSCROLL or
// WINDOW_VSCROLL is set.  Safe to call when neither bar is visible (no-op).
void draw_builtin_scrollbars(window_t *win);

#endif

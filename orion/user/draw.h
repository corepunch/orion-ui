#ifndef __UI_DRAW_H__
#define __UI_DRAW_H__

#include <stdint.h>
#include <orion/user/user.h>
#include "rect.h"
#include "text.h"
#include "theme.h"

// Rectangle drawing functions
// Color arguments are packed 0xAABBGGRR with sRGB RGB and linear alpha.
void fill_rect(uint32_t color, irect16_t r);
void fill_rounded_rect(uint32_t color, irect16_t r, int radius);
// Vertical, linear-light gradient with one rounded silhouette; radius 0 fills a row.
void fill_gradient_rounded_rect(uint32_t top, uint32_t bottom, irect16_t r, int radius);
void render_rounded_rect_gradient(int tex, irect16_t r, int pixel_w, int pixel_h,
                                  float radius, uint32_t top, uint32_t bottom);
// Inset stroke of `thickness` logical pixels. One silhouette, same radius as a card.
void stroke_rounded_rect(uint32_t color, irect16_t r, int radius, int thickness);
// Same colour with a different alpha (0 transparent .. 255 opaque).
static inline uint32_t color_with_alpha(uint32_t color, uint8_t alpha) {
  return ((uint32_t)alpha << 24) | (color & 0x00FFFFFFu);
}
// A card: a face (brControlBg, brButtonHover when hovered) with an optional accent edge on its left side.
// The edge is a plain `edge_width`-pixel rectangle; the active theme's card_corner_radius rounds (or not)
// the card and the edge together. CTRL_SELECTED draws a ring in the accent edge's colour (theme accent when there is no edge) in the theme's card_ring_width margin,
// which is reserved inside `r` for every card so selecting never shifts content.
void draw_card(irect16_t r, ctrl_state_t state, uint32_t edge_color);
// Tinted vertical gradient, top highlight and selection ring in one themed silhouette.
void draw_gradient_card(irect16_t r, ctrl_state_t state, uint32_t color);
void render_gradient_card(irect16_t r, int pixel_w, int pixel_h, float radius,
                          float ring_width, float highlight_width, ctrl_state_t state, uint32_t color);
// Single shader pass; shadow is reserved inside r, so controls never paint outside their bounds.
void draw_plastic_button(irect16_t r, ctrl_state_t state, uint32_t color, const char *icon);
void draw_plastic_card(irect16_t r, ctrl_state_t state, uint32_t color);
void render_plastic_surface(irect16_t r, float radius, float bevel, float shadow,
                            ctrl_state_t state, uint32_t color, uint32_t shadow_color,
                            uint32_t icon_tex, const frect_t *icon_uv, ipoint16_t icon_size);
// Small tinted label ("3 modified"): a rounded fill in `color` at low alpha with the text in `color`.
// Drawn at (x, y) with the given height; returns the badge width so callers can chain badges.
int  draw_badge(ui_font_t font, const char *text, int x, int y, int height, uint32_t color);
int  measure_badge(ui_font_t font, const char *text);   // width draw_badge() will use
#define BADGE_HEIGHT 18
// Procedural rounded-box shadow; radius, blur (Gaussian sigma), and offset are logical pixels.
void draw_rect_shadow(irect16_t r, float radius, float blur, ipoint16_t offset, uint32_t color);
void draw_gradient_rect(irect16_t r, uint32_t left_color, uint32_t right_color);
void draw_rect(int tex, irect16_t r);
// R8 textures expose their byte in alpha; palette entries use packed UI colors.
void draw_indexed_rect(uint32_t tex, irect16_t r, const uint32_t palette[256], int transparent, float alpha);
void draw_rect_ex(int tex, irect16_t r, int type, float alpha);
void draw_program_rect(int tex, irect16_t r, uint32_t program, float mix_amount);

// UVs for draw_sprite_region() are packed into frect_t as normalized floats:
// x=u0, y=v0, w=u1, h=v1.
#define UV_RECT(U0, V0, U1, V1) \
    (&(frect_t){ (U0), (V0), (U1), (V1) })

enum {
    DRAW_SPRITE_NO_ALPHA = 1u << 0
};

// Short alias for callers that prefer terse draw flags.
#define NO_ALPHA DRAW_SPRITE_NO_ALPHA

void draw_sprite_region(int tex, irect16_t r,
                        frect_t const *uv,
                        uint32_t color, uint32_t flags);
// Draw a texture with SDF rounded-corner masking (anti-aliased).
void draw_rounded_rect(int tex, irect16_t r, int win_w, int win_h,
                       float radius, float alpha);
// Same as draw_rounded_rect, for textures that already contain premultiplied
// linear RGB (for example a cached root-window surface).
void draw_rounded_rect_premultiplied(int tex, irect16_t r, int win_w, int win_h,
                                     float radius, float alpha);
void render_rounded_rect(int tex, irect16_t r, int pixel_w, int pixel_h,
                         float radius, float alpha, uint32_t color);
void render_rounded_rect_stroke(int tex, irect16_t r, int pixel_w, int pixel_h,
                                float radius, float alpha, uint32_t color, float stroke);
// Rounded fill whose left `edge_width` pixels are painted in `edge_color`, sharing one rounded silhouette.
void render_rounded_rect_edged(int tex, irect16_t r, int pixel_w, int pixel_h, float radius,
                               float alpha, uint32_t color, uint32_t edge_color, float edge_width);
// Draw a dashed selection-outline rectangle (2–4 GL draw calls depending on dimensions, O(1) regardless of size)
void draw_sel_rect(irect16_t r);

// Icon drawing functions
void draw_theme_icon(int id, int x, int y, int size, uint32_t col);
void draw_theme_icon_in_rect(int id, irect16_t r, uint32_t col);
void draw_icon8(int icon, int x, int y, uint32_t col);
void draw_icon8_clipped(int icon, irect16_t rect, uint32_t col);
void draw_sysicon(const char *name, int x, int y, int size, uint32_t col);
void draw_icon16(int icon, int x, int y, uint32_t col);
void draw_checkerboard(irect16_t r, int square_px);

// Viewport and projection
void set_viewport(irect16_t frame);
void set_projection(int x, int y, int w, int h);
void set_clip_rect(window_t const *, irect16_t r);
void set_viewport_for_fbo(window_t *root);
void set_scissor_fbo(window_t const *root, irect16_t r);

// Stencil management (internal use)
void ui_set_stencil_for_window(uint32_t window_id);
void ui_set_stencil_for_root_window(uint32_t window_id);

// Draw built-in scrollbars for a window on top of its painted content.
// Called by send_message() after evPaint when WINDOW_HSCROLL or
// WINDOW_VSCROLL is set.  Safe to call when neither bar is visible (no-op).
void draw_builtin_scrollbars(window_t *win);

// Composite all visible root windows from their FBO textures to the screen.
void composite_root_windows(void);

#endif

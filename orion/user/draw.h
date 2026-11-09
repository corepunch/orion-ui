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
// Alternating vertical bands inside one rounded silhouette; band zero is transparent.
void fill_rounded_rect_bands(uint32_t color, irect16_t r, int radius, int band_width, int offset);
void render_rounded_rect_bands(irect16_t r, isize16_t size, float radius, uint32_t color, float band_width, float offset);
// A box's border with per-side widths and colours (top, right, bottom, left),
// the corners mitred as CSS draws them.
void fill_border_sides(irect16_t r, const int widths[4], const uint32_t colors[4]);
void render_border_sides(irect16_t r, isize16_t size, const float widths[4], const uint32_t colors[4]);
// One rounded bubble face and tail over the existing rounded rectangle shadow.
typedef enum { TOOLTIP_TAIL_TOP, TOOLTIP_TAIL_BOTTOM, TOOLTIP_TAIL_LEFT, TOOLTIP_TAIL_RIGHT } tooltip_tail_side_t;
// r includes tooltip.shadow.size on every side; tail_offset is measured along that side within r.
irect16_t tooltip_bubble_body_rect(irect16_t r, tooltip_tail_side_t side);
// The speech variant uses speechbubble geometry and shadow margins.
irect16_t speech_bubble_body_rect(irect16_t r, tooltip_tail_side_t side);
void draw_tooltip_bubble(irect16_t r, int tail_offset, tooltip_tail_side_t side);
void draw_speech_bubble(irect16_t r, int tail_offset, tooltip_tail_side_t side);
// BUTTON_COMMANDLINK face: theme bullet, ellipsized text in `ink`, underline on hover, focus rectangle.
// Like Office 97 balloon labels, there is no button bevel; `r` is the whole clickable row.
void draw_command_link(irect16_t r, const char *text, ctrl_state_t state, uint32_t ink);
void render_bubble(irect16_t r, isize16_t face_size, float radius, float tail,
                          float tail_offset, float padding, tooltip_tail_side_t side, uint32_t color, uint32_t border, float border_width, float tail_width);
void render_tooltip_bubble(irect16_t r, isize16_t face_size, float radius, float tail,
                          float tail_offset, float padding, tooltip_tail_side_t side, uint32_t color);
// Vertical gradient, interpolated in sRGB as CSS does, with one rounded silhouette; radius 0 fills a row.
void fill_gradient_rounded_rect(uint32_t top, uint32_t bottom, irect16_t r, int radius);
// Same gradient with rounded top corners and square bottom corners, for tabs.
void fill_gradient_rounded_top_rect(uint32_t top, uint32_t bottom, irect16_t r, int radius);
void render_rounded_rect_gradient(int tex, irect16_t r, int pixel_w, int pixel_h,
                                  float radius, uint32_t top, uint32_t bottom);
void render_rounded_top_rect_gradient(int tex, irect16_t r, int pixel_w, int pixel_h,
                                      float radius, uint32_t top, uint32_t bottom);
// Inset stroke of `thickness` logical pixels. One silhouette, same radius as a card.
void stroke_rounded_rect(uint32_t color, irect16_t r, int radius, int thickness);
// Same colour with a different alpha (0 transparent .. 255 opaque).
static inline uint32_t color_with_alpha(uint32_t color, uint8_t alpha) {
  return ((uint32_t)alpha << 24) | (color & 0x00FFFFFFu);
}
// a moved `amount`/255 of the way towards b, channel by channel (alpha too).
static inline uint32_t color_mix(uint32_t a, uint32_t b, uint8_t amount) {
  uint32_t out = 0;
  for (int shift = 0; shift < 32; shift += 8) {
    int ca = (int)((a >> shift) & 0xFF), cb = (int)((b >> shift) & 0xFF);
    out |= (uint32_t)(ca + (cb - ca) * amount / 255) << shift;
  }
  return out;
}
// A Card is a CSS box (theme_draw_in(win, THEME_PART_CARD, ...), base.css); only the plastic material
// (Groove's block cards) has a painter of its own.
// Framework internals behind theme_draw_ex(CTRL_PLASTIC); controls and apps never call these directly.
// Single shader pass; shadow is reserved inside r, so controls never paint outside their bounds.
void draw_plastic_card(irect16_t r, ctrl_state_t state, uint32_t color);
// Kernel entry point: plain floats only, so the renderer knows nothing about control state.
// pressed/hover/selected/disabled are 0..1; gloss/rim/lift come from theme_t.plastic.
typedef struct {
  float pressed, hover, selected, disabled;
  float gloss, rim, lift;
} plastic_look_t;
void render_plastic_surface(irect16_t r, float radius, float bevel, float shadow,
                            const plastic_look_t *look, uint32_t color, uint32_t shadow_color);
// A small chip with its own fill and text colours (Groove's variant tag); its corners are Badge.chip's radius.
void draw_badge_ex(ui_font_t font, const char *text, irect16_t r, uint32_t text_color, uint32_t background_color);
#define BADGE_HEIGHT 18
// A status badge ("Posted", "Paid"): the Badge box (THEME_PART_BADGE) toned by the instance variable
// --tone, a leading icon (THEME_PART_BADGE_ICON) and the text in the Badge font; base.css says how the
// tone tints the pill and leans the text towards the normal ink. `vars` carries --tone for a badge that is
// not a window (a table cell); NULL takes win's own (window_set_style_var). win may be NULL.
int  measure_status_badge(const char *text, const char *icon);
void draw_status_badge(const window_t *win, irect16_t r, const char *text, const char *icon, const ui_style_vars_t *vars);
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
// A rounded fill (colour may be fully transparent) whose left `edge_width` pixels are `edge_color`,
// both clipped by one rounded silhouette: a box's border-left.
void fill_rounded_rect_edged(uint32_t color, irect16_t r, int radius, uint32_t edge_color, int edge_width);
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
void draw_checkerboard(irect16_t r, int square_px);

// Viewport and projection
void set_viewport(irect16_t frame);
void set_projection(int x, int y, int w, int h);
void set_clip_rect(window_t const *, irect16_t r);
// Rounded clip in addition to set_clip_rect, for framed content; radius 0 clears it.
void set_clip_rounded_rect(window_t const *, irect16_t r, int radius);
void set_viewport_for_fbo(window_t *root);
// Paint redirection for a child subtree; NULL selects the root surface.
window_t *set_draw_surface(window_t *win);
window_t *get_draw_surface(window_t *root);
float ui_surface_scale(void);          // HiDPI scale (>= 1.0), shared by paint and composite
int   ui_surface_px(int logical);      // logical length -> physical surface pixels
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

// One redirected surface handed to the compositor (≈ a DWM visual). Sizes of the texture are
// physical pixels; the frame, shadow and projection are logical.
typedef struct R_CompositeLayer {
  uint32_t tex;
  int w, h;                    // texture size, physical pixels
  irect16_t frame;             // logical screen rect
  irect16_t clip;              // logical screen clip, when clipped=true
  bool clipped;
  float corner_radius;         // physical pixels; clamped to half the surface
  bool shadow;
  float shadow_radius, shadow_blur;
  ipoint16_t shadow_offset;
  uint32_t shadow_color;
  uint32_t overlay_color;       // sRGB tint mixed into the surface, preserving its alpha
  bool border;                 // draw_border runs after the surface
  void *user;                  // opaque to the compositor; handed back to draw_border
} R_CompositeLayer;
void R_Composite(const R_CompositeLayer *layers, int count, uint32_t clear_color,
                 int logical_w, int logical_h,
                 void (*draw_border)(const R_CompositeLayer *layer));

#endif

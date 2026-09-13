// Drawing primitives implementation
// Extracted from mapview/window.c

#include <platform/platform.h>
#include "gl_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "user.h"
#include "messages.h"
#include "draw.h"
#include "scrollbar.h"
#include "theme.h"
#include "svg_icon_loader.h"
#include "toolbar.h"

// External references
extern window_t *get_root_window(window_t *window);

static bool g_scissor_valid = false;
static irect16_t g_scissor_rect = {0};

static void set_scissor_cached(irect16_t const *r) {
  if (!r) return;
  glEnable(GL_SCISSOR_TEST);
  if (g_scissor_valid &&
      g_scissor_rect.x == r->x && g_scissor_rect.y == r->y &&
      g_scissor_rect.w == r->w && g_scissor_rect.h == r->h) {
    return;
  }
  g_scissor_rect = *r;
  g_scissor_valid = true;
  glScissor(r->x, r->y, r->w, r->h);
}

// Returns true if win is the root window that currently "owns" keyboard focus
// (either win itself is focused, or one of its descendants is focused).
bool window_has_focus(const window_t *win) {
  return g_ui_runtime.focused && get_root_window(g_ui_runtime.focused) == (window_t *)win;
}

// Forward declarations
extern intptr_t send_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
extern void set_projection(int x, int y, int w, int h);

void set_fullscreen(void) {
  int w = ui_get_system_metrics(kSystemMetricScreenWidth);
  int h = ui_get_system_metrics(kSystemMetricScreenHeight);
  set_viewport((irect16_t){0, 0, w, h});
  set_projection(0, 0, w, h);
}

irect16_t get_opengl_rect(irect16_t r) {
  uint32_t ws = axGetSize(NULL);
  float scale_x = (float)LOWORD(ws) * axGetScaling() / (float)MAX(1,ui_get_system_metrics(kSystemMetricScreenWidth));
  float scale_y = (float)HIWORD(ws) * axGetScaling() / (float)MAX(1,ui_get_system_metrics(kSystemMetricScreenHeight));
  
  return (irect16_t){
    (int)(r.x * scale_x),
    (int)((ui_get_system_metrics(kSystemMetricScreenHeight) - r.y - r.h) * scale_y), // flip Y
    (int)(r.w * scale_x),
    (int)(r.h * scale_y)
  };
}

// Get titlebar height
int titlebar_height(window_t const *win) {
  int t = 0;
  if (!(win->flags & WINDOW_NOTITLE)) t += TITLEBAR_HEIGHT;
  if (win->flags & WINDOW_TOOLBAR) {
    // Toolbar children are always laid out in a single row (no wrapping).
    // The band height = bevel (top) + padding + bsz + padding + bevel (bottom).
    t += toolbar_effective_item_height(win) + 2 * (TOOLBAR_PADDING + TOOLBAR_BEVEL_WIDTH);
  }
  return t;
}

// Get statusbar height
int statusbar_height(window_t const *win) {
  int s = 0;
  if (win->flags&WINDOW_STATUSBAR) {
    s += STATUSBAR_HEIGHT;
  }
  return s;
}

void draw_wire_rect(irect16_t r, int expand, uint32_t col) {
  fill_rect(col, R(r.x-expand, r.y-expand, r.w+2*expand, 1));
  fill_rect(col, R(r.x-expand, r.y-expand, 1, r.h+2*expand));
  fill_rect(col, R(r.x + r.w - 1 + expand, r.y-expand, 1, r.h+2*expand));
  fill_rect(col, R(r.x-expand, r.y + r.h - 1 + expand, r.w+2*expand, 1));
}

// Draw focused border
void draw_focused(irect16_t r) {
  draw_wire_rect(r, 1, get_sys_color(brAccent));
}

// Draw bevel border
void draw_bevel(irect16_t r) {
  fill_rect(get_sys_color(brLightEdge), R(r.x-1, r.y-1, r.w+2, 1));
  fill_rect(get_sys_color(brLightEdge), R(r.x-1, r.y-1, 1, r.h+2));
  fill_rect(get_sys_color(brDarkEdge), R(r.x+r.w, r.y, 1, r.h+1));
  fill_rect(get_sys_color(brDarkEdge), R(r.x, r.y+r.h, r.w+1, 1));
  fill_rect(get_sys_color(brFlare), R(r.x-1, r.y-1, 1, 1));
}

// Draw button
void draw_button(irect16_t r, int dx, int dy, bool pressed) {
  (void)dx; (void)dy;
  if (pressed) {
    fill_rect(get_sys_color(brDarkEdge), r);
    fill_rect(get_sys_color(brLightEdge), R(r.x+1, r.y+1, r.w-1, r.h-1));
    fill_rect(get_sys_color(brDarkEdge), R(r.x+1, r.y+1, r.w-2, r.h-2));
    fill_rect(get_sys_color(brWindowDarkBg), R(r.x+2, r.y+2, r.w-3, r.h-3));
    fill_rect(get_sys_color(brFlare), R(r.x+r.w-1, r.y+r.h-1, 1, 1));
  } else {
    fill_rect(get_sys_color(brDarkEdge), r);
    fill_rect(get_sys_color(brLightEdge), R(r.x, r.y, r.w-1, r.h-1));
    fill_rect(get_sys_color(brDarkEdge), R(r.x+1, r.y+1, r.w-2, r.h-2));
    fill_rect(get_sys_color(brControlBg), R(r.x+1, r.y+1, r.w-3, r.h-3));
    fill_rect(get_sys_color(brFlare), R(r.x, r.y, 1, 1));
  }
}

// Draw window panel
void draw_panel(window_t const *win) {
  irect16_t r = win->frame;
  draw_bevel(r);
  if (!(win->flags & WINDOW_NORESIZE)) {
    int sb = SCROLLBAR_WIDTH;
    fill_rect(get_sys_color(brLightEdge), R(r.x+r.w, r.y+r.h-sb+1, 1, sb));
    fill_rect(get_sys_color(brLightEdge), R(r.x+r.w-sb+1, r.y+r.h, sb, 1));
  }
  if (!(win->flags&WINDOW_NOFILL)) {
    fill_rect(get_sys_color(brControlBg), r);
  }
}

// Draw a theme icon centred inside rect r.
void draw_theme_icon_in_rect(int id, irect16_t r, uint32_t col) {
  draw_theme_icon(id,
                  r.x + (r.w - THEME_ICON_SIZE) / 2,
                  r.y + (r.h - THEME_ICON_SIZE) / 2,
                  THEME_ICON_SIZE, col);
}

// Draw window controls (close, minimize, etc.)
void draw_window_controls(window_t *win) {
  irect16_t r = win->frame;
  fill_rect(get_sys_color(window_has_focus(win) ? brActiveTitlebar : brInactiveTitlebar),
            rect_split_top(r, titlebar_height(win)));
  set_fullscreen();
  draw_theme_icon_in_rect(THEME_ICON_CLOSE,
                          rect_split_right(rect_split_top(r, TITLEBAR_HEIGHT), TITLEBAR_HEIGHT),
                          get_sys_color(brTextNormal));
}

// Draw status bar
// When WINDOW_HSCROLL is also set and the horizontal bar is visible, the row
// is shared: status text occupies the left 20 % and the scrollbar the rest.
void draw_statusbar(window_t *win, const char *text) {
  if (!(win->flags&WINDOW_STATUSBAR)) return;

  irect16_t r = win->frame;
  int s = statusbar_height(win);
  irect16_t row = rect_split_bottom(r, s);  // the statusbar row at the bottom of the frame

  bool has_h = (win->flags & WINDOW_HSCROLL) && win->hscroll.visible;
  int split_x = has_h ? SB_STATUS_SPLIT_X(r.w) : r.w;

  irect16_t text_area = rect_split_left(row, split_x);
  fill_rect(get_sys_color(brStatusbarBg), text_area);
  set_fullscreen();

  if (text) {
    draw_text_clipped(FONT_SMALL, text, &text_area,
                      get_sys_color(brTextNormal), TEXT_PADDING_LEFT);
  }

  if (has_h) {
    scrollbar_draw_statusbar_merged_hscroll(win, row, split_x);
  }
}

// Set OpenGL viewport for window
void set_viewport(irect16_t frame) {
  if (!g_ui_runtime.running) return;
  irect16_t ogl_rect = get_opengl_rect(frame);
  
  glViewport(ogl_rect.x, ogl_rect.y, ogl_rect.w, ogl_rect.h);
  set_scissor_cached(&ogl_rect);
}

void set_clip_rect(window_t const *win, irect16_t r) {
  if (!g_ui_runtime.running) return;
  irect16_t ogl_rect = get_opengl_rect(win ? rect_offset(r, win->frame.x, win->frame.y) : r);
  set_scissor_cached(&ogl_rect);
}

// Set viewport and projection for rendering into a root window's FBO.
// Translates coordinates so the root's top-left is at FBO origin (0,0).
void set_viewport_for_fbo(window_t *root) {
  if (!g_ui_runtime.running || !root) return;
  int w = root->surface_w;
  int h = root->surface_h;
  if (w <= 0 || h <= 0) return;
  glViewport(0, 0, w, h);
  glDisable(GL_SCISSOR_TEST);
  g_scissor_valid = false;
  set_projection(root->frame.x, root->frame.y, w, h);
}

// Set scissor rect in FBO pixel coordinates (Y already flipped).
void set_scissor_fbo(irect16_t r) {
  if (!g_ui_runtime.running) return;
  glEnable(GL_SCISSOR_TEST);
  glScissor(r.x, r.y, r.w, r.h);
}

static void clear_rounded_corner(uint32_t clear_stencil_id, int x0, int y0, int radius,
                                int cx2, int cy2, int r2_2x) {
  for (int y = 0; y < radius; y++) {
    int py2 = (y0 + y) * 2 + 1;
    for (int x = 0; x < radius; x++) {
      int px = x0 + x;
      int dx2 = ((x0 + x) * 2 + 1) - cx2;
      int dy2 = py2 - cy2;
      if (dx2 * dx2 + dy2 * dy2 > r2_2x) {
        fill_rect(clear_stencil_id, R(px, y0 + y, 1, 1));
      }
    }
  }
}

// Paint window to stencil buffer
void paint_window_stencil(window_t const *w) {
  extern uint32_t ui_white_texture;
  int p = 1;
  glStencilFunc(GL_ALWAYS, w->id, 0xFF);            // Always pass
  glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE); // Replace stencil with window ID
  draw_rect(ui_white_texture, R(w->frame.x-p, w->frame.y-p, w->frame.w+p*2, w->frame.h+p*2));

  int radius = (int)(4.0f * (float)axGetScaling() + 0.5f);
  int max_radius_x = w->frame.w / 2;
  int max_radius_y = w->frame.h / 2;
  if (radius > max_radius_x) radius = max_radius_x;
  if (radius > max_radius_y) radius = max_radius_y;
  if (radius <= 0) return;

  int frame_x = w->frame.x;
  int frame_y = w->frame.y;
  int frame_x2 = w->frame.x + w->frame.w;
  int frame_y2 = w->frame.y + w->frame.h;
  int cx_tl = (frame_x + radius) * 2;
  int cy_tl = (frame_y + radius) * 2;
  int cx_tr = (frame_x2 - radius) * 2;
  int cy_br = (frame_y2 - radius) * 2;
  int r2_2x = (radius * 2) * (radius * 2);
  
  glStencilFunc(GL_ALWAYS, 0, 0xFF); // clear only outside rounded corners
  clear_rounded_corner(0, frame_x, frame_y, radius, cx_tl, cy_tl, r2_2x);
  clear_rounded_corner(0, frame_x2 - radius, frame_y, radius, cx_tr, cy_tl, r2_2x);
  clear_rounded_corner(0, frame_x, frame_y2 - radius, radius, cx_tl, cy_br, r2_2x);
  clear_rounded_corner(0, frame_x2 - radius, frame_y2 - radius, radius, cx_tr, cy_br, r2_2x);
}

// Repaint window stencil buffer
void repaint_stencil(void) {
  set_fullscreen();
  
  glEnable(GL_STENCIL_TEST);
  glClearStencil(0);
  glClear(GL_STENCIL_BUFFER_BIT);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
    if (!window_has_state(w, WINDOW_STATE_VISIBLE))
      continue;
    send_message(w, evPaintStencil, 0, NULL);
  }
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}

// Set stencil test to render for specific window
void ui_set_stencil_for_window(uint32_t window_id) {
  glStencilFunc(GL_EQUAL, window_id, 0xFF);
}

// Set stencil test to render for root window
void ui_set_stencil_for_root_window(uint32_t window_id) {
  glStencilFunc(GL_EQUAL, window_id, 0xFF);
}

// Fill a rectangle with a solid color
void fill_rect(uint32_t color, irect16_t r) {
  extern uint32_t ui_white_texture;
  if (!g_ui_runtime.running) return;
  // Pass color via tint+alpha uniforms — the white texture stays constant white,
  // no glTexSubImage2D needed.  draw_sprite_region unpacks RGBA from color and
  // sets the tint and alpha uniforms so the shader outputs the desired color.
  draw_sprite_region((int)ui_white_texture, r, NULL, color, 0);
}

static void color_to_params(uint32_t color, ui_render_effect_params_t *params, int base) {
  if (!params) return;
  params->f[base + 0] = (float)((color      ) & 0xFF) / 255.0f;
  params->f[base + 1] = (float)((color >>  8) & 0xFF) / 255.0f;
  params->f[base + 2] = (float)((color >> 16) & 0xFF) / 255.0f;
  params->f[base + 3] = (float)((color >> 24) & 0xFF) / 255.0f;
}

void draw_gradient_rect(irect16_t r, uint32_t left_color, uint32_t right_color) {
  extern uint32_t ui_white_texture;
  if (!g_ui_runtime.running || r.w < 1 || r.h < 1) return;

  ui_render_effect_params_t params = {{0}};
  color_to_params(left_color, &params, 0);
  color_to_params(right_color, &params, 4);
  draw_rect_gradient(ui_white_texture, r.x, r.y, r.w, r.h, &params);
}

// Draw a dashed selection-outline rectangle using 4 tiled draw calls instead of
// one fill_rect per dash segment.  The 4x4 checker texture is sampled with tiled
// UVs so that the first row/column produces a B,B,W,W repeating dash regardless
// of the selection size, keeping the GL call count constant (O(1)).
void draw_sel_rect(irect16_t r) {
  extern uint32_t ui_checker_texture;

  if (!g_ui_runtime.running || r.w < 1 || r.h < 1) return;
  int x = r.x;
  int y = r.y;
  int w = r.w;
  int h = r.h;

  // Top edge: tile along U, sample only the first texture row (v 0..0.25)
  draw_sprite_region(ui_checker_texture, R(x, y, w, 1),
                     UV_RECT(0.0f, 0.0f, (float)w / 4.0f, 0.25f),
                     0xFFFFFFFF, 0);
  // Bottom edge
  draw_sprite_region(ui_checker_texture, R(x, y + h - 1, w, 1),
                     UV_RECT(0.0f, 0.0f, (float)w / 4.0f, 0.25f),
                     0xFFFFFFFF, 0);
  if (h > 2) {
    // Left edge (skip corners already drawn above): tile along V, sample col 0 (u 0..0.25)
    draw_sprite_region(ui_checker_texture, R(x, y + 1, 1, h - 2),
                       UV_RECT(0.0f, 0.0f, 0.25f, (float)(h - 2) / 4.0f),
                       0xFFFFFFFF, 0);
    // Right edge
    draw_sprite_region(ui_checker_texture, R(x + w - 1, y + 1, 1, h - 2),
                       UV_RECT(0.0f, 0.0f, 0.25f, (float)(h - 2) / 4.0f),
                       0xFFFFFFFF, 0);
  }
}

void draw_theme_icon(int id, int x, int y, int size, uint32_t col) {
  bitmap_strip_t *s = ui_get_theme_strip();
  if (!s || s->tex == 0 || s->cols <= 0) return;
  int total = s->cols * (s->sheet_h / s->icon_h);
  if (id < 0 || id >= total) return;
  int scol = id % s->cols;
  int srow = id / s->cols;
  float u0 = (float)(scol * s->icon_w) / (float)s->sheet_w;
  float v0 = (float)(srow * s->icon_h) / (float)s->sheet_h;
  float u1 = u0 + (float)s->icon_w / (float)s->sheet_w;
  float v1 = v0 + (float)s->icon_h / (float)s->sheet_h;
  draw_sprite_region((int)s->tex, R(x, y, size, size), UV_RECT(u0, v0, u1, v1), col, 0);
}

void draw_icon8(int icon, int x, int y, uint32_t col) {
  draw_theme_icon(icon, x, y, THEME_ICON_SIZE, col);
}

void draw_icon8_clipped(int icon, irect16_t rect, uint32_t col) {
  draw_theme_icon(icon,
                  rect.x + (rect.w - THEME_ICON_SIZE) / 2,
                  rect.y + (rect.h - THEME_ICON_SIZE) / 2,
                  THEME_ICON_SIZE, col);
}

void draw_sysicon(const char *name, int x, int y, int size, uint32_t col) {
  if (!name || !name[0] || size <= 0) return;
  sysicon_resolved_t r;
  if (!sysicon_resolve(name, &r)) return;
  draw_sprite_region((int)r.tex, R(x, y, size, size),
                     UV_RECT(r.u0, r.v0, r.u1, r.v1), col, 0);
}

void draw_icon16(int icon, int x, int y, uint32_t col) {
  icon *= 2;
  draw_text_small((char[]) { icon+128, icon+129, 0 }, x, y, col);
  draw_text_small((char[]) { icon+144, icon+145, 0 }, x, y+8, col);
}

void draw_checkerboard(irect16_t r, int square_px) {
  extern uint32_t ui_transparency_checker_texture;
  if (!g_ui_runtime.running || r.w < 1 || r.h < 1 || square_px < 1) return;
  if (ui_transparency_checker_texture == 0) return;
  float uv_x = (float)r.w / (float)(square_px * 2);
  float uv_y = (float)r.h / (float)(square_px * 2);
  draw_sprite_region(ui_transparency_checker_texture, r,
                     UV_RECT(0.0f, 0.0f, uv_x, uv_y),
                     0xFFFFFFFF, 0);
}

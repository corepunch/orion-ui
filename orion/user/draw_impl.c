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

// When non-NULL, viewport/projection/scissor functions redirect from
// screen-space to FBO-local coordinates automatically.
static window_t *g_fbo_root = NULL;

// OpenGL's framebuffer origin is bottom-left. Keep that backend detail here;
// every public drawing/scissor API uses logical top-left coordinates.
static irect16_t fbo_rect(window_t const *root, irect16_t r) {
  int scale = (int)axGetScaling();
  if (scale < 1) scale = 1;
  return R(r.x * scale,
           root->surface_h - (r.y + r.h) * scale,
           r.w * scale, r.h * scale);
}

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
  if (g_fbo_root) {
    set_viewport_for_fbo(g_fbo_root);
    return;
  }
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
    t += toolbar_effective_item_height(win) + 2 * (TOOLBAR_PADDING + TOOLBAR_BEVEL_WIDTH);
  }
  return t;
}

static ipoint16_t window_origin_in_root(window_t const *win) {
  ipoint16_t p = {0, 0};
  for (window_t const *w = win; w && w->parent; w = w->parent) {
    p.x += w->frame.x;
    p.y += w->frame.y + titlebar_height(w->parent);
  }
  return p;
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

// Draw button background — routed through the active theme.
// dx/dy were unused press-offset params; kept for ABI compatibility.
void draw_button(irect16_t r, int dx, int dy, bool pressed) {
  (void)dx; (void)dy;
  ctrl_state_t state = pressed ? CTRL_PRESSED : CTRL_NORMAL;
  theme_draw(THEME_PART_BUTTON, r, state);
}

// Draw window panel — border/grip via theme, fill guarded by WINDOW_NOFILL.
void draw_panel(window_t const *win) {
  irect16_t r = R(0, 0, win->frame.w, win->frame.h);
  if (win->maximized) {
    if (!(win->flags & WINDOW_NOFILL)) fill_rect(get_sys_color(brControlBg), r);
    return;
  }
  theme_draw((win->flags & WINDOW_NOFILL) ? THEME_PART_PANEL_BORDER : THEME_PART_PANEL,
             r, CTRL_NORMAL);
  if (!(win->flags & WINDOW_NORESIZE)) theme_draw(THEME_PART_RESIZE_GRIP, r, CTRL_NORMAL);
}

// Draw a theme icon centred inside rect r.
void draw_theme_icon_in_rect(int id, irect16_t r, uint32_t col) {
  int size = (id == THEME_ICON_CLOSE || id == THEME_ICON_MAXIMIZE || id == THEME_ICON_RESTORE)
    ? MIN(16, MIN(r.w, r.h)) : THEME_ICON_SIZE;
  irect16_t icon = rect_center(r, size, size);
  draw_theme_icon(id, icon.x, icon.y, size, col);
}

// Draw window controls (titlebar + close button).
void draw_window_controls(window_t *win) {
  irect16_t r = R(0, 0, win->frame.w, win->frame.h);
  get_theme()->draw_window_chrome(rect_split_top(r, TITLEBAR_HEIGHT),
                                  rect_split_top(r, TITLEBAR_HEIGHT), win->title,
                                  (window_has_focus(win) ? CTRL_FOCUSED : CTRL_NORMAL) |
                                  ((win->flags & WINDOW_NOCLOSE) ? CTRL_NO_CLOSE : 0),
                                  win->maximizable && !win->parent && !(win->flags & (WINDOW_NORESIZE | WINDOW_DIALOG | WINDOW_ALWAYSINBACK | WINDOW_ALWAYSONTOP)));
}

// Draw status bar
// When WINDOW_HSCROLL is also set and the horizontal bar is visible, the row
// is shared: status text occupies the left 20 % and the scrollbar the rest.
void draw_statusbar(window_t *win, const char *text) {
  if (!(win->flags&WINDOW_STATUSBAR)) return;

  irect16_t r = R(0, 0, win->frame.w, win->frame.h);
  int s = statusbar_height(win);
  irect16_t row = rect_split_bottom(r, s);  // the statusbar row at the bottom of the frame

  bool has_h = (win->flags & WINDOW_HSCROLL) && win->hscroll.visible;
  int split_x = has_h ? SB_STATUS_SPLIT_X(r.w) : r.w;

  set_scissor_fbo(get_root_window(win), row);
  theme_draw(THEME_PART_STATUSBAR, row, CTRL_NORMAL);
  irect16_t text_area = rect_split_left(row, split_x);
  set_scissor_fbo(get_root_window(win), text_area);
  get_theme()->draw_statusbar_text(text_area, text);
  set_scissor_fbo(get_root_window(win), r);

  if (has_h) {
    scrollbar_draw_statusbar_merged_hscroll(win, row, split_x);
  }
}

// Set OpenGL viewport for window
void set_viewport(irect16_t frame) {
  if (!g_ui_runtime.running) return;
  if (g_fbo_root) {
    // FBO callers use root-local, top-left coordinates.
    irect16_t r = fbo_rect(g_fbo_root, frame);
    glViewport(r.x, r.y, r.w, r.h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(r.x, r.y, r.w, r.h);
    g_scissor_valid = false;
    return;
  }
  irect16_t ogl_rect = get_opengl_rect(frame);
  
  glViewport(ogl_rect.x, ogl_rect.y, ogl_rect.w, ogl_rect.h);
  set_scissor_cached(&ogl_rect);
}

void set_clip_rect(window_t const *win, irect16_t r) {
  if (!g_ui_runtime.running) return;
  if (g_fbo_root) {
    ipoint16_t origin = win ? window_origin_in_root(win) : (ipoint16_t){0, 0};
    irect16_t local = rect_offset(r, origin.x, origin.y);
    irect16_t clip = fbo_rect(g_fbo_root, local);
    glEnable(GL_SCISSOR_TEST);
    glScissor(clip.x, clip.y, clip.w, clip.h);
    g_scissor_valid = false;
    return;
  }
  irect16_t absolute = win
                     ? rect_offset(r, window_screen_x(win), window_screen_y(win))
                     : r;
  irect16_t ogl_rect = get_opengl_rect(absolute);
  set_scissor_cached(&ogl_rect);
}

// Set viewport and projection for rendering into a root window's FBO.
// The FBO is sized in physical pixels (logical × scale), but drawing
// coordinates are logical.  The projection maps logical coords → physical
// FBO pixels, with Y flipped so logical y=0 (top) maps to FBO y=0 (top).
void set_viewport_for_fbo(window_t *root) {
  if (!g_ui_runtime.running || !root) return;
  g_fbo_root = root;
  int w = root->surface_w;
  int h = root->surface_h;
  if (w <= 0 || h <= 0) return;
  glViewport(0, 0, w, h);
  glDisable(GL_SCISSOR_TEST);
  g_scissor_valid = false;
  int scale = (int)axGetScaling();
  if (scale < 1) scale = 1;
  int log_w = w / scale;
  int log_h = h / scale;
  set_projection(0, 0, log_w, log_h);
}

// Set an FBO scissor using root-local, top-left logical coordinates.
void set_scissor_fbo(window_t const *root, irect16_t r) {
  if (!g_ui_runtime.running || !root) return;
  irect16_t clip = fbo_rect(root, r);
  glEnable(GL_SCISSOR_TEST);
  glScissor(clip.x, clip.y, clip.w, clip.h);
}

// ── Stencil no-ops (kept for API compat, superseded by FBO compositing) ───
void paint_window_stencil(window_t const *w) { (void)w; }
void repaint_stencil(void) {}
void ui_set_stencil_for_window(uint32_t id) { (void)id; }
void ui_set_stencil_for_root_window(uint32_t id) { (void)id; }

// Fill a rectangle with a solid color
void fill_rect(uint32_t color, irect16_t r) {
  extern uint32_t ui_white_texture;
  if (!g_ui_runtime.running) return;
  // Pass color via tint+alpha uniforms — the white texture stays constant white,
  // no glTexSubImage2D needed.  draw_sprite_region unpacks RGBA from color and
  // sets the tint and alpha uniforms so the shader outputs the desired color.
  draw_sprite_region((int)ui_white_texture, r, NULL, color, 0);
}

void fill_rounded_rect(uint32_t color, irect16_t r, int radius) {
  extern uint32_t ui_white_texture;
  if (!g_ui_runtime.running || r.w <= 0 || r.h <= 0) return;
  if (radius <= 0) { fill_rect(color, r); return; }
  float scale = MAX(1.0f, axGetScaling());
  render_rounded_rect(ui_white_texture, r, (int)(r.w * scale + 0.5f),
                      (int)(r.h * scale + 0.5f), radius * scale, 1.0f, color);
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
  static const char *names[THEME_ICON_COUNT] = {
    "lucide-x", "lucide-chevron-up", "lucide-chevron-down", "lucide-chevrons-up-down",
    "lucide-check", "lucide-chevron-up", "lucide-chevron-right", "lucide-chevron-down",
    "lucide-chevron-left", "lucide-grip", "lucide-arrow-up", "lucide-copy"
  };
  if (id < 0 || id >= THEME_ICON_COUNT || size <= 0) {
    fprintf(stderr, "[draw] invalid theme icon id=%d size=%d\n", id, size);
    fflush(stderr);
    return;
  }
  sysicon_resolved_t icon;
  if (!sysicon_resolve(names[id], &icon)) return;
  draw_sprite_region((int)icon.tex, R(x, y, size, size),
                     UV_RECT(icon.u0, icon.v0, icon.u1, icon.v1), col, 0);
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

// Composite all visible root windows from their FBO textures to the
// default framebuffer, applying SDF rounded-corner masking.
void composite_root_windows(void) {
  if (!g_ui_runtime.running) return;
  g_fbo_root = NULL;

  // iOS and offscreen hosts present a platform-owned, nonzero framebuffer.
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  axBindFramebuffer();
  uint32_t ws = axGetSize(NULL);
  int screen_w = (int)LOWORD(ws);
  int screen_h = (int)HIWORD(ws);
  glViewport(0, 0, screen_w, screen_h);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // Set projection for screen-space compositing.
  set_fullscreen();

  theme_t *theme = get_theme();
  float base_radius = theme->window_corner_radius * axGetScaling();

  for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
    if (!window_has_state(w, WINDOW_STATE_VISIBLE)) continue;
    if (!w->surface_tex) continue;

    // Clamp radius to half the smallest dimension (in physical pixels).
    int max_r = w->surface_w < w->surface_h ? w->surface_w / 2 : w->surface_h / 2;
    float radius = w->maximized ? 0.0f : base_radius;
    if (radius > max_r) radius = (float)max_r;

    if (!w->maximized && !(w->flags & WINDOW_TRANSPARENT))
      draw_rect_shadow(w->frame, theme->window_corner_radius, theme->window_shadow_blur,
                       theme->window_shadow_offset, theme->window_shadow_color);
    draw_rounded_rect((int)w->surface_tex,
                      (irect16_t){w->frame.x, w->frame.y, w->frame.w, w->frame.h},
                      w->surface_w, w->surface_h,
                      radius, 1.0f);
  }

  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
}

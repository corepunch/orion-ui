#include <stdlib.h>
#include <stdio.h>
#include "user.h"
#include "draw.h"
#include <orion/kernel/renderer.h>
#include "theme.h"

// Compositor-owned redirection surfaces, keyed by root window (≈ DWM). The
// window struct carries no surface state; procs only paint.
typedef struct { const window_t *win; window_surface_t surface; int attr[WCA_COUNT]; } surface_entry_t;
static surface_entry_t *g_surfaces;
static int g_surface_count, g_surface_cap;

static surface_entry_t *surface_find(const window_t *win) {
  for (int i = 0; i < g_surface_count; i++)
    if (g_surfaces[i].win == win) return &g_surfaces[i];
  return NULL;
}

window_surface_t *window_surface(const window_t *win) {
  surface_entry_t *e = win ? surface_find(win) : NULL;
  return e ? &e->surface : NULL;
}

bool window_has_surface(const window_t *win) {
  window_surface_t *s = window_surface(win);
  return s && s->tex;
}

window_surface_t *window_surface_ensure(window_t *win) {
  if (!win) {
    fprintf(stderr, "[surface] ensure rejected: window unavailable\n");
    fflush(stderr);
    return NULL;
  }
  surface_entry_t *e = surface_find(win);
  if (e) return &e->surface;
  if (g_surface_count == g_surface_cap) {
    int cap = g_surface_cap ? g_surface_cap * 2 : 8;
    surface_entry_t *grown = realloc(g_surfaces, (size_t)cap * sizeof(*grown));
    if (!grown) {
      fprintf(stderr, "[surface] ensure rejected win=%u: allocation failed cap=%d\n", win->id, cap);
      fflush(stderr);
      return NULL;
    }
    g_surfaces = grown;
    g_surface_cap = cap;
  }
  e = &g_surfaces[g_surface_count++];
  *e = (surface_entry_t){.win = win};
  for (int i = 0; i < WCA_COUNT; i++) e->attr[i] = WCA_AUTO;
  return &e->surface;
}

void window_surface_release(window_t *win) {
  surface_entry_t *e = win ? surface_find(win) : NULL;
  if (!e) return;
  R_DestroyWindowTarget(&e->surface.fbo, &e->surface.tex, &e->surface.w, &e->surface.h);
  *e = g_surfaces[--g_surface_count];
}

// Adopt an externally created target (tests that build their own FBO).
void window_surface_adopt(window_t *win, uint32_t fbo, uint32_t tex, int w, int h) {
  window_surface_t *s = window_surface_ensure(win);
  if (s) *s = (window_surface_t){fbo, tex, w, h};
}

// Per-window compositor attributes (≈ DwmSetWindowAttribute). WCA_AUTO defers to the theme and the
// window's state (maximized or transparent windows have square corners, no shadow, no border).
void window_set_composition_attr(window_t *win, window_composition_attr_t attr, int value) {
  if (!win || attr < 0 || attr >= WCA_COUNT || value < WCA_AUTO) {
    fprintf(stderr, "[surface] composition attribute rejected win=%u attr=%d value=%d\n", win ? win->id : 0, (int)attr, value);
    fflush(stderr);
    return;
  }
  surface_entry_t *e = surface_find(win);
  if (!e) { window_surface_ensure(win); e = surface_find(win); }
  if (!e || e->attr[attr] == value) return;
  e->attr[attr] = value;
  request_composite();
}

int window_composition_attr(const window_t *win, window_composition_attr_t attr) {
  if (!win || attr < 0 || attr >= WCA_COUNT) return 0;
  surface_entry_t *e = surface_find(win);
  if (e && e->attr[attr] != WCA_AUTO) return e->attr[attr];
  bool flat = window_is_maximized(win) || (win->flags & WINDOW_TRANSPARENT);
  const theme_t *theme = get_theme();
  switch (attr) {
    case WCA_CORNERS: return flat ? 0 : theme->window_corner_radius;
    case WCA_SHADOW:  return !flat && theme->window_shadow_blur > 0;
    case WCA_BORDER:  return !flat;
    default:          return 0;
  }
}

// Read a root window's redirected surface (≈ PrintWindow with PW_RENDERFULLCONTENT): straight-alpha
// RGBA, top row first, at physical resolution. The caller frees *out_rgba with free().
bool window_capture(const window_t *win, uint8_t **out_rgba, int *out_w, int *out_h) {
  window_surface_t *surf = window_surface(win);
  if (!surf || !surf->tex || !out_rgba || !out_w || !out_h) {
    fprintf(stderr, "[surface] capture rejected win=%u surface=%p out=%p\n", win ? win->id : 0, (void *)surf, (void *)out_rgba);
    fflush(stderr);
    return false;
  }
  uint8_t *rgba = malloc((size_t)surf->w * surf->h * 4);
  if (!rgba) {
    fprintf(stderr, "[surface] capture allocation failed win=%u size=%dx%d\n", win->id, surf->w, surf->h);
    fflush(stderr);
    return false;
  }
  if (!R_ReadTextureSRGBA8(surf->tex, surf->w, surf->h, rgba)) { free(rgba); return false; }
  *out_rgba = rgba; *out_w = surf->w; *out_h = surf->h;
  return true;
}

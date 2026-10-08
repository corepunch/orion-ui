#include <stdlib.h>
#include <stdio.h>
#include "user.h"
#include "draw.h"
#include <orion/kernel/renderer.h>

// Compositor-owned redirection surfaces, keyed by root window (≈ DWM). The
// window struct carries no surface state; procs only paint.
typedef struct { const window_t *win; window_surface_t surface; } surface_entry_t;
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

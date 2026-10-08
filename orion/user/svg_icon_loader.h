#ifndef __SVG_ICON_LOADER_H__
#define __SVG_ICON_LOADER_H__

#include <stdio.h>
#include <stdbool.h>
#include "draw.h"

// Set the primary icons directory (global pool, e.g. share/orion/icons).
void svg_set_icons_dir(const char *dir);

// Append an icons directory (SVG or BMP files) to the search path, scoped to an application
// instance (≈ LoadImage(hInst, ...)): icons resolve from a scoped directory only while that
// instance paints. hinstance 0 joins the shared system pool. Pass g_gem_hinstance from apps.
// Directories are searched in registration order; a full registry rejects with a log line.
void svg_add_icons_dir(hinstance_t hinstance, const char *dir);

// The instance whose directories sysicon_resolve() consults; the message dispatcher sets it
// to the receiving window's hinstance for the duration of each message.
void svg_set_icon_scope(hinstance_t hinstance);
hinstance_t svg_icon_scope(void);

// Resolved draw info for a named icon.
typedef struct {
  uint32_t tex;
  float    u0, v0, u1, v1;
  int      w, h;
} sysicon_resolved_t;

// Resolve an SVG base name (e.g. "git-fork", "undo") to GPU draw info.
// Loads the BMP or SVG on demand; textures live in an LRU cache and SVGs are re-rasterized when display density changes.
// Returned dimensions are logical pixels; texture resolution follows display density.
// Returns false if the icon cannot be found in any registered icons directory.
bool sysicon_resolve(const char *name, sysicon_resolved_t *out);
// Rasterize at the final logical draw size to avoid filtering a canonical 24px mask down.
// Separate sizes coexist in the cache; display-density changes refresh each entry.
bool sysicon_resolve_size(const char *name, int size, sysicon_resolved_t *out);

#endif

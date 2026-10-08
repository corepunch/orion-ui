#ifndef __SVG_ICON_LOADER_H__
#define __SVG_ICON_LOADER_H__

#include <stdio.h>
#include <stdbool.h>
#include "draw.h"

// Set the primary icons directory (global pool, e.g. share/orion/icons).
void svg_set_icons_dir(const char *dir);

// Append an additional icons directory to the search path.
// Icons not found in the primary pool are looked up here in registration order.
// Use this for app-specific icon sets (e.g. apps/gitclient/share/icons).
void svg_add_icons_dir(const char *dir);

// Resolved draw info for a named icon.
typedef struct {
  uint32_t tex;
  float    u0, v0, u1, v1;
  int      w, h;
} sysicon_resolved_t;

// Resolve an SVG base name (e.g. "git-fork", "undo") to GPU draw info.
// Loads the SVG on demand and refreshes cached pixels when display density changes.
// Returned dimensions are logical pixels; texture resolution follows display density.
// Returns false if the icon cannot be found in any registered icons directory.
bool sysicon_resolve(const char *name, sysicon_resolved_t *out);
// Rasterize at the final logical draw size to avoid filtering a canonical 24px mask down.
// Separate sizes coexist in the cache; display-density changes refresh each entry.
bool sysicon_resolve_size(const char *name, int size, sysicon_resolved_t *out);

#endif

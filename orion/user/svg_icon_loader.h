#ifndef __SVG_ICON_LOADER_H__
#define __SVG_ICON_LOADER_H__

#include <stdio.h>
#include <stdbool.h>
#include "draw.h"

// Build a bitmap_strip_t by rasterizing SVG files from a directory.
//
// svg_names : array of `count` iconoir base names (no .svg extension);
//             NULL entries produce blank tiles.
// icon_size : logical tile size in pixels (square); rasterized at display density.
// cols      : sheet columns; rows are computed automatically.
// missing   : optional FILE* to receive one diagnostic line per blank tile.
//
// On success, fills *out and returns true.  The texture is uploaded to GPU and
// must be released with R_DeleteTexture(out->tex) when done.
bool svg_build_strip(const char *icons_dir,
                     const char **svg_names, int count,
                     int icon_size, int cols,
                     bitmap_strip_t *out,
                     FILE *missing);

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

#endif

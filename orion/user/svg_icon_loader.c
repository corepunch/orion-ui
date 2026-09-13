// SVG icon strip loader — rasterizes iconoir SVGs into bitmap_strip_t at startup.
// nanosvg implementation is compiled here (single TU).

#define NANOSVG_IMPLEMENTATION
#include "tools/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "tools/nanosvgrast.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>

#include <platform/platform.h>
#include "bmp_icon_loader.h"
#include "svg_icon_loader.h"

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static int svg_raster_size(int logical_size) {
  float scale = fmaxf(1.0f, axGetScaling()) * UI_WINDOW_SCALE;
  double size = ceil((double)logical_size * scale);
  if (logical_size <= 0 || !isfinite(size) || size > INT_MAX / 4) {
    fprintf(stderr, "[svg] invalid raster size logical=%d scale=%g\n", logical_size, scale);
    fflush(stderr);
    return 0;
  }
  return (int)size;
}

// Replace all occurrences of "currentColor" (12 bytes) in-place so iconoir SVGs
// rasterize white, ready to be tinted at draw time. Must be a same-length
// replacement. A named color padded with spaces fails: nanosvg's color parser
// (nsvg__parseColorName) strcmp()s against the raw value with no trailing-space
// trim, so "white       " falls through to the gray fallback (128,128,128).
// A hex literal works because nsvg__parseColorHex uses sscanf("#%2x%2x%2x"),
// which stops cleanly at the padding spaces.
static void patch_current_color(char *svg) {
    const char needle[]  = "currentColor";
    const char replace[] = "#ffffff     ";  // 12 chars each
    const size_t n = sizeof(needle) - 1;
    char *p = svg;
    while ((p = strstr(p, needle)) != NULL) {
        memcpy(p, replace, n);
        p += n;
    }
}

// Rasterize one SVG file into `out_rgba` (icon_size × icon_size × 4 bytes, RGBA).
// Returns true on success.  The tile is centered inside the square tile.
static bool rasterize_svg(const char *path, int size, uint8_t *out_rgba) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);

    if (file_size <= 0 || file_size > 512 * 1024) {
        fclose(f);
        return false;
    }

    char *buf = malloc((size_t)file_size + 1);
    if (!buf) { fclose(f); return false; }

    size_t read = fread(buf, 1, (size_t)file_size, f);
    fclose(f);
    buf[read] = '\0';

    patch_current_color(buf);

    // nsvgParse modifies buf in-place; we pass a copy so we can still free buf.
    NSVGimage *img = nsvgParse(buf, "px", 96.0f);
    free(buf);

    if (!img || img->width <= 0.0f || img->height <= 0.0f) {
        if (img) nsvgDelete(img);
        return false;
    }

    NSVGrasterizer *rast = nsvgCreateRasterizer();
    if (!rast) { nsvgDelete(img); return false; }

    float scale = fminf((float)size / img->width, (float)size / img->height);
    float tx    = ((float)size - img->width  * scale) * 0.5f;
    float ty    = ((float)size - img->height * scale) * 0.5f;

    memset(out_rgba, 0, (size_t)size * size * 4);
    nsvgRasterize(rast, img, tx, ty, scale, out_rgba, size, size, size * 4);

    nsvgDeleteRasterizer(rast);
    nsvgDelete(img);
    return true;
}

// ---------------------------------------------------------------------------
// Public: generic strip builder
// ---------------------------------------------------------------------------

bool svg_build_strip(const char *icons_dir,
                     const char **svg_names, int count,
                     int icon_size, int cols,
                     bitmap_strip_t *out,
                     FILE *missing) {
    if (!icons_dir || !svg_names || count <= 0 || icon_size <= 0 || cols <= 0 || !out) {
        fprintf(stderr, "[svg] invalid strip parameters count=%d size=%d cols=%d\n",
                count, icon_size, cols);
        fflush(stderr);
        return false;
    }

    int raster_size = svg_raster_size(icon_size);
    int rows = 1 + (count - 1) / cols;
    if (!raster_size) return false;
    if (cols > INT_MAX / raster_size || rows > INT_MAX / raster_size) {
        fprintf(stderr, "[svg] strip dimensions overflow count=%d size=%d cols=%d\n",
                count, raster_size, cols);
        fflush(stderr);
        return false;
    }
    int sheet_w = cols * raster_size;
    int sheet_h = rows * raster_size;
    if ((size_t)sheet_w > SIZE_MAX / 4 / (size_t)sheet_h) {
        fprintf(stderr, "[svg] strip allocation overflow width=%d height=%d\n", sheet_w, sheet_h);
        fflush(stderr);
        return false;
    }

    uint8_t *sheet = calloc((size_t)sheet_w * sheet_h * 4, 1);
    if (!sheet) return false;

    uint8_t *tile = malloc((size_t)raster_size * raster_size * 4);
    if (!tile) { free(sheet); return false; }

    int ok_count = 0;
    for (int i = 0; i < count; i++) {
        const char *name  = svg_names[i];
        bool drawn = false;
        if (name && name[0]) {
            char path[4096];
            snprintf(path, sizeof(path), "%s/%s.svg", icons_dir, name);
            drawn = rasterize_svg(path, raster_size, tile);
            if (drawn) {
                ok_count++;
            } else if (missing) {
                fprintf(missing, "MISSING icon[%d] \"%s\"\n", i, name);
            }
        } else if (missing) {
            fprintf(missing, "UNMAPPED icon[%d]\n", i);
        }

        if (!drawn) memset(tile, 0, (size_t)raster_size * raster_size * 4);

        int col = i % cols;
        int row = i / cols;
        for (int y = 0; y < raster_size; y++) {
            memcpy(sheet + ((size_t)(row * raster_size + y) * sheet_w + col * raster_size) * 4,
                   tile  + (size_t)y * raster_size * 4,
                   (size_t)raster_size * 4);
        }
    }

    free(tile);

    // Refuse to upload a fully-blank sheet: if no SVGs were found the caller
    // should fall back to its PNG (or accept an empty strip).
    if (ok_count == 0) {
        free(sheet);
        return false;
    }

    uint32_t tex = R_CreateTextureRGBA(sheet_w, sheet_h, (uint8_t *)sheet,
                                       R_FILTER_LINEAR, R_WRAP_CLAMP);
    free(sheet);
    if (!tex) return false;

    out->tex     = tex;
    out->icon_w  = icon_size;
    out->icon_h  = icon_size;
    out->cols    = cols;
    // UV ratios and layout stay logical even when the texture has more texels.
    out->sheet_w = cols * icon_size;
    out->sheet_h = rows * icon_size;
    return true;
}


// ---------------------------------------------------------------------------
// On-demand icon resolution (sysicon_resolve / svg_set_icons_dir)
// ---------------------------------------------------------------------------

#define MAX_ICON_DIRS 8
static char g_icon_dirs[MAX_ICON_DIRS][4096];
static int  g_icon_dir_count;

typedef struct {
    char     name[64];
    uint32_t tex;
    int      w, h;
    int      raster_size;
} sysicon_cache_t;
static sysicon_cache_t g_sysicon_cache[64];
static int             g_sysicon_cache_n;

void svg_set_icons_dir(const char *dir) {
    g_icon_dir_count = 0;
    if (dir && dir[0]) {
        strncpy(g_icon_dirs[0], dir, sizeof(g_icon_dirs[0]) - 1);
        g_icon_dir_count = 1;
    }
}

void svg_add_icons_dir(const char *dir) {
    if (!dir || !dir[0] || g_icon_dir_count >= MAX_ICON_DIRS) return;
    strncpy(g_icon_dirs[g_icon_dir_count], dir, sizeof(g_icon_dirs[0]) - 1);
    g_icon_dir_count++;
}

bool sysicon_resolve(const char *name, sysicon_resolved_t *out) {
    if (!name || !name[0] || !out) {
        fprintf(stderr, "[svg] invalid icon request name=%p out=%p\n", (const void *)name, (void *)out);
        fflush(stderr);
        return false;
    }

    if (bmp_icon_resolve(name, out)) return true;

    int raster_size = svg_raster_size(SYSICON_SIZE);
    if (!raster_size) return false;
    sysicon_cache_t *entry = NULL;
    for (int i = 0; i < g_sysicon_cache_n; i++) {
        if (strcmp(g_sysicon_cache[i].name, name) == 0) {
            entry = &g_sysicon_cache[i];
            if (entry->raster_size != raster_size) break;
            out->tex = g_sysicon_cache[i].tex;
            out->u0 = 0.0f; out->v0 = 0.0f; out->u1 = 1.0f; out->v1 = 1.0f;
            out->w  = g_sysicon_cache[i].w;
            out->h  = g_sysicon_cache[i].h;
            return true;
        }
    }

    if (!g_icon_dir_count || (!entry && g_sysicon_cache_n >= 64)) return false;
    uint8_t *pixels = (uint8_t *)malloc((size_t)raster_size * raster_size * 4);
    if (!pixels) return false;
    bool drawn = false;
    char path[5120];
    for (int di = 0; di < g_icon_dir_count && !drawn; di++) {
        snprintf(path, sizeof(path), "%s/%s.svg", g_icon_dirs[di], name);
        drawn = rasterize_svg(path, raster_size, pixels);
    }
    if (!drawn) { free(pixels); return false; }
    uint32_t tex = R_CreateTextureRGBA(raster_size, raster_size, pixels,
                                       R_FILTER_LINEAR, R_WRAP_CLAMP);
    free(pixels);
    if (!tex) return false;
    sysicon_cache_t *e = entry ? entry : &g_sysicon_cache[g_sysicon_cache_n++];
    if (entry) R_DeleteTexture(entry->tex);
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->name[sizeof(e->name) - 1] = '\0';
    e->tex = tex; e->w = SYSICON_SIZE; e->h = SYSICON_SIZE;
    e->raster_size = raster_size;
    out->tex = tex; out->u0 = 0.0f; out->v0 = 0.0f; out->u1 = 1.0f; out->v1 = 1.0f;
    out->w = SYSICON_SIZE; out->h = SYSICON_SIZE;
    return true;
}

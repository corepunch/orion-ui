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

// NanoSVG emits premultiplied encoded bytes. Uploads to SRGBA8 accept straight
// sRGB input, so undo that association before the renderer premultiplies in
// linear light for filtered sampling.
static void svg_unpremultiply_rgba(uint8_t *rgba, size_t pixels) {
    if (!rgba) return;
    for (size_t i = 0; i < pixels; i++, rgba += 4) {
        unsigned a = rgba[3];
        if (!a) { rgba[0] = rgba[1] = rgba[2] = 0; continue; }
        for (int c = 0; c < 3; c++) {
            unsigned value = ((unsigned)rgba[c] * 255u + a / 2u) / a;
            rgba[c] = (uint8_t)(value > 255u ? 255u : value);
        }
    }
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
    return sysicon_resolve_size(name, SYSICON_SIZE, out);
}

bool sysicon_resolve_size(const char *name, int size, sysicon_resolved_t *out) {
    if (!name || !name[0] || !out || size <= 0 || size > 512) {
        fprintf(stderr, "[svg] invalid icon request name=%p size=%d out=%p\n", (const void *)name, size, (void *)out);
        fflush(stderr);
        return false;
    }

    if (bmp_icon_resolve(name, out)) return true;

    int raster_size = svg_raster_size(size);
    if (!raster_size) return false;
    sysicon_cache_t *entry = NULL;
    for (int i = 0; i < g_sysicon_cache_n; i++) {
        if (g_sysicon_cache[i].w == size && strcmp(g_sysicon_cache[i].name, name) == 0) {
            entry = &g_sysicon_cache[i];
            if (entry->raster_size != raster_size) break;
            out->tex = g_sysicon_cache[i].tex;
            out->u0 = 0.0f; out->v0 = 0.0f; out->u1 = 1.0f; out->v1 = 1.0f;
            out->w  = g_sysicon_cache[i].w;
            out->h  = g_sysicon_cache[i].h;
            return true;
        }
    }

    if (!g_icon_dir_count || (!entry && g_sysicon_cache_n >= ARRAY_LEN(g_sysicon_cache))) {
        fprintf(stderr, "[svg] icon cache unavailable name=%s size=%d dirs=%d entries=%d\n",
                name, size, g_icon_dir_count, g_sysicon_cache_n);
        fflush(stderr);
        return false;
    }
    uint8_t *pixels = (uint8_t *)malloc((size_t)raster_size * raster_size * 4);
    if (!pixels) {
        fprintf(stderr, "[svg] icon allocation failed name=%s raster=%d\n", name, raster_size);
        fflush(stderr);
        return false;
    }
    bool drawn = false;
    char path[5120];
    for (int di = 0; di < g_icon_dir_count && !drawn; di++) {
        snprintf(path, sizeof(path), "%s/%s.svg", g_icon_dirs[di], name);
        drawn = rasterize_svg(path, raster_size, pixels);
    }
    if (!drawn) { free(pixels); return false; }
    svg_unpremultiply_rgba(pixels, (size_t)raster_size * raster_size);
    uint32_t tex = R_CreateTextureSRGBA8(raster_size, raster_size, pixels,
                                         R_FILTER_LINEAR, R_WRAP_CLAMP);
    free(pixels);
    if (!tex) return false;
    sysicon_cache_t *e = entry ? entry : &g_sysicon_cache[g_sysicon_cache_n++];
    if (entry) R_DeleteTexture(entry->tex);
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->name[sizeof(e->name) - 1] = '\0';
    e->tex = tex; e->w = size; e->h = size;
    e->raster_size = raster_size;
    out->tex = tex; out->u0 = 0.0f; out->v0 = 0.0f; out->u1 = 1.0f; out->v1 = 1.0f;
    out->w = size; out->h = size;
    return true;
}

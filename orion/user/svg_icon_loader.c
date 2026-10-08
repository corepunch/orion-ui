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
#include "image.h"
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
// On-demand icon resolution (sysicon_resolve)
//
// Icon directories are scoped by application instance (≈ LoadImage(hInst, ...)): a directory
// registered for an instance is searched only while that instance's windows run, so hosted
// apps cannot shadow each other's icons. Directories registered with hinstance 0 form the
// shared system pool. A name resolves to a .bmp (bitmap art, nearest-filtered) or an .svg
// (rasterized at the draw size) in the first matching directory. Resolved textures live in
// one LRU cache keyed by (scope, name, size).
// ---------------------------------------------------------------------------

#define MAX_ICON_DIRS 32
#define ICON_CACHE_MAX 256

static struct { hinstance_t scope; char path[4096]; } g_icon_dirs[MAX_ICON_DIRS];
static int g_icon_dir_count;
static hinstance_t g_icon_scope;

typedef struct {
    char       name[64];
    hinstance_t scope;
    int        size;         // requested logical size; 0 for bitmap art, which has one size
    uint32_t   tex;
    int        w, h;
    int        raster_size;
    uint32_t   last_use;
} sysicon_cache_t;
static sysicon_cache_t g_sysicon_cache[ICON_CACHE_MAX];
static int             g_sysicon_cache_n;
static uint32_t        g_sysicon_clock;

void svg_set_icon_scope(hinstance_t hinstance) { g_icon_scope = hinstance; }
hinstance_t svg_icon_scope(void) { return g_icon_scope; }

void svg_set_icons_dir(const char *dir) {
    g_icon_dir_count = 0;
    if (dir && dir[0]) svg_add_icons_dir(0, dir);
}

void svg_add_icons_dir(hinstance_t hinstance, const char *dir) {
    if (!dir || !dir[0]) {
        fprintf(stderr, "[svg] icon dir rejected scope=%u: empty path\n", hinstance);
        fflush(stderr);
        return;
    }
    for (int i = 0; i < g_icon_dir_count; i++)
        if (g_icon_dirs[i].scope == hinstance && strcmp(g_icon_dirs[i].path, dir) == 0) return;
    if (g_icon_dir_count >= MAX_ICON_DIRS) {
        fprintf(stderr, "[svg] icon dir rejected scope=%u dir=%s: registry full (%d)\n", hinstance, dir, MAX_ICON_DIRS);
        fflush(stderr);
        return;
    }
    g_icon_dirs[g_icon_dir_count].scope = hinstance;
    snprintf(g_icon_dirs[g_icon_dir_count].path, sizeof(g_icon_dirs[0].path), "%s", dir);
    g_icon_dir_count++;
}

bool sysicon_resolve(const char *name, sysicon_resolved_t *out) {
    return sysicon_resolve_size(name, SYSICON_SIZE, out);
}

static bool icon_dir_visible(int i) {
    return g_icon_dirs[i].scope == 0 || g_icon_dirs[i].scope == g_icon_scope;
}

static sysicon_cache_t *icon_cache_slot(void) {
    if (g_sysicon_cache_n < ICON_CACHE_MAX) return &g_sysicon_cache[g_sysicon_cache_n++];
    sysicon_cache_t *oldest = &g_sysicon_cache[0];
    for (int i = 1; i < g_sysicon_cache_n; i++)
        if ((int32_t)(g_sysicon_cache[i].last_use - oldest->last_use) < 0) oldest = &g_sysicon_cache[i];
    R_DeleteTexture(oldest->tex);
    memset(oldest, 0, sizeof(*oldest));
    return oldest;
}

static void icon_fill_result(sysicon_resolved_t *out, const sysicon_cache_t *e) {
    *out = (sysicon_resolved_t){.tex = e->tex, .u0 = 0.0f, .v0 = 0.0f, .u1 = 1.0f, .v1 = 1.0f, .w = e->w, .h = e->h};
}

bool sysicon_resolve_size(const char *name, int size, sysicon_resolved_t *out) {
    if (!name || !name[0] || !out || size <= 0 || size > 512) {
        fprintf(stderr, "[svg] invalid icon request name=%p size=%d out=%p\n", (const void *)name, size, (void *)out);
        fflush(stderr);
        return false;
    }
    int raster_size = svg_raster_size(size);
    if (!raster_size) return false;

    sysicon_cache_t *stale = NULL;
    for (int i = 0; i < g_sysicon_cache_n; i++) {
        sysicon_cache_t *e = &g_sysicon_cache[i];
        if (e->scope != g_icon_scope || strcmp(e->name, name) != 0 || (e->size && e->size != size)) continue;
        if (e->size && e->raster_size != raster_size) { stale = e; break; }  // density changed: rerasterize
        e->last_use = ++g_sysicon_clock;
        icon_fill_result(out, e);
        return true;
    }

    if (!g_icon_dir_count) {
        fprintf(stderr, "[svg] icon unavailable name=%s size=%d scope=%u: no icon directories registered\n", name, size, g_icon_scope);
        fflush(stderr);
        return false;
    }

    char path[5120];
    uint8_t *pixels = NULL;
    int w = size, h = size;
    bool bitmap = false;
    // Own scope and the system pool first; windows created without an instance fall back to every directory.
    for (int pass = 0; pass < 2 && !pixels; pass++)
        for (int i = 0; i < g_icon_dir_count && !pixels; i++) {   // bitmap art wins over SVG
            if (pass == 0 && !icon_dir_visible(i)) continue;
            snprintf(path, sizeof(path), "%s/%s.bmp", g_icon_dirs[i].path, name);
            if ((pixels = load_image(path, &w, &h))) bitmap = true;
        }
    if (!bitmap) {
        pixels = (uint8_t *)malloc((size_t)raster_size * raster_size * 4);
        if (!pixels) {
            fprintf(stderr, "[svg] icon allocation failed name=%s raster=%d\n", name, raster_size);
            fflush(stderr);
            return false;
        }
        bool drawn = false;
        for (int pass = 0; pass < 2 && !drawn; pass++)
            for (int i = 0; i < g_icon_dir_count && !drawn; i++) {
                if (pass == 0 && !icon_dir_visible(i)) continue;
                snprintf(path, sizeof(path), "%s/%s.svg", g_icon_dirs[i].path, name);
                drawn = rasterize_svg(path, raster_size, pixels);
            }
        if (!drawn) { free(pixels); return false; }
        svg_unpremultiply_rgba(pixels, (size_t)raster_size * raster_size);
    }
    uint32_t tex = bitmap ? R_CreateTextureSRGBA8(w, h, pixels, R_FILTER_NEAREST, R_WRAP_CLAMP)
                          : R_CreateTextureSRGBA8(raster_size, raster_size, pixels, R_FILTER_LINEAR, R_WRAP_CLAMP);
    if (bitmap) image_free(pixels); else free(pixels);
    if (!tex) return false;

    sysicon_cache_t *e = stale;
    if (stale) R_DeleteTexture(stale->tex);
    else e = icon_cache_slot();
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->scope = g_icon_scope;
    e->size = bitmap ? 0 : size;
    e->tex = tex; e->w = bitmap ? w : size; e->h = bitmap ? h : size;
    e->raster_size = raster_size;
    e->last_use = ++g_sysicon_clock;
    icon_fill_result(out, e);
    return true;
}

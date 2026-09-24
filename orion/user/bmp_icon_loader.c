#include "bmp_icon_loader.h"

#include <stdlib.h>
#include <string.h>

#include <orion/kernel/renderer.h>
#include "image.h"

#define BMP_ICON_DIR_MAX 8
#define BMP_ICON_CACHE_MAX 64

typedef struct {
  char name[128];
  uint32_t tex;
  int w, h;
} bmp_icon_cache_t;

static char g_bmp_icon_dirs[BMP_ICON_DIR_MAX][4096];
static int g_bmp_icon_dir_count;
static bmp_icon_cache_t g_bmp_icon_cache[BMP_ICON_CACHE_MAX];
static int g_bmp_icon_cache_count;

static bool bmp_load_named(const char *icons_dir, const char *name,
                           uint8_t **out_pixels, int *out_w, int *out_h) {
  char path[5120];
  snprintf(path, sizeof(path), "%s/%s.bmp", icons_dir, name);
  *out_pixels = load_image(path, out_w, out_h);
  return *out_pixels != NULL;
}

void bmp_add_icons_dir(const char *dir) {
  if (!dir || !dir[0] || g_bmp_icon_dir_count >= BMP_ICON_DIR_MAX)
    return;
  snprintf(g_bmp_icon_dirs[g_bmp_icon_dir_count],
           sizeof(g_bmp_icon_dirs[g_bmp_icon_dir_count]), "%s", dir);
  g_bmp_icon_dir_count++;
}

bool bmp_build_strip(const char *icons_dir, const char **bmp_names, int count,
                     int icon_size, int cols, bitmap_strip_t *out,
                     FILE *missing) {
  if (!icons_dir || !bmp_names || count <= 0 || icon_size <= 0 || cols <= 0 || !out)
    return false;

  int rows = (count + cols - 1) / cols;
  int sheet_w = cols * icon_size;
  int sheet_h = rows * icon_size;
  uint8_t *sheet = calloc((size_t)sheet_w * sheet_h * 4, 1);
  if (!sheet)
    return false;

  int loaded = 0;
  for (int i = 0; i < count; i++) {
    uint8_t *pixels = NULL;
    int w = 0, h = 0;
    bool found = bmp_names[i] && bmp_names[i][0] &&
                 bmp_load_named(icons_dir, bmp_names[i], &pixels, &w, &h);
    if (!found) {
      if (missing)
        fprintf(missing, "MISSING BMP icon[%d] \"%s\"\n", i,
                bmp_names[i] ? bmp_names[i] : "");
      continue;
    }
    if (w != icon_size || h != icon_size) {
      if (missing)
        fprintf(missing, "INVALID BMP icon[%d] \"%s\": expected %dx%d, got %dx%d\n",
                i, bmp_names[i], icon_size, icon_size, w, h);
      image_free(pixels);
      continue;
    }
    int x = (i % cols) * icon_size;
    int y = (i / cols) * icon_size;
    for (int row = 0; row < icon_size; row++)
      memcpy(sheet + ((size_t)(y + row) * sheet_w + x) * 4,
             pixels + (size_t)row * icon_size * 4, (size_t)icon_size * 4);
    image_free(pixels);
    loaded++;
  }
  if (!loaded) {
    free(sheet);
    return false;
  }

  uint32_t tex = R_CreateTextureSRGBA8(sheet_w, sheet_h, sheet,
                                       R_FILTER_NEAREST, R_WRAP_CLAMP);
  free(sheet);
  if (!tex)
    return false;
  *out = (bitmap_strip_t){
    .tex = tex, .icon_w = icon_size, .icon_h = icon_size, .cols = cols,
    .sheet_w = sheet_w, .sheet_h = sheet_h,
  };
  return true;
}

bool bmp_icon_resolve(const char *name, sysicon_resolved_t *out) {
  if (!name || !name[0] || !out)
    return false;
  for (int i = 0; i < g_bmp_icon_cache_count; i++) {
    bmp_icon_cache_t *entry = &g_bmp_icon_cache[i];
    if (strcmp(entry->name, name) != 0)
      continue;
    *out = (sysicon_resolved_t){.tex = entry->tex, .u1 = 1.0f, .v1 = 1.0f,
                                .w = entry->w, .h = entry->h};
    return true;
  }
  if (!g_bmp_icon_dir_count || g_bmp_icon_cache_count >= BMP_ICON_CACHE_MAX)
    return false;

  uint8_t *pixels = NULL;
  int w = 0, h = 0;
  for (int i = 0; i < g_bmp_icon_dir_count && !pixels; i++)
    bmp_load_named(g_bmp_icon_dirs[i], name, &pixels, &w, &h);
  if (!pixels)
    return false;

  uint32_t tex = R_CreateTextureSRGBA8(w, h, pixels, R_FILTER_NEAREST, R_WRAP_CLAMP);
  image_free(pixels);
  if (!tex)
    return false;

  bmp_icon_cache_t *entry = &g_bmp_icon_cache[g_bmp_icon_cache_count++];
  snprintf(entry->name, sizeof(entry->name), "%s", name);
  entry->tex = tex;
  entry->w = w;
  entry->h = h;
  *out = (sysicon_resolved_t){.tex = tex, .u0 = 0.0f, .v0 = 0.0f,
                              .u1 = 1.0f, .v1 = 1.0f, .w = w, .h = h};
  return true;
}

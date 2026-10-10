#ifndef __UI_GDI_H__
#define __UI_GDI_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <orion/user/user.h>
#include <orion/user/rect.h>

// Device-independent bitmaps and block transfers (≈ HBITMAP / BitBlt / StretchBlt).
// A bitmap owns straight-alpha sRGB RGBA8 pixels; its GPU texture is created on the first
// blit and shared by every window that draws it. Blits are only valid inside evPaint and
// use window-local logical coordinates. Pixels are never filtered: scaling is nearest.
typedef struct bitmap_s bitmap_t;

bitmap_t *bitmap_create(int w, int h, const uint8_t *rgba);          // copies; NULL rgba = transparent black
bitmap_t *bitmap_load_memory(const void *data, size_t size);          // PNG, JPEG or BMP by magic bytes
bitmap_t *bitmap_load(const char *path);
void      bitmap_free(bitmap_t *bm);
isize16_t bitmap_size(const bitmap_t *bm);
// Straight RGBA at (x, y) packed 0xAABBGGRR; 0 when outside the bitmap (≈ GetPixel returning CLR_INVALID).
uint32_t  bitmap_pixel(const bitmap_t *bm, int x, int y);
// Makes pixels equal to `rgb` (0xBBGGRR, alpha ignored) transparent (≈ TransparentBlt colour key).
void      bitmap_set_color_key(bitmap_t *bm, uint32_t rgb);

// Copies `src` (x, y of the bitmap) to `dst` at 1:1 (≈ BitBlt). Source areas outside the bitmap are skipped.
void bit_blt(irect16_t dst, const bitmap_t *bm, ipoint16_t src);
// Copies the bitmap rect `src` scaled to `dst` (≈ StretchBlt).
void stretch_blt(irect16_t dst, const bitmap_t *bm, irect16_t src);
// Repeats the bitmap rect `src` across `dst`, clipping the last row and column.
void tile_blt(irect16_t dst, const bitmap_t *bm, irect16_t src);

#endif

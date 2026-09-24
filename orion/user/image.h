#ifndef __UI_IMAGE_H__
#define __UI_IMAGE_H__

#include <stdint.h>
#include <stdbool.h>

// Load an image file into a heap-allocated straight-alpha RGBA pixel buffer.
// RGB bytes are sRGB encoded; untagged PNGs use the sRGB fallback. PNGs with
// unsupported profiles/transfer metadata and PNGs above 8 bits are rejected
// with an unconditional diagnostic rather than silently reinterpreted.
// Supports PNG, JPEG (.jpg and .jpeg), and BMP via stb_image.
// The image type is determined by the file's magic bytes (first few bytes),
// not by the filename extension, so renaming a file does not affect loading.
// Returns NULL on failure; on failure *out_w and *out_h are set to 0.
// out_w and out_h must not be NULL.
// Release the returned buffer with image_free().
uint8_t *load_image(const char *path, int *out_w, int *out_h);

// Release a pixel buffer returned by load_image().
void image_free(uint8_t *pixels);

// Center-crop to a square and area-resample straight sRGB RGBA to target_size
// squared. Filtering is alpha-weighted in linear light.
// Returns a new buffer (also for small sources); release with image_free().
uint8_t *downscale_image(const uint8_t *pixels, int w, int h, int target_size);

enum {
  IMAGE_DOWNSCALE_STROKES = 1 << 0, // Maintain visibility of thin transparent artwork.
  IMAGE_DOWNSCALE_FLIP_Y = 1 << 1,  // Bottom-up rows for framebuffer-style drawing.
};
uint8_t *downscale_image_ex(const uint8_t *pixels, int w, int h, int target_size,
                            unsigned flags);

// Save straight-alpha sRGB RGBA8 pixels to PNG and declare the sRGB color space.
// Returns true on success.
bool save_image_png(const char *path, const uint8_t *pixels, int w, int h);

// Save RGBA pixel data to a JPEG file with the specified quality (1-100).
// Returns true on success.
bool save_image_jpg(const char *path, const uint8_t *pixels, int w, int h,
                    int quality);

#endif

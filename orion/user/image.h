#ifndef __UI_IMAGE_H__
#define __UI_IMAGE_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

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

// Same as load_image() for an encoded image already in memory. Returns NULL on failure.
uint8_t *load_image_memory(const void *data, size_t size, int *out_w, int *out_h);

// Inflates a raw deflate stream (no zlib header). Release with free(); NULL on failure.
uint8_t *inflate_raw(const void *data, size_t size, size_t *out_size);

// Release a pixel buffer returned by load_image().
void image_free(uint8_t *pixels);

// Save straight-alpha sRGB RGBA8 pixels to PNG and declare the sRGB color space.
// Returns true on success.
bool save_image_png(const char *path, const uint8_t *pixels, int w, int h);

// Save RGBA pixel data to a JPEG file with the specified quality (1-100).
// Returns true on success.
bool save_image_jpg(const char *path, const uint8_t *pixels, int w, int h,
                    int quality);

// Encode straight-alpha sRGB RGBA8 pixels as an in-memory PNG.
// Release with free(); NULL on failure.
uint8_t *encode_image_png(const uint8_t *pixels, int w, int h, size_t *out_size);

// Largest centered x,y,w,h area of a src_w x src_h image with the aspect ratio
// of dst_w x dst_h: the part a "cover" fit keeps.
void image_cover_rect(int src_w, int src_h, int dst_w, int dst_h, int rect[4]);

// Resample the rect (x, y, w, h) of straight-alpha sRGB RGBA8 pixels to
// dst_w x dst_h. Filtering is done in linear light on premultiplied alpha:
// downscaling averages every covered source pixel, upscaling interpolates
// linearly. Release with free(); NULL on invalid arguments or allocation failure.
uint8_t *image_resample_srgba8(const uint8_t *src, int src_w, int src_h,
                               const int rect[4], int dst_w, int dst_h);

#endif

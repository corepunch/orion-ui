#ifndef __UI_IMAGE_H__
#define __UI_IMAGE_H__

#include <stdint.h>
#include <stdbool.h>

// Load an image file into a heap-allocated RGBA pixel buffer.
// Supports PNG, JPEG, and BMP via stb_image.
// Returns NULL on failure; on failure *out_w and *out_h are set to 0.
// out_w and out_h must not be NULL.
// Release the returned buffer with image_free().
uint8_t *load_image(const char *path, int *out_w, int *out_h);

// Release a pixel buffer returned by load_image().
void image_free(uint8_t *pixels);

// Save RGBA pixel data to a PNG file.
// Returns true on success.
bool save_image_png(const char *path, const uint8_t *pixels, int w, int h);

#endif

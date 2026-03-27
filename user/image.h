#ifndef __UI_IMAGE_H__
#define __UI_IMAGE_H__

#include <stdint.h>
#include <stdbool.h>

// Load an image file into a heap-allocated RGBA pixel buffer.
// Supports PNG, JPEG, BMP, and other formats via stb_image.
// Caller must free() the returned pointer.
// Returns NULL on failure.
uint8_t *load_image(const char *path, int *out_w, int *out_h);

// Save RGBA pixel data to a PNG file.
// Returns true on success.
bool save_image_png(const char *path, const uint8_t *pixels, int w, int h);

#endif

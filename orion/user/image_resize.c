#include "image.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

uint8_t *downscale_image(const uint8_t *pixels, int w, int h, int target_size) {
  return downscale_image_ex(pixels, w, h, target_size, 0);
}

uint8_t *downscale_image_ex(const uint8_t *pixels, int w, int h, int target_size,
                            unsigned flags) {
  if (!pixels || w <= 0 || h <= 0 || target_size <= 0 ||
      (size_t)w > SIZE_MAX / 4 / (size_t)h ||
      (size_t)target_size > SIZE_MAX / 4 / (size_t)target_size ||
      (flags & ~(IMAGE_DOWNSCALE_STROKES | IMAGE_DOWNSCALE_FLIP_Y))) {
    fprintf(stderr, "[image] invalid downscale pixels=%p size=%dx%d target=%d flags=%u\n",
            (const void *)pixels, w, h, target_size, flags);
    fflush(stderr);
    return NULL;
  }
  uint8_t *out = malloc((size_t)target_size * target_size * 4);
  if (!out) {
    fprintf(stderr, "[image] downscale allocation failed target=%d\n", target_size);
    fflush(stderr);
    return NULL;
  }
  int side = w < h ? w : h;
  double scale = (double)side / target_size;
  double ox = (w - side) * 0.5, oy = (h - side) * 0.5;
  for (int y = 0; y < target_size; y++) {
    double top = oy + y * scale, bottom = oy + (y + 1) * scale;
    for (int x = 0; x < target_size; x++) {
      double left = ox + x * scale, right = ox + (x + 1) * scale;
      double rgb[3] = {0}, alpha = 0, max_alpha = 0;
      for (int sy = (int)floor(top); sy < (int)ceil(bottom) && sy < h; sy++) {
        double wy = fmin(bottom, sy + 1.0) - fmax(top, sy);
        for (int sx = (int)floor(left); sx < (int)ceil(right) && sx < w; sx++) {
          double weight = wy * (fmin(right, sx + 1.0) - fmax(left, sx));
          const uint8_t *p = pixels + ((size_t)sy * w + sx) * 4;
          double a = weight * p[3];
          if (weight > 0) max_alpha = fmax(max_alpha, p[3]);
          alpha += a;
          for (int c = 0; c < 3; c++) rgb[c] += a * p[c];
        }
      }
      int dst_y = flags & IMAGE_DOWNSCALE_FLIP_Y ? target_size - 1 - y : y;
      uint8_t *p = out + ((size_t)dst_y * target_size + x) * 4;
      // A one-source-pixel stroke loses coverage in proportion to the scale.
      // Compensate before quantization, without exceeding its original opacity.
      double gain = flags & IMAGE_DOWNSCALE_STROKES ? fmax(1.0, scale) : 1.0;
      p[3] = (uint8_t)(fmin(max_alpha, alpha * gain / (scale * scale)) + 0.5);
      for (int c = 0; c < 3; c++)
        p[c] = p[3] ? (uint8_t)fmin(255.0, rgb[c] / alpha + 0.5) : 0;
    }
  }
  return out;
}

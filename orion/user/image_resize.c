#include "image.h"
#include "color.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *downscale_image(const uint8_t *pixels, int w, int h, int target_size) {
  return downscale_image_ex(pixels, w, h, target_size, 0);
}

static uint8_t *box2x(const uint8_t *src, int w, int h) {
  int nw = w / 2, nh = h / 2;
  uint8_t *dst = malloc((size_t)nw * nh * 4);
  if (!dst) return NULL;
  for (int y = 0; y < nh; y++) {
    for (int x = 0; x < nw; x++) {
      const uint8_t *p00 = src + ((size_t)(y * 2) * w + x * 2) * 4;
      const uint8_t *p01 = p00 + 4;
      const uint8_t *p10 = p00 + (size_t)w * 4;
      const uint8_t *p11 = p10 + 4;
      int a00 = p00[3], a01 = p01[3], a10 = p10[3], a11 = p11[3];
      int as = a00 + a01 + a10 + a11;
      uint8_t *d = dst + ((size_t)y * nw + x) * 4;
      d[3] = (uint8_t)((as + 2) / 4);
      if (as) {
        for (int c = 0; c < 3; c++) {
          float sum = ui_srgb8_to_linear(p00[c]) * a00 +
                      ui_srgb8_to_linear(p01[c]) * a01 +
                      ui_srgb8_to_linear(p10[c]) * a10 +
                      ui_srgb8_to_linear(p11[c]) * a11;
          d[c] = ui_linear_to_srgb8(sum / as);
        }
      } else {
        d[0] = d[1] = d[2] = 0;
      }
    }
  }
  return dst;
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
  float linear_rgb[256];
  for (int i = 0; i < 256; i++) linear_rgb[i] = ui_srgb8_to_linear((uint8_t)i);

  int side = w < h ? w : h;
  double ox = (w - side) * 0.5, oy = (h - side) * 0.5;
  const uint8_t *src = pixels;
  int src_w = w, src_h = h;
  uint8_t *owned = NULL;
  double orig_scale = (double)side / target_size;

  if (ox == 0 && oy == 0 && w == h) {
    while (src_w >= 32 && src_w >= target_size * 2 && (src_w % 2) == 0) {
      uint8_t *next = box2x(src, src_w, src_h);
      if (!next) { free(out); free(owned); return NULL; }
      if (owned) free(owned);
      owned = next;
      src = owned;
      src_w /= 2;
      src_h /= 2;
    }
    side = src_w;
    ox = oy = 0;
  }

  double scale = (double)side / target_size;
  for (int y = 0; y < target_size; y++) {
    double top = oy + y * scale, bottom = oy + (y + 1) * scale;
    for (int x = 0; x < target_size; x++) {
      double left = ox + x * scale, right = ox + (x + 1) * scale;
      double rgb[3] = {0}, alpha = 0, max_alpha = 0;
      double min_rgb[3] = {255, 255, 255};
      int min_set = 0;
      for (int sy = (int)floor(top); sy < (int)ceil(bottom) && sy < src_h; sy++) {
        if (sy < 0) continue;
        double wy = fmin(bottom, sy + 1.0) - fmax(top, sy);
        for (int sx = (int)floor(left); sx < (int)ceil(right) && sx < src_w; sx++) {
          if (sx < 0) continue;
          double weight = wy * (fmin(right, sx + 1.0) - fmax(left, sx));
          if (weight <= 0) continue;
          const uint8_t *sp = src + ((size_t)sy * src_w + sx) * 4;
          double a = weight * sp[3];
          max_alpha = fmax(max_alpha, sp[3]);
          alpha += a;
          for (int c = 0; c < 3; c++)
            rgb[c] += a * linear_rgb[sp[c]];
          if (sp[3] && (!min_set || sp[0] + sp[1] + sp[2] < min_rgb[0] + min_rgb[1] + min_rgb[2])) {
            min_rgb[0] = sp[0]; min_rgb[1] = sp[1]; min_rgb[2] = sp[2];
            min_set = 1;
          }
        }
      }
      int dst_y = flags & IMAGE_DOWNSCALE_FLIP_Y ? target_size - 1 - y : y;
      uint8_t *p = out + ((size_t)dst_y * target_size + x) * 4;
      double gain = flags & IMAGE_DOWNSCALE_STROKES ? fmax(1.0, orig_scale) : 1.0;
      p[3] = (uint8_t)(fmin(max_alpha, alpha * gain / (scale * scale)) + 0.5);
      if (!p[3] || alpha <= 0) {
        p[0] = p[1] = p[2] = 0;
        continue;
      }
      for (int c = 0; c < 3; c++)
        p[c] = ui_linear_to_srgb8((float)(rgb[c] / alpha));
      if ((flags & IMAGE_DOWNSCALE_STROKES) && min_set)
        for (int c = 0; c < 3; c++)
          p[c] = (uint8_t)(p[c] * 0.85 + min_rgb[c] * 0.15 + 0.5);
    }
  }
  free(owned);
  return out;
}

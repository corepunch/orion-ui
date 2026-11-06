#include "image.h"
#include "color.h"
#include "image_codec.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

static uint16_t image_read16_be(const uint8_t *p) {
  return (uint16_t)((p[0] << 8) | p[1]);
}

static uint16_t image_read16_tiff(const uint8_t *p, bool le) {
  return le ? (uint16_t)(p[0] | (p[1] << 8)) : image_read16_be(p);
}

static uint32_t image_read32_tiff(const uint8_t *p, bool le) {
  return le ? ((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
               ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24))
            : (((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] << 8) | (uint32_t)p[3]);
}

static bool image_parse_exif_orientation(const uint8_t *data, size_t len,
                                         int *out_orientation) {
  if (!data || len < 14 || memcmp(data, "Exif\0\0", 6) != 0)
    return false;

  const uint8_t *tiff = data + 6;
  size_t tiff_len = len - 6;
  bool le;
  if (tiff_len < 8 || (tiff[0] != 'I' && tiff[0] != 'M') ||
      tiff[0] != tiff[1])
    return false;
  le = (tiff[0] == 'I');
  if (image_read16_tiff(tiff + 2, le) != 42)
    return false;

  uint32_t ifd0_off = image_read32_tiff(tiff + 4, le);
  if (ifd0_off > tiff_len || tiff_len - ifd0_off < 2)
    return false;

  const uint8_t *ifd = tiff + ifd0_off;
  uint16_t count = image_read16_tiff(ifd, le);
  if (count > (tiff_len - ifd0_off - 2) / 12)
    count = (uint16_t)((tiff_len - ifd0_off - 2) / 12);

  for (uint16_t i = 0; i < count; i++) {
    const uint8_t *entry = ifd + 2 + (size_t)i * 12;
    uint16_t tag = image_read16_tiff(entry, le);
    uint16_t type = image_read16_tiff(entry + 2, le);
    uint32_t n = image_read32_tiff(entry + 4, le);
    if (tag == 0x0112 && type == 3 && n == 1) {
      int orientation = image_read16_tiff(entry + 8, le);
      if (out_orientation)
        *out_orientation = (orientation >= 1 && orientation <= 8) ? orientation : 1;
      return true;
    }
  }

  return false;
}

static bool image_png_srgb_supported(const char *path) {
  static const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  FILE *f = fopen(path, "rb");
  if (!f) return true;
  uint8_t sig[8];
  if (fread(sig, 1, sizeof(sig), f) != sizeof(sig) ||
      memcmp(sig, signature, sizeof(sig)) != 0) {
    fclose(f);
    return true;
  }

  bool has_srgb = false, has_iccp = false, has_gamma = false;
  bool has_chrm = false, has_cicp = false;
  int bit_depth = 8;
  bool valid = true;
  for (;;) {
    uint8_t header[8];
    if (fread(header, 1, sizeof(header), f) != sizeof(header)) break;
    uint32_t n = ((uint32_t)header[0] << 24) | ((uint32_t)header[1] << 16) |
                 ((uint32_t)header[2] << 8) | (uint32_t)header[3];
    if (n > (1u << 30)) { valid = false; break; }
    const uint8_t *type = header + 4;
    if (memcmp(type, "IHDR", 4) == 0) {
      uint8_t ihdr[13];
      if (n != sizeof(ihdr) || fread(ihdr, 1, sizeof(ihdr), f) != sizeof(ihdr)) {
        valid = false;
        break;
      }
      bit_depth = ihdr[8];
      if (fseek(f, 4, SEEK_CUR) != 0) { valid = false; break; }
      continue;
    }
    if (memcmp(type, "sRGB", 4) == 0) has_srgb = true;
    else if (memcmp(type, "iCCP", 4) == 0) has_iccp = true;
    else if (memcmp(type, "gAMA", 4) == 0) has_gamma = true;
    else if (memcmp(type, "cHRM", 4) == 0) has_chrm = true;
    else if (memcmp(type, "cICP", 4) == 0) has_cicp = true;
    if (fseek(f, (long)n + 4, SEEK_CUR) != 0) { valid = false; break; }
    if (memcmp(type, "IEND", 4) == 0) break;
  }
  fclose(f);
  if (!valid) {
    fprintf(stderr, "[image] malformed PNG metadata path=%s\n", path);
    fflush(stderr);
    return false;
  }
  if (bit_depth > 8) {
    fprintf(stderr, "[image] unsupported PNG bit depth=%d path=%s; Orion documents use 8-bit RGBA\n",
            bit_depth, path);
    fflush(stderr);
    return false;
  }
  if (has_iccp || has_cicp || ((!has_srgb) && (has_gamma || has_chrm))) {
    fprintf(stderr, "[image] unsupported PNG color metadata path=%s sRGB=%d iCCP=%d gAMA=%d cHRM=%d cICP=%d\n",
            path, has_srgb, has_iccp, has_gamma, has_chrm, has_cicp);
    fflush(stderr);
    return false;
  }
  return true;
}

static int image_jpeg_orientation(const char *path, bool *has_icc) {
  FILE *f = fopen(path, "rb");
  if (has_icc) *has_icc = false;
  if (!f) return 1;

  uint8_t hdr[2];
  if (fread(hdr, 1, 2, f) != 2 || hdr[0] != 0xff || hdr[1] != 0xd8) {
    fclose(f);
    return 1;
  }

  int orientation = 1;
  for (;;) {
    int c;
    do {
      c = fgetc(f);
    } while (c == 0xff);
    if (c == EOF) break;

    uint8_t marker = (uint8_t)c;
    if (marker == 0xd9 || marker == 0xda)
      break;
    if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
      continue;

    uint8_t len_bytes[2];
    if (fread(len_bytes, 1, 2, f) != 2) break;
    uint16_t seg_len = image_read16_be(len_bytes);
    if (seg_len < 2) break;
    size_t payload_len = (size_t)seg_len - 2;

    if (marker == 0xe1 && payload_len >= 14) {
      uint8_t *payload = malloc(payload_len);
      if (!payload) break;
      if (fread(payload, 1, payload_len, f) != payload_len) {
        free(payload);
        break;
      }
      (void)image_parse_exif_orientation(payload, payload_len, &orientation);
      free(payload);
      continue;
    }

    if (marker == 0xe2 && payload_len >= 12) {
      uint8_t profile_sig[12];
      if (fread(profile_sig, 1, sizeof(profile_sig), f) != sizeof(profile_sig)) break;
      if (memcmp(profile_sig, "ICC_PROFILE\0", sizeof(profile_sig)) == 0 && has_icc)
        *has_icc = true;
      if (fseek(f, (long)(payload_len - sizeof(profile_sig)), SEEK_CUR) != 0) break;
      continue;
    }

    if (fseek(f, (long)payload_len, SEEK_CUR) != 0)
      break;
  }

  fclose(f);
  return orientation;
}

static uint8_t *image_apply_orientation(uint8_t *src, int *w, int *h,
                                        int orientation) {
  if (!src || !w || !h || *w <= 0 || *h <= 0 ||
      orientation < 2 || orientation > 8)
    return src;

  int sw = *w, sh = *h;
  int dw = (orientation >= 5 && orientation <= 8) ? sh : sw;
  int dh = (orientation >= 5 && orientation <= 8) ? sw : sh;
  uint8_t *dst = malloc((size_t)dw * dh * 4);
  if (!dst) {
    fprintf(stderr, "[image] orientation transform allocation failed size=%dx%d orientation=%d\n",
            dw, dh, orientation);
    fflush(stderr);
    return src;
  }

  for (int y = 0; y < sh; y++) {
    for (int x = 0; x < sw; x++) {
      int dx = x, dy = y;
      switch (orientation) {
        case 2: dx = sw - 1 - x; dy = y; break;
        case 3: dx = sw - 1 - x; dy = sh - 1 - y; break;
        case 4: dx = x; dy = sh - 1 - y; break;
        case 5: dx = y; dy = x; break;
        case 6: dx = sh - 1 - y; dy = x; break;
        case 7: dx = sh - 1 - y; dy = sw - 1 - x; break;
        case 8: dx = y; dy = sw - 1 - x; break;
      }
      memcpy(dst + ((size_t)dy * dw + dx) * 4,
             src + ((size_t)y * sw + x) * 4,
             4);
    }
  }

  free(src);
  *w = dw;
  *h = dh;
  return dst;
}

uint8_t *load_image(const char *path, int *out_w, int *out_h) {
  if (!out_w || !out_h) return NULL;
  *out_w = *out_h = 0;
  if (!path || !image_png_srgb_supported(path)) return NULL;
  bool has_jpeg_icc = false;
  int orientation = image_jpeg_orientation(path, &has_jpeg_icc);
  if (has_jpeg_icc) {
    fprintf(stderr, "[image] unsupported JPEG ICC profile path=%s; import skipped\n", path);
    fflush(stderr); return NULL;
  }
  FILE *file = fopen(path, "rb");
  if (!file) return NULL;
  long len = !fseek(file, 0, SEEK_END) ? ftell(file) : -1;
  uint8_t *data = len > 0 && !fseek(file, 0, SEEK_SET) ? malloc((size_t)len) : NULL;
  uint8_t *pixels = data && fread(data, 1, (size_t)len, file) == (size_t)len
      ? load_image_memory(data, (size_t)len, out_w, out_h) : NULL;
  free(data); fclose(file);
  return image_apply_orientation(pixels, out_w, out_h, orientation);
}

void image_free(uint8_t *pixels) { free(pixels); }

bool save_image_png(const char *path, const uint8_t *pixels, int w, int h) {
  if (!path || !pixels || w < 1 || h < 1 || (size_t)w > SIZE_MAX / 4 / (size_t)h) return false;
  png_image image = {0}; image.version = PNG_IMAGE_VERSION;
  image.width = w; image.height = h; image.format = PNG_FORMAT_RGBA;
  return png_image_write_to_file(&image, path, 0, pixels, 0, NULL) != 0;
}

bool save_image_jpg(const char *path, const uint8_t *pixels, int w, int h, int quality) {
  return image_encode_jpeg(path, pixels, w, h, quality);
}

uint8_t *encode_image_png(const uint8_t *pixels, int w, int h, size_t *out_size) {
  if (out_size) *out_size = 0;
  if (!pixels || w < 1 || h < 1 || !out_size || (size_t)w > SIZE_MAX / 4 / (size_t)h) return NULL;
  png_image image = {0}; image.version = PNG_IMAGE_VERSION;
  image.width = w; image.height = h; image.format = PNG_FORMAT_RGBA;
  png_alloc_size_t size = 0;
  if (!png_image_write_to_memory(&image, NULL, &size, 0, pixels, 0, NULL)) return NULL;
  uint8_t *data = malloc(size);
  if (!data || !png_image_write_to_memory(&image, data, &size, 0, pixels, 0, NULL)) { free(data); return NULL; }
  *out_size = size;
  return data;
}

void image_cover_rect(int src_w, int src_h, int dst_w, int dst_h, int rect[4]) {
  rect[0] = rect[1] = 0; rect[2] = src_w; rect[3] = src_h;
  if (src_w < 1 || src_h < 1 || dst_w < 1 || dst_h < 1) return;
  if ((int64_t)src_w * dst_h > (int64_t)src_h * dst_w)
    rect[2] = (int)MAX(1, ((int64_t)src_h * dst_w + dst_h / 2) / dst_h);
  else
    rect[3] = (int)MAX(1, ((int64_t)src_w * dst_h + dst_w / 2) / dst_w);
  rect[0] = (src_w - rect[2]) / 2;
  rect[1] = (src_h - rect[3]) / 2;
}

// One output sample's source taps: a box of max(1, scale) source pixels
// centered on the sample, clipped to the image and weighted by overlap.
typedef struct { int first, count; float *weights; } image_taps_t;

static image_taps_t *image_build_taps(int src_n, int start, int len, int dst_n, float **storage) {
  float scale = (float)len / (float)dst_n, radius = MAX(scale, 1.0f) * 0.5f;
  int max_taps = (int)ceilf(radius * 2.0f) + 2;
  image_taps_t *taps = malloc((size_t)dst_n * sizeof(*taps));
  *storage = malloc((size_t)dst_n * max_taps * sizeof(float));
  if (!taps || !*storage) { free(taps); free(*storage); *storage = NULL; return NULL; }
  for (int i = 0; i < dst_n; i++) {
    float center = start + (i + 0.5f) * scale;
    float lo = MAX(0.0f, center - radius), hi = MIN((float)src_n, center + radius);
    image_taps_t *t = &taps[i];
    t->first = MIN(src_n - 1, (int)floorf(lo));
    t->weights = *storage + (size_t)i * max_taps;
    t->count = 0;
    float total = 0.0f;
    for (int j = t->first; j < src_n && j < hi && t->count < max_taps; j++) {
      float w = MIN(hi, (float)(j + 1)) - MAX(lo, (float)j);
      t->weights[t->count++] = MAX(0.0f, w);
      total += MAX(0.0f, w);
    }
    if (!t->count) { t->weights[0] = 1.0f; t->count = 1; total = 1.0f; }
    for (int k = 0; k < t->count; k++) t->weights[k] /= total;
  }
  return taps;
}

uint8_t *image_resample_srgba8(const uint8_t *src, int src_w, int src_h,
                               const int rect[4], int dst_w, int dst_h) {
  if (!src || !rect || src_w < 1 || src_h < 1 || dst_w < 1 || dst_h < 1 ||
      dst_w > 16384 || dst_h > 16384 || rect[2] < 1 || rect[3] < 1 ||
      rect[0] < 0 || rect[1] < 0 || rect[0] > src_w - rect[2] || rect[1] > src_h - rect[3]) {
    fprintf(stderr, "[image] resample rejected src=%dx%d rect=%d,%d,%d,%d dst=%dx%d\n", src_w, src_h,
            rect ? rect[0] : 0, rect ? rect[1] : 0, rect ? rect[2] : 0, rect ? rect[3] : 0, dst_w, dst_h);
    fflush(stderr);
    return NULL;
  }
  float lut[256], *xs_storage = NULL, *ys_storage = NULL;
  for (int i = 0; i < 256; i++) lut[i] = ui_srgb8_to_linear((uint8_t)i);
  image_taps_t *xs = image_build_taps(src_w, rect[0], rect[2], dst_w, &xs_storage);
  image_taps_t *ys = image_build_taps(src_h, rect[1], rect[3], dst_h, &ys_storage);
  float *acc = malloc((size_t)dst_w * 4 * sizeof(float));
  uint8_t *dst = malloc((size_t)dst_w * dst_h * 4);
  if (!xs || !ys || !acc || !dst) {
    fprintf(stderr, "[image] resample allocation failed dst=%dx%d\n", dst_w, dst_h);
    fflush(stderr);
    free(dst); dst = NULL;
    goto done;
  }
  for (int oy = 0; oy < dst_h; oy++) {
    memset(acc, 0, (size_t)dst_w * 4 * sizeof(float));
    for (int ty = 0; ty < ys[oy].count; ty++) {
      const uint8_t *row = src + (size_t)(ys[oy].first + ty) * src_w * 4;
      float wy = ys[oy].weights[ty];
      for (int ox = 0; ox < dst_w; ox++) {
        float *a = acc + (size_t)ox * 4;
        for (int tx = 0; tx < xs[ox].count; tx++) {
          const uint8_t *p = row + (size_t)(xs[ox].first + tx) * 4;
          float w = wy * xs[ox].weights[tx] * (p[3] / 255.0f);
          a[0] += lut[p[0]] * w; a[1] += lut[p[1]] * w; a[2] += lut[p[2]] * w; a[3] += w;
        }
      }
    }
    uint8_t *out = dst + (size_t)oy * dst_w * 4;
    for (int ox = 0; ox < dst_w; ox++, out += 4) {
      const float *a = acc + (size_t)ox * 4;
      if (a[3] <= 0.0f) { memset(out, 0, 4); continue; }
      for (int c = 0; c < 3; c++) out[c] = ui_linear_to_srgb8(a[c] / a[3]);
      out[3] = (uint8_t)(MIN(1.0f, a[3]) * 255.0f + 0.5f);
    }
  }
done:
  free(xs); free(ys); free(xs_storage); free(ys_storage); free(acc);
  return dst;
}

uint8_t *load_image_memory(const void *data, size_t size, int *out_w, int *out_h) {
  if (!out_w || !out_h) return NULL;
  *out_w = *out_h = 0;
  if (!data || size < 2) return NULL;
  const uint8_t *bytes = data;
  if (size >= 8 && !png_sig_cmp(bytes, 0, 8)) return image_decode_png(data, size, out_w, out_h);
  if (bytes[0] == 0xff && bytes[1] == 0xd8) return image_decode_jpeg(data, size, out_w, out_h);
  if (bytes[0] == 'B' && bytes[1] == 'M') return image_decode_bmp(data, size, out_w, out_h);
  return NULL;
}

uint8_t *inflate_raw(const void *data, size_t size, size_t *out_size) {
  if (out_size) *out_size = 0;
  if (!data || !out_size || size > UINT_MAX) {
    fprintf(stderr, "[image] inflate rejected data=%p size=%zu out_size=%p\n", data, size, (void *)out_size);
    fflush(stderr); return NULL;
  }
  z_stream stream = {0};
  stream.next_in = (Bytef *)data; stream.avail_in = (uInt)size;
  if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return NULL;
  size_t capacity = 4096, used = 0;
  uint8_t *result = malloc(capacity);
  int status = Z_OK;
  while (result && status == Z_OK) {
    if (used == capacity) {
      if (capacity > SIZE_MAX / 2 || capacity > UINT_MAX / 2) { status = Z_MEM_ERROR; break; }
      capacity *= 2;
      uint8_t *grown = realloc(result, capacity);
      if (!grown) { status = Z_MEM_ERROR; break; }
      result = grown;
    }
    stream.next_out = result + used; stream.avail_out = (uInt)(capacity - used);
    status = inflate(&stream, Z_NO_FLUSH);
    used = capacity - stream.avail_out;
  }
  inflateEnd(&stream);
  if (!result || status != Z_STREAM_END) {
    fprintf(stderr, "[image] inflate failed size=%zu status=%d\n", size, status); fflush(stderr);
    free(result); return NULL;
  }
  *out_size = used; return result;
}

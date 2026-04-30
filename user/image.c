#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int image_jpeg_orientation(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return 1;

  uint8_t hdr[2];
  if (fread(hdr, 1, 2, f) != 2 || hdr[0] != 0xff || hdr[1] != 0xd8) {
    fclose(f);
    return 1;
  }

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
      int orientation = 1;
      bool found = image_parse_exif_orientation(payload, payload_len,
                                                &orientation);
      free(payload);
      if (found) {
        fclose(f);
        return orientation;
      }
      continue;
    }

    if (fseek(f, (long)payload_len, SEEK_CUR) != 0)
      break;
  }

  fclose(f);
  return 1;
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
  if (!dst)
    return src;

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

  stbi_image_free(src);
  *w = dw;
  *h = dh;
  return dst;
}

uint8_t *load_image(const char *path, int *out_w, int *out_h) {
  if (!out_w || !out_h) return NULL;
  *out_w = 0;
  *out_h = 0;
  if (!path) return NULL;
  int channels;
  uint8_t *pixels = stbi_load(path, out_w, out_h, &channels, 4);
  if (!pixels)
    return NULL;
  int orientation = image_jpeg_orientation(path);
  return image_apply_orientation(pixels, out_w, out_h, orientation);
}

void image_free(uint8_t *pixels) {
  stbi_image_free(pixels);
}

bool save_image_png(const char *path, const uint8_t *pixels, int w, int h) {
  return stbi_write_png(path, w, h, 4, pixels, w * 4) != 0;
}

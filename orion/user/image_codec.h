#ifndef __UI_IMAGE_CODEC_H__
#define __UI_IMAGE_CODEC_H__

#include <limits.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <jpeglib.h>
#include <libnsbmp.h>
#define BMPLIB_LIB  // bmplib is compiled into libuser, not imported from a DLL
#include <bmplib.h>
#include <zlib.h>

typedef struct { struct jpeg_error_mgr base; jmp_buf jump; } image_jpeg_error_t;

static void image_jpeg_error_exit(j_common_ptr codec) {
  image_jpeg_error_t *error = (image_jpeg_error_t *)codec->err;
  longjmp(error->jump, 1);
}

static uint8_t *image_decode_png(const void *data, size_t size, int *w, int *h) {
  png_image image = {0};
  image.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_memory(&image, data, size)) return NULL;
  if (!image.width || !image.height || image.width > INT_MAX || image.height > INT_MAX || image.width > SIZE_MAX / 4 / image.height) {
    png_image_free(&image); return NULL;
  }
  image.format = PNG_FORMAT_RGBA;
  uint8_t *pixels = malloc(PNG_IMAGE_SIZE(image));
  if (!pixels || !png_image_finish_read(&image, NULL, pixels, 0, NULL)) {
    free(pixels); png_image_free(&image); return NULL;
  }
  *w = (int)image.width; *h = (int)image.height;
  png_image_free(&image);
  return pixels;
}

static uint8_t *image_decode_jpeg(const void *data, size_t size, int *w, int *h) {
  if (size > ULONG_MAX) return NULL;
  struct jpeg_decompress_struct codec = {0};
  image_jpeg_error_t error;
  uint8_t *volatile pixels = NULL, *volatile row = NULL;
  codec.err = jpeg_std_error(&error.base);
  error.base.error_exit = image_jpeg_error_exit;
  if (setjmp(error.jump)) { free((void *)row); free((void *)pixels); jpeg_destroy_decompress(&codec); return NULL; }
  jpeg_create_decompress(&codec);
  jpeg_mem_src(&codec, (unsigned char *)data, (unsigned long)size);
  jpeg_read_header(&codec, TRUE);
  bool cmyk = codec.jpeg_color_space == JCS_CMYK || codec.jpeg_color_space == JCS_YCCK;
  codec.out_color_space = cmyk ? JCS_CMYK : JCS_RGB;
  jpeg_start_decompress(&codec);
  if (!codec.output_width || !codec.output_height || codec.output_width > INT_MAX || codec.output_height > INT_MAX ||
      codec.output_width > SIZE_MAX / 4 / codec.output_height) { jpeg_destroy_decompress(&codec); return NULL; }
  size_t width = codec.output_width, height = codec.output_height;
  pixels = malloc(width * height * 4); row = malloc(width * (cmyk ? 4 : 3));
  if (!pixels || !row) { free((void *)pixels); free((void *)row); jpeg_destroy_decompress(&codec); return NULL; }
  while (codec.output_scanline < codec.output_height) {
    JSAMPROW scan = (uint8_t *)row;
    size_t y = codec.output_scanline;
    jpeg_read_scanlines(&codec, &scan, 1);
    for (size_t x = 0; x < width; x++) {
      for (int c = 0; c < 3; c++) {
        unsigned value = scan[x * (cmyk ? 4 : 3) + c];
        if (cmyk) {
          unsigned black = scan[x * 4 + 3];
          value = codec.saw_Adobe_marker ? value * black / 255 : (255 - value) * (255 - black) / 255;
        }
        ((uint8_t *)pixels)[(y * width + x) * 4 + c] = (uint8_t)value;
      }
      ((uint8_t *)pixels)[(y * width + x) * 4 + 3] = 255;
    }
  }
  jpeg_finish_decompress(&codec); jpeg_destroy_decompress(&codec); free((void *)row);
  *w = (int)width; *h = (int)height;
  return (uint8_t *)pixels;
}

static void *image_bmp_create(int w, int h, unsigned flags) {
  (void)flags;
  if (w <= 0 || h <= 0 || (size_t)w > SIZE_MAX / 4 / (size_t)h) return NULL;
  return calloc((size_t)w * (size_t)h, 4);
}
static void image_bmp_destroy(void *pixels) { free(pixels); }
static unsigned char *image_bmp_buffer(void *pixels) { return pixels; }

static uint8_t *image_decode_bmp_fallback(const void *data, size_t size, int *w, int *h) {
#if defined(_WIN32)
  FILE *file = tmpfile();
  if (file && (fwrite(data, 1, size, file) != size || fseek(file, 0, SEEK_SET))) {
    fclose(file); file = NULL;
  }
#else
  FILE *file = fmemopen((void *)data, size, "rb");
#endif
  if (!file) {
    fprintf(stderr, "[image] BMP fallback stream failed size=%zu\n", size); fflush(stderr);
    return NULL;
  }
  BMPHANDLE bmp = bmpread_new(file);
  uint8_t *source = NULL, *pixels = NULL;
  uint8_t palette[256 * 4] = {0}, *palette_data = palette;
  int width = 0, height = 0, channels = 0, bits = 0;
  if (bmp) {
    bmpread_set_undefined(bmp, BMP_UNDEFINED_LEAVE);
    bool ok = bmpread_load_info(bmp) == BMP_RESULT_OK;
    int colors = ok ? bmpread_num_palette_colors(bmp) : 0;
    if (colors > 0) ok = colors <= 256 && bmpread_load_palette(bmp, &palette_data) == BMP_RESULT_OK;
    if (ok && bmpread_dimensions(bmp, &width, &height, &channels, &bits, NULL) == BMP_RESULT_OK &&
        width > 0 && height > 0 && (channels == 1 || channels == 3 || channels == 4) && bits == 8 &&
        (size_t)width <= SIZE_MAX / 4 / (size_t)height &&
        bmpread_load_image(bmp, &source) == BMP_RESULT_OK) {
      size_t count = (size_t)width * height;
      pixels = malloc(count * 4);
      if (pixels) {
        for (size_t i = 0; i < count; i++) {
          const uint8_t *color = channels == 1 ? palette + (size_t)source[i] * 4 : source + i * channels;
          memcpy(pixels + i * 4, color, 3);
          pixels[i * 4 + 3] = channels == 4 ? source[i * channels + 3] : 255;
        }
        *w = width; *h = height;
      } else { fprintf(stderr, "[image] BMP fallback allocation failed size=%dx%d\n", width, height); fflush(stderr); }
    }
    bmp_free(bmp);
  }
  free(source); fclose(file);
  return pixels;
}

static uint8_t *image_decode_bmp(const void *data, size_t size, int *w, int *h) {
  if (size > UINT32_MAX) return NULL;
  bmp_bitmap_callback_vt callbacks = {image_bmp_create, image_bmp_destroy, image_bmp_buffer};
  bmp_image image;
  bmp_create(&image, &callbacks);
  if (bmp_analyse(&image, size, (uint8_t *)data) != BMP_OK || image.width > INT_MAX || image.height > INT_MAX || bmp_decode(&image) != BMP_OK) {
    bmp_finalise(&image);
    return image_decode_bmp_fallback(data, size, w, h);
  }
  *w = (int)image.width; *h = (int)image.height;
  uint8_t *pixels = image.bitmap; image.bitmap = NULL;
  bmp_finalise(&image);
  return pixels;
}

static bool image_encode_jpeg(const char *path, const uint8_t *pixels, int w, int h, int quality) {
  if (!path || !pixels || w < 1 || h < 1 || (size_t)w > SIZE_MAX / 4 / (size_t)h) return false;
  FILE *file = fopen(path, "wb");
  if (!file) return false;
  struct jpeg_compress_struct codec = {0};
  image_jpeg_error_t error;
  uint8_t *volatile row = NULL;
  codec.err = jpeg_std_error(&error.base); error.base.error_exit = image_jpeg_error_exit;
  if (setjmp(error.jump)) { free((void *)row); jpeg_destroy_compress(&codec); fclose(file); return false; }
  jpeg_create_compress(&codec); jpeg_stdio_dest(&codec, file);
  codec.image_width = w; codec.image_height = h; codec.input_components = 3; codec.in_color_space = JCS_RGB;
  jpeg_set_defaults(&codec); jpeg_set_quality(&codec, quality, TRUE); jpeg_start_compress(&codec, TRUE);
  row = malloc((size_t)w * 3);
  if (!row) { jpeg_destroy_compress(&codec); fclose(file); return false; }
  while (codec.next_scanline < codec.image_height) {
    const uint8_t *src = pixels + (size_t)codec.next_scanline * w * 4;
    for (int x = 0; x < w; x++) memcpy((uint8_t *)row + (size_t)x * 3, src + (size_t)x * 4, 3);
    JSAMPROW scan = (uint8_t *)row;
    jpeg_write_scanlines(&codec, &scan, 1);
  }
  jpeg_finish_compress(&codec); jpeg_destroy_compress(&codec); free((void *)row);
  return fclose(file) == 0;
}
#endif

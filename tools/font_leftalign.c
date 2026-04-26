#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "tiny_png.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../user/stb_image.h"

#define GRID_COLS 16
#define GRID_ROWS 16

static unsigned char *read_file_bytes(const char *path, size_t *out_size) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  long sz = ftell(f);
  if (sz <= 0) {
    fclose(f);
    return NULL;
  }
  rewind(f);

  unsigned char *buf = (unsigned char *)malloc((size_t)sz);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    free(buf);
    fclose(f);
    return NULL;
  }
  fclose(f);
  *out_size = (size_t)sz;
  return buf;
}

static uint32_t rd_u32be(const unsigned char *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int extract_raw_font_chunk(const unsigned char *png, size_t png_size,
                                  unsigned char **font_chunk,
                                  uint32_t *font_chunk_len) {
  if (!png || png_size < 8) return 0;

  size_t pos = 8;
  while (pos + 12 <= png_size) {
    uint32_t clen = rd_u32be(png + pos);
    const unsigned char *type = png + pos + 4;
    const unsigned char *data = png + pos + 8;

    if (pos + 12 + clen > png_size) return 0;

    if (memcmp(type, "foNT", 4) == 0) {
      unsigned char *copy = (unsigned char *)malloc(clen);
      if (!copy) return 0;
      memcpy(copy, data, clen);
      *font_chunk = copy;
      *font_chunk_len = clen;
      return 1;
    }

    if (memcmp(type, "IEND", 4) == 0) break;
    pos += 12 + clen;
  }
  return 0;
}

static int write_png_keep_font_chunk(const char *path,
                                     const unsigned char *gray,
                                     int w, int h,
                                     const unsigned char *font_chunk,
                                     uint32_t font_chunk_len) {
  if (!path || !gray || w <= 0 || h <= 0 || !font_chunk) return 0;

  size_t raw_size = (size_t)h * (size_t)(1 + w);
  unsigned char *raw = (unsigned char *)malloc(raw_size);
  if (!raw) return 0;

  for (int y = 0; y < h; y++) {
    raw[y * (size_t)(1 + w)] = 0;
    memcpy(raw + y * (size_t)(1 + w) + 1, gray + y * (size_t)w, (size_t)w);
  }

  FILE *f = fopen(path, "wb");
  if (!f) {
    free(raw);
    return 0;
  }

  static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  fwrite(sig, 1, 8, f);

  {
    unsigned char ihdr[13];
    unsigned char *p = ihdr;
    tpng__u32be(&p, (uint32_t)w);
    tpng__u32be(&p, (uint32_t)h);
    tpng__u8(&p, 8);
    tpng__u8(&p, 0);
    tpng__u8(&p, 0);
    tpng__u8(&p, 0);
    tpng__u8(&p, 0);
    tpng__chunk(f, "IHDR", ihdr, 13);
  }

  tpng__chunk(f, "foNT", font_chunk, font_chunk_len);
  tpng__idat(f, raw, raw_size);
  tpng__chunk(f, "IEND", NULL, 0);

  fclose(f);
  free(raw);
  return 1;
}

static void left_align_cell(unsigned char *gray, int img_w,
                            int cell_x, int cell_y,
                            int cell_w, int cell_h) {
  int lo = cell_w;
  int hi = -1;

  for (int y = 0; y < cell_h; y++) {
    for (int x = 0; x < cell_w; x++) {
      if (gray[(cell_y + y) * img_w + (cell_x + x)] > 0) {
        if (x < lo) lo = x;
        if (x > hi) hi = x;
      }
    }
  }

  if (hi < 0 || lo <= 0) return;

  size_t cell_size = (size_t)cell_w * (size_t)cell_h;
  unsigned char *tmp = (unsigned char *)malloc(cell_size);
  if (!tmp) return;

  for (int y = 0; y < cell_h; y++) {
    memcpy(tmp + y * cell_w, gray + (cell_y + y) * img_w + cell_x, (size_t)cell_w);
    memset(gray + (cell_y + y) * img_w + cell_x, 0, (size_t)cell_w);
  }

  for (int y = 0; y < cell_h; y++) {
    for (int x = 0; x < cell_w; x++) {
      unsigned char v = tmp[y * cell_w + x];
      if (!v) continue;
      int nx = x - lo;
      if (nx >= 0 && nx < cell_w)
        gray[(cell_y + y) * img_w + (cell_x + nx)] = v;
    }
  }

  free(tmp);
}

static void scan_cell_bounds(const unsigned char *gray, int img_w,
                             int cell_x, int cell_y,
                             int cell_w, int cell_h,
                             int *out_lo, int *out_hi,
                             int *out_top, int *out_bottom) {
  int lo = cell_w, hi = -1;
  int top = cell_h, bottom = -1;

  for (int y = 0; y < cell_h; y++) {
    for (int x = 0; x < cell_w; x++) {
      if (gray[(cell_y + y) * img_w + (cell_x + x)] > 0) {
        if (x < lo) lo = x;
        if (x > hi) hi = x;
        if (y < top) top = y;
        if (y > bottom) bottom = y;
      }
    }
  }

  *out_lo = lo;
  *out_hi = hi;
  *out_top = top;
  *out_bottom = bottom;
}

static TinyPngGlyph *build_manual_glyphs_from_bitmap(const unsigned char *gray,
                                                      int w, int h,
                                                      int cell_w, int cell_h,
                                                      int *out_num_chars) {
  int cols = w / cell_w;
  int rows = h / cell_h;
  int num_chars = cols * rows;
  TinyPngGlyph *glyphs = (TinyPngGlyph *)calloc((size_t)num_chars, sizeof(TinyPngGlyph));
  if (!glyphs) return NULL;

  for (int row = 0; row < rows; row++) {
    for (int col = 0; col < cols; col++) {
      int idx = row * cols + col;
      int cell_x = col * cell_w;
      int cell_y = row * cell_h;

      int lo, hi, top, bottom;
      scan_cell_bounds(gray, w, cell_x, cell_y, cell_w, cell_h, &lo, &hi, &top, &bottom);

      glyphs[idx].cell_col = (uint8_t)col;
      glyphs[idx].cell_row = (uint8_t)row;

      if (hi < 0) {
        glyphs[idx].x0 = 0;
        glyphs[idx].y0 = 0;
        glyphs[idx].w = 0;
        glyphs[idx].h = 0;
        glyphs[idx].advance = 0;
      } else {
        int draw_w = hi - lo + 2; // 1px glyph width + 1px inter-char gap
        int draw_h = bottom - top + 1;
        if (draw_w < 1) draw_w = 1;
        if (draw_h < 1) draw_h = 1;
        if (draw_w > 255) draw_w = 255;
        if (draw_h > 255) draw_h = 255;

        // After left-align, bitmap starts at x=0 in each cell.
        glyphs[idx].x0 = 0;
        glyphs[idx].y0 = (int8_t)top;
        glyphs[idx].w = (uint8_t)draw_w;
        glyphs[idx].h = (uint8_t)draw_h;
        glyphs[idx].advance = (uint8_t)draw_w;
      }
    }
  }

  *out_num_chars = num_chars;
  return glyphs;
}

int main(int argc, char **argv) {
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "Usage: %s input.png [output.png]\n", argv[0]);
    fprintf(stderr, "If output is omitted, input is overwritten.\n");
    return 1;
  }

  const char *input = argv[1];
  const char *output = (argc >= 3) ? argv[2] : argv[1];

  size_t png_size = 0;
  unsigned char *png = read_file_bytes(input, &png_size);
  if (!png) {
    fprintf(stderr, "Failed to read PNG: %s\n", input);
    return 1;
  }

  TinyPngFontInfo fi = {0};
  TinyPngGlyph *glyphs = NULL;
  int has_font_chunk = tiny_png_read_font_chunk(png, png_size, &fi, &glyphs);

  unsigned char *font_chunk = NULL;
  uint32_t font_chunk_len = 0;
  if (has_font_chunk) {
    if (!extract_raw_font_chunk(png, png_size, &font_chunk, &font_chunk_len)) {
      fprintf(stderr, "Failed to extract raw foNT chunk: %s\n", input);
      free(glyphs);
      free(png);
      return 1;
    }
  }

  int w = 0, h = 0;
  uint8_t *rgba = stbi_load(input, &w, &h, NULL, 4);
  if (!rgba) {
    fprintf(stderr, "Failed to decode image pixels: %s\n", input);
    free(font_chunk);
    free(glyphs);
    free(png);
    return 1;
  }

  if (w <= 0 || h <= 0 || (w % GRID_COLS) != 0 || (h % GRID_ROWS) != 0) {
    fprintf(stderr, "Expected a %dx%d grid image, got %dx%d\n", GRID_COLS, GRID_ROWS, w, h);
    stbi_image_free(rgba);
    free(font_chunk);
    free(glyphs);
    free(png);
    return 1;
  }

  unsigned char *gray = (unsigned char *)malloc((size_t)w * (size_t)h);
  if (!gray) {
    stbi_image_free(rgba);
    free(font_chunk);
    free(glyphs);
    free(png);
    return 1;
  }

  for (size_t i = 0; i < (size_t)w * (size_t)h; i++)
    gray[i] = rgba[i * 4];
  stbi_image_free(rgba);

  int cell_w = w / GRID_COLS;
  int cell_h = h / GRID_ROWS;

  for (int row = 0; row < GRID_ROWS; row++) {
    for (int col = 0; col < GRID_COLS; col++) {
      left_align_cell(gray, w, col * cell_w, row * cell_h, cell_w, cell_h);
    }
  }

  if (has_font_chunk) {
    if (!write_png_keep_font_chunk(output, gray, w, h, font_chunk, font_chunk_len)) {
      fprintf(stderr, "Failed to write PNG: %s\n", output);
      free(gray);
      free(font_chunk);
      free(glyphs);
      free(png);
      return 1;
    }
    printf("Left-aligned glyph bitmaps in %s and preserved foNT chunk.\n", output);
  } else {
    int num_chars = 0;
    TinyPngGlyph *manual = build_manual_glyphs_from_bitmap(gray, w, h, cell_w, cell_h, &num_chars);
    if (!manual) {
      fprintf(stderr, "Failed to build manual glyph metadata.\n");
      free(gray);
      free(png);
      return 1;
    }

    TinyPngFontInfo out_fi = {0};
    out_fi.name_full = "BitmapFont";
    out_fi.name_family = "BitmapFont";
    out_fi.name_style = "Regular";
    out_fi.name_version = "generated";
    out_fi.pixel_height = (float)cell_h;
    out_fi.scale = 1.0f;
    out_fi.first_char = 0;
    out_fi.num_chars = num_chars;
    out_fi.cell_w = cell_w;
    out_fi.cell_h = cell_h;
    out_fi.atlas_w = w;
    out_fi.atlas_h = h;
    out_fi.baseline = cell_h;
    out_fi.flags = 1; // sharp
    out_fi.glyphs = manual;

    if (!tiny_png_save_font(output, gray, w, h, 0, &out_fi)) {
      fprintf(stderr, "Failed to write PNG with generated foNT: %s\n", output);
      free(manual);
      free(gray);
      free(png);
      return 1;
    }
    printf("Left-aligned glyph bitmaps in %s and generated foNT chunk from bitmap metrics.\n", output);
    free(manual);
  }

  free(gray);
  if (font_chunk) free(font_chunk);
  if (glyphs) free(glyphs);
  free(png);
  return 0;
}

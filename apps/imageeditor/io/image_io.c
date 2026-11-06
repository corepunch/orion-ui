// Unified image file I/O for the image editor.
//
// IMAGEEDITOR_INDEXED=1: PCX (ZSoft v5, 8-bit) and BMP (8-bit indexed)
//   load and save.  These are the formats used by the 256-colour era tools
//   (Deluxe Paint, Autodesk Animator, Disney Animation Studio).
//
// IMAGEEDITOR_INDEXED=0: delegates PNG/JPEG/BMP loading and PNG saving to user/image.h.
//
// Public API (both modes):
//   uint8_t *image_io_load(path, out_w, out_h, out_pal, out_pal_count)
//   bool     image_io_save(path, doc)

#include "imageeditor.h"

// ============================================================
// Shared helpers
// ============================================================

// Maximum image dimension accepted from any format (16384×16384).
// Keeps pixel buffer arithmetic safely within size_t bounds.
#define IIO_MAX_DIM 16384

// ============================================================
// Indexed-mode path (PCX and BMP, 8-bit palette)
// ============================================================

#if IMAGEEDITOR_INDEXED

#include <bmplib.h>

static bool iio_has_ext(const char *path, const char *ext) {
  if (!path || !ext) return false;
  size_t plen = strlen(path);
  size_t elen = strlen(ext);
  if (plen < elen) return false;
  return strcasecmp(path + plen - elen, ext) == 0;
}

// ---- Byte-order helpers ----

static void iio_write_u16_le(uint8_t *buf, uint16_t v) {
  buf[0] = (uint8_t)(v);
  buf[1] = (uint8_t)(v >> 8);
}

static uint16_t iio_read_u16_le(const uint8_t *buf) {
  return (uint16_t)(buf[0] | ((uint16_t)buf[1] << 8));
}

// ============================================================
// PCX — ZSoft PCX version 5, 8-bit indexed
// ============================================================

// Write one RLE-compressed PCX scanline.
static bool pcx_write_rle_row(FILE *fp, const uint8_t *row, int len) {
  int i = 0;
  while (i < len) {
    uint8_t val = row[i];
    int run = 1;
    while (run < 63 && i + run < len && row[i + run] == val)
      run++;
    if (run > 1 || (val & 0xC0) == 0xC0) {
      if (fputc((int)(0xC0 | run), fp) == EOF ||
          fputc((int)val, fp) == EOF)
        return false;
    } else {
      if (fputc((int)val, fp) == EOF)
        return false;
    }
    i += run;
  }
  return true;
}

// Decode one RLE-compressed PCX scanline; *pos is updated in-place.
static bool pcx_decode_rle_row(const uint8_t *data, size_t data_end,
                                size_t *pos, uint8_t *out, int bpl) {
  int written = 0;
  while (written < bpl) {
    if (*pos >= data_end) return false;
    uint8_t b = data[(*pos)++];
    if ((b & 0xC0) == 0xC0) {
      int run = b & 0x3F;
      if (*pos >= data_end) return false;
      uint8_t val = data[(*pos)++];
      for (int k = 0; k < run && written < bpl; k++)
        out[written++] = val;
    } else {
      out[written++] = b;
    }
  }
  return true;
}

static bool pcx_save(const char *path, const canvas_doc_t *doc) {
  if (!doc || doc->layer.count == 0 ||
      !doc->layer.stack[0] || !doc->layer.stack[0]->pixels) return false;
  int w = doc->canvas_w;
  int h = doc->canvas_h;
  // PCX requires bytes-per-line to be even.
  int bpl = (w + 1) & ~1;

  FILE *fp = fopen(path, "wb");
  if (!fp) return false;

  // 128-byte PCX header (version 5, RLE, 8bpp, 1 plane).
  uint8_t hdr[128];
  memset(hdr, 0, sizeof(hdr));
  hdr[0] = 0x0A;                             // ZSoft manufacturer
  hdr[1] = 0x05;                             // Version 5 (256-colour palette)
  hdr[2] = 0x01;                             // RLE encoding
  hdr[3] = 0x08;                             // 8 bits per plane
  iio_write_u16_le(hdr + 4,  0);             // Xmin
  iio_write_u16_le(hdr + 6,  0);             // Ymin
  iio_write_u16_le(hdr + 8,  (uint16_t)(w - 1)); // Xmax
  iio_write_u16_le(hdr + 10, (uint16_t)(h - 1)); // Ymax
  iio_write_u16_le(hdr + 12, 96);            // HRes (dpi, cosmetic)
  iio_write_u16_le(hdr + 14, 96);            // VRes (dpi, cosmetic)
  hdr[65] = 1;                               // colour planes
  iio_write_u16_le(hdr + 66, (uint16_t)bpl); // bytes per line
  iio_write_u16_le(hdr + 68, 1);             // palette type: colour
  if (fwrite(hdr, 1, 128, fp) != 128) {
    fclose(fp); return false;
  }

  // RLE-compressed pixel rows.
  const uint8_t *pixels = doc->layer.stack[0]->pixels;
  uint8_t *row_buf = malloc((size_t)bpl);
  if (!row_buf) { fclose(fp); return false; }
  bool ok = true;
  for (int y = 0; y < h && ok; y++) {
    memset(row_buf, 0, (size_t)bpl);
    memcpy(row_buf, pixels + (size_t)y * w, (size_t)w);
    ok = pcx_write_rle_row(fp, row_buf, bpl);
  }
  free(row_buf);
  if (!ok) { fclose(fp); return false; }

  // 256-colour palette at end of file: marker 0x0C then 256 × RGB.
  if (fputc(0x0C, fp) == EOF) { fclose(fp); return false; }
  for (int i = 0; i < 256; i++) {
    uint32_t c = (i < doc->ipal.count) ? doc->ipal.entries[i] : 0u;
    if (fputc((int)COLOR_R(c), fp) == EOF ||
        fputc((int)COLOR_G(c), fp) == EOF ||
        fputc((int)COLOR_B(c), fp) == EOF) {
      fclose(fp); return false;
    }
  }
  fclose(fp);
  return true;
}

static uint8_t *pcx_load(const char *path, int *out_w, int *out_h,
                          uint32_t out_pal[256], int *out_pal_count) {
  FILE *fp = fopen(path, "rb");
  if (!fp) return NULL;

  fseek(fp, 0, SEEK_END);
  long fsz = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  // Minimum: 128 header + 769 palette = 897 bytes.
  if (fsz < 897) { fclose(fp); return NULL; }

  uint8_t *fdata = malloc((size_t)fsz);
  if (!fdata) { fclose(fp); return NULL; }
  if ((long)fread(fdata, 1, (size_t)fsz, fp) != fsz) {
    free(fdata); fclose(fp); return NULL;
  }
  fclose(fp);

  // Validate: manufacturer, version 5, RLE, 8bpp, 1 plane.
  if (fdata[0] != 0x0A || fdata[1] != 0x05 ||
      fdata[2] != 0x01 || fdata[3] != 0x08 || fdata[65] != 1) {
    free(fdata); return NULL;
  }

  int xmin = iio_read_u16_le(fdata + 4);
  int ymin = iio_read_u16_le(fdata + 6);
  int xmax = iio_read_u16_le(fdata + 8);
  int ymax = iio_read_u16_le(fdata + 10);
  int bpl  = iio_read_u16_le(fdata + 66);
  int w = xmax - xmin + 1;
  int h = ymax - ymin + 1;
  if (w <= 0 || h <= 0 || bpl < w ||
      (size_t)w > IIO_MAX_DIM || (size_t)h > IIO_MAX_DIM) {
    free(fdata); return NULL;
  }

  // 256-colour palette — last 769 bytes: 0x0C + 768 bytes RGB.
  // Index IMAGEEDITOR_TRANSPARENT_INDEX is given alpha=0 to mark it as the
  // transparent slot, consistent with the convention used by Deluxe Paint and
  // this editor.  All other entries are fully opaque.
  if (out_pal && out_pal_count) {
    memset(out_pal, 0, 256 * sizeof(uint32_t));
    *out_pal_count = 0;
    if (fdata[fsz - 769] == 0x0C) {
      const uint8_t *pal = fdata + fsz - 768;
      for (int i = 0; i < 256; i++) {
        uint8_t r = pal[i * 3];
        uint8_t g = pal[i * 3 + 1];
        uint8_t b = pal[i * 3 + 2];
        uint8_t a = (i == IMAGEEDITOR_TRANSPARENT_INDEX) ? 0 : 255;
        out_pal[i] = MAKE_COLOR(r, g, b, a);
      }
      *out_pal_count = 256;
    }
  }

  // Decompress RLE pixel data (header ends at byte 128; palette begins
  // 769 bytes before end-of-file).
  size_t data_end = (size_t)fsz - 769;
  uint8_t *pixels = malloc((size_t)w * h);
  if (!pixels) { free(fdata); return NULL; }
  uint8_t *row_buf = malloc((size_t)bpl);
  if (!row_buf) { free(pixels); free(fdata); return NULL; }

  bool ok = true;
  size_t pos = 128;
  for (int y = 0; y < h && ok; y++) {
    ok = pcx_decode_rle_row(fdata, data_end, &pos, row_buf, bpl);
    if (ok) memcpy(pixels + (size_t)y * w, row_buf, (size_t)w);
  }
  free(row_buf);
  free(fdata);
  if (!ok) { free(pixels); return NULL; }

  *out_w = w;
  *out_h = h;
  return pixels;
}

// ============================================================
// BMP — Windows BMP, 8-bit indexed (BI_RGB)
// ============================================================

static bool bmp8_save(const char *path, const canvas_doc_t *doc) {
  if (!doc || doc->layer.count == 0 || !doc->layer.stack[0] || !doc->layer.stack[0]->pixels ||
      doc->canvas_w <= 0 || doc->canvas_h <= 0 || doc->canvas_w > IIO_MAX_DIM || doc->canvas_h > IIO_MAX_DIM) return false;
  FILE *file = fopen(path, "wb");
  if (!file) return false;
  BMPHANDLE bmp = bmpwrite_new(file);
  uint8_t palette[256 * 4] = {0};
  for (int i = 0; i < 256; i++) {
    uint32_t c = i < doc->ipal.count ? doc->ipal.entries[i] : 0;
    palette[i * 4] = COLOR_R(c); palette[i * 4 + 1] = COLOR_G(c); palette[i * 4 + 2] = COLOR_B(c);
  }
  bool ok = bmp && bmpwrite_set_dimensions(bmp, doc->canvas_w, doc->canvas_h, 1, 8) == BMP_RESULT_OK &&
            bmpwrite_set_palette(bmp, 256, palette) == BMP_RESULT_OK &&
            bmpwrite_save_image(bmp, doc->layer.stack[0]->pixels) == BMP_RESULT_OK;
  if (!ok) {
    const char *reason = bmp ? bmp_errmsg(bmp) : NULL;
    fprintf(stderr, "[imageeditor] BMP save failed path=%s: %s\n", path, reason ? reason : "allocation or configuration failed");
    fflush(stderr);
  }
  if (bmp) bmp_free(bmp);
  if (fclose(file) != 0) { fprintf(stderr, "[imageeditor] BMP close failed path=%s\n", path); fflush(stderr); return false; }
  return ok;
}

static uint8_t *bmp8_load(const char *path, int *out_w, int *out_h,
                          uint32_t out_pal[256], int *out_pal_count) {
  FILE *file = fopen(path, "rb");
  if (!file) return NULL;
  BMPHANDLE bmp = bmpread_new(file);
  uint8_t *pixels = NULL, palette[256 * 4] = {0}, *palette_data = palette;
  int w = 0, h = 0, count = 0, channels = 0, bits = 0;
  bool ok = bmp != NULL;
  if (ok) {
    bmpread_set_insanity_limit(bmp, (size_t)IIO_MAX_DIM * IIO_MAX_DIM * 4);
    ok = bmpread_load_info(bmp) == BMP_RESULT_OK && bmpread_info_bitcount(bmp) == 8;
  }
  if (ok) {
    w = bmpread_width(bmp); h = bmpread_height(bmp); count = bmpread_num_palette_colors(bmp);
    ok = w > 0 && h > 0 && w <= IIO_MAX_DIM && h <= IIO_MAX_DIM && count > 0 && count <= 256 &&
         bmpread_load_palette(bmp, &palette_data) == BMP_RESULT_OK &&
         bmpread_dimensions(bmp, &w, &h, &channels, &bits, NULL) == BMP_RESULT_OK && channels == 1 && bits == 8 &&
         bmpread_load_image(bmp, &pixels) == BMP_RESULT_OK;
  }
  if (ok && out_pal && out_pal_count) {
    memset(out_pal, 0, 256 * sizeof(*out_pal));
    for (int i = 0; i < count; i++)
      out_pal[i] = MAKE_COLOR(palette[i * 4], palette[i * 4 + 1], palette[i * 4 + 2],
                              i == IMAGEEDITOR_TRANSPARENT_INDEX ? 0 : 255);
    *out_pal_count = count;
  }
  if (!ok) {
    const char *reason = bmp ? bmp_errmsg(bmp) : NULL;
    fprintf(stderr, "[imageeditor] BMP load failed path=%s: %s\n", path, reason ? reason : "unsupported format or allocation failed");
    fflush(stderr); free(pixels); pixels = NULL;
  } else { *out_w = w; *out_h = h; }
  if (bmp) bmp_free(bmp);
  fclose(file);
  return pixels;
}

// ============================================================
// Public API — indexed mode
// ============================================================

// Load an image file.  Detects format from magic bytes: 'BM' → BMP, else PCX.
// Returns a malloc'd 1-byte-per-pixel index buffer.  Fills out_pal[256] with
// the file's embedded RGBA palette.  Returns NULL on failure.
// Caller must free() the returned buffer.
uint8_t *image_io_load(const char *path, int *out_w, int *out_h,
                       uint32_t out_pal[256], int *out_pal_count) {
  if (!path || !out_w || !out_h) return NULL;
  *out_w = 0; *out_h = 0;
  if (out_pal_count) *out_pal_count = 0;

  FILE *probe = fopen(path, "rb");
  if (!probe) return NULL;
  uint8_t magic[2] = { 0, 0 };
  size_t nread = fread(magic, 1, 2, probe);
  fclose(probe);
  if (nread < 2) return NULL; // File too small or unreadable

  if (magic[0] == 'B' && magic[1] == 'M')
    return bmp8_load(path, out_w, out_h, out_pal, out_pal_count);
  return pcx_load(path, out_w, out_h, out_pal, out_pal_count);
}

// Save a document.  Dispatches on extension: .bmp → BMP, else PCX (default).
bool image_io_save(const char *path, const canvas_doc_t *doc) {
  if (!path || !doc) return false;
  if (iio_has_ext(path, ".flc") || iio_has_ext(path, ".ptf")) return flc_save(path, doc);
#if IMAGEEDITOR_BW
  return false;
#endif
  if (iio_has_ext(path, ".bmp"))
    return bmp8_save(path, doc);
  return pcx_save(path, doc);
}

#else // !IMAGEEDITOR_INDEXED

// ============================================================
// Public API — 32-bit RGBA mode (delegates to user/image.h + PNG)
// ============================================================

// Wraps the framework's load_image().  Returns a malloc'd 4-byte/px RGBA
// buffer that the caller must free() — image_free() is called internally.
// out_pal and out_pal_count are set to zero (unused in 32-bit mode).
uint8_t *image_io_load(const char *path, int *out_w, int *out_h,
                       uint32_t out_pal[256], int *out_pal_count) {
  (void)out_pal;
  if (out_pal_count) *out_pal_count = 0;
  uint8_t *src = load_image(path, out_w, out_h);
  if (!src) return NULL;
  // Copy into a malloc-owned buffer so callers can always use free().
  size_t sz = (size_t)(*out_w) * (size_t)(*out_h) * 4;
  uint8_t *buf = malloc(sz);
  if (buf) memcpy(buf, src, sz);
  image_free(src);
  return buf;
}

// Composites all layers and saves the result as PNG.
bool image_io_save(const char *path, const canvas_doc_t *doc) {
  if (!path || !doc) return false;
  size_t sz = (size_t)doc->canvas_w * doc->canvas_h * 4;
  uint8_t *comp = malloc(sz);
  if (!comp) return false;
  canvas_composite(doc, comp);
  canvas_composite_over_bg(doc, comp);
  bool ok = save_image_png(path, comp, doc->canvas_w, doc->canvas_h);
  free(comp);
  return ok;
}

#endif // IMAGEEDITOR_INDEXED

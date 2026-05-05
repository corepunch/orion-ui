// Unified image file I/O for the image editor.
//
// IMAGEEDITOR_INDEXED=1: PCX (ZSoft v5, 8-bit) and BMP (8-bit indexed)
//   load and save.  These are the formats used by the 256-colour era tools
//   (Deluxe Paint, Autodesk Animator, Disney Animation Studio).
//
// IMAGEEDITOR_INDEXED=0: delegates to user/image.h (stb_image PNG/JPEG/BMP)
//   for loading and stb_image_write PNG for saving.
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

static bool iio_has_ext(const char *path, const char *ext) {
  if (!path || !ext) return false;
  size_t plen = strlen(path);
  size_t elen = strlen(ext);
  if (plen < elen) return false;
  return strcasecmp(path + plen - elen, ext) == 0;
}

// ============================================================
// Indexed-mode path (PCX and BMP, 8-bit palette)
// ============================================================

#if IMAGEEDITOR_INDEXED

// ---- Byte-order helpers ----

static void iio_write_u16_le(uint8_t *buf, uint16_t v) {
  buf[0] = (uint8_t)(v);
  buf[1] = (uint8_t)(v >> 8);
}

static void iio_write_u32_le(uint8_t *buf, uint32_t v) {
  buf[0] = (uint8_t)(v);
  buf[1] = (uint8_t)(v >> 8);
  buf[2] = (uint8_t)(v >> 16);
  buf[3] = (uint8_t)(v >> 24);
}

static uint16_t iio_read_u16_le(const uint8_t *buf) {
  return (uint16_t)(buf[0] | ((uint16_t)buf[1] << 8));
}

static uint32_t iio_read_u32_le(const uint8_t *buf) {
  return (uint32_t)buf[0]         |
         ((uint32_t)buf[1] << 8)  |
         ((uint32_t)buf[2] << 16) |
         ((uint32_t)buf[3] << 24);
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
  if (!doc || doc->layer.count == 0 ||
      !doc->layer.stack[0] || !doc->layer.stack[0]->pixels) return false;
  int w = doc->canvas_w;
  int h = doc->canvas_h;
  int row_stride = (w + 3) & ~3; // rows padded to a multiple of 4 bytes
  uint32_t pixel_bytes = (uint32_t)(row_stride * h);
  uint32_t data_offset = 14 + 40 + 256 * 4; // 1078
  uint32_t file_size   = data_offset + pixel_bytes;

  FILE *fp = fopen(path, "wb");
  if (!fp) return false;

  // BITMAPFILEHEADER (14 bytes)
  uint8_t fhdr[14];
  memset(fhdr, 0, sizeof(fhdr));
  fhdr[0] = 'B'; fhdr[1] = 'M';
  iio_write_u32_le(fhdr + 2,  file_size);
  iio_write_u32_le(fhdr + 10, data_offset);
  if (fwrite(fhdr, 1, 14, fp) != 14) { fclose(fp); return false; }

  // BITMAPINFOHEADER (40 bytes)
  uint8_t ihdr[40];
  memset(ihdr, 0, sizeof(ihdr));
  iio_write_u32_le(ihdr + 0,  40);
  iio_write_u32_le(ihdr + 4,  (uint32_t)w);
  iio_write_u32_le(ihdr + 8,  (uint32_t)h); // positive = bottom-up
  iio_write_u16_le(ihdr + 12, 1);           // planes
  iio_write_u16_le(ihdr + 14, 8);           // bits per pixel
  iio_write_u32_le(ihdr + 32, 256);         // clrUsed
  if (fwrite(ihdr, 1, 40, fp) != 40) { fclose(fp); return false; }

  // Colour table: 256 × RGBQUAD (B, G, R, 0)
  for (int i = 0; i < 256; i++) {
    uint32_t c = (i < doc->ipal.count) ? doc->ipal.entries[i] : 0u;
    uint8_t quad[4] = { COLOR_B(c), COLOR_G(c), COLOR_R(c), 0 };
    if (fwrite(quad, 1, 4, fp) != 4) { fclose(fp); return false; }
  }

  // Pixel data: rows stored bottom-up, padded to 4 bytes.
  const uint8_t *pixels = doc->layer.stack[0]->pixels;
  uint8_t *row_buf = calloc(1, (size_t)row_stride);
  if (!row_buf) { fclose(fp); return false; }
  bool ok = true;
  for (int y = h - 1; y >= 0 && ok; y--) {
    memset(row_buf, 0, (size_t)row_stride);
    memcpy(row_buf, pixels + (size_t)y * w, (size_t)w);
    ok = (fwrite(row_buf, 1, (size_t)row_stride, fp) == (size_t)row_stride);
  }
  free(row_buf);
  fclose(fp);
  return ok;
}

static uint8_t *bmp8_load(const char *path, int *out_w, int *out_h,
                           uint32_t out_pal[256], int *out_pal_count) {
  FILE *fp = fopen(path, "rb");
  if (!fp) return NULL;

  // BITMAPFILEHEADER (14 bytes)
  uint8_t fhdr[14];
  if (fread(fhdr, 1, 14, fp) != 14 ||
      fhdr[0] != 'B' || fhdr[1] != 'M') {
    fclose(fp); return NULL;
  }
  uint32_t data_offset = iio_read_u32_le(fhdr + 10);

  // BITMAPINFOHEADER (40 bytes minimum)
  uint8_t ihdr[40];
  if (fread(ihdr, 1, 40, fp) != 40) { fclose(fp); return NULL; }
  uint32_t info_size   = iio_read_u32_le(ihdr + 0);
  int      w           = (int)iio_read_u32_le(ihdr + 4);
  int32_t  h_signed    = (int32_t)iio_read_u32_le(ihdr + 8);
  uint16_t bit_count   = iio_read_u16_le(ihdr + 14);
  uint32_t compression = iio_read_u32_le(ihdr + 16);
  uint32_t clr_used    = iio_read_u32_le(ihdr + 32);

  if (bit_count != 8 || compression != 0) {
    fclose(fp); return NULL; // Only uncompressed 8-bit indexed
  }
  int h = (h_signed < 0) ? -h_signed : (int)h_signed;
  bool bottom_up = (h_signed > 0);
  if (w <= 0 || h <= 0 ||
      (size_t)w > IIO_MAX_DIM || (size_t)h > IIO_MAX_DIM) {
    fclose(fp); return NULL;
  }

  // Skip any extra bytes beyond the 40-byte BITMAPINFOHEADER.
  if (info_size > 40)
    fseek(fp, (long)(info_size - 40), SEEK_CUR);

  // Colour table: BMP stores each entry as B, G, R, reserved.
  // Index IMAGEEDITOR_TRANSPARENT_INDEX is given alpha=0 to mark it as the
  // transparent slot, consistent with the PCX loader and this editor's
  // palette convention.
  int pal_count = (clr_used > 0 && clr_used <= 256) ? (int)clr_used : 256;
  if (out_pal && out_pal_count) {
    memset(out_pal, 0, 256 * sizeof(uint32_t));
    *out_pal_count = pal_count;
    for (int i = 0; i < pal_count; i++) {
      uint8_t quad[4];
      if (fread(quad, 1, 4, fp) != 4) { fclose(fp); return NULL; }
      uint8_t a = (i == IMAGEEDITOR_TRANSPARENT_INDEX) ? 0 : 255;
      out_pal[i] = MAKE_COLOR(quad[2], quad[1], quad[0], a);
    }
  } else {
    fseek(fp, (long)(pal_count * 4), SEEK_CUR);
  }

  // Jump to pixel data using the absolute offset from the file header.
  fseek(fp, (long)data_offset, SEEK_SET);

  int row_stride = (w + 3) & ~3;
  uint8_t *pixels = malloc((size_t)w * h);
  if (!pixels) { fclose(fp); return NULL; }
  uint8_t *row_buf = malloc((size_t)row_stride);
  if (!row_buf) { free(pixels); fclose(fp); return NULL; }

  bool ok = true;
  for (int row = 0; row < h && ok; row++) {
    int dst_y = bottom_up ? (h - 1 - row) : row;
    if (fread(row_buf, 1, (size_t)row_stride, fp) != (size_t)row_stride) {
      ok = false; break;
    }
    memcpy(pixels + (size_t)dst_y * w, row_buf, (size_t)w);
  }
  free(row_buf);
  if (!ok) { free(pixels); fclose(fp); return NULL; }

  fclose(fp);
  *out_w = w;
  *out_h = h;
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
  if (iio_has_ext(path, ".bmp"))
    return bmp8_save(path, doc);
  return pcx_save(path, doc);
}

#else // !IMAGEEDITOR_INDEXED

// ============================================================
// Public API — 32-bit RGBA mode (delegates to user/image.h + PNG)
// ============================================================

// Wraps the framework's load_image().  Returns 4-byte/px RGBA.
// out_pal and out_pal_count are set to zero (unused in 32-bit mode).
// Caller must free() the returned buffer (stb uses system malloc).
uint8_t *image_io_load(const char *path, int *out_w, int *out_h,
                       uint32_t out_pal[256], int *out_pal_count) {
  (void)out_pal;
  if (out_pal_count) *out_pal_count = 0;
  return load_image(path, out_w, out_h);
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

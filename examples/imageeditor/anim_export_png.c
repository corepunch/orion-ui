// APNG and sprite-sheet PNG exporter for the image editor.
//
// APNG export: extends the single-image PNG written by png_save() with the
// three APNG chunks (acTL, fcTL, fdAT) needed for animation.
// Sprite-sheet export: composites all frames side-by-side into one wide RGBA
// canvas and calls png_save() on a temporary document.

#include "imageeditor.h"

// ============================================================
// APNG export
// ============================================================

// APNG chunk tags.
#define APNG_MAGIC_acTL  0x6163544CU  // 'acTL'
#define APNG_MAGIC_fcTL  0x6663544CU  // 'fcTL'
#define APNG_MAGIC_fdAT  0x66644154U  // 'fdAT'

// PNG CRC32 (standard PKZIP polynomial).
static uint32_t png_crc32(const uint8_t *buf, size_t len) {
  static uint32_t table[256];
  static bool table_init = false;
  if (!table_init) {
    for (uint32_t n = 0; n < 256; n++) {
      uint32_t c = n;
      for (int k = 0; k < 8; k++)
        c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      table[n] = c;
    }
    table_init = true;
  }
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++)
    crc = table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

static void write_be32(FILE *fp, uint32_t v) {
  uint8_t b[4] = {
    (uint8_t)(v >> 24), (uint8_t)(v >> 16),
    (uint8_t)(v >>  8), (uint8_t)(v)
  };
  fwrite(b, 1, 4, fp);
}

// Write a PNG chunk: length + type + data + CRC.
static void write_png_chunk(FILE *fp, uint32_t type,
                             const uint8_t *data, size_t data_len) {
  write_be32(fp, (uint32_t)data_len);

  uint8_t type_bytes[4] = {
    (uint8_t)(type >> 24), (uint8_t)(type >> 16),
    (uint8_t)(type >>  8), (uint8_t)(type)
  };
  fwrite(type_bytes, 1, 4, fp);
  if (data && data_len)
    fwrite(data, 1, data_len, fp);

  // CRC covers type + data.
  uint32_t crc = png_crc32(type_bytes, 4);
  if (data && data_len) {
    // Chain CRC over data.
    static uint32_t tbl[256];
    static bool tbl_init = false;
    if (!tbl_init) {
      for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
          c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        tbl[n] = c;
      }
      tbl_init = true;
    }
    uint32_t c = crc ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < data_len; i++)
      c = tbl[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    crc = c ^ 0xFFFFFFFFu;
  }
  write_be32(fp, crc);
}

// Build an acTL chunk payload (8 bytes).
static void build_actl(uint8_t out[8], uint32_t num_frames, uint32_t num_plays) {
  out[0] = (uint8_t)(num_frames >> 24); out[1] = (uint8_t)(num_frames >> 16);
  out[2] = (uint8_t)(num_frames >>  8); out[3] = (uint8_t)(num_frames);
  out[4] = (uint8_t)(num_plays  >> 24); out[5] = (uint8_t)(num_plays  >> 16);
  out[6] = (uint8_t)(num_plays  >>  8); out[7] = (uint8_t)(num_plays);
}

// Build an fcTL chunk payload (26 bytes).
// APNG fcTL layout:
//   [0..3]   sequence_number (BE)
//   [4..7]   width (BE)
//   [8..11]  height (BE)
//   [12..15] x_offset (BE)
//   [16..19] y_offset (BE)
//   [20..21] delay_num (BE)
//   [22..23] delay_den (BE)
//   [24]     dispose_op
//   [25]     blend_op
static void build_fctl(uint8_t out[26], uint32_t seq, uint32_t w, uint32_t h,
                       uint16_t delay_num, uint16_t delay_den) {
  out[ 0] = (uint8_t)(seq >> 24); out[ 1] = (uint8_t)(seq >> 16);
  out[ 2] = (uint8_t)(seq >>  8); out[ 3] = (uint8_t)(seq);
  out[ 4] = (uint8_t)(w   >> 24); out[ 5] = (uint8_t)(w   >> 16);
  out[ 6] = (uint8_t)(w   >>  8); out[ 7] = (uint8_t)(w);
  out[ 8] = (uint8_t)(h   >> 24); out[ 9] = (uint8_t)(h   >> 16);
  out[10] = (uint8_t)(h   >>  8); out[11] = (uint8_t)(h);
  // x_offset = 0
  out[12] = out[13] = out[14] = out[15] = 0;
  // y_offset = 0
  out[16] = out[17] = out[18] = out[19] = 0;
  // delay_num (BE 16-bit)
  out[20] = (uint8_t)(delay_num >> 8); out[21] = (uint8_t)(delay_num);
  // delay_den (BE 16-bit)
  out[22] = (uint8_t)(delay_den >> 8); out[23] = (uint8_t)(delay_den);
  // dispose_op = 1 (APNG_DISPOSE_OP_BACKGROUND), blend_op = 0 (APNG_BLEND_OP_SOURCE)
  out[24] = 1;
  out[25] = 0;
}

// Encode a single RGBA frame as IDAT/fdAT PNG image data using a very simple
// filter-0 (none) + deflate approach.
// This implementation uses the PNG uncompressed deflate blocks (type 0x01 for
// the last block) so it doesn't need zlib.  The resulting APNG is valid but
// uncompressed; most viewers will decode it without issues.
//
// Returns a heap-allocated buffer.  Caller frees.
static uint8_t *encode_frame_png_idat(const uint8_t *rgba, int w, int h,
                                       size_t *out_size) {
  // Each row: 1 filter byte + w*4 bytes.
  size_t row_size = 1 + (size_t)w * 4;
  size_t raw_size = row_size * (size_t)h;

  // Build the raw data (filter-0 scanlines).
  uint8_t *raw = malloc(raw_size);
  if (!raw) return NULL;
  for (int r = 0; r < h; r++) {
    raw[r * row_size] = 0; // filter type: none
    memcpy(raw + r * row_size + 1, rgba + (size_t)r * (size_t)w * 4,
           (size_t)w * 4);
  }

  // Deflate stored blocks: each block can hold at most 65535 bytes.
  // Emit as many BTYPE=00 (uncompressed) blocks as needed.
  const size_t BSIZE = 65535;
  size_t num_blocks  = (raw_size + BSIZE - 1) / BSIZE;
  if (num_blocks == 0) num_blocks = 1;

  // 2-byte zlib header + per-block header (5 bytes) + raw_size + 4-byte Adler32.
  size_t deflate_sz = 2 + num_blocks * 5 + raw_size + 4;
  uint8_t *deflate = malloc(deflate_sz);
  if (!deflate) { free(raw); return NULL; }

  uint8_t *dp = deflate;
  // zlib header: CMF=0x78 (deflate, window=32K), FLG=0x01 (no dict, level 0;
  // 0x7801 is divisible by 31).
  *dp++ = 0x78; *dp++ = 0x01;

  size_t offset = 0;
  for (size_t bi = 0; bi < num_blocks; bi++) {
    bool last = (bi == num_blocks - 1);
    size_t blen = (offset + BSIZE <= raw_size) ? BSIZE : (raw_size - offset);
    *dp++ = last ? 0x01 : 0x00; // BFINAL | BTYPE=00
    uint16_t len = (uint16_t)blen;
    uint16_t nlen = ~len;
    memcpy(dp, &len,  2); dp += 2;
    memcpy(dp, &nlen, 2); dp += 2;
    memcpy(dp, raw + offset, blen);
    dp     += blen;
    offset += blen;
  }

  // Adler-32 of the raw data.
  uint32_t s1 = 1, s2 = 0;
  for (size_t i = 0; i < raw_size; i++) {
    s1 = (s1 + raw[i]) % 65521;
    s2 = (s2 + s1)     % 65521;
  }
  uint32_t adler = (s2 << 16) | s1;
  *dp++ = (uint8_t)(adler >> 24);
  *dp++ = (uint8_t)(adler >> 16);
  *dp++ = (uint8_t)(adler >>  8);
  *dp++ = (uint8_t)(adler);

  free(raw);
  *out_size = (size_t)(dp - deflate);
  return deflate;
}

// Write a minimal single-image PNG to fp (used as the base layer for APNG).
static bool write_png_base(FILE *fp, const uint8_t *rgba, int w, int h) {
  // PNG signature.
  static const uint8_t kPNGSig[8] = {137,80,78,71,13,10,26,10};
  fwrite(kPNGSig, 1, 8, fp);

  // IHDR (13 bytes).
  uint8_t ihdr[13];
  ihdr[ 0] = (uint8_t)(w >> 24); ihdr[ 1] = (uint8_t)(w >> 16);
  ihdr[ 2] = (uint8_t)(w >>  8); ihdr[ 3] = (uint8_t)(w);
  ihdr[ 4] = (uint8_t)(h >> 24); ihdr[ 5] = (uint8_t)(h >> 16);
  ihdr[ 6] = (uint8_t)(h >>  8); ihdr[ 7] = (uint8_t)(h);
  ihdr[ 8] = 8;  // bit depth
  ihdr[ 9] = 2;  // colour type: RGB (we'll write RGBA as RGB+A below)
  ihdr[ 9] = 6;  // colour type: RGBA
  ihdr[10] = 0;  // compression method
  ihdr[11] = 0;  // filter method
  ihdr[12] = 0;  // interlace method
  write_png_chunk(fp, 0x49484452u /*IHDR*/, ihdr, 13);

  // IDAT.
  size_t idat_size = 0;
  uint8_t *idat = encode_frame_png_idat(rgba, w, h, &idat_size);
  if (!idat) return false;
  write_png_chunk(fp, 0x49444154u /*IDAT*/, idat, idat_size);
  free(idat);

  // IEND.
  write_png_chunk(fp, 0x49454E44u /*IEND*/, NULL, 0);
  return true;
}

bool anim_export_apng(canvas_doc_t *doc, const char *path) {
  if (!doc || !doc->anim || !path) return false;
  anim_timeline_t *tl = doc->anim;
  if (tl->frame_count == 0) return false;

  // Commit the current working buffer; abort if compression fails to avoid
  // silently wiping the active frame's stored data.
  if (!anim_frame_compress(tl->frames[tl->active_frame],
                           doc->pixels, doc->canvas_w, doc->canvas_h,
                           FRAME_FORMAT_INDEXED))
    return false;

  FILE *fp = fopen(path, "wb");
  if (!fp) return false;

  bool ok = true;
  int w = doc->canvas_w;
  int h = doc->canvas_h;

  uint8_t *rgba = malloc((size_t)w * (size_t)h * 4);
  if (!rgba) { fclose(fp); return false; }

  // Expand first frame.
  if (!anim_frame_expand(tl->frames[0], rgba, w, h)) {
    free(rgba); fclose(fp); return false;
  }

  // Write PNG signature + IHDR.
  static const uint8_t kPNGSig[8] = {137,80,78,71,13,10,26,10};
  fwrite(kPNGSig, 1, 8, fp);

  uint8_t ihdr[13] = {0};
  ihdr[ 0]=(uint8_t)(w>>24); ihdr[1]=(uint8_t)(w>>16);
  ihdr[ 2]=(uint8_t)(w>> 8); ihdr[3]=(uint8_t)(w);
  ihdr[ 4]=(uint8_t)(h>>24); ihdr[5]=(uint8_t)(h>>16);
  ihdr[ 6]=(uint8_t)(h>> 8); ihdr[7]=(uint8_t)(h);
  ihdr[ 8]=8; ihdr[9]=6; // 8-bit RGBA
  write_png_chunk(fp, 0x49484452u, ihdr, 13);

  // acTL chunk.
  uint8_t actl[8];
  build_actl(actl, (uint32_t)tl->frame_count, tl->loop ? 0u : 1u);
  write_png_chunk(fp, APNG_MAGIC_acTL, actl, 8);

  uint32_t seq = 0;

  for (int fi = 0; fi < tl->frame_count && ok; fi++) {
    anim_frame_t *frame = tl->frames[fi];

    if (fi > 0) {
      if (!anim_frame_expand(frame, rgba, w, h)) { ok = false; break; }
    }

    // fcTL: sequence, width, height, delay_num/den, disposal, blend.
    uint8_t fctl[26];
    uint16_t delay_num = (uint16_t)(frame->delay_ms > 0 ? frame->delay_ms : 80);
    uint16_t delay_den = 1000;
    build_fctl(fctl, seq++, (uint32_t)w, (uint32_t)h, delay_num, delay_den);
    write_png_chunk(fp, APNG_MAGIC_fcTL, fctl, 26);

    // Encode image data.
    size_t idat_size = 0;
    uint8_t *idat = encode_frame_png_idat(rgba, w, h, &idat_size);
    if (!idat) { ok = false; break; }

    if (fi == 0) {
      // First frame uses IDAT.
      write_png_chunk(fp, 0x49444154u, idat, idat_size);
    } else {
      // Subsequent frames use fdAT (prepend 4-byte sequence number).
      uint8_t *fdat = malloc(idat_size + 4);
      if (!fdat) { free(idat); ok = false; break; }
      uint32_t s = seq++;
      fdat[0]=(uint8_t)(s>>24); fdat[1]=(uint8_t)(s>>16);
      fdat[2]=(uint8_t)(s>> 8); fdat[3]=(uint8_t)(s);
      memcpy(fdat + 4, idat, idat_size);
      write_png_chunk(fp, APNG_MAGIC_fdAT, fdat, idat_size + 4);
      free(fdat);
    }
    free(idat);
  }

  write_png_chunk(fp, 0x49454E44u, NULL, 0); // IEND
  fclose(fp);
  free(rgba);
  return ok;
}

// ============================================================
// Sprite-sheet export
// ============================================================

bool anim_export_spritesheet(canvas_doc_t *doc, const char *path) {
  if (!doc || !doc->anim || !path) return false;
  anim_timeline_t *tl = doc->anim;
  if (tl->frame_count == 0) return false;

  // Commit the current working buffer; abort if compression fails.
  if (!anim_frame_compress(tl->frames[tl->active_frame],
                           doc->pixels, doc->canvas_w, doc->canvas_h,
                           FRAME_FORMAT_INDEXED))
    return false;

  int w = doc->canvas_w;
  int h = doc->canvas_h;
  int n = tl->frame_count;

  // Composite all frames side-by-side into a wide RGBA buffer.
  size_t sheet_pixels = (size_t)w * n * h;
  uint8_t *sheet = malloc(sheet_pixels * 4);
  if (!sheet) return false;

  bool ok = true;
  for (int fi = 0; fi < n && ok; fi++) {
    uint8_t *tmp = malloc((size_t)w * h * 4);
    if (!tmp) { ok = false; break; }
    ok = anim_frame_expand(tl->frames[fi], tmp, w, h);
    if (ok) {
      for (int r = 0; r < h; r++) {
        uint8_t *src_row = tmp  + (size_t)r * w * 4;
        uint8_t *dst_row = sheet + ((size_t)r * (size_t)(w * n) + (size_t)(fi * w)) * 4;
        memcpy(dst_row, src_row, (size_t)w * 4);
      }
    }
    free(tmp);
  }

  if (ok) {
    // Write using our internal PNG writer.
    FILE *fp = fopen(path, "wb");
    if (!fp) { free(sheet); return false; }
    ok = write_png_base(fp, sheet, w * n, h);
    fclose(fp);
  }

  free(sheet);
  return ok;
}

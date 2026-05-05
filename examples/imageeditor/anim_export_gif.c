// GIF89a animation exporter for the image editor.
//
// Writes a minimal GIF89a file from the timeline frames without any external
// library dependency.  The encoder uses the LZW variant required by GIF.

#include "imageeditor.h"

// ============================================================
// GIF byte-output helpers
// ============================================================

typedef struct {
  FILE *fp;
} gif_writer_t;

static void gw_byte(gif_writer_t *gw, uint8_t b) {
  fputc(b, gw->fp);
}

static void gw_word(gif_writer_t *gw, uint16_t w) {
  gw_byte(gw, (uint8_t)(w & 0xFF));
  gw_byte(gw, (uint8_t)(w >> 8));
}

static void gw_bytes(gif_writer_t *gw, const void *buf, size_t len) {
  fwrite(buf, 1, len, gw->fp);
}

// ============================================================
// Minimal LZW encoder
// ============================================================

#define LZW_MAX_BITS 12
#define LZW_TABLE_SIZE (1 << LZW_MAX_BITS)

typedef struct {
  int prefix; // code of previous string  (-1 = none)
  int suffix; // last appended character
} lzw_entry_t;

typedef struct {
  uint8_t buf[255];  // sub-block accumulator
  int     buf_pos;
  int     bit_buf;
  int     bit_pos;
  gif_writer_t *gw;
} lzw_stream_t;

static void lzw_flush_buf(lzw_stream_t *ls) {
  if (ls->buf_pos == 0) return;
  gw_byte(ls->gw, (uint8_t)ls->buf_pos);
  gw_bytes(ls->gw, ls->buf, (size_t)ls->buf_pos);
  ls->buf_pos = 0;
}

static void lzw_emit_bits(lzw_stream_t *ls, int code, int bits) {
  ls->bit_buf |= (code << ls->bit_pos);
  ls->bit_pos += bits;
  while (ls->bit_pos >= 8) {
    ls->buf[ls->buf_pos++] = (uint8_t)(ls->bit_buf & 0xFF);
    ls->bit_buf >>= 8;
    ls->bit_pos  -= 8;
    if (ls->buf_pos == 255)
      lzw_flush_buf(ls);
  }
}

static void lzw_flush_bits(lzw_stream_t *ls) {
  if (ls->bit_pos > 0) {
    ls->buf[ls->buf_pos++] = (uint8_t)(ls->bit_buf & 0xFF);
    ls->bit_buf = 0;
    ls->bit_pos = 0;
  }
  lzw_flush_buf(ls);
  // GIF block terminator
  gw_byte(ls->gw, 0);
}

// Encode `npx` bytes of palette indices into LZW.
// min_code_size is the GIF minimum code size (typically palette depth clamped
// to [2..8]).
static void lzw_encode(gif_writer_t *gw, const uint8_t *indices, size_t npx,
                       int min_code_size) {
  int clear_code = 1 << min_code_size;
  int eoi_code   = clear_code + 1;
  int next_code  = eoi_code + 1;
  int code_size  = min_code_size + 1;

  lzw_entry_t table[LZW_TABLE_SIZE];
  int hash[LZW_TABLE_SIZE];
  memset(hash, -1, sizeof(hash));

  lzw_stream_t ls = {0};
  ls.gw = gw;

  gw_byte(gw, (uint8_t)min_code_size);
  lzw_emit_bits(&ls, clear_code, code_size);

  if (npx == 0) {
    lzw_emit_bits(&ls, eoi_code, code_size);
    lzw_flush_bits(&ls);
    return;
  }

  int prefix = indices[0];

  for (size_t i = 1; i < npx; i++) {
    int suf = indices[i];

    // Look up (prefix, suf) in hash table.
    int h = (prefix * 256 + suf) & (LZW_TABLE_SIZE - 1);
    int code = -1;
    while (hash[h] >= 0) {
      int c = hash[h];
      if (table[c].prefix == prefix && table[c].suffix == suf) {
        code = c;
        break;
      }
      h = (h + 1) & (LZW_TABLE_SIZE - 1);
    }

    if (code >= 0) {
      prefix = code;
    } else {
      lzw_emit_bits(&ls, prefix, code_size);
      if (next_code < LZW_TABLE_SIZE) {
        table[next_code].prefix = prefix;
        table[next_code].suffix = suf;
        hash[h] = next_code;
        next_code++;
        if (next_code > (1 << code_size) && code_size < LZW_MAX_BITS)
          code_size++;
      } else {
        // Table full — emit clear and reinitialise.
        lzw_emit_bits(&ls, clear_code, code_size);
        next_code = eoi_code + 1;
        code_size = min_code_size + 1;
        memset(hash, -1, sizeof(hash));
      }
      prefix = suf;
    }
  }

  lzw_emit_bits(&ls, prefix,     code_size);
  lzw_emit_bits(&ls, eoi_code,   code_size);
  lzw_flush_bits(&ls);
}

// ============================================================
// GIF header / trailer / extension blocks
// ============================================================

// Write a GIF header with no global color table.
static void write_gif_header(gif_writer_t *gw, int w, int h) {
  gw_bytes(gw, "GIF89a", 6);
  gw_word(gw, (uint16_t)w);
  gw_word(gw, (uint16_t)h);
  // Packed: no GCT (bit 7 = 0), color resolution = 7, GCT size field ignored.
  gw_byte(gw, 0x70);
  gw_byte(gw, 0);   // background colour index
  gw_byte(gw, 0);   // pixel aspect ratio
}

static void write_netscape_ext(gif_writer_t *gw, uint16_t loop_count) {
  // Netscape 2.0 extension for animation looping.
  gw_byte(gw, 0x21); // Extension introducer
  gw_byte(gw, 0xFF); // App extension label
  gw_byte(gw, 11);   // Block size
  gw_bytes(gw, "NETSCAPE2.0", 11);
  gw_byte(gw, 3);    // Sub-block length
  gw_byte(gw, 1);    // Sub-block ID
  gw_word(gw, loop_count); // 0 = infinite
  gw_byte(gw, 0);    // Block terminator
}

static void write_graphic_ctrl(gif_writer_t *gw, int delay_cs) {
  // Graphic Control Extension — sets per-frame delay.
  gw_byte(gw, 0x21); // Extension introducer
  gw_byte(gw, 0xF9); // Graphic control label
  gw_byte(gw, 4);    // Block size
  gw_byte(gw, 0x04); // Packed: dispose=1 (restore to bg), no user input
  gw_word(gw, (uint16_t)delay_cs); // delay in centiseconds
  gw_byte(gw, 0);    // transparent colour index (not used)
  gw_byte(gw, 0);    // block terminator
}

// Write a GIF Image Descriptor with a per-frame local color table.
static void write_image_descriptor(gif_writer_t *gw, int w, int h,
                                   const uint32_t *lct, int lct_size) {
  // Find depth such that 2^depth >= lct_size.
  int depth = 0;
  while ((1 << depth) < lct_size) depth++;
  if (depth < 1) depth = 1;
  if (depth > 8) depth = 8;
  int lct_entries = 1 << depth;

  gw_byte(gw, 0x2C); // Image separator
  gw_word(gw, 0);    // left
  gw_word(gw, 0);    // top
  gw_word(gw, (uint16_t)w);
  gw_word(gw, (uint16_t)h);
  // Packed: LCT present (bit 7), not interlaced, LCT size = depth-1.
  gw_byte(gw, (uint8_t)(0x80 | (depth - 1)));

  // Local color table (RGB triples).
  for (int i = 0; i < lct_entries; i++) {
    uint32_t c = (i < lct_size) ? lct[i] : 0;
    gw_byte(gw, COLOR_R(c));
    gw_byte(gw, COLOR_G(c));
    gw_byte(gw, COLOR_B(c));
  }
}

// ============================================================
// Export entry point
// ============================================================

bool anim_export_gif(canvas_doc_t *doc, const char *path) {
  if (!doc || !doc->anim || !path) return false;
  anim_timeline_t *tl = doc->anim;
  if (tl->frame_count == 0) return false;

  FILE *fp = fopen(path, "wb");
  if (!fp) return false;
  gif_writer_t gw = { fp };

  // Commit the current working buffer to the active frame first.
  // Abort if compression fails to avoid wiping the active frame's data.
  if (!anim_frame_compress(tl->frames[tl->active_frame],
                           doc->pixels, doc->canvas_w, doc->canvas_h,
                           FRAME_FORMAT_INDEXED)) {
    fclose(fp);
    return false;
  }

  // Write GIF header without a global color table.  Each frame carries its
  // own local color table so all palettes are independent.
  write_gif_header(&gw, doc->canvas_w, doc->canvas_h);

  // Emit the Netscape looping extension only when looping is enabled.
  // Loop count 0 = infinite; no extension = play once.
  if (tl->frame_count > 1 && tl->loop)
    write_netscape_ext(&gw, 0);

  bool ok = true;
  uint8_t *rgba_tmp = NULL;

  for (int fi = 0; fi < tl->frame_count && ok; fi++) {
    anim_frame_t *frame = tl->frames[fi];

    // Per-frame palette and index data.
    const uint8_t  *indices    = NULL;
    uint8_t        *local_idx  = NULL;
    const uint32_t *lct        = NULL;
    int             lct_size   = 256;
    uint32_t        tmp_pal[256];

    if (frame->format == FRAME_FORMAT_INDEXED && frame->data) {
      // Use the palette stored with the compressed frame directly.
      indices  = frame->data;
      lct      = frame->palette;
      lct_size = 256;
    } else {
      // Expand to RGBA then quantize into a fresh palette.
      if (!rgba_tmp) {
        rgba_tmp = malloc((size_t)doc->canvas_w * (size_t)doc->canvas_h * 4);
        if (!rgba_tmp) { ok = false; break; }
      }
      if (!anim_frame_expand(frame, rgba_tmp, doc->canvas_w, doc->canvas_h)) {
        ok = false;
        break;
      }
      local_idx = malloc((size_t)doc->canvas_w * (size_t)doc->canvas_h);
      if (!local_idx) { ok = false; break; }
      int pal_sz = quantize_rgba_indexed(rgba_tmp, doc->canvas_w, doc->canvas_h,
                                         tmp_pal, local_idx);
      if (pal_sz == 0) { ok = false; free(local_idx); local_idx = NULL; break; }
      indices  = local_idx;
      lct      = tmp_pal;
      lct_size = pal_sz;
    }

    int delay_cs = frame->delay_ms / 10;
    if (delay_cs < 2) delay_cs = 2; // GIF minimum

    write_graphic_ctrl(&gw, delay_cs);
    write_image_descriptor(&gw, doc->canvas_w, doc->canvas_h, lct, lct_size);
    lzw_encode(&gw, indices, (size_t)doc->canvas_w * (size_t)doc->canvas_h, 8);

    free(local_idx);
    local_idx = NULL;
  }

  free(rgba_tmp);

  // GIF trailer
  gw_byte(&gw, 0x3B);
  fclose(fp);
  return ok;
}

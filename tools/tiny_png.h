#ifndef TINY_PNG_H
#define TINY_PNG_H

/*
  tiny_png.h — minimal PNG writer with font atlas metadata chunk.

  Supports:
    color_type 0 = grayscale (8-bit)
    color_type 6 = RGBA (8-bit)
  No DEFLATE compression (stored blocks — valid per spec).

  Custom chunk  "foNT"  (private, safe-to-copy per PNG naming rules):
  ─────────────────────────────────────────────────────────────────────
  All multi-byte integers are big-endian (PNG convention).

  Header  (22 bytes, fixed):
    u8[4]   magic          "foNT"
    u16     version        1
    u16     first_char     first codepoint in glyph table
    u16     num_chars      number of entries in glyph table
    u16     cell_w
    u16     cell_h
    u16     atlas_w
    u16     atlas_h
    u16     baseline       pixels from cell top to text baseline
    u16     flags          bit0=sharp  bit1=em_scale  bit2=rgba  bit3=invert

  Floats  (8 bytes):
    f32     pixel_height   as IEEE-754 big-endian
    f32     scale          stbtt scale factor, big-endian

  Name table  (variable):
    u8      name_full_len    followed by name_full_len bytes (no NUL)
    u8      name_family_len  followed by bytes
    u8      name_style_len   followed by bytes
    u8      name_version_len followed by bytes
    (empty strings are encoded as a single 0x00 length byte)

  Glyph table  (num_chars x 8 bytes, fixed stride):
    per entry, in codepoint order starting at first_char:
      i8    x0       bitmap-box left offset from pen (may be negative)
      i8    y0       bitmap-box top  offset from baseline (negative = above)
      u8    w        bitmap width  in pixels
      u8    h        bitmap height in pixels
      u8    advance  advance width in pixels (clamped to 255)
      u8    cell_col column of this glyph's cell in the atlas grid
      u8    cell_row row    of this glyph's cell in the atlas grid
      u8    _pad     reserved, write 0
  ─────────────────────────────────────────────────────────────────────
*/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define TPNG_MAYBE_UNUSED __attribute__((unused))
#else
#define TPNG_MAYBE_UNUSED
#endif

/* ------------------------------------------------------------------ */
/*  Glyph descriptor (one per character, filled by font_atlas.c)       */
/* ------------------------------------------------------------------ */
typedef struct {
    int8_t  x0, y0;    /* bitmap box offsets                          */
    uint8_t w,  h;     /* bitmap dimensions                           */
    uint8_t advance;   /* advance width in pixels                     */
    uint8_t cell_col;  /* atlas grid column                           */
    uint8_t cell_row;  /* atlas grid row                              */
} TinyPngGlyph;

/* ------------------------------------------------------------------ */
/*  Font metadata (passed alongside the pixel buffer)                  */
/* ------------------------------------------------------------------ */
typedef struct {
    /* names — NULL or NUL-terminated C strings (ASCII) */
    const char* name_full;
    const char* name_family;
    const char* name_style;
    const char* name_version;

    float  pixel_height;
    float  scale;
    int    first_char;
    int    num_chars;
    int    cell_w, cell_h;
    int    atlas_w, atlas_h;
    int    baseline;
    int    flags;        /* bit0=sharp bit1=em_scale bit2=rgba bit3=invert */

    const TinyPngGlyph* glyphs;   /* num_chars entries                 */
} TinyPngFontInfo;

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */
static uint32_t tpng__crc32(const unsigned char* data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
    }
    return ~crc;
}

static uint32_t tpng__adler32(const unsigned char* data, size_t len)
{
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % 65521u;
        b = (b + a)       % 65521u;
    }
    return (b << 16) | a;
}

static void tpng__u8(unsigned char** p, uint8_t v)
    { *(*p)++ = v; }

static void tpng__u16be(unsigned char** p, uint16_t v)
    { *(*p)++ = (v >> 8) & 0xFF; *(*p)++ = v & 0xFF; }

static void tpng__u32be(unsigned char** p, uint32_t v)
{
    *(*p)++ = (v >> 24) & 0xFF; *(*p)++ = (v >> 16) & 0xFF;
    *(*p)++ = (v >>  8) & 0xFF; *(*p)++ =  v        & 0xFF;
}

/* IEEE-754 f32 → big-endian bytes (no UB: memcpy through uint32_t) */
static void tpng__f32be(unsigned char** p, float f)
{
    uint32_t bits; memcpy(&bits, &f, 4);
    tpng__u32be(p, bits);
}

static void tpng__str8(unsigned char** p, const char* s)
{
    /* u8 length + bytes (no NUL terminator) */
    size_t n = s ? strlen(s) : 0;
    if (n > 255) n = 255;
    tpng__u8(p, (uint8_t)n);
    if (n) { memcpy(*p, s, n); *p += n; }
}

/* Write a complete PNG chunk to file */
static void tpng__chunk(FILE* f, const char type[4],
                         const unsigned char* data, uint32_t len)
{
    /* length */
    unsigned char lb[4] = {
        (len>>24)&0xFF, (len>>16)&0xFF, (len>>8)&0xFF, len&0xFF };
    fwrite(lb, 1, 4, f);
    /* type + data */
    fwrite(type, 1, 4, f);
    if (len) fwrite(data, 1, len, f);
    /* CRC over type+data */
    uint32_t crc = tpng__crc32((const unsigned char*)type, 4);
    if (len) {
        /* incremental CRC — re-run over data portion */
        /* (reuse the same table-free loop for simplicity) */
        crc = ~crc;
        for (uint32_t i = 0; i < len; i++) {
            crc ^= data[i];
            for (int k = 0; k < 8; k++)
                crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
        }
        crc = ~crc;
    }
    unsigned char cb[4] = {
        (crc>>24)&0xFF, (crc>>16)&0xFF, (crc>>8)&0xFF, crc&0xFF };
    fwrite(cb, 1, 4, f);
}

/* Build and write IDAT (stored-deflate zlib stream) */
static void tpng__idat(FILE* f, const unsigned char* raw, size_t raw_size)
{
    size_t max_blocks = (raw_size + 65534) / 65535 + 1;
    size_t idat_cap   = 2 + max_blocks * 5 + raw_size + 4;
    unsigned char* buf = (unsigned char*)malloc(idat_cap);
    unsigned char* p   = buf;

    /* zlib header: CM=8, CINFO=7 → 0x78; FLG → 0x01 (check bits, 78*256+01=19969, div31 ok) */
    tpng__u8(&p, 0x78); tpng__u8(&p, 0x01);

    const unsigned char* src = raw;
    size_t rem = raw_size;
    do {
        uint16_t take  = (rem > 65535) ? 65535 : (uint16_t)rem;
        uint16_t ntake = (uint16_t)~take;
        int last = (take == rem);
        tpng__u8(&p, last ? 0x01 : 0x00);   /* BFINAL|BTYPE=00 */
        tpng__u8(&p, take  & 0xFF); tpng__u8(&p, take  >> 8);
        tpng__u8(&p, ntake & 0xFF); tpng__u8(&p, ntake >> 8);
        memcpy(p, src, take); p += take; src += take; rem -= take;
        if (last) break;
    } while (1);

    uint32_t adl = tpng__adler32(raw, raw_size);
    tpng__u32be(&p, adl);

    tpng__chunk(f, "IDAT", buf, (uint32_t)(p - buf));
    free(buf);
}

/* ------------------------------------------------------------------ */
/*  Build the foNT chunk payload into a malloc'd buffer                 */
/* ------------------------------------------------------------------ */
static unsigned char* tpng__build_font_chunk(const TinyPngFontInfo* fi,
                                              uint32_t* out_len)
{
    const char* nfull    = fi->name_full    ? fi->name_full    : "";
    const char* nfamily  = fi->name_family  ? fi->name_family  : "";
    const char* nstyle   = fi->name_style   ? fi->name_style   : "";
    const char* nversion = fi->name_version ? fi->name_version : "";

    size_t nfl = strlen(nfull),   nfal = strlen(nfamily);
    size_t nsl = strlen(nstyle),  nvl  = strlen(nversion);
    if (nfl  > 255) nfl  = 255;
    if (nfal > 255) nfal = 255;
    if (nsl  > 255) nsl  = 255;
    if (nvl  > 255) nvl  = 255;

    /* header=4+2+2+2+2+2+2+2+2+2 = 22, floats=8,
       names=4 length bytes + actual chars,
       glyphs=num_chars*8                                              */
    size_t name_bytes = 4 + nfl + nfal + nsl + nvl;
    size_t glyph_bytes = (size_t)fi->num_chars * 8;
    size_t total = 22 + 8 + name_bytes + glyph_bytes;

    unsigned char* buf = (unsigned char*)malloc(total);
    if (!buf) return NULL;
    unsigned char* p = buf;

    /* magic */
    memcpy(p, "foNT", 4); p += 4;

    /* header fields */
    tpng__u16be(&p, 1);                         /* version       */
    tpng__u16be(&p, (uint16_t)fi->first_char);
    tpng__u16be(&p, (uint16_t)fi->num_chars);
    tpng__u16be(&p, (uint16_t)fi->cell_w);
    tpng__u16be(&p, (uint16_t)fi->cell_h);
    tpng__u16be(&p, (uint16_t)fi->atlas_w);
    tpng__u16be(&p, (uint16_t)fi->atlas_h);
    tpng__u16be(&p, (uint16_t)fi->baseline);
    tpng__u16be(&p, (uint16_t)fi->flags);

    /* floats */
    tpng__f32be(&p, fi->pixel_height);
    tpng__f32be(&p, fi->scale);

    /* name table */
    tpng__str8(&p, fi->name_full);
    tpng__str8(&p, fi->name_family);
    tpng__str8(&p, fi->name_style);
    tpng__str8(&p, fi->name_version);

    /* glyph table */
    for (int i = 0; i < fi->num_chars; i++) {
        const TinyPngGlyph* g = &fi->glyphs[i];
        tpng__u8(&p, (uint8_t)g->x0);
        tpng__u8(&p, (uint8_t)g->y0);
        tpng__u8(&p, g->w);
        tpng__u8(&p, g->h);
        tpng__u8(&p, g->advance);
        tpng__u8(&p, g->cell_col);
        tpng__u8(&p, g->cell_row);
        tpng__u8(&p, 0);    /* _pad */
    }

    *out_len = (uint32_t)(p - buf);
    return buf;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

/*
 * tiny_png_save_font()
 *
 * Writes a PNG with an embedded foNT chunk.
 * fi may be NULL to skip the font chunk (falls back to plain save).
 * color_type: 0 = greyscale, 6 = RGBA.
 */
static int tiny_png_save_font(
    const char*           filename,
    const unsigned char*  pixels,
    int w, int h,
    int color_type,
    const TinyPngFontInfo* fi      /* may be NULL */
)
{
    if (w <= 0 || h <= 0) return 0;
    if (color_type != 0 && color_type != 6) return 0;

    int channels = (color_type == 6) ? 4 : 1;
    int stride   = w * channels;

    /* Pre-filter scanlines (filter type 0 = None) */
    size_t raw_size = (size_t)h * (1 + stride);
    unsigned char* raw = (unsigned char*)malloc(raw_size);
    if (!raw) return 0;
    for (int y = 0; y < h; y++) {
        raw[y * (1 + stride)] = 0;
        memcpy(raw + y * (1 + stride) + 1, pixels + y * stride, stride);
    }

    FILE* f = fopen(filename, "wb");
    if (!f) { free(raw); return 0; }

    /* PNG signature */
    static const unsigned char sig[8] = {137,80,78,71,13,10,26,10};
    fwrite(sig, 1, 8, f);

    /* IHDR */
    {
        unsigned char ih[13];
        unsigned char* p = ih;
        tpng__u32be(&p, (uint32_t)w);
        tpng__u32be(&p, (uint32_t)h);
        tpng__u8(&p, 8);                            /* bit depth      */
        tpng__u8(&p, (uint8_t)color_type);
        tpng__u8(&p, 0);                            /* compression    */
        tpng__u8(&p, 0);                            /* filter         */
        tpng__u8(&p, 0);                            /* interlace      */
        tpng__chunk(f, "IHDR", ih, 13);
    }

    /* foNT (before IDAT — ancillary chunks may appear anywhere before IEND) */
    if (fi) {
        uint32_t flen = 0;
        unsigned char* fbuf = tpng__build_font_chunk(fi, &flen);
        if (fbuf) {
            tpng__chunk(f, "foNT", fbuf, flen);
            free(fbuf);
        }
    }

    /* IDAT */
    tpng__idat(f, raw, raw_size);

    /* IEND */
    tpng__chunk(f, "IEND", NULL, 0);

    fclose(f);
    free(raw);
    return 1;
}

/* Convenience: plain save without font metadata (backward compat) */
static TPNG_MAYBE_UNUSED int tiny_png_save(
    const char* filename,
    const unsigned char* pixels,
    int w, int h,
    int color_type)
{
    return tiny_png_save_font(filename, pixels, w, h, color_type, NULL);
}

/* ------------------------------------------------------------------ */
/*  Reader helper (for runtime use in your engine)                      */
/*                                                                      */
/*  tiny_png_read_font_chunk()                                          */
/*    Scans raw PNG bytes for a foNT chunk and fills *fi and *glyphs.   */
/*    *glyphs is malloc'd by the callee; caller must free() it.         */
/*    Returns 1 on success, 0 if not found or malformed.                */
/* ------------------------------------------------------------------ */
static uint16_t tpng__rd_u16be(const unsigned char* p)
    { return ((uint16_t)p[0] << 8) | p[1]; }
static uint32_t tpng__rd_u32be(const unsigned char* p)
    { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static float tpng__rd_f32be(const unsigned char* p)
    { uint32_t b = tpng__rd_u32be(p); float f; memcpy(&f,&b,4); return f; }

static TPNG_MAYBE_UNUSED int tiny_png_read_font_chunk(
    const unsigned char* png_bytes, size_t png_size,
    TinyPngFontInfo* fi, TinyPngGlyph** glyphs_out)
{
    if (png_size < 8) return 0;

    /* Skip PNG signature (8) and IHDR chunk (4+4+13+4 = 25) */
    size_t pos = 8;
    while (pos + 12 <= png_size) {
        uint32_t clen = tpng__rd_u32be(png_bytes + pos);
        const unsigned char* type = png_bytes + pos + 4;
        const unsigned char* data = png_bytes + pos + 8;

        if (memcmp(type, "foNT", 4) == 0 && clen >= 30) {
            const unsigned char* p = data;

            /* magic */
            if (memcmp(p, "foNT", 4) != 0) return 0;
            p += 4;

            uint16_t version    = tpng__rd_u16be(p); p += 2;
            if (version != 1) return 0;

            fi->first_char   = tpng__rd_u16be(p); p += 2;
            fi->num_chars    = tpng__rd_u16be(p); p += 2;
            fi->cell_w       = tpng__rd_u16be(p); p += 2;
            fi->cell_h       = tpng__rd_u16be(p); p += 2;
            fi->atlas_w      = tpng__rd_u16be(p); p += 2;
            fi->atlas_h      = tpng__rd_u16be(p); p += 2;
            fi->baseline     = tpng__rd_u16be(p); p += 2;
            fi->flags        = tpng__rd_u16be(p); p += 2;

            fi->pixel_height = tpng__rd_f32be(p); p += 4;
            fi->scale        = tpng__rd_f32be(p); p += 4;

            /* name table — pointers into the PNG buffer (not malloc'd) */
            /* caller must not free these; they alias png_bytes          */
            fi->name_full    = NULL;
            fi->name_family  = NULL;
            fi->name_style   = NULL;
            fi->name_version = NULL;
            /* (for simplicity the reader skips the name strings here;  */
            /*  extend as needed by reading u8 length + advancing)      */
            for (int n = 0; n < 4; n++) {
                uint8_t slen = *p++;
                /* skip for now — lengths available if caller wants them */
                p += slen;
            }

            /* glyph table */
            int nc = fi->num_chars;
            TinyPngGlyph* g = (TinyPngGlyph*)malloc((size_t)nc * sizeof(TinyPngGlyph));
            if (!g) return 0;
            for (int i = 0; i < nc; i++) {
                g[i].x0       = (int8_t)p[0];
                g[i].y0       = (int8_t)p[1];
                g[i].w        = p[2];
                g[i].h        = p[3];
                g[i].advance  = p[4];
                g[i].cell_col = p[5];
                g[i].cell_row = p[6];
                /* p[7] = pad */
                p += 8;
            }
            *glyphs_out = g;
            fi->glyphs  = g;
            return 1;
        }

        if (memcmp(type, "IEND", 4) == 0) break;
        pos += 12 + clen;
    }
    return 0;
}

#endif /* TINY_PNG_H */
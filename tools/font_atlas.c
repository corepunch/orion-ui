#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "tiny_png.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Defaults                                                            */
/* ------------------------------------------------------------------ */
#define DEFAULT_ATLAS_COLS   16   // glyphs per row in the atlas
#define DEFAULT_ATLAS_ROWS   16   // glyph rows in the atlas
#define DEFAULT_CELL_W        16
#define DEFAULT_CELL_H        16
// Atlas dimensions are derived: cols * cell_w × rows * cell_h.
// DEFAULT_ATLAS_W / DEFAULT_ATLAS_H kept as helpers for the help text.
#define DEFAULT_ATLAS_W      (DEFAULT_ATLAS_COLS * DEFAULT_CELL_W)
#define DEFAULT_ATLAS_H      (DEFAULT_ATLAS_ROWS * DEFAULT_CELL_H)
#define DEFAULT_PIXEL_HEIGHT  16.0f
#define DEFAULT_THRESHOLD     128
#define DEFAULT_FIRST_CHAR      0
#define DEFAULT_NUM_CHARS     256

/* ------------------------------------------------------------------ */
/*  Options                                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    const char* font_path;
    const char* out_path;
    int   atlas_w, atlas_h;
    int   cell_w,  cell_h;
    float pixel_height;
    int   sharp, threshold, em_scale;
    int   center_x, baseline_align;
    int   first_char, num_chars;
    int   invert, rgba, verbose;
    int   scan_width;           // if true, compute advance from bitmap width
    int   letter_spacing;       // pixels to add to advance
} Opts;

/* ------------------------------------------------------------------ */
/*  Help                                                                */
/* ------------------------------------------------------------------ */
static void print_help(const char* prog)
{
    printf(
        "Usage: %s font.ttf out.png [options]\n"
        "\n"
        "Rasterises a font into a fixed-cell bitmap atlas PNG.\n"
        "Font metadata and per-glyph metrics are embedded as a binary\n"
        "foNT chunk (private PNG chunk, safe-to-copy). See tiny_png.h\n"
        "for the chunk layout and a reader helper.\n"
        "\n"
        "Options:\n"
        "  -?  -h  --help          Show this help\n"
        "\n"
        "  Sizing\n"
        "  -pixelsize=N            Glyph pixel height (default %d)\n"
        "  -em                     Scale by EM square instead of pixel height\n"
        "                          (recommended for true pixel/bitmap fonts)\n"
        "  -cellw=N                Cell width  in pixels (default %d)\n"
        "  -cellh=N                Cell height in pixels (default %d)\n"
        "  -atlasw=N               Atlas width  (default %d)\n"
        "  -atlash=N               Atlas height (default %d)\n"
        "\n"
        "  Quality\n"
        "  -sharp                  Hard-threshold AA output → crisp 1-bit look\n"
        "  -threshold=N            Threshold 0-255 used with -sharp (default %d)\n"
        "  -smooth                 Keep anti-aliased output (default)\n"
        "\n"
        "  Layout\n"
        "  -center                 Centre glyph horizontally in cell (default: left-aligned)\n"
        "  -nobaseline             Top-align instead of baseline-align\n"
        "  -first=N                First codepoint to rasterise (default %d)\n"
        "  -num=N                  Number of codepoints (default %d)\n"
        "\n"
        "  Output\n"
        "  -invert                 Invert bitmap (black glyphs on white bg)\n"
        "  -rgba                   Write RGBA PNG (white glyph + alpha channel)\n"
        "  -v                      Verbose output\n"
        "\n"
        "  Metrics\n"
        "  -scan-width             Compute advance from actual bitmap width (proportional)\n"
        "  -letter-spacing=N       Add N pixels to advance (for letter spacing)\n"
        "\n"
        "Embedded foNT chunk fields\n"
        "  Header  version, first_char, num_chars, cell_w/h, atlas_w/h,\n"
        "          baseline, flags, pixel_height (f32), scale (f32)\n"
        "  Names   full, family, style, version (u8-length-prefixed strings)\n"
        "  Glyphs  per codepoint: x0, y0, w, h, advance, cell_col, cell_row\n"
        "          (8 bytes each, fixed stride)\n"
        "\n"
        "Examples\n"
        "  # Sharp pixel font at native size:\n"
        "  %s ChiKareGo.ttf atlas.png -pixelsize=8 -em -sharp -cellw=8 -cellh=8 -v\n"
        "\n"
        "  # Smooth AA atlas, RGBA, larger glyphs:\n"
        "  %s myfont.ttf atlas.png -pixelsize=32 -cellw=32 -cellh=32 -rgba\n"
        "\n"
        "  # Printable ASCII only (32-127), inverted:\n"
        "  %s font.ttf atlas.png -sharp -invert -first=32 -num=96\n",
        prog,
        (int)DEFAULT_PIXEL_HEIGHT, DEFAULT_CELL_W, DEFAULT_CELL_H,
        DEFAULT_ATLAS_W, DEFAULT_ATLAS_H, DEFAULT_THRESHOLD,
        DEFAULT_FIRST_CHAR, DEFAULT_NUM_CHARS,
        prog, prog, prog
    );
}

/* ------------------------------------------------------------------ */
/*  Utilities                                                           */
/* ------------------------------------------------------------------ */
static unsigned char* read_file(const char* path, int* size_out)
{
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    int sz = (int)ftell(f);
    rewind(f);
    unsigned char* d = (unsigned char*)malloc(sz);
    fread(d, 1, sz, f);
    fclose(f);
    *size_out = sz;
    return d;
}

static int parse_int_arg(const char* a, const char* pfx, int* out) {
    size_t n = strlen(pfx);
    if (strncmp(a, pfx, n)) return 0;
    *out = atoi(a + n); return 1;
}
static int parse_float_arg(const char* a, const char* pfx, float* out) {
    size_t n = strlen(pfx);
    if (strncmp(a, pfx, n)) return 0;
    *out = (float)atof(a + n); return 1;
}

/*
 * Extract a name string from the font's name table.
 * Prefers Windows UTF-16BE (platform 3), falls back to Mac Roman (platform 1).
 * Returns a malloc'd NUL-terminated ASCII string, or NULL.
 */
static char* get_font_name(const stbtt_fontinfo* font, int nameID)
{
    int len = 0;
    const char* raw = stbtt_GetFontNameString(font, &len,
                          STBTT_PLATFORM_ID_MICROSOFT,
                          STBTT_PLATFORM_ID_UNICODE,
                          STBTT_MS_LANG_ENGLISH,
                          nameID);
    if (raw && len > 0) {
        int chars = len / 2;
        char* out = (char*)malloc(chars + 1);
        for (int i = 0; i < chars; i++) {
            unsigned char hi = (unsigned char)raw[i * 2];
            unsigned char lo = (unsigned char)raw[i * 2 + 1];
            out[i] = (hi == 0 && lo >= 0x20 && lo < 0x7F) ? (char)lo : '?';
        }
        out[chars] = '\0';
        return out;
    }
    raw = stbtt_GetFontNameString(font, &len,
              STBTT_PLATFORM_ID_MAC, STBTT_MAC_EID_ROMAN,
              STBTT_MAC_LANG_ENGLISH, nameID);
    if (raw && len > 0) {
        char* out = (char*)malloc(len + 1);
        memcpy(out, raw, len);
        out[len] = '\0';
        return out;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char** argv)
{
    Opts o = {
        .atlas_w      = -1,   // computed from cell size after arg parsing
        .atlas_h      = -1,
        .cell_w       = DEFAULT_CELL_W,
        .cell_h       = DEFAULT_CELL_H,
        .pixel_height = DEFAULT_PIXEL_HEIGHT,
        .sharp        = 0,
        .threshold    = DEFAULT_THRESHOLD,
        .em_scale     = 0,
        .center_x     = 0,
        .baseline_align = 1,
        .first_char   = DEFAULT_FIRST_CHAR,
        .num_chars    = DEFAULT_NUM_CHARS,
        .invert       = 0,
        .rgba         = 0,
        .verbose      = 0,
        .scan_width   = 0,
        .letter_spacing = 0,
    };

    int positional = 0;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (!strcmp(a,"-?")||!strcmp(a,"-h")||!strcmp(a,"--help"))
            { print_help(argv[0]); return 0; }
        else if (!strcmp(a,"-sharp"))        o.sharp = 1;
        else if (!strcmp(a,"-smooth"))       o.sharp = 0;
        else if (!strcmp(a,"-em"))           o.em_scale = 1;
        else if (!strcmp(a,"-center"))       o.center_x = 1;
        else if (!strcmp(a,"-nobaseline"))   o.baseline_align = 0;
        else if (!strcmp(a,"-invert"))       o.invert = 1;
        else if (!strcmp(a,"-rgba"))         o.rgba = 1;
        else if (!strcmp(a,"-v"))            o.verbose = 1;
        else if (!strcmp(a,"-scan-width"))   o.scan_width = 1;
        else if (parse_int_arg(a, "-letter-spacing=", &o.letter_spacing)) {}
        else if (parse_float_arg(a, "-pixelsize=", &o.pixel_height)) {}
        else if (parse_int_arg(a, "-threshold=",   &o.threshold))    {}
        else if (parse_int_arg(a, "-cellw=",       &o.cell_w))       {}
        else if (parse_int_arg(a, "-cellh=",       &o.cell_h))       {}
        else if (parse_int_arg(a, "-atlasw=",      &o.atlas_w))      {}
        else if (parse_int_arg(a, "-atlash=",      &o.atlas_h))      {}
        else if (parse_int_arg(a, "-first=",       &o.first_char))   {}
        else if (parse_int_arg(a, "-num=",         &o.num_chars))    {}
        else if (a[0] != '-') {
            if      (positional == 0) o.font_path = a;
            else if (positional == 1) o.out_path  = a;
            positional++;
        } else {
            fprintf(stderr, "Unknown option: %s  (-? for help)\n", a);
            return 1;
        }
    }

    if (!o.font_path || !o.out_path) {
        fprintf(stderr, "Usage: %s font.ttf out.png [options]  (-? for help)\n", argv[0]);
        return 1;
    }

    /* ---- derive atlas size from cell grid if not set explicitly --- */
    if (o.atlas_w < 0) o.atlas_w = DEFAULT_ATLAS_COLS * o.cell_w;
    if (o.atlas_h < 0) o.atlas_h = DEFAULT_ATLAS_ROWS * o.cell_h;

    /* ---- load font -------------------------------------------- */
    int ttf_size = 0;
    unsigned char* ttf_data = read_file(o.font_path, &ttf_size);
    if (!ttf_data) { fprintf(stderr, "Cannot read font: %s\n", o.font_path); return 1; }

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, ttf_data, stbtt_GetFontOffsetForIndex(ttf_data, 0))) {
        fprintf(stderr, "Failed to initialise font\n");
        free(ttf_data); return 1;
    }

    float scale = o.em_scale
        ? stbtt_ScaleForMappingEmToPixels(&font, o.pixel_height)
        : stbtt_ScaleForPixelHeight(&font, o.pixel_height);

    int ascent, descent, linegap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &linegap);
    int baseline = (int)(ascent * scale + 0.5f);

    /* ---- extract font names ----------------------------------- */
    char* name_full    = get_font_name(&font, 4);  /* full name   */
    char* name_family  = get_font_name(&font, 1);  /* family      */
    char* name_style   = get_font_name(&font, 2);  /* subfamily   */
    char* name_version = get_font_name(&font, 5);  /* version     */

    if (o.verbose) {
        printf("Font    : %s\n", name_full    ? name_full    : "(unknown)");
        printf("Family  : %s / %s\n",
               name_family ? name_family : "?",
               name_style  ? name_style  : "?");
        printf("Version : %s\n", name_version ? name_version : "?");
        printf("Scale   : %.5f  baseline=%d  em=%d  sharp=%d  threshold=%d\n",
               scale, baseline, o.em_scale, o.sharp, o.threshold);
        printf("Atlas   : %dx%d  cell=%dx%d  chars=%d..%d\n",
               o.atlas_w, o.atlas_h, o.cell_w, o.cell_h,
               o.first_char, o.first_char + o.num_chars - 1);
    }

    /* ---- allocate atlas and glyph table ----------------------- */
    int channels  = o.rgba ? 4 : 1;
    unsigned char* atlas = (unsigned char*)calloc(1, o.atlas_w * o.atlas_h * channels);
    int cols = o.atlas_w / o.cell_w;

    TinyPngGlyph* glyphs = (TinyPngGlyph*)calloc(o.num_chars, sizeof(TinyPngGlyph));

    /* ---- rasterise -------------------------------------------- */
    for (int ci = 0; ci < o.num_chars; ci++) {
        int ch = o.first_char + ci;

        /* metrics */
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&font, ch, &adv, &lsb);
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&font, ch, scale, scale, &x0, &y0, &x1, &y1);

        int gw = x1 - x0;
        int gh = y1 - y0;
        int adv_px = (int)(adv * scale + 0.5f);
        if (adv_px > 255) adv_px = 255;

        int cell_col = ci % cols;
        int cell_row = ci / cols;

        /* fill glyph descriptor (even for whitespace/missing glyphs) */
        glyphs[ci].x0       = (int8_t)(x0 < -128 ? -128 : x0 > 127 ? 127 : x0);
        glyphs[ci].y0       = (int8_t)(y0 < -128 ? -128 : y0 > 127 ? 127 : y0);
        glyphs[ci].w        = (uint8_t)(gw < 0 ? 0 : gw > 255 ? 255 : gw);
        glyphs[ci].h        = (uint8_t)(gh < 0 ? 0 : gh > 255 ? 255 : gh);
        // Compute advance: use bitmap width if scan_width, else stbtt metrics
        int adv_final = o.scan_width ? (gw > 0 ? gw : 0) : adv_px;
        adv_final += o.letter_spacing;   // add letter spacing
        if (adv_final > 255) adv_final = 255;
        glyphs[ci].advance  = (uint8_t)adv_final;
        glyphs[ci].cell_col = (uint8_t)cell_col;
        glyphs[ci].cell_row = (uint8_t)cell_row;

        /* rasterise bitmap */
        int bw, bh;
        unsigned char* bmp = stbtt_GetCodepointBitmap(
            &font, 0, scale, ch, &bw, &bh, NULL, NULL);
        if (!bmp) continue;

        if (o.sharp)
            for (int p = 0; p < bw * bh; p++)
                bmp[p] = (bmp[p] >= o.threshold) ? 255 : 0;

        int cell_x = cell_col * o.cell_w;
        int cell_y = cell_row * o.cell_h;
        int draw_x = o.center_x ? cell_x + (o.cell_w - bw) / 2 : cell_x;
        int draw_y = o.baseline_align ? cell_y + baseline + y0      : cell_y;

        for (int y = 0; y < bh; y++) {
            for (int x = 0; x < bw; x++) {
                int ax = draw_x + x;
                int ay = draw_y + y;
                if (ax < 0 || ay < 0 || ax >= o.atlas_w || ay >= o.atlas_h) continue;
                unsigned char v = bmp[y * bw + x];
                if (o.invert) v = 255 - v;
                if (o.rgba) {
                    int idx = (ay * o.atlas_w + ax) * 4;
                    if (v > atlas[idx + 3]) {
                        atlas[idx]     = 255;
                        atlas[idx + 1] = 255;
                        atlas[idx + 2] = 255;
                        atlas[idx + 3] = v;
                    }
                } else {
                    unsigned char* dst = &atlas[ay * o.atlas_w + ax];
                    if (v > *dst) *dst = v;
                }
            }
        }
        stbtt_FreeBitmap(bmp, NULL);
    }

    /* ---- assemble font info for chunk ------------------------- */
    int flags = 0;
    if (o.sharp)    flags |= 1;
    if (o.em_scale) flags |= 2;
    if (o.rgba)     flags |= 4;
    if (o.invert)   flags |= 8;

    TinyPngFontInfo fi = {
        .name_full    = name_full,
        .name_family  = name_family,
        .name_style   = name_style,
        .name_version = name_version,
        .pixel_height = o.pixel_height,
        .scale        = scale,
        .first_char   = o.first_char,
        .num_chars    = o.num_chars,
        .cell_w       = o.cell_w,
        .cell_h       = o.cell_h,
        .atlas_w      = o.atlas_w,
        .atlas_h      = o.atlas_h,
        .baseline     = baseline,
        .flags        = flags,
        .glyphs       = glyphs,
    };

    /* ---- write PNG -------------------------------------------- */
    int color_type = o.rgba ? 6 : 0;
    if (!tiny_png_save_font(o.out_path, atlas, o.atlas_w, o.atlas_h,
                             color_type, &fi)) {
        fprintf(stderr, "Failed to write: %s\n", o.out_path);
    } else {
        printf("Wrote %s  (%dx%d %s %s)\n",
               o.out_path, o.atlas_w, o.atlas_h,
               o.rgba  ? "RGBA"   : "greyscale",
               o.sharp ? "sharp"  : "smooth");
    }

    free(glyphs);
    free(atlas);
    free(ttf_data);
    free(name_full);
    free(name_family);
    free(name_style);
    free(name_version);
    return 0;
}
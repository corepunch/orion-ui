// Text rendering implementation — dual-font design.
//
// Two named atlases at UI_WINDOW_SCALE == 1:
//   big   = ChiKareGo2 (16×16 cells, foNT metrics) — FONT_SYSTEM chrome
//   small = Geneva9 / SmallFont (8×8 cells)        — FONT_SMALL content + icons
//
// At UI_WINDOW_SCALE >= 2 both atlases map to the same SmallFont data.

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"
#include "user.h"
#include "image.h"
#include "../kernel/kernel.h"

// foNT chunk reader — no writer/stbtt deps needed.
#include "../tools/tiny_png.h"

#define MAX_TEXT_LENGTH   4096
#define VERTICES_PER_CHAR 6

#define WIN_PADDING 4

typedef struct {
  int16_t  x, y;
  float    u, v;
  uint32_t col;
} text_vertex_t;

// Per-character rendering metrics, indexed by the full char code (0-255).
typedef struct {
  uint8_t advance[256];  // cursor advance in pixels
  uint8_t x0[256];       // UV left offset within cell (proportional trim)
  uint8_t draw_w[256];   // quad draw width in pixels
} glyph_metrics_t;

// One font atlas: a GL_RED texture + mesh + glyph metrics.
typedef struct {
  R_Mesh          mesh;
  R_Texture       texture;   // GL_RED with swizzle: R→alpha
  int             cell_w;
  int             cell_h;
  int             chars_per_row;
} font_atlas_t;

static struct {
  font_atlas_t    big;          // FONT_SYSTEM: ChiKareGo2 at scale=1, SmallFont at scale>=2
  font_atlas_t    small;        // FONT_SMALL: Geneva9/SmallFont 0-255 at scale=1
  glyph_metrics_t big_met;      // glyph metrics for big atlas (FONT_SYSTEM)
  glyph_metrics_t small_met;    // glyph metrics for small atlas (FONT_SMALL)
  bool            has_small;    // true when small is a distinct second atlas
  int             big_height;   // cell pixel height — FONT_SYSTEM
  int             big_line;     // line height — FONT_SYSTEM
  int             big_space;    // space advance — FONT_SYSTEM
  int             small_height; // cell pixel height — FONT_SMALL
  int             small_line;   // line height — FONT_SMALL
  int             small_space;  // space advance — FONT_SMALL
} text_state = {0};

// ── Dynamic metric accessors ──────────────────────────────────────────────────
// Legacy getters: return FONT_SYSTEM (big atlas) metrics.

int get_char_height(void) { return text_state.big_height ? text_state.big_height : 8;  }
int get_line_height(void) { return text_state.big_line   ? text_state.big_line   : 12; }
int get_space_width(void) { return text_state.big_space  ? text_state.big_space  : 3;  }

// ── Helper: read a raw file into a heap buffer ────────────────────────────────

static unsigned char *read_file(const char *path, size_t *out_size) {
  FILE *f = fopen(path, "rb");
  if (!f) { *out_size = 0; return NULL; }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  rewind(f);
  if (sz <= 0) { fclose(f); *out_size = 0; return NULL; }
  unsigned char *buf = (unsigned char *)malloc((size_t)sz);
  if (!buf) { fclose(f); *out_size = 0; return NULL; }
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    free(buf); fclose(f); *out_size = 0; return NULL;
  }
  fclose(f);
  *out_size = (size_t)sz;
  return buf;
}

// ── Initialise a mesh for the given atlas ─────────────────────────────────────

static void init_atlas_mesh(font_atlas_t *atlas) {
  R_VertexAttrib attribs[] = {
    {0, 2, GL_SHORT,         GL_FALSE, offsetof(text_vertex_t, x)},
    {1, 2, GL_FLOAT,         GL_FALSE, offsetof(text_vertex_t, u)},
    {2, 4, GL_UNSIGNED_BYTE, GL_TRUE,  offsetof(text_vertex_t, col)},
  };
  R_MeshInit(&atlas->mesh, attribs, 3, sizeof(text_vertex_t), GL_TRIANGLES);
}

// ── Scan pixel columns of one 8-bit R-channel cell to find advance metrics ───

static void scan_cell_metrics(const unsigned char *red_buf, int tex_w,
                               int cell_x, int cell_y, int cell_w, int cell_h,
                               uint8_t *out_x0, uint8_t *out_draw_w, uint8_t *out_advance) {
  int lo = cell_w, hi = -1;
  for (int cy = 0; cy < cell_h; cy++) {
    for (int cx = 0; cx < cell_w; cx++) {
      if (red_buf[(cell_y + cy) * tex_w + (cell_x + cx)] > 0) {
        if (cx < lo) lo = cx;
        if (cx > hi) hi = cx;
      }
    }
  }
  if (hi < 0) {
    // Empty cell (e.g. space char) — advance=0 signals "use default space width"
    *out_x0      = 0;
    *out_draw_w  = 0;
    *out_advance = 0;
  } else {
    *out_x0     = (uint8_t)lo;
    *out_draw_w = (uint8_t)(hi - lo + 2); // +1 for pixel itself, +1 inter-char gap
    *out_advance = *out_draw_w;
  }
}

// ── Load one atlas from a PNG file ───────────────────────────────────────────
//
// first_char/last_char: range of char codes to populate metrics for.
// full_cell: if true, each char is drawn as a full cell_w×cell_h quad (ChiKareGo2);
//            if false, each char is trimmed proportionally by pixel scanning (SmallFont).
// foNT_data/foNT_size: raw PNG bytes already read (may be NULL to skip foNT parsing).

static bool load_atlas(font_atlas_t *atlas, glyph_metrics_t *met,
                       const char *path,
                       int first_char, int last_char,
                       bool full_cell) {
  // ── Load pixel data ───────────────────────────────────────────────────────
  int img_w = 0, img_h = 0;
  uint8_t *rgba = load_image(path, &img_w, &img_h);
  if (!rgba) {
    printf("text: failed to load font image: %s\n", path);
    return false;
  }

  // ── Detect cell layout from image dimensions ──────────────────────────────
  // Default cell size for each known font: SmallFont=8×8 (128px atlas),
  // ChiKareGo2=16×16 (256px atlas).  The foNT chunk always takes precedence
  // and overrides these defaults when present.
  int cell_w = (img_w >= 256) ? 16 : 8;
  int cell_h = (img_h >= 256) ? 16 : 8;
  int chars_per_row = img_w / cell_w;
  // TODO: use baseline for sub-pixel vertical alignment when supported.
  int baseline = cell_h;

  // ── Try to read foNT chunk ────────────────────────────────────────────────
  TinyPngFontInfo fi = {0};
  TinyPngGlyph *glyphs = NULL;
  size_t raw_sz = 0;
  unsigned char *raw = read_file(path, &raw_sz);
  bool has_font_meta = false;
  if (raw) {
    has_font_meta = (tiny_png_read_font_chunk(raw, raw_sz, &fi, &glyphs) == 1);
    free(raw);
  }
  if (has_font_meta) {
    cell_w        = fi.cell_w;
    cell_h        = fi.cell_h;
    chars_per_row = img_w / cell_w;
    baseline      = fi.baseline;
  }

  // ── Extract R channel → single-byte GL_RED buffer ────────────────────────
  size_t npx = (size_t)(img_w * img_h);
  unsigned char *red = (unsigned char *)malloc(npx);
  if (!red) {
    image_free(rgba);
    if (glyphs) free(glyphs);
    return false;
  }
  for (size_t i = 0; i < npx; i++)
    red[i] = rgba[i * 4];   // R channel; for greyscale PNG loaded as RGBA this is the grayval
  image_free(rgba);

  // ── Populate glyph metrics ────────────────────────────────────────────────
  for (int c = first_char; c <= last_char; c++) {
    int cell_col = c % chars_per_row;
    int cell_row = c / chars_per_row;
    int cx0      = cell_col * cell_w;
    int cy0      = cell_row * cell_h;

    if (has_font_meta && c < fi.first_char + fi.num_chars) {
      int idx = c - fi.first_char;
      uint8_t adv = glyphs[idx].advance;   // use local var; same ptr as fi.glyphs
      if (full_cell) {
        // Glyphs are left-aligned in their cells; draw quad is advance-wide so
        // characters don't overlap, and we only sample the glyph's own pixels.
        met->x0[c]      = 0;
        met->draw_w[c]  = adv ? adv : (uint8_t)cell_w;
        met->advance[c] = met->draw_w[c];
      } else {
        scan_cell_metrics(red, img_w, cx0, cy0, cell_w, cell_h,
                          &met->x0[c], &met->draw_w[c], &met->advance[c]);
        if (!met->advance[c] && adv) met->advance[c] = adv;
      }
    } else {
      // No foNT — scan pixels
      if (full_cell) {
        met->x0[c]      = 0;
        met->draw_w[c]  = (uint8_t)cell_w;
        met->advance[c] = (uint8_t)cell_w;
      } else {
        scan_cell_metrics(red, img_w, cx0, cy0, cell_w, cell_h,
                          &met->x0[c], &met->draw_w[c], &met->advance[c]);
      }
    }
  }

  // ── Upload GL_RED texture ─────────────────────────────────────────────────
  atlas->texture.width  = img_w;
  atlas->texture.height = img_h;
  atlas->texture.format = GL_RED;
  R_AllocateFontTexture(&atlas->texture, red);
  free(red);

  if (glyphs) free(glyphs);

  atlas->cell_w       = cell_w;
  atlas->cell_h       = cell_h;
  atlas->chars_per_row = chars_per_row;

  // ── Initialise vertex mesh ────────────────────────────────────────────────
  init_atlas_mesh(atlas);

  (void)baseline; /* reserved: used for vertical alignment once baseline rendering is added */
  return true;
}

// ── Legacy fallback: build atlas from hardcoded font_6x8 + icons_bits ────────

static void create_legacy_atlas(void) {
  extern unsigned char console_font_6x8[];
  extern unsigned char icons_bits[];

  const int tex_sz = 128, gw = 8, gh = 8, cpr = 16;
  unsigned char *buf = (unsigned char *)calloc((size_t)(tex_sz * tex_sz), 1);
  if (!buf) return;

  for (int c = 0; c < 128; c++) {
    int ax = (c % cpr) * gw, ay = (c / cpr) * gh;
    text_state.big_met.advance[c] = 0;
    text_state.big_met.x0[c]      = 0xff;
    uint8_t hi = 0;
    for (int y = 0; y < gh; y++) {
      int byte = console_font_6x8[c * gh + y];
      for (int x = 0; x < 6; x++) {
        if ((byte >> (5 - x)) & 1) {
          buf[(ay + y) * tex_sz + ax + x] = 255;
          if (x < text_state.big_met.x0[c]) text_state.big_met.x0[c] = (uint8_t)x;
          if (x + 2 > hi) hi = (uint8_t)(x + 2);
        }
      }
    }
    if (hi == 0) { text_state.big_met.x0[c] = 0; hi = 3; } // space
    text_state.big_met.draw_w[c]  = hi - text_state.big_met.x0[c];
    text_state.big_met.advance[c] = text_state.big_met.draw_w[c];
  }
  memcpy(buf + tex_sz * tex_sz / 2, icons_bits, (size_t)(tex_sz * tex_sz / 2));
  for (int i = 128; i < 256; i++) {
    text_state.big_met.x0[i]      = 0;
    text_state.big_met.draw_w[i]  = 8;
    text_state.big_met.advance[i] = 8;
  }

  text_state.big.texture.width  = tex_sz;
  text_state.big.texture.height = tex_sz;
  text_state.big.texture.format = GL_RED;
  R_AllocateFontTexture(&text_state.big.texture, buf);
  free(buf);

  text_state.big.cell_w        = gw;
  text_state.big.cell_h        = gh;
  text_state.big.chars_per_row = cpr;
  init_atlas_mesh(&text_state.big);

  text_state.big_height   = gh;
  text_state.big_line     = gh + 4;
  text_state.big_space    = 3;
  // No separate small atlas in legacy mode — small metrics mirror big.
  text_state.small_height = gh;
  text_state.small_line   = gh + 4;
  text_state.small_space  = 3;
  text_state.has_small    = false;
}

// ── init_text_rendering ───────────────────────────────────────────────────────

void init_text_rendering(void) {
  memset(&text_state, 0, sizeof(text_state));

  const char *exe = ui_get_exe_dir();
  char small_path[4096], chika_path[4096], geneva_path[4096];
  snprintf(small_path,  sizeof(small_path),  "%s/../share/orion/SmallFont.png",  exe);
  snprintf(chika_path,  sizeof(chika_path),  "%s/../share/orion/ChiKareGo2.png", exe);
  snprintf(geneva_path, sizeof(geneva_path), "%s/../share/orion/Geneva-12.png",    exe);

#if UI_WINDOW_SCALE == 1
  // At native (1:1) scale: ChiKareGo2 for chrome (FONT_SYSTEM), Geneva9 /
  // SmallFont for content (FONT_SMALL) plus icon chars (128-255).
  bool chika_ok = load_atlas(&text_state.big, &text_state.big_met,
                              chika_path, 0, 255, /*full_cell=*/true);
  if (chika_ok) {
    text_state.big_height = text_state.big.cell_h;
    text_state.big_line   = text_state.big.cell_h + 4;
    text_state.big_space  = text_state.big_met.advance[' ']
                            ? text_state.big_met.advance[' '] : 5;

    // Try Geneva9 first (Silkscreen text + SmallFont icons composite),
    // then fall back to SmallFont for the small atlas.
    bool geneva_ok = load_atlas(&text_state.small, &text_state.small_met,
                                geneva_path, 0, 255, /*full_cell=*/false);
    if (!geneva_ok)
      geneva_ok = load_atlas(&text_state.small, &text_state.small_met,
                             small_path,   0, 255, /*full_cell=*/false);
    text_state.has_small = geneva_ok;
    if (geneva_ok) {
      text_state.small_height = text_state.small.cell_h;
      text_state.small_line   = text_state.small.cell_h + 4;
      text_state.small_space  = text_state.small_met.advance[' ']
                                ? text_state.small_met.advance[' '] : 3;
    } else {
      text_state.small_height = text_state.big_height;
      text_state.small_line   = text_state.big_line;
      text_state.small_space  = text_state.big_space;
    }
    printf("text: ChiKareGo2 (%dx%d)%s\n",
           text_state.big.cell_w, text_state.big.cell_h,
           geneva_ok ? " + small atlas" : "");
    return;
  }
#endif

  // Default (scale>=2 or ChiKareGo2 unavailable): SmallFont for everything.
  bool font_ok = load_atlas(&text_state.big, &text_state.big_met,
                             small_path, 0, 255, /*full_cell=*/false);
  if (font_ok) {
    text_state.has_small    = false;
    int h  = text_state.big.cell_h;
    int sp = text_state.big_met.advance[' '] ? text_state.big_met.advance[' '] : 3;
    text_state.big_height   = text_state.small_height = h;
    text_state.big_line     = text_state.small_line   = h + 4;
    text_state.big_space    = text_state.small_space  = sp;
    printf("text: SmallFont (%dx%d)\n", text_state.big.cell_w, h);
    return;
  }

  // Final fallback: generate atlas from hardcoded font_6x8 data.
  printf("text: PNG fonts unavailable, using built-in font_6x8\n");
  create_legacy_atlas();
}

// ── Internal helpers ──────────────────────────────────────────────────────────

// Return the atlas + metrics for the given font role and char code.
// For FONT_SYSTEM: c < 128 → big atlas (ChiKareGo2); c >= 128 → small (icons).
// For FONT_SMALL:  all chars → small atlas.
// When has_small is false both roles use big.
static inline font_atlas_t *atlas_for_font(ui_font_t font, unsigned char c,
                                           glyph_metrics_t **met_out) {
  if (text_state.has_small && (font == FONT_SMALL || c >= 128)) {
    *met_out = &text_state.small_met;
    return &text_state.small;
  }
  *met_out = &text_state.big_met;
  return &text_state.big;
}

static inline int char_advance(unsigned char c) {
  glyph_metrics_t *m;
  atlas_for_font(FONT_SYSTEM, c, &m);
  return m->advance[c];
}

// Public API: pixel width of one glyph from the FONT_SYSTEM atlas.
int char_width(unsigned char c) {
  if (!text_state.big_height) return 0;
  return char_advance(c);
}

// ── Render a filled vertex batch for one atlas ────────────────────────────────

extern void push_sprite_args(int tex, int x, int y, int w, int h, float alpha);

static void flush_batch(font_atlas_t *atlas, text_vertex_t *buf, int count) {
  if (count == 0) return;
  R_SetBlendMode(true);
  push_sprite_args((int)atlas->texture.id, 0, 0, 1, 1, 1.0f);
  R_TextureBind(&atlas->texture);
  R_MeshDrawDynamic(&atlas->mesh, buf, (size_t)count);
}

// ── Build vertices for one character ─────────────────────────────────────────

static int emit_char_verts(text_vertex_t *buf, int cursor_x, int y,
                           unsigned char c, uint32_t col,
                           font_atlas_t *atlas, glyph_metrics_t *met) {
  int cell_col = c % atlas->chars_per_row;
  int cell_row = c / atlas->chars_per_row;
  int cx0      = cell_col * atlas->cell_w;
  int cy0      = cell_row * atlas->cell_h;
  int dw       = met->draw_w[c];
  int dh       = atlas->cell_h;
  if (dw == 0) return 0;

  float tw = (float)atlas->texture.width;
  float th = (float)atlas->texture.height;
  float u1 = (cx0 + met->x0[c])        / tw;
  float v1 = cy0                        / th;
  float u2 = (cx0 + met->x0[c] + dw)   / tw;
  float v2 = (cy0 + dh)                 / th;

  int x = cursor_x;
  buf[0] = (text_vertex_t){ (int16_t)x,      (int16_t)y,      u1, v1, col };
  buf[1] = (text_vertex_t){ (int16_t)x,      (int16_t)(y+dh), u1, v2, col };
  buf[2] = (text_vertex_t){ (int16_t)(x+dw), (int16_t)y,      u2, v1, col };
  buf[3] = (text_vertex_t){ (int16_t)x,      (int16_t)(y+dh), u1, v2, col };
  buf[4] = (text_vertex_t){ (int16_t)(x+dw), (int16_t)(y+dh), u2, v2, col };
  buf[5] = (text_vertex_t){ (int16_t)(x+dw), (int16_t)y,      u2, v1, col };
  return VERTICES_PER_CHAR;
}

// ── strwidth / strnwidth ──────────────────────────────────────────────────────

// Internal font-parameterised width measurement.
static int strnwidth_impl(ui_font_t font, const char *text, int len) {
  if (!text || !*text) return 0;
  if (len > MAX_TEXT_LENGTH) len = MAX_TEXT_LENGTH;
  int sw = (font == FONT_SMALL && text_state.has_small && text_state.small_space)
           ? text_state.small_space
           : (text_state.big_space ? text_state.big_space : 3);
  int w = 0;
  for (int i = 0; i < len; i++) {
    unsigned char c = (unsigned char)text[i];
    if (c == ' ') { w += sw; continue; }
    if (c == '\n') continue;
    glyph_metrics_t *met;
    atlas_for_font(font, c, &met);
    w += met->advance[c];
  }
  return w;
}

int strnwidth(const char *text, int text_length) {
  return strnwidth_impl(FONT_SYSTEM, text, text_length);
}

int strwidth(const char *text) {
  if (!text || !*text) return 0;
  return strnwidth_impl(FONT_SYSTEM, text, (int)strlen(text));
}

// ── New explicit-font metric API ──────────────────────────────────────────────

int text_char_height(ui_font_t font) {
  if (font == FONT_SMALL)
    return text_state.small_height ? text_state.small_height : 8;
  return text_state.big_height ? text_state.big_height : 8;
}

int text_strwidth(ui_font_t font, const char *text) {
  if (!text || !*text) return 0;
  return strnwidth_impl(font, text, (int)strlen(text));
}

// ── draw_text / draw_text_small ───────────────────────────────────────────────

void draw_text(ui_font_t font, const char *text, int x, int y, uint32_t col) {
  if (!text || !*text || !g_ui_runtime.running) return;

  int text_length = (int)strlen(text);
  if (text_length > MAX_TEXT_LENGTH) text_length = MAX_TEXT_LENGTH;

  // Two static vertex buffers — one per atlas.
  // For FONT_SMALL (has_small): all chars go to buf_small.
  // For FONT_SYSTEM (has_small): chars 0-127 → buf_big, 128-255 → buf_small.
  // Without has_small: all chars go to buf_big.
  static text_vertex_t buf_big  [MAX_TEXT_LENGTH * VERTICES_PER_CHAR];
  static text_vertex_t buf_small[MAX_TEXT_LENGTH * VERTICES_PER_CHAR];
  int vc_big = 0, vc_small = 0;

  int sw = (font == FONT_SMALL && text_state.has_small && text_state.small_space)
           ? text_state.small_space
           : (text_state.big_space ? text_state.big_space : 3);
  int lh = (font == FONT_SMALL && text_state.has_small && text_state.small_line)
           ? text_state.small_line
           : (text_state.big_line ? text_state.big_line : 12);

  int cursor_x = x;
  for (int i = 0; i < text_length; i++) {
    unsigned char c = (unsigned char)text[i];
    if (c == ' ')  { cursor_x += sw; continue; }
    if (c == '\n') { cursor_x = x; y += lh; continue; }

    glyph_metrics_t *met;
    font_atlas_t    *atlas = atlas_for_font(font, c, &met);

    bool use_small = (text_state.has_small && atlas == &text_state.small);
    text_vertex_t *buf = use_small ? buf_small : buf_big;
    int           *vc  = use_small ? &vc_small  : &vc_big;

    *vc += emit_char_verts(buf + *vc, cursor_x, y, c, col, atlas, met);
    cursor_x += met->advance[c];
  }

  if (vc_big > 0)
    flush_batch(&text_state.big,   buf_big,   vc_big);
  if (vc_small > 0)
    flush_batch(&text_state.small, buf_small, vc_small);
}

void draw_text_clipped(ui_font_t font, const char *text,
                       rect_t const *viewport, uint32_t col, uint32_t flags) {
  if (!text || !*text || !g_ui_runtime.running || !viewport) return;
  int cell_h = text_char_height(font);
  int x = viewport->x;
  int y = viewport->y + (viewport->h - cell_h) / 2;
  if (flags & TEXT_ALIGN_RIGHT)
    x = viewport->x + viewport->w - text_strwidth(font, text);
  else if (flags & TEXT_PADDING_LEFT)
    x += WIN_PADDING;
  draw_text(font, text, x, y, col);
}

// ── Legacy FONT_SYSTEM wrappers ───────────────────────────────────────────────

void draw_text_small(const char *text, int x, int y, uint32_t col) {
  draw_text(FONT_SYSTEM, text, x, y, col);
}

void draw_text_small_clipped(const char *text, rect_t const *viewport,
                              uint32_t col, uint32_t flags) {
  draw_text_clipped(FONT_SYSTEM, text, viewport, col, flags);
}

// ── calc_text_height ──────────────────────────────────────────────────────────

int calc_text_height(const char *text, int width) {
  if (!text || !*text || width <= 0 || !text_state.big_height) return 0;
  int lines = 1, cx = 0;
  for (const char *p = text; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c == '\n') { lines++; cx = 0; continue; }
    if (c == ' ')  { cx += get_space_width(); continue; }
    int cw = char_advance(c);
    if (cx + cw > width) { lines++; cx = cw; }
    else                 { cx += cw; }
  }
  return lines * get_line_height();
}

// ── draw_text_wrapped ─────────────────────────────────────────────────────────

void draw_text_wrapped(const char *text, rect_t const *viewport, uint32_t col) {
  if (!text || !*text || !g_ui_runtime.running || !viewport) return;
  if (!text_state.big_height) return;

  static text_vertex_t buf_big  [MAX_TEXT_LENGTH * VERTICES_PER_CHAR];
  static text_vertex_t buf_small[MAX_TEXT_LENGTH * VERTICES_PER_CHAR];
  int vc_big = 0, vc_small = 0;
  int lh = get_line_height(), sw = get_space_width();

  int x = viewport->x, y = viewport->y, w = viewport->w;
  int cx = x, cy = y;

  for (const char *p = text;
       *p && vc_big   < MAX_TEXT_LENGTH * VERTICES_PER_CHAR - VERTICES_PER_CHAR
          && vc_small < MAX_TEXT_LENGTH * VERTICES_PER_CHAR - VERTICES_PER_CHAR;
       p++) {
    unsigned char c = (unsigned char)*p;
    if (c == '\n') { cx = x; cy += lh; continue; }
    if (c == ' ')  { cx += sw; continue; }

    int cw = char_advance(c);
    if (cx + cw > x + w) { cx = x; cy += lh; }

    glyph_metrics_t *met;
    font_atlas_t    *atlas = atlas_for_font(FONT_SYSTEM, c, &met);

    bool use_small = (text_state.has_small && atlas == &text_state.small);
    text_vertex_t *buf = use_small ? buf_small : buf_big;
    int           *vc  = use_small ? &vc_small  : &vc_big;

    *vc += emit_char_verts(buf + *vc, cx, cy, c, col, atlas, met);
    cx += cw;
  }

  if (vc_big > 0)
    flush_batch(&text_state.big,   buf_big,   vc_big);
  if (vc_small > 0)
    flush_batch(&text_state.small, buf_small, vc_small);
}

// ── shutdown_text_rendering ───────────────────────────────────────────────────

void shutdown_text_rendering(void) {
  R_DeleteTexture((uint32_t)text_state.big.texture.id);
  text_state.big.texture.id = 0;
  R_MeshDestroy(&text_state.big.mesh);

  if (text_state.has_small) {
    R_DeleteTexture((uint32_t)text_state.small.texture.id);
    text_state.small.texture.id = 0;
    R_MeshDestroy(&text_state.small.mesh);
  }

  memset(&text_state, 0, sizeof(text_state));
}

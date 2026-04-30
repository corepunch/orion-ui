// Text rendering implementation — dual-font design.
//
// Two named atlases at UI_WINDOW_SCALE == 1:
//   big   = ChiKareGo2 (16x16 cells, foNT metrics) — FONT_SYSTEM chrome
//   small = Geneva9 / SmallFont (8x8 cells)        — FONT_SMALL content + icons
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
  int8_t  x0[256];       // bitmap box left offset (can be negative)
  uint8_t draw_w[256];   // bitmap width in pixels
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

// ── Load one atlas from a PNG file ───────────────────────────────────────────
//
// first_char/last_char: range of char codes to populate metrics for.
// Requires foNT metadata chunk in PNG; fails if not present.

static bool load_atlas(font_atlas_t *atlas, glyph_metrics_t *met,
                       const char *path,
                       int first_char, int last_char) {
  // ── Load pixel data ───────────────────────────────────────────────────────
  int img_w = 0, img_h = 0;
  uint8_t *rgba = load_image(path, &img_w, &img_h);
  if (!rgba) {
    printf("text: failed to load font image: %s\n", path);
    return false;
  }

  // ── Cell layout will come from foNT chunk ─────────────────────────────────
  int cell_w = 16, cell_h = 16;      // placeholder; will be set from foNT
  int chars_per_row = img_w / cell_w;
  int baseline = cell_h;

  // ── Read foNT chunk (required) ─────────────────────────────────────────────
  TinyPngFontInfo fi = {0};
  TinyPngGlyph *glyphs = NULL;
  size_t raw_sz = 0;
  unsigned char *raw = read_file(path, &raw_sz);
  if (!raw || tiny_png_read_font_chunk(raw, raw_sz, &fi, &glyphs) != 1) {
    printf("text: font %s missing required foNT chunk\n", path);
    image_free(rgba);
    if (raw) free(raw);
    return false;
  }
  free(raw);
  cell_w        = fi.cell_w;
  cell_h        = fi.cell_h;
  if (cell_w <= 0 || cell_h <= 0 || (img_w % cell_w) != 0 || (img_h % cell_h) != 0) {
    printf("text: font %s has invalid cell dimensions (%dx%d) for image (%dx%d)\n",
           path, cell_w, cell_h, img_w, img_h);
    if (glyphs) free(glyphs);
    image_free(rgba);
    return false;
  }
  chars_per_row = img_w / cell_w;
  baseline      = fi.baseline;

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

  // ── Populate glyph metrics from foNT ──────────────────────────────────────
  for (int c = first_char; c <= last_char; c++) {
    if (c >= fi.first_char && c < fi.first_char + fi.num_chars) {
      int idx = c - fi.first_char;
      met->x0[c]      = glyphs[idx].x0;
      met->draw_w[c]  = glyphs[idx].w;
      met->advance[c] = glyphs[idx].advance;
    } else {
      // Char outside foNT range — use defaults
      met->x0[c]      = 0;
      met->draw_w[c]  = (uint8_t)cell_w;
      met->advance[c] = (uint8_t)cell_w;
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

// ── init_text_rendering ───────────────────────────────────────────────────────

void init_text_rendering(void) {
  memset(&text_state, 0, sizeof(text_state));

  const char *exe = ui_get_exe_dir();
  char small_path[4096], chicago_path[4096], geneva9_path[4096], geneva12_path[4096];
  snprintf(small_path,  sizeof(small_path),  "%s/../share/orion/SmallFont.png",  exe);
  snprintf(chicago_path,  sizeof(chicago_path),  "%s/../share/orion/Chicago-12.png", exe);
  snprintf(geneva9_path, sizeof(geneva9_path), "%s/../share/orion/Geneva-9.png", exe);
  snprintf(geneva12_path, sizeof(geneva12_path), "%s/../share/orion/Geneva-12.png", exe);

#if UI_WINDOW_SCALE == 1
  // At native (1:1) scale: ChiKareGo2 for chrome (FONT_SYSTEM), Geneva9 /
  // SmallFont for content (FONT_SMALL) plus icon chars (128-255).
  bool chicago_ok = load_atlas(&text_state.big, &text_state.big_met,
                              chicago_path, 0, 255);
  if (chicago_ok) {
    text_state.big_height = text_state.big.cell_h;
    text_state.big_line   = text_state.big.cell_h + 4;
    text_state.big_space  = text_state.big_met.advance[' ']
                            ? text_state.big_met.advance[' '] : 5;

    // Try Geneva-9 first, then fall back to the older Geneva-12/SmallFont atlases.
    bool geneva_ok = load_atlas(&text_state.small, &text_state.small_met,
                                geneva9_path, 0, 255);
    if (!geneva_ok)
      geneva_ok = load_atlas(&text_state.small, &text_state.small_met,
                             geneva12_path, 0, 255);
    if (!geneva_ok)
      geneva_ok = load_atlas(&text_state.small, &text_state.small_met,
                             small_path,   0, 255);
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

  // Default (scale>=2 or ChiKareGo2 unavailable): SmallFont for chrome, with
  // Geneva-9 still preferred for FONT_SMALL content when the atlas exists.
  bool font_ok = load_atlas(&text_state.big, &text_state.big_met,
                             small_path, 0, 255);
  if (font_ok) {
    int h  = text_state.big.cell_h;
    int sp = text_state.big_met.advance[' '] ? text_state.big_met.advance[' '] : 3;
    text_state.big_height = text_state.small_height = h;
    text_state.big_line   = text_state.small_line   = h + 4;
    text_state.big_space  = text_state.small_space  = sp;

    bool geneva_ok = load_atlas(&text_state.small, &text_state.small_met,
                                geneva9_path, 0, 255);
    if (geneva_ok) {
      text_state.has_small = true;
      text_state.small_height = text_state.small.cell_h;
      text_state.small_line   = text_state.small.cell_h + 4;
      text_state.small_space  = text_state.small_met.advance[' ']
                                ? text_state.small_met.advance[' '] : 3;
    } else {
      text_state.has_small = false;
    }
    printf("text: SmallFont (%dx%d)%s\n", text_state.big.cell_w, h,
           geneva_ok ? " + Geneva-9" : "");
    return;
  }

  fprintf(stderr, "error: failed to load any usable share/orion font atlases.\n"
                  "       Tried:\n"
                  "         %s\n"
                  "         %s\n"
                  "         %s\n"
                  "         %s\n"
                  "       Assets may be missing, corrupted, or invalid.\n"
                  "       Please verify the share/orion directory next to the executable\n"
                  "       (looked in %s/../share/orion/).\n",
                  chicago_path, geneva9_path, geneva12_path, small_path, exe);
  exit(1);
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
  // Glyphs are stored left-aligned in cells; x0 is a pen offset for rendering, not texture coords.
  float u1 = cx0                 / tw;
  float v1 = cy0                 / th;
  float u2 = (cx0 + dw)          / tw;
  float v2 = (cy0 + dh)          / th;

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

int text_strnwidth(ui_font_t font, const char *text, int len) {
  if (!text) return 0;
  int slen = (int)strlen(text);
  if (len > slen) len = slen;
  return strnwidth_impl(font, text, len);
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
  if (flags & TEXT_ALIGN_CENTER)
    x = viewport->x + (viewport->w - text_strwidth(font, text)) / 2;
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

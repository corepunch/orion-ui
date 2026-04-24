// Text rendering implementation
// Loads glyph atlases from PNG files (SmallFont.png, ChiKareGo2.png).
// SmallFont (8×8 cells, 256 chars) is used when UI_WINDOW_SCALE > 1.
// ChiKareGo2 (16×16 cells, foNT metrics) is used when UI_WINDOW_SCALE == 1;
// the icon chars (128-255) are then drawn from a separate SmallFont atlas.

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
  font_atlas_t  text;         // text font: SmallFont (all 256) or ChiKareGo2 (0-127)
  font_atlas_t  icons;        // icon atlas: SmallFont (128-255), used only when has_icons
  glyph_metrics_t text_met;   // metrics for text atlas
  glyph_metrics_t icons_met;  // metrics for icons atlas
  bool          has_icons;    // true when icons atlas is separate from text atlas
  int           char_height;
  int           line_height;
  int           space_width;
} text_state = {0};

// ── Dynamic metric accessors ──────────────────────────────────────────────────

int get_char_height(void) { return text_state.char_height ? text_state.char_height : 8;  }
int get_line_height(void) { return text_state.line_height ? text_state.line_height : 12; }
int get_space_width(void) { return text_state.space_width ? text_state.space_width : 3;  }

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
  // Expect cell_w × cell_h consistent with the file; default to 8×8 for
  // SmallFont and 16×16 for ChiKareGo2. We detect by examining the foNT chunk.
  int cell_w = (img_w >= 256) ? 16 : 8;
  int cell_h = (img_h >= 256) ? 16 : 8;
  int chars_per_row = img_w / cell_w;
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
      uint8_t adv = fi.glyphs[idx].advance;
      if (full_cell) {
        met->x0[c]     = 0;
        met->draw_w[c] = (uint8_t)cell_w;
        met->advance[c] = adv ? adv : (uint8_t)cell_w;
      } else {
        scan_cell_metrics(red, img_w, cx0, cy0, cell_w, cell_h,
                          &met->x0[c], &met->draw_w[c], &met->advance[c]);
        if (!met->advance[c] && adv) met->advance[c] = adv;
      }
    } else {
      // No foNT — scan pixels
      if (full_cell) {
        // For full-cell mode without metadata, use fixed cell width
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

  (void)baseline;
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
    text_state.text_met.advance[c] = 0;
    text_state.text_met.x0[c]      = 0xff;
    uint8_t hi = 0;
    for (int y = 0; y < gh; y++) {
      int byte = console_font_6x8[c * gh + y];
      for (int x = 0; x < 6; x++) {
        if ((byte >> (5 - x)) & 1) {
          buf[(ay + y) * tex_sz + ax + x] = 255;
          if (x < text_state.text_met.x0[c]) text_state.text_met.x0[c] = (uint8_t)x;
          if (x + 2 > hi) hi = (uint8_t)(x + 2);
        }
      }
    }
    if (hi == 0) { text_state.text_met.x0[c] = 0; hi = 3; } // space
    text_state.text_met.draw_w[c]  = hi - text_state.text_met.x0[c];
    text_state.text_met.advance[c] = text_state.text_met.draw_w[c];
  }
  memcpy(buf + tex_sz * tex_sz / 2, icons_bits, (size_t)(tex_sz * tex_sz / 2));
  for (int i = 128; i < 256; i++) {
    text_state.text_met.x0[i]      = 0;
    text_state.text_met.draw_w[i]  = 8;
    text_state.text_met.advance[i] = 8;
  }

  text_state.text.texture.width  = tex_sz;
  text_state.text.texture.height = tex_sz;
  text_state.text.texture.format = GL_RED;
  R_AllocateFontTexture(&text_state.text.texture, buf);
  free(buf);

  text_state.text.cell_w        = gw;
  text_state.text.cell_h        = gh;
  text_state.text.chars_per_row = cpr;
  init_atlas_mesh(&text_state.text);

  text_state.char_height = gh;
  text_state.line_height = gh + 4;
  text_state.space_width = 3;
}

// ── init_text_rendering ───────────────────────────────────────────────────────

void init_text_rendering(void) {
  memset(&text_state, 0, sizeof(text_state));

  const char *exe = ui_get_exe_dir();
  char small_path[4096], chika_path[4096];
  snprintf(small_path, sizeof(small_path),
           "%s/../share/orion/SmallFont.png",  exe);
  snprintf(chika_path, sizeof(chika_path),
           "%s/../share/orion/ChiKareGo2.png", exe);

#if UI_WINDOW_SCALE == 1
  // At native (1:1) scale: prefer ChiKareGo2 for text, SmallFont for icons.
  bool chika_ok = load_atlas(&text_state.text, &text_state.text_met,
                              chika_path, 0, 255, /*full_cell=*/true);
  if (chika_ok) {
    bool icons_ok = load_atlas(&text_state.icons, &text_state.icons_met,
                                small_path, 128, 255, /*full_cell=*/false);
    text_state.has_icons    = icons_ok;
    text_state.char_height  = text_state.text.cell_h;
    text_state.line_height  = text_state.text.cell_h + 4;
    // Space advance from foNT (char 32 = space); fall back to 5px.
    text_state.space_width  = text_state.text_met.advance[' ']
                              ? text_state.text_met.advance[' '] : 5;
    printf("text: ChiKareGo2 loaded (%dx%d cells)%s\n",
           text_state.text.cell_w, text_state.text.cell_h,
           text_state.has_icons ? " + SmallFont icons" : "");
    return;
  }
#endif

  // Default (or ChiKareGo2 unavailable): load SmallFont for everything.
  bool small_ok = load_atlas(&text_state.text, &text_state.text_met,
                              small_path, 0, 255, /*full_cell=*/false);
  if (small_ok) {
    text_state.has_icons   = false;
    text_state.char_height = text_state.text.cell_h;
    text_state.line_height = text_state.text.cell_h + 4;
    text_state.space_width = text_state.text_met.advance[' ']
                             ? text_state.text_met.advance[' '] : 3;
    printf("text: SmallFont loaded (%dx%d cells)\n",
           text_state.text.cell_w, text_state.text.cell_h);
    return;
  }

  // Final fallback: generate atlas from hardcoded font_6x8 data.
  printf("text: PNG fonts unavailable, using built-in font_6x8\n");
  create_legacy_atlas();
}

// ── Internal helpers ──────────────────────────────────────────────────────────

// Return the atlas + metrics for char c.
static inline font_atlas_t *atlas_for(unsigned char c, glyph_metrics_t **met_out) {
  if (text_state.has_icons && c >= 128) {
    *met_out = &text_state.icons_met;
    return &text_state.icons;
  }
  *met_out = &text_state.text_met;
  return &text_state.text;
}

static inline int char_advance(unsigned char c) {
  glyph_metrics_t *m; atlas_for(c, &m); return m->advance[c];
}

// Public API: pixel width of one glyph.
int char_width(unsigned char c) {
  if (!text_state.char_height) return 0;
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

int strnwidth(const char *text, int text_length) {
  if (!text || !*text) return 0;
  if (text_length > MAX_TEXT_LENGTH) text_length = MAX_TEXT_LENGTH;
  int w = 0;
  for (int i = 0; i < text_length; i++) {
    unsigned char c = (unsigned char)text[i];
    if (c == ' ') { w += get_space_width(); continue; }
    if (c == '\n') continue;
    w += char_advance(c);
  }
  return w;
}

int strwidth(const char *text) {
  if (!text || !*text) return 0;
  return strnwidth(text, (int)strlen(text));
}

// ── draw_text_small ───────────────────────────────────────────────────────────

void draw_text_small(const char *text, int x, int y, uint32_t col) {
  if (!text || !*text || !g_ui_runtime.running) return;

  int text_length = (int)strlen(text);
  if (text_length > MAX_TEXT_LENGTH) text_length = MAX_TEXT_LENGTH;

  // Two static buffers: one per atlas.  For the common single-atlas case
  // (has_icons == false) only buf_text is used.
  static text_vertex_t buf_text [MAX_TEXT_LENGTH * VERTICES_PER_CHAR];
  static text_vertex_t buf_icons[MAX_TEXT_LENGTH * VERTICES_PER_CHAR];
  int vc_text = 0, vc_icons = 0;

  int cursor_x = x;
  for (int i = 0; i < text_length; i++) {
    unsigned char c = (unsigned char)text[i];
    if (c == ' ')  { cursor_x += get_space_width(); continue; }
    if (c == '\n') { cursor_x = x; y += get_line_height(); continue; }

    glyph_metrics_t *met;
    font_atlas_t    *atlas = atlas_for(c, &met);
    text_vertex_t   *buf;
    int             *vc;

    if (text_state.has_icons && c >= 128) {
      buf = buf_icons; vc = &vc_icons;
    } else {
      buf = buf_text;  vc = &vc_text;
    }
    *vc += emit_char_verts(buf + *vc, cursor_x, y, c, col, atlas, met);
    cursor_x += met->advance[c];
  }

  flush_batch(&text_state.text,  buf_text,  vc_text);
  if (text_state.has_icons)
    flush_batch(&text_state.icons, buf_icons, vc_icons);
}

// ── calc_text_height ──────────────────────────────────────────────────────────

int calc_text_height(const char *text, int width) {
  if (!text || !*text || width <= 0 || !text_state.char_height) return 0;
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
  if (!text_state.char_height) return;

  static text_vertex_t buf_text [MAX_TEXT_LENGTH * VERTICES_PER_CHAR];
  static text_vertex_t buf_icons[MAX_TEXT_LENGTH * VERTICES_PER_CHAR];
  int vc_text = 0, vc_icons = 0;
  int lh = get_line_height(), sw = get_space_width();

  int x = viewport->x, y = viewport->y, w = viewport->w;
  int cx = x, cy = y;

  for (const char *p = text;
       *p && vc_text  < MAX_TEXT_LENGTH * VERTICES_PER_CHAR - VERTICES_PER_CHAR
          && vc_icons < MAX_TEXT_LENGTH * VERTICES_PER_CHAR - VERTICES_PER_CHAR;
       p++) {
    unsigned char c = (unsigned char)*p;
    if (c == '\n') { cx = x; cy += lh; continue; }
    if (c == ' ')  { cx += sw; continue; }

    int cw = char_advance(c);
    if (cx + cw > x + w) { cx = x; cy += lh; }

    glyph_metrics_t *met;
    font_atlas_t    *atlas = atlas_for(c, &met);
    text_vertex_t   *buf;
    int             *vc;

    if (text_state.has_icons && c >= 128) {
      buf = buf_icons; vc = &vc_icons;
    } else {
      buf = buf_text;  vc = &vc_text;
    }
    *vc += emit_char_verts(buf + *vc, cx, cy, c, col, atlas, met);
    cx += cw;
  }

  flush_batch(&text_state.text,  buf_text,  vc_text);
  if (text_state.has_icons)
    flush_batch(&text_state.icons, buf_icons, vc_icons);
}

// ── shutdown_text_rendering ───────────────────────────────────────────────────

void shutdown_text_rendering(void) {
  R_DeleteTexture((uint32_t)text_state.text.texture.id);
  text_state.text.texture.id = 0;
  R_MeshDestroy(&text_state.text.mesh);

  if (text_state.has_icons) {
    R_DeleteTexture((uint32_t)text_state.icons.texture.id);
    text_state.icons.texture.id = 0;
    R_MeshDestroy(&text_state.icons.mesh);
  }

  memset(&text_state, 0, sizeof(text_state));
}

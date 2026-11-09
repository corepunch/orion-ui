// Proportional UTF-8 text rendering backed by lazily populated TTF atlases.

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"
#include "user.h"
#include "font_cache.h"
#include "theme.h"
#include "style.h"
#include <orion/kernel/kernel.h>

#define MAX_TEXT_LENGTH   4096
#define VERTICES_PER_CHAR 6

#define WIN_PADDING 4

typedef struct {
  float    x, y;
  float    u, v;
  uint32_t col;
} text_vertex_t;

typedef struct {
  R_Mesh mesh;
  font_cache_t *cache;
  char path[1024];   // the face and size the atlas was built from
  float size;
} font_atlas_t;

// One atlas per face and size the style sheet asks for, made on first use.
#define TEXT_MAX_FACES 32
static struct {
  font_atlas_t faces[TEXT_MAX_FACES];
  int face_count;
  bool ready;
} text_state = {0};

static font_atlas_t g_no_face;   // before init_text_rendering(): no glyphs, default metrics

// ── Faces from the style sheet ────────────────────────────────────────────────
//
// A font is a CSS selector: the text takes the `font` of that element and part
// (`TableView::row`, `Caption`), or of :root when the element sets none. The
// value is the CSS font shorthand, `400 13px "IBM Plex Sans"`; the file is
// share/orion/fonts/<Family without spaces>-<Weight>.ttf (Regular, Medium,
// SemiBold, Bold, ...; a 400 face may also be <Family>.ttf). Sizes are desktop
// logical pixels, scaled for the platform. A missing face falls back to DejaVu.

static void init_atlas_mesh(font_atlas_t *atlas) {
  R_VertexAttrib attribs[] = {
    {0, 2, GL_FLOAT,         GL_FALSE, offsetof(text_vertex_t, x)},
    {1, 2, GL_FLOAT,         GL_FALSE, offsetof(text_vertex_t, u)},
    {2, 4, GL_UNSIGNED_BYTE, GL_TRUE,  offsetof(text_vertex_t, col)},
  };
  R_MeshInit(&atlas->mesh, attribs, 3, sizeof(text_vertex_t), GL_TRIANGLES);
}

static float text_platform_scale(void) {
#if defined(AX_PLATFORM_IOS)
  return 1.2f;
#elif UI_WINDOW_SCALE == 1
  return 1.0f;
#else
  return 2.0f / 3.0f;
#endif
}

static bool text_file_exists(const char *path) {
  FILE *f = fopen(path, "rb");
  if (f) fclose(f);
  return f != NULL;
}

static void text_default_face(char *path, size_t n, float *size) {
  snprintf(path, n, "%s/../share/orion/fonts/DejaVuSans.ttf", ui_get_exe_dir());
  *size = 12.0f * text_platform_scale();
}

// Resolves a CSS font shorthand to a font file and size.
static bool text_css_face(const char *value, char *path, size_t n, float *size) {
  char buf[256]; snprintf(buf, sizeof(buf), "%s", value);
  int weight = 400; float px = 0;
  char *c = buf, family[96] = "";
  while (*c) {
    while (*c == ' ') c++;
    if (!*c) break;
    if (*c == '"' || *c == '\'') {   // the quoted family ends the shorthand
      char quote = *c++, *end = strchr(c, quote);
      if (end) *end = 0;
      snprintf(family, sizeof(family), "%s", c);
      break;
    }
    char *end = c; while (*end && *end != ' ') end++;
    char saved = *end; *end = 0;
    char *unit;
    double v = strtod(c, &unit);
    if (!strcmp(c, "bold")) weight = 700;
    else if (!strcmp(c, "normal")) weight = 400;
    else if (unit != c && !strcmp(unit, "px")) px = (float)v;
    else if (unit != c && !*unit) weight = (int)v;
    else { snprintf(family, sizeof(family), "%s%s", c, saved ? end + 1 : ""); break; }
    *end = saved; c = saved ? end + 1 : end;
  }
  if (!family[0] || px <= 0) return false;
  char stem[96]; size_t k = 0;
  for (const char *f = family; *f && k + 1 < sizeof(stem); f++) if (*f != ' ') stem[k++] = *f;
  stem[k] = 0;
  static const char *const weights[] = {"Thin", "ExtraLight", "Light", "Regular", "Medium", "SemiBold", "Bold", "ExtraBold", "Black"};
  int index = weight / 100 - 1;
  if (index < 0) index = 0;
  if (index > 8) index = 8;
  const char *dir = ui_get_exe_dir();
  snprintf(path, n, "%s/../share/orion/fonts/%s-%s.ttf", dir, stem, weights[index]);
  if (!text_file_exists(path) && index == 3) snprintf(path, n, "%s/../share/orion/fonts/%s.ttf", dir, stem);
  if (!text_file_exists(path)) {
    fprintf(stderr, "[text] font %s %d is not installed: %s\n", family, weight, path);
    fflush(stderr);
    return false;
  }
  *size = px * text_platform_scale();
  return true;
}

static font_atlas_t *text_face(const char *path, float size) {
  for (int i = 0; i < text_state.face_count; i++)
    if (text_state.faces[i].size == size && !strcmp(text_state.faces[i].path, path)) return &text_state.faces[i];
  if (text_state.face_count == TEXT_MAX_FACES) {
    fprintf(stderr, "[text] more than %d faces; %s at %g uses the first\n", TEXT_MAX_FACES, path, size);
    fflush(stderr);
    return &text_state.faces[0];
  }
  font_atlas_t *atlas = &text_state.faces[text_state.face_count];
  atlas->cache = font_cache_create(path, size);
  if (!atlas->cache) return NULL;
  snprintf(atlas->path, sizeof(atlas->path), "%s", path);
  atlas->size = size;
  init_atlas_mesh(atlas);
  text_state.face_count++;
  return atlas;
}

// Selector → face, until the style sheet changes.
#define TEXT_FONT_SLOTS 256
static struct { ui_font_t selector; unsigned generation; font_atlas_t *atlas; } g_font_slots[TEXT_FONT_SLOTS];

static font_atlas_t *font_for_role(ui_font_t font) {
  if (!text_state.ready) return &g_no_face;
  if (!font) font = "Text";
  unsigned h = (unsigned)(((uintptr_t)font >> 3) % TEXT_FONT_SLOTS);
  if (g_font_slots[h].selector == font && g_font_slots[h].generation == g_theme_style_generation && g_font_slots[h].atlas)
    return g_font_slots[h].atlas;
  char value[256], path[1024]; float size = 0;
  font_atlas_t *atlas = NULL;
  if ((ui_string(font, "font", value, sizeof(value)) || ui_string(":root", "font", value, sizeof(value))) &&
      text_css_face(value, path, sizeof(path), &size))
    atlas = text_face(path, size);
  if (!atlas) {
    text_default_face(path, sizeof(path), &size);
    atlas = text_face(path, size);
  }
  if (!atlas) {
    fprintf(stderr, "[text] required font asset failed to load: %s\n", path);
    fflush(stderr);
    exit(1);
  }
  g_font_slots[h].selector = font;
  g_font_slots[h].generation = g_theme_style_generation;
  g_font_slots[h].atlas = atlas;
  return atlas;
}

static inline int font_space(ui_font_t font) {
  font_atlas_t *atlas = font_for_role(font);
  const font_cache_glyph_t *glyph = font_cache_get_glyph(atlas->cache, ' ');
  return glyph && glyph->advance > 0 ? glyph->advance : 3;
}

// Advances in 1/64 logical pixel: summed before rounding, as typesetters do.
static inline int font_space64(ui_font_t font) {
  const font_cache_glyph_t *glyph = font_cache_get_glyph(font_for_role(font)->cache, ' ');
  return glyph && glyph->advance64 > 0 ? glyph->advance64 : 3 * 64;
}

static inline int font_line(ui_font_t font) {
  font_atlas_t *atlas = font_for_role(font);
  int height = font_cache_line_height(atlas->cache);
  return height > 0 ? height : 12;
}

// ── init_text_rendering ───────────────────────────────────────────────────────

void init_text_rendering(void) {
  memset(&text_state, 0, sizeof(text_state));
  memset(g_font_slots, 0, sizeof(g_font_slots));
  text_state.ready = true;
  printf("text: body line %d, caption line %d\n", font_line("Text"), font_line("Caption"));
}

// ── Internal helpers ──────────────────────────────────────────────────────────

static uint32_t utf8_codepoint(const char *text, int remaining, int *length) {
  const unsigned char *s = (const unsigned char *)text;
  *length = 1;
  if (remaining <= 0 || s[0] < 0x80) return s[0];
  if ((s[0] & 0xE0) == 0xC0 && remaining >= 2 && (s[1] & 0xC0) == 0x80) {
    *length = 2;
    return ((uint32_t)(s[0] & 0x1F) << 6) | (uint32_t)(s[1] & 0x3F);
  }
  if ((s[0] & 0xF0) == 0xE0 && remaining >= 3 &&
      (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
    *length = 3;
    return ((uint32_t)(s[0] & 0x0F) << 12) |
           ((uint32_t)(s[1] & 0x3F) << 6) | (uint32_t)(s[2] & 0x3F);
  }
  if ((s[0] & 0xF8) == 0xF0 && remaining >= 4 &&
      (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 &&
      (s[3] & 0xC0) == 0x80) {
    *length = 4;
    return ((uint32_t)(s[0] & 0x07) << 18) |
           ((uint32_t)(s[1] & 0x3F) << 12) |
           ((uint32_t)(s[2] & 0x3F) << 6) | (uint32_t)(s[3] & 0x3F);
  }
  return 0xFFFD;
}

static inline int codepoint_advance(ui_font_t font, uint32_t codepoint) {
  const font_cache_glyph_t *glyph =
    font_cache_get_glyph(font_for_role(font)->cache, codepoint);
  return glyph ? glyph->advance : 0;
}

static inline int codepoint_advance64(ui_font_t font, uint32_t codepoint) {
  const font_cache_glyph_t *glyph = font_cache_get_glyph(font_for_role(font)->cache, codepoint);
  return glyph ? glyph->advance64 : 0;
}

static inline int fallback_char_advance(uint32_t c, ui_font_t font) {
  if (c == '\n') return 0;
  if (c == ' ') {
    int sw = font_space(font);
    return sw > 0 ? sw : 3;
  }

  int ch = text_char_height(font);
  int aw = ch / 2 + 2;
  return aw > 0 ? aw : 6;
}

static inline int wrapped_line_advance(ui_font_t font) {
  int adv = text_char_height(font);
  if (adv <= 0) adv = font_line("Text");
  return adv > 0 ? adv : 1;
}

// In 1/64 logical pixel.
static inline int wrap_char_advance(ui_font_t font, uint32_t c) {
  if (c == '\n') return 0;
  if (c == ' ') return font_space64(font);
  if (font_for_role(font)->cache) return codepoint_advance64(font, c);
  return fallback_char_advance(c, font) * 64;
}

// ── Render a filled vertex batch for one atlas ────────────────────────────────

extern void push_sprite_args(int tex, int x, int y, int w, int h, float alpha);

static void flush_batch(font_atlas_t *atlas, text_vertex_t *buf, int count) {
  if (count == 0) return;
  R_Texture texture = {
    .id = font_cache_texture(atlas->cache),
    .width = font_cache_texture_width(atlas->cache),
    .height = font_cache_texture_height(atlas->cache),
    .format = GL_RED,
  };
  R_SetBlendMode(true);
  push_sprite_args((int)texture.id, 0, 0, 1, 1, 1.0f);
  R_TextureBind(&texture);
  R_MeshDrawDynamic(&atlas->mesh, buf, (size_t)count);
}

// ── Build vertices for one character ─────────────────────────────────────────

static int emit_char_verts(text_vertex_t *buf, float cursor_x, int y,
                           uint32_t codepoint, uint32_t col,
                           font_atlas_t *atlas) {
  const font_cache_glyph_t *glyph = font_cache_get_glyph(atlas->cache, codepoint);
  if (!glyph) return 0;
  int cx0 = glyph->atlas_x;
  int cy0 = glyph->atlas_y;
  float bitmap_scale = font_cache_bitmap_scale(atlas->cache);
  float dw = glyph->width / bitmap_scale;
  float dh = glyph->height / bitmap_scale;
  if (dw == 0) return 0;

  float tw = (float)font_cache_texture_width(atlas->cache);
  float th = (float)font_cache_texture_height(atlas->cache);
  float u1 = cx0                 / tw;
  float v1 = cy0                 / th;
  float u2 = (cx0 + glyph->width)  / tw;
  float v2 = (cy0 + glyph->height) / th;

  // Each glyph starts on a device pixel, so hinted bitmaps stay sharp.
  float x = roundf(cursor_x * bitmap_scale) / bitmap_scale + glyph->x_offset / bitmap_scale;
  float draw_y = y + glyph->y_offset / bitmap_scale;
  buf[0] = (text_vertex_t){ x,    draw_y,    u1, v1, col };
  buf[1] = (text_vertex_t){ x,    draw_y+dh, u1, v2, col };
  buf[2] = (text_vertex_t){ x+dw, draw_y,    u2, v1, col };
  buf[3] = (text_vertex_t){ x,    draw_y+dh, u1, v2, col };
  buf[4] = (text_vertex_t){ x+dw, draw_y+dh, u2, v2, col };
  buf[5] = (text_vertex_t){ x+dw, draw_y,    u2, v1, col };
  return VERTICES_PER_CHAR;
}

// ── strwidth / strnwidth ──────────────────────────────────────────────────────



// Internal font-parameterised width measurement.
static int strnwidth_impl(ui_font_t font, const char *text, int len) {
  if (!text || !*text) return 0;
  if (len > MAX_TEXT_LENGTH) len = MAX_TEXT_LENGTH;
  int sw = font_space64(font);
  int w = 0;
  for (int i = 0; i < len;) {
    int length;
    uint32_t codepoint = utf8_codepoint(text + i, len - i, &length);
    i += length;
    if (codepoint == ' ') { w += sw; continue; }
    if (codepoint == '\n') continue;
    w += codepoint_advance64(font, codepoint);
  }
  return (w + 32) >> 6;
}

// ── New explicit-font metric API ──────────────────────────────────────────────

int text_char_height(ui_font_t font) {
  return font_line(font);
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

int text_ellipsize(ui_font_t font, const char *text, int max_w, char *out, size_t n) {
  if (!out || n == 0) return 0;
  out[0] = '\0';
  if (!text || max_w <= 0) return 0;
  if (text_strwidth(font, text) <= max_w) { snprintf(out, n, "%s", text); return text_strwidth(font, out); }
  static const char kEllipsis[] = "...";
  int budget = max_w - text_strwidth(font, kEllipsis), len = 0, step;
  if (budget <= 0) { snprintf(out, n, "%s", kEllipsis); return text_strwidth(font, out); }
  // Grow the kept prefix one code point at a time while it still fits.
  for (int total = (int)strlen(text); len < total && (size_t)len + sizeof(kEllipsis) < n; len += step) {
    utf8_codepoint(text + len, total - len, &step);
    if (step < 1 || strnwidth_impl(font, text, len + step) > budget) break;
  }
  snprintf(out, n, "%.*s%s", len, text, kEllipsis);
  return text_strwidth(font, out);
}

void draw_text_ellipsized(ui_font_t font, const char *text, int x, int y, int max_w, uint32_t col) {
  char fitted[512];
  text_ellipsize(font, text, max_w, fitted, sizeof(fitted));
  draw_text(font, fitted, x, y, col);
}

// ── draw_text / draw_text_small ───────────────────────────────────────────────

void draw_text(ui_font_t font, const char *text, int x, int y, uint32_t col) {
  if (!text || !*text || !g_ui_runtime.running) return;

  int text_length = (int)strlen(text);
  if (text_length > MAX_TEXT_LENGTH) text_length = MAX_TEXT_LENGTH;

  // Each font role has its own atlas; a string is one batch of that atlas.
  static text_vertex_t buf[MAX_TEXT_LENGTH * VERTICES_PER_CHAR];
  font_atlas_t *atlas = font_for_role(font);
  int vc = 0;

  int sw = font_space64(font);
  int lh = font_line(font);

  int pen = x * 64;
  for (int i = 0; i < text_length;) {
    int length;
    uint32_t codepoint = utf8_codepoint(text + i, text_length - i, &length);
    i += length;
    if (codepoint == ' ')  { pen += sw; continue; }
    if (codepoint == '\n') { pen = x * 64; y += lh; continue; }

    vc += emit_char_verts(buf + vc, pen / 64.0f, y, codepoint, col, atlas);
    pen += codepoint_advance64(font, codepoint);
  }

  if (vc > 0) flush_batch(atlas, buf, vc);
}

void draw_text_clipped(ui_font_t font, const char *text,
                       irect16_t const *viewport, uint32_t col, uint32_t flags) {
  if (!text || !*text || !g_ui_runtime.running || !viewport) return;
  int cell_h = text_char_height(font);
  int x = viewport->x;
  int y = viewport->y + (viewport->h - cell_h) / 2;
  if (flags & TEXT_ALIGN_RIGHT)
    x = viewport->x + viewport->w - text_strwidth(font, text);
  else if (flags & TEXT_ALIGN_CENTER)
    x = viewport->x + (viewport->w - text_strwidth(font, text)) / 2;
  else if (flags & TEXT_PADDING_LEFT)
    x += WIN_PADDING;
  draw_text(font, text, x, y, col);
}


// ── calc_text_height ──────────────────────────────────────────────────────────

text_wrap_result_t text_wrap_layout_font(ui_font_t font, const char *text,
                                         irect16_t const *viewport,
                                         uint32_t col, bool draw) {
  text_wrap_result_t out = {0, 0, false};
  if (!text || !*text || !viewport || viewport->w <= 0) return out;
  if (draw && (!g_ui_runtime.running || !font_for_role(font)->cache)) return out;

  static text_vertex_t buf[MAX_TEXT_LENGTH * VERTICES_PER_CHAR];
  int vc = 0;
  int x = viewport->x;
  int y = viewport->y;
  int w = viewport->w;
  int lh = wrapped_line_advance(font);
  // Horizontal positions in 1/64 logical pixel.
  int sw = font_space64(font);
  x *= 64; w *= 64;

  int cx = x;
  int cy = y;
  int line_w = 0;
  int max_line_w = 0;
  int lines = 1;
  font_atlas_t *cur_atlas = NULL;

#define NEW_LINE() do { if (line_w > max_line_w) max_line_w = line_w; \
                       line_w = 0; cx = x; cy += lh; lines++; out.wrapped = true; } while (0)
  int text_length = (int)strlen(text);
  bool word_start = true;
  for (int i = 0; i < text_length;) {
    int length;
    uint32_t codepoint = utf8_codepoint(text + i, text_length - i, &length);
    i += length;

    if (codepoint == '\n') { NEW_LINE(); word_start = true; continue; }

    if (codepoint == ' ') {
      if (cx + sw > x + w) NEW_LINE();
      else { cx += sw; line_w += sw; }
      word_start = true;
      continue;
    }

    // Lines break between words; only a word longer than the line breaks inside.
    if (word_start && cx > x) {
      int word_w = 0;
      for (int j = i - length, l; j < text_length; j += l) {
        uint32_t cp = utf8_codepoint(text + j, text_length - j, &l);
        if (cp == ' ' || cp == '\n') break;
        word_w += wrap_char_advance(font, cp);
      }
      if (cx + word_w > x + w) NEW_LINE();
    }
    word_start = false;

    int cw = wrap_char_advance(font, codepoint);
    if (cx + cw > x + w) NEW_LINE();

    if (draw && vc < MAX_TEXT_LENGTH * VERTICES_PER_CHAR - VERTICES_PER_CHAR) {
      font_atlas_t *atlas = font_for_role(font);
      if (cur_atlas && atlas != cur_atlas && vc > 0) {
        flush_batch(cur_atlas, buf, vc);
        vc = 0;
      }
      cur_atlas = atlas;
      vc += emit_char_verts(buf + vc, cx / 64.0f, cy, codepoint, col, atlas);
    }
    cx += cw;
    line_w += cw;
  }

#undef NEW_LINE
  if (line_w > max_line_w) max_line_w = line_w;
  out.width = (max_line_w + 63) >> 6;
  out.height = lines * lh;

  if (draw && vc > 0 && cur_atlas)
    flush_batch(cur_atlas, buf, vc);

  return out;
}

int calc_text_height_font(ui_font_t font, const char *text, int width) {
  irect16_t vp = {0, 0, width, 1};
  return text_wrap_layout_font(font, text, &vp, 0, false).height;
}

// ── draw_text_wrapped ─────────────────────────────────────────────────────────

void draw_text_wrapped_font(ui_font_t font, const char *text,
                            irect16_t const *viewport, uint32_t col) {
  (void)text_wrap_layout_font(font, text, viewport, col, true);
}

// ── shutdown_text_rendering ───────────────────────────────────────────────────

void shutdown_text_rendering(void) {
  for (int i = 0; i < text_state.face_count; i++) {
    font_cache_destroy(text_state.faces[i].cache);
    R_MeshDestroy(&text_state.faces[i].mesh);
  }
  memset(&text_state, 0, sizeof(text_state));
  memset(g_font_slots, 0, sizeof(g_font_slots));
}

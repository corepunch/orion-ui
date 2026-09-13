// Proportional UTF-8 text rendering backed by lazily populated TTF atlases.

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"
#include "user.h"
#include "font_cache.h"
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
} font_atlas_t;

static struct {
  font_atlas_t big;
  font_atlas_t small;
  font_atlas_t smallest;
} text_state = {0};

// ── Dynamic metric accessors ──────────────────────────────────────────────────
// Space and line metrics come from each TTF at its logical em size.

static inline font_atlas_t *font_for_role(ui_font_t font) {
  if (font == FONT_SMALLEST) return &text_state.smallest;
  if (font == FONT_SMALL) return &text_state.small;
  return &text_state.big;
}

static inline int font_space(ui_font_t font) {
  font_atlas_t *atlas = font_for_role(font);
  const font_cache_glyph_t *glyph = font_cache_get_glyph(atlas->cache, ' ');
  return glyph && glyph->advance > 0 ? glyph->advance : 3;
}

static inline int font_line(ui_font_t font) {
  font_atlas_t *atlas = font_for_role(font);
  int height = font_cache_line_height(atlas->cache);
  return height > 0 ? height : 12;
}

// Legacy getters: return FONT_SYSTEM (big atlas) metrics.

int get_char_height(void) { return font_line(FONT_SYSTEM); }
int get_line_height(void) { return font_line(FONT_SYSTEM); }
int get_space_width(void) { return font_space(FONT_SYSTEM); }

// ── Initialise a mesh for the given atlas ─────────────────────────────────────

static void init_atlas_mesh(font_atlas_t *atlas) {
  R_VertexAttrib attribs[] = {
    {0, 2, GL_FLOAT,         GL_FALSE, offsetof(text_vertex_t, x)},
    {1, 2, GL_FLOAT,         GL_FALSE, offsetof(text_vertex_t, u)},
    {2, 4, GL_UNSIGNED_BYTE, GL_TRUE,  offsetof(text_vertex_t, col)},
  };
  R_MeshInit(&atlas->mesh, attribs, 3, sizeof(text_vertex_t), GL_TRIANGLES);
}

static bool load_atlas(font_atlas_t *atlas, const char *path, float em_size) {
  atlas->cache = font_cache_create(path, em_size);
  if (!atlas->cache) return false;
  init_atlas_mesh(atlas);
  return true;
}

// ── init_text_rendering ───────────────────────────────────────────────────────

void init_text_rendering(void) {
  memset(&text_state, 0, sizeof(text_state));

  const char *exe = ui_get_exe_dir();
  char system_path[4096], compact_path[4096];
  snprintf(system_path, sizeof(system_path), "%s/../share/orion/fonts/NotoSans-Medium.ttf", exe);
  snprintf(compact_path, sizeof(compact_path), "%s/../share/orion/fonts/NotoSans-Regular.ttf", exe);

#if UI_WINDOW_SCALE == 1
  const float system_size = 12.0f, small_size = 12.0f, smallest_size = 9.0f;
#else
  const float system_size = 8.0f, small_size = 8.0f, smallest_size = 7.0f;
#endif
  bool loaded = load_atlas(&text_state.big, system_path, system_size);
  loaded = load_atlas(&text_state.small, compact_path, small_size) && loaded;
  loaded = load_atlas(&text_state.smallest, compact_path, smallest_size) && loaded;
  if (!loaded) {
    fprintf(stderr, "[text] required font assets failed to load:\n  %s\n  %s\n",
            system_path, compact_path);
    fflush(stderr);
    exit(1);
  }
  printf("text: lazy TTF atlases loaded system=%d small=%d smallest=%d\n",
         font_line(FONT_SYSTEM), font_line(FONT_SMALL), font_line(FONT_SMALLEST));
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

static inline int fallback_char_advance(uint32_t c, ui_font_t font) {
  if (c == '\n') return 0;
  if (c == ' ') {
    int sw = get_space_width();
    return sw > 0 ? sw : 3;
  }

  int ch = text_char_height(font);
  int aw = ch / 2 + 2;
  return aw > 0 ? aw : 6;
}

static inline int wrapped_line_advance(ui_font_t font) {
  int adv = text_char_height(font);
  if (adv <= 0) adv = font_line(FONT_SMALL);
  return adv > 0 ? adv : 1;
}

static inline int wrap_char_advance(ui_font_t font, uint32_t c) {
  if (c == '\n') return 0;
  if (c == ' ') {
    int sw = font_space(font);
    return sw > 0 ? sw : 3;
  }
  if (font_for_role(font)->cache) return codepoint_advance(font, c);
  return fallback_char_advance(c, font);
}

// Public API: pixel width of one glyph from the FONT_SYSTEM atlas.
int char_width(unsigned char c) {
  if (!text_state.big.cache) return 0;
  return codepoint_advance(FONT_SYSTEM, c);
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

static int emit_char_verts(text_vertex_t *buf, int cursor_x, int y,
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

  float x = cursor_x + glyph->x_offset / bitmap_scale;
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
  int sw = font_space(font);
  int w = 0;
  for (int i = 0; i < len;) {
    int length;
    uint32_t codepoint = utf8_codepoint(text + i, len - i, &length);
    i += length;
    if (codepoint == ' ') { w += sw; continue; }
    if (codepoint == '\n') continue;
    w += codepoint_advance(font, codepoint);
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

// ── draw_text / draw_text_small ───────────────────────────────────────────────

void draw_text(ui_font_t font, const char *text, int x, int y, uint32_t col) {
  if (!text || !*text || !g_ui_runtime.running) return;

  int text_length = (int)strlen(text);
  if (text_length > MAX_TEXT_LENGTH) text_length = MAX_TEXT_LENGTH;

  // Static vertex buffers — one per atlas.
  // FONT_SMALL and FONT_SMALLEST use their role atlases. FONT_SYSTEM uses the
  // system atlas for text and the small atlas for characters 128-255.
  static text_vertex_t buf_big  [MAX_TEXT_LENGTH * VERTICES_PER_CHAR];
  static text_vertex_t buf_small[MAX_TEXT_LENGTH * VERTICES_PER_CHAR];
  static text_vertex_t buf_smallest[MAX_TEXT_LENGTH * VERTICES_PER_CHAR];
  int vc_big = 0, vc_small = 0, vc_smallest = 0;

  int sw = font_space(font);
  int lh = font_line(font);

  int cursor_x = x;
  for (int i = 0; i < text_length;) {
    int length;
    uint32_t codepoint = utf8_codepoint(text + i, text_length - i, &length);
    i += length;
    if (codepoint == ' ')  { cursor_x += sw; continue; }
    if (codepoint == '\n') { cursor_x = x; y += lh; continue; }

    font_atlas_t *atlas = font_for_role(font);

    bool use_smallest = atlas == &text_state.smallest;
    bool use_small = atlas == &text_state.small;
    text_vertex_t *buf = use_smallest ? buf_smallest : (use_small ? buf_small : buf_big);
    int           *vc  = use_smallest ? &vc_smallest : (use_small ? &vc_small : &vc_big);

    *vc += emit_char_verts(buf + *vc, cursor_x, y, codepoint, col, atlas);
    cursor_x += codepoint_advance(font, codepoint);
  }

  if (vc_big > 0)
    flush_batch(&text_state.big,   buf_big,   vc_big);
  if (vc_small > 0)
    flush_batch(&text_state.small, buf_small, vc_small);
  if (vc_smallest > 0)
    flush_batch(&text_state.smallest, buf_smallest, vc_smallest);
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

// ── Legacy FONT_SYSTEM wrappers ───────────────────────────────────────────────

void draw_text_small(const char *text, int x, int y, uint32_t col) {
  draw_text(FONT_SYSTEM, text, x, y, col);
}

void draw_text_small_clipped(const char *text, irect16_t const *viewport,
                              uint32_t col, uint32_t flags) {
  draw_text_clipped(FONT_SYSTEM, text, viewport, col, flags);
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
  int sw = text_strwidth(font, " ");
  if (sw <= 0) sw = font_space(font);
  if (sw <= 0) sw = 3;

  int cx = x;
  int cy = y;
  int line_w = 0;
  int max_line_w = 0;
  int lines = 1;
  font_atlas_t *cur_atlas = NULL;

  int text_length = (int)strlen(text);
  for (int i = 0; i < text_length;) {
    int length;
    uint32_t codepoint = utf8_codepoint(text + i, text_length - i, &length);
    i += length;

    if (codepoint == '\n') {
      if (line_w > max_line_w) max_line_w = line_w;
      line_w = 0;
      cx = x;
      cy += lh;
      lines++;
      out.wrapped = true;
      continue;
    }

    if (codepoint == ' ') {
      int cw = sw;
      if (cx + cw > x + w) {
        if (line_w > max_line_w) max_line_w = line_w;
        line_w = 0;
        cx = x;
        cy += lh;
        lines++;
        out.wrapped = true;
      } else {
        cx += cw;
        line_w += cw;
      }
      continue;
    }

    int cw = wrap_char_advance(font, codepoint);
    if (cx + cw > x + w) {
      if (line_w > max_line_w) max_line_w = line_w;
      line_w = 0;
      cx = x;
      cy += lh;
      lines++;
      out.wrapped = true;
    }

    if (draw && vc < MAX_TEXT_LENGTH * VERTICES_PER_CHAR - VERTICES_PER_CHAR) {
      font_atlas_t *atlas = font_for_role(font);
      if (cur_atlas && atlas != cur_atlas && vc > 0) {
        flush_batch(cur_atlas, buf, vc);
        vc = 0;
      }
      cur_atlas = atlas;
      vc += emit_char_verts(buf + vc, cx, cy, codepoint, col, atlas);
    }
    cx += cw;
    line_w += cw;
  }

  if (line_w > max_line_w) max_line_w = line_w;
  out.width = max_line_w;
  out.height = lines * lh;

  if (draw && vc > 0 && cur_atlas)
    flush_batch(cur_atlas, buf, vc);

  return out;
}

text_wrap_result_t text_wrap_layout(const char *text, irect16_t const *viewport,
                                    uint32_t col, bool draw) {
  return text_wrap_layout_font(FONT_SMALL, text, viewport, col, draw);
}

int calc_text_height_font(ui_font_t font, const char *text, int width) {
  irect16_t vp = {0, 0, width, 1};
  return text_wrap_layout_font(font, text, &vp, 0, false).height;
}

int calc_text_height(const char *text, int width) {
  return calc_text_height_font(FONT_SMALL, text, width);
}

// ── draw_text_wrapped ─────────────────────────────────────────────────────────
// Wrapped content uses FONT_SMALL by default.

void draw_text_wrapped_font(ui_font_t font, const char *text,
                            irect16_t const *viewport, uint32_t col) {
  (void)text_wrap_layout_font(font, text, viewport, col, true);
}

void draw_text_wrapped(const char *text, irect16_t const *viewport, uint32_t col) {
  draw_text_wrapped_font(FONT_SMALL, text, viewport, col);
}

// ── shutdown_text_rendering ───────────────────────────────────────────────────

void shutdown_text_rendering(void) {
  font_cache_destroy(text_state.big.cache);
  R_MeshDestroy(&text_state.big.mesh);

  font_cache_destroy(text_state.small.cache);
  R_MeshDestroy(&text_state.small.mesh);

  font_cache_destroy(text_state.smallest.cache);
  R_MeshDestroy(&text_state.smallest.mesh);

  memset(&text_state, 0, sizeof(text_state));
}

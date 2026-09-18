// Animation GL rendering: thumbnail generation for indexed and 1-bit frames.

#include "imageeditor.h"

// ============================================================
// GL shader programs
// ============================================================

// Vertex shader shared by both programs.
static const char kAnimVertSrc[] =
  "#version 150 core\n"
  "in  vec2 a_pos;\n"
  "in  vec2 a_uv;\n"
  "out vec2 v_uv;\n"
  "void main() {\n"
  "  v_uv        = a_uv;\n"
  "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
  "}\n";

// Indexed-colour fragment shader:
//   u_indices  – GL_R8 texture, one byte per pixel (palette index)
//   u_palette  – GL_RGBA texture, 256x1 colour LUT
static const char kIndexedFragSrc[] =
  "#version 150 core\n"
  "uniform sampler2D u_indices;\n"
  "uniform sampler2D u_palette;\n"
  "in  vec2 v_uv;\n"
  "out vec4 frag_color;\n"
  "void main() {\n"
  "  float idx   = texture(u_indices, v_uv).r * 255.0;\n"
  "  vec2  lut_uv = vec2((idx + 0.5) / 256.0, 0.5);\n"
  "  frag_color  = texture(u_palette, lut_uv);\n"
  "}\n";

// 1-bit fragment shader:
//   u_bits     – GL_R8 texture, 0.0 or ~1.0 per pixel
//   u_fg_color – foreground RGBA
//   u_bg_color – background RGBA
static const char k1BitFragSrc[] =
  "#version 150 core\n"
  "uniform sampler2D u_bits;\n"
  "uniform vec4 u_fg_color;\n"
  "uniform vec4 u_bg_color;\n"
  "in  vec2 v_uv;\n"
  "out vec4 frag_color;\n"
  "void main() {\n"
  "  float bit  = step(0.5, texture(u_bits, v_uv).r);\n"
  "  frag_color = mix(u_bg_color, u_fg_color, bit);\n"
  "}\n";

static GLuint s_indexed_prog = 0;
static GLuint s_1bit_prog    = 0;

static GLuint compile_shader(GLenum type, const char *src) {
  GLuint sh = glCreateShader(type);
  if (!sh) return 0;
#ifdef ORION_OPENGL_ES
  const char *body = strchr(src, '\n');
  const char *parts[] = {"#version 300 es\nprecision highp float;\nprecision highp int;\n", body ? body + 1 : src};
  glShaderSource(sh, 2, parts, NULL);
#else
  glShaderSource(sh, 1, &src, NULL);
#endif
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetShaderInfoLog(sh, sizeof(log), NULL, log);
    IE_DEBUG("anim_render shader compile error: %s", log);
    glDeleteShader(sh);
    return 0;
  }
  return sh;
}

static GLuint link_program(const char *vert_src, const char *frag_src) {
  GLuint vs = compile_shader(GL_VERTEX_SHADER,   vert_src);
  if (!vs) return 0;
  GLuint fs = compile_shader(GL_FRAGMENT_SHADER, frag_src);
  if (!fs) { glDeleteShader(vs); return 0; }

  GLuint prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glLinkProgram(prog);
  glDeleteShader(vs);
  glDeleteShader(fs);

  GLint ok = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetProgramInfoLog(prog, sizeof(log), NULL, log);
    IE_DEBUG("anim_render program link error: %s", log);
    glDeleteProgram(prog);
    return 0;
  }
  return prog;
}

bool anim_render_init(void) {
  if (s_indexed_prog && s_1bit_prog) return true; // already initialised

  s_indexed_prog = link_program(kAnimVertSrc, kIndexedFragSrc);
  s_1bit_prog    = link_program(kAnimVertSrc, k1BitFragSrc);
  return s_indexed_prog != 0 && s_1bit_prog != 0;
}

void anim_render_shutdown(void) {
  if (s_indexed_prog) { glDeleteProgram(s_indexed_prog); s_indexed_prog = 0; }
  if (s_1bit_prog)    { glDeleteProgram(s_1bit_prog);    s_1bit_prog    = 0; }
}

// ============================================================
// Thumbnail generation
// ============================================================

// Expand the compressed frame to RGBA in a temporary buffer, then upload
// that buffer into a RGBA GL texture.  This is the universal path that
// works for all three formats without requiring a separate FBO pass.
//
// palette: for INDEXED frames, the caller's palette (doc->ipal.entries).
//          In indexed builds the per-frame palette is not stored (the
//          working palette lives in doc->ipal), so this parameter is
//          required.  Pass NULL for non-indexed formats.
static uint8_t *anim_frame_rgba(const anim_frame_t *frame, int w, int h,
                                const uint32_t *palette) {
  if (!frame || w <= 0 || h <= 0 || (size_t)w > SIZE_MAX / 4 / (size_t)h)
    return NULL;

  size_t sz = (size_t)w * (size_t)h * 4;
  uint8_t *rgba = malloc(sz);
  if (!rgba) return NULL;

  bool ok = false;

  if (frame->data && frame->data_size > 0) {
    if (frame->format == FRAME_FORMAT_INDEXED) {
      // Indexed frames: expand palette indices → RGBA manually.
      // anim_frame_expand() in indexed builds does a raw memcpy of indices
      // (targeting doc->pixels, which is 1-byte/pixel), but here we always
      // need RGBA output.  Use the caller-supplied palette (doc->ipal).
      size_t npx = (size_t)w * (size_t)h;
      if (frame->data_size < npx) { free(rgba); return NULL; }
      const uint32_t *pal = palette ? palette : frame->palette;
      for (size_t i = 0; i < npx; i++) {
        uint8_t idx = frame->data[i];
        uint32_t col = pal[idx];
        rgba[i*4+0] = COLOR_R(col);
        rgba[i*4+1] = COLOR_G(col);
        rgba[i*4+2] = COLOR_B(col);
        rgba[i*4+3] = COLOR_A(col);
      }
      ok = true;
    } else {
      ok = anim_frame_expand(frame, rgba, w, h);
    }
  } else {
    // Empty frame — produce a transparent black thumbnail.
    memset(rgba, 0, sz);
    ok = true;
  }

  if (!ok) { free(rgba); return NULL; }
  return rgba;
}

void anim_onion_tint_rgba(uint8_t *rgba, size_t npx, uint32_t tint) {
  if (!rgba || npx == 0 || COLOR_A(tint) == 0) return;
  uint8_t tr = COLOR_R(tint), tg = COLOR_G(tint), tb = COLOR_B(tint);
  for (size_t i = 0; i < npx; i++) {
    uint8_t *p = rgba + i * 4;
    if (p[3] == 0) continue;
    int luma = (p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8;
    if (luma >= 248) { memset(p, 0, 4); continue; }
    p[0] = tr;
    p[1] = tg;
    p[2] = tb;
  }
}

static bool anim_upload_thumbnail_rgba(uint8_t *rgba, int w, int h, uint32_t *tex) {
  if (!rgba || !tex) return false;
  if (*tex == 0) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    *tex = t;
  } else {
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  }
  return *tex != 0;
}

bool anim_render_frame_thumbnail(const anim_frame_t *frame, int w, int h,
                                 uint32_t *tex, const uint32_t *palette) {
  if (!tex) return false;
  uint8_t *rgba = anim_frame_rgba(frame, w, h, palette);
  if (!rgba) return false;
  bool ok = anim_upload_thumbnail_rgba(rgba, w, h, tex);
  free(rgba);
  return ok;
}

bool anim_render_frame_thumbnail_tinted(const anim_frame_t *frame, int w, int h,
                                        uint32_t *tex, const uint32_t *palette,
                                        uint32_t tint) {
  if (!tex) return false;
  uint8_t *rgba = anim_frame_rgba(frame, w, h, palette);
  if (!rgba) return false;
  anim_onion_tint_rgba(rgba, (size_t)w * (size_t)h, tint);
  bool ok = anim_upload_thumbnail_rgba(rgba, w, h, tex);
  free(rgba);
  return ok;
}

bool anim_render_frame_thumbnail_scaled(const anim_frame_t *frame,
                                        int w, int h, int target_size,
                                        uint32_t *tex, const uint32_t *palette) {
  if (!tex)
    return false;

  uint8_t *rgba = anim_frame_rgba(frame, w, h, palette);
  if (!rgba) {
    IE_TRACE("thumbnail expansion failed size=%dx%d", w, h);
    return false;
  }
  // Keep a little resolution in the cached thumbnail and let the linear
  // texture sampler perform the final reduction when it is drawn.  This
  // gives curves and diagonal strokes a second, sub-pixel filtering pass.
  int render_size = target_size * 2;
  uint8_t *small = downscale_image_ex(rgba, w, h, render_size,
                                      IMAGE_DOWNSCALE_STROKES | IMAGE_DOWNSCALE_FLIP_Y);
  free(rgba);
  if (!small) return false;
  uint32_t scaled = R_CreateTextureRGBA(render_size, render_size, small,
                                        R_FILTER_LINEAR, R_WRAP_CLAMP);
  image_free(small);
  if (!scaled) {
    IE_TRACE("thumbnail texture allocation failed target=%d", render_size);
    return false;
  }
  if (*tex) R_DeleteTexture(*tex);
  *tex = scaled;
  return true;
}

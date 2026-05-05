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
  glShaderSource(sh, 1, &src, NULL);
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
bool anim_render_frame_thumbnail(const anim_frame_t *frame, int w, int h,
                                 uint32_t *tex) {
  if (!frame || w <= 0 || h <= 0 || !tex) return false;

  size_t sz = (size_t)w * (size_t)h * 4;
  uint8_t *rgba = malloc(sz);
  if (!rgba) return false;

  bool ok = false;

  if (frame->data && frame->data_size > 0) {
    ok = anim_frame_expand(frame, rgba, w, h);
  } else {
    // Empty frame — produce a transparent black thumbnail.
    memset(rgba, 0, sz);
    ok = true;
  }

  if (ok) {
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
  }

  free(rgba);
  return ok;
}


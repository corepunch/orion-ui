#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#else
#  include <unistd.h>
#endif

#include <orion/ui.h>
#include <orion/user/gl_compat.h>
#include "fmat16.h"

#define OFFSET_OF(type, field) (void*)((size_t)&(((type *)0)->field))

static int screen_width, screen_height;

void ui_shutdown_prog(void);

// Return the directory that contains the running executable (no trailing slash).
// The returned pointer is to a static buffer valid until the next call.
// Returns "" on any error.
const char *ui_get_exe_dir(void) {
  static char buf[4096];
  buf[0] = '\0';

#if defined(_WIN32) || defined(_WIN64)
  DWORD len = GetModuleFileNameA(NULL, buf, (DWORD)sizeof(buf));
  if (len == 0 || len >= (DWORD)sizeof(buf)) { buf[0] = '\0'; return buf; }
  char *last = strrchr(buf, '\\');
  if (last) *last = '\0';
#elif defined(__APPLE__)
  uint32_t size = (uint32_t)sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) != 0) { buf[0] = '\0'; return buf; }
  char *last = strrchr(buf, '/');
  if (last) *last = '\0';
#else
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len <= 0) { buf[0] = '\0'; return buf; }
  buf[len] = '\0';
  char *last = strrchr(buf, '/');
  if (last) *last = '\0';
#endif

#ifdef AX_PLATFORM_IOS
  // Preserve the shared bin/../share resource convention inside a flat iOS bundle.
  size_t len = strlen(buf);
  if (len + 4 < sizeof(buf)) strcat(buf, "/bin");
#endif
  return buf;
}

// Vertex structure for our buffer (xyzuv)
typedef struct {
  int16_t x, y, z;    // Position
  int16_t u, v;       // Texture coordinates
  int8_t nx, ny, nz;  // Normal
  int32_t color;
} wall_vertex_t;

// Sprite vertices (quad)
wall_vertex_t sprite_verts[] = {
  {0, 0, 0, 0, 0, 0, 0, 0, -1}, // bottom left
  {0, 1, 0, 0, 1, 0, 0, 0, -1},  // top left
  {1, 1, 0, 1, 1, 0, 0, 0, -1}, // top right
  {1, 0, 0, 1, 0, 0, 0, 0, -1}, // bottom right
};

// Sprite system state
typedef struct {
  GLuint program;
  GLint projection_u;
  GLint offset_u;
  GLint scale_u;
  GLint uv_offset_u;
  GLint uv_scale_u;
  GLint tint_u;
  GLint alpha_u;
  GLint params0_u;
  GLint params1_u;
  GLint tex0_u;
} sprite_program_t;

typedef struct {
  sprite_program_t copy_sprite;
  sprite_program_t gradient_sprite;
  sprite_program_t rounded_rect_sprite; // SDF rounded-corner compositor
  GLuint vga_program;    // VGA text renderer program
  R_Mesh mesh;           // Sprite mesh for drawing quads
  fmat16_t projection;   // Orthographic projection matrix
} renderer_system_t;

renderer_system_t g_ref = {0};
static fmat16_t g_active_projection;

typedef struct {
  GLuint program;
  GLint projection;
  GLint offset;
  GLint scale;
  GLint uv_offset;
  GLint uv_scale;
  GLint grid_size;
  GLint cell_size;
  GLint cell_tex;
  GLint font_tex;
  GLint palette_tex;
  GLuint palette_texture;
} vga_renderer_t;

static vga_renderer_t g_vga = {0};

// Cached uniforms for the rounded-rect SDF compositor.
typedef struct {
  GLint size_u;
  GLint radius_u;
} rounded_rect_uniforms_t;

static rounded_rect_uniforms_t g_rounded_rect = {0};

static void draw_rect_program_common(int tex, int x, int y, int w, int h,
                                     float alpha, uint32_t program,
                                     float mix_amount,
                                     const ui_render_effect_params_t *params);

static char *read_text_file(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) return NULL;
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return NULL;
  }
  long sz = ftell(fp);
  if (sz < 0) {
    fclose(fp);
    return NULL;
  }
  rewind(fp);
  char *buf = malloc((size_t)sz + 1);
  if (!buf) {
    fclose(fp);
    return NULL;
  }
  size_t got = fread(buf, 1, (size_t)sz, fp);
  fclose(fp);
  buf[got] = '\0';
  return buf;
}

static char *read_shader_file(const char *name) {
  char path[4096];
  snprintf(path, sizeof(path), "%s/../share/orion/shaders/%s",
           ui_get_exe_dir(), name);
  return read_text_file(path);
}

// Compile a shader
GLuint compile_shader(GLenum type, const char* src) {
  GLuint shader = glCreateShader(type);
#ifdef ORION_OPENGL_ES
  const char *body = src;
  if (strncmp(body, "#version", 8) == 0 && strchr(body, '\n')) body = strchr(body, '\n') + 1;
  const char *parts[] = {"#version 300 es\nprecision highp float;\nprecision highp int;\n", body};
  glShaderSource(shader, 2, parts, NULL);
#else
  glShaderSource(shader, 1, &src, 0);
#endif
  glCompileShader(shader);
  
  // Check for errors
  GLint status;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE) {
    GLint log_length;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
    char* log = malloc(log_length);
    glGetShaderInfoLog(shader, log_length, NULL, log);
    printf("Shader compilation error: %s\n", log);
    free(log);
  }
  
  return shader;
}

int get_sprite_prog(void) {
  return g_ref.copy_sprite.program;
}

int get_sprite_vao(void) {
  return g_ref.mesh.vao;
}

static void cache_sprite_uniforms(sprite_program_t *prog) {
  if (!prog || !prog->program) return;
  prog->projection_u = glGetUniformLocation(prog->program, "projection");
  prog->offset_u     = glGetUniformLocation(prog->program, "offset");
  prog->scale_u      = glGetUniformLocation(prog->program, "scale");
  prog->uv_offset_u  = glGetUniformLocation(prog->program, "uv_offset");
  prog->uv_scale_u   = glGetUniformLocation(prog->program, "uv_scale");
  prog->tint_u       = glGetUniformLocation(prog->program, "tint");
  prog->alpha_u      = glGetUniformLocation(prog->program, "alpha");
  prog->params0_u    = glGetUniformLocation(prog->program, "params0");
  prog->params1_u    = glGetUniformLocation(prog->program, "params1");
  prog->tex0_u       = glGetUniformLocation(prog->program, "tex0");
}

static void cache_vga_uniforms(void) {
  if (!g_ref.vga_program) return;
  g_vga.projection  = glGetUniformLocation(g_ref.vga_program, "projection");
  g_vga.offset      = glGetUniformLocation(g_ref.vga_program, "offset");
  g_vga.scale       = glGetUniformLocation(g_ref.vga_program, "scale");
  g_vga.uv_offset   = glGetUniformLocation(g_ref.vga_program, "uv_offset");
  g_vga.uv_scale    = glGetUniformLocation(g_ref.vga_program, "uv_scale");
  g_vga.grid_size   = glGetUniformLocation(g_ref.vga_program, "gridSize");
  g_vga.cell_size   = glGetUniformLocation(g_ref.vga_program, "cellSize");
  g_vga.cell_tex    = glGetUniformLocation(g_ref.vga_program, "cellTex");
  g_vga.font_tex    = glGetUniformLocation(g_ref.vga_program, "fontTex");
  g_vga.palette_tex = glGetUniformLocation(g_ref.vga_program, "paletteTex");
}

static void update_sprite_projection_uniforms(const fmat16_t *projection) {
  GLint prev_prog = 0;
  glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
  if (g_ref.copy_sprite.program && g_ref.copy_sprite.projection_u >= 0) {
    glUseProgram(g_ref.copy_sprite.program);
    glUniformMatrix4fv(g_ref.copy_sprite.projection_u, 1, GL_FALSE, fmat16_data(projection));
  }
  if (g_ref.gradient_sprite.program && g_ref.gradient_sprite.projection_u >= 0) {
    glUseProgram(g_ref.gradient_sprite.program);
    glUniformMatrix4fv(g_ref.gradient_sprite.projection_u, 1, GL_FALSE, fmat16_data(projection));
  }
  if (g_ref.rounded_rect_sprite.program && g_ref.rounded_rect_sprite.projection_u >= 0) {
    glUseProgram(g_ref.rounded_rect_sprite.program);
    glUniformMatrix4fv(g_ref.rounded_rect_sprite.projection_u, 1, GL_FALSE, fmat16_data(projection));
  }
  glUseProgram((GLuint)prev_prog);
}

static GLuint link_program_from_sources(const char *vs_src, const char *fs_src) {
  GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
  GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
  if (!vs || !fs) {
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    return 0;
  }

  GLuint program = glCreateProgram();
  if (!program) {
    glDeleteShader(vs);
    glDeleteShader(fs);
    return 0;
  }

  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glDeleteShader(vs);
  glDeleteShader(fs);
  return program;
}

bool ui_load_program_from_source(const char *vs_src, const char *fs_src,
                                 const char *attrib0, const char *attrib1,
                                 const char *attrib2, uint32_t *out_program) {
  if (!vs_src || !fs_src || !attrib0 || !attrib1 || !out_program) return false;
  GLuint program = link_program_from_sources(vs_src, fs_src);
  if (!program) return false;

  glBindAttribLocation(program, 0, attrib0);
  glBindAttribLocation(program, 1, attrib1);
  if (attrib2)
    glBindAttribLocation(program, 2, attrib2);
  glLinkProgram(program);

  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (linked != GL_TRUE) {
    GLint n = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &n);
    if (n > 1) {
      char *log = malloc((size_t)n);
      if (log) {
        glGetProgramInfoLog(program, n, NULL, log);
        printf("Shader link error (source): %s\n", log);
        free(log);
      }
    }
    glDeleteProgram(program);
    return false;
  }

  *out_program = program;
  return true;
}

void ui_delete_program(uint32_t program) {
  if (program) glDeleteProgram(program);
}

static GLuint load_program_from_files(const char *fs_name,
                                      const char *attrib0, const char *attrib1,
                                      const char *attrib2) {
  const char *vs_name = "common.vert.glsl";
  char *vs_src = read_shader_file(vs_name);
  char *fs_src = read_shader_file(fs_name);
  if (!vs_src || !fs_src) {
    printf("Shader load error: %s / %s\n", vs_name, fs_name);
    free(vs_src);
    free(fs_src);
    return 0;
  }

  GLuint program = link_program_from_sources(vs_src, fs_src);
  free(vs_src);
  free(fs_src);
  if (!program) return 0;

  glBindAttribLocation(program, 0, attrib0);
  glBindAttribLocation(program, 1, attrib1);
  if (attrib2)
    glBindAttribLocation(program, 2, attrib2);
  glLinkProgram(program);

  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (linked != GL_TRUE) {
    GLint n = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &n);
    if (n > 1) {
      char *log = malloc((size_t)n);
      if (log) {
        glGetProgramInfoLog(program, n, NULL, log);
        printf("Shader link error (%s/%s): %s\n", vs_name, fs_name, log);
        free(log);
      }
    }
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

// Initialize the sprite system
bool ui_init_prog(void) {
  memset(&g_ref, 0, sizeof(g_ref));
  memset(&g_vga, 0, sizeof(g_vga));

  g_ref.copy_sprite.program = load_program_from_files("sprite_copy.frag.glsl",
                                                      "position", "texcoord", "color");
  if (!g_ref.copy_sprite.program) {
    ui_shutdown_prog();
    return false;
  }
  cache_sprite_uniforms(&g_ref.copy_sprite);

  g_ref.gradient_sprite.program = load_program_from_files("sprite_gradient.frag.glsl",
                                                          "position", "texcoord", "color");
  if (!g_ref.gradient_sprite.program) {
    ui_shutdown_prog();
    return false;
  }
  cache_sprite_uniforms(&g_ref.gradient_sprite);

  g_ref.rounded_rect_sprite.program = load_program_from_files("sprite_rounded_rect.frag.glsl",
                                                               "position", "texcoord", "color");
  if (!g_ref.rounded_rect_sprite.program) {
    ui_shutdown_prog();
    return false;
  }
  cache_sprite_uniforms(&g_ref.rounded_rect_sprite);
  if (g_ref.rounded_rect_sprite.program) {
    g_rounded_rect.size_u   = glGetUniformLocation(g_ref.rounded_rect_sprite.program, "size");
    g_rounded_rect.radius_u = glGetUniformLocation(g_ref.rounded_rect_sprite.program, "radius");
  }

  g_ref.vga_program = load_program_from_files("vga.frag.glsl",
                                              "position", "texcoord", NULL);
  if (!g_ref.vga_program) {
    ui_shutdown_prog();
    return false;
  }
  cache_vga_uniforms();
  glGenTextures(1, &g_vga.palette_texture);
  glBindTexture(GL_TEXTURE_2D, g_vga.palette_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

  // Initialize mesh for sprite rendering using Renderer API
  // Vertex attribute layout: 0 = Position, 1 = UV, 2 = Color
  R_VertexAttrib attribs[] = {
    {0, 3, GL_SHORT, GL_FALSE, offsetof(wall_vertex_t, x)},      // Position (x, y, z)
    {1, 2, GL_SHORT, GL_FALSE, offsetof(wall_vertex_t, u)},      // UV
    {2, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(wall_vertex_t, color)} // Color
  };
  R_MeshInit(&g_ref.mesh, attribs, 3, sizeof(wall_vertex_t), GL_TRIANGLE_FAN);
  
  // Upload static sprite vertex data
  R_MeshUpload(&g_ref.mesh, sprite_verts, 4);
  
  // Create orthographic projection matrix for screen-space rendering
  uint32_t ws = axGetSize(NULL);
  int width  = (int)LOWORD(ws);
  int height = (int)HIWORD(ws);
  //  float scale = (float)height / DOOM_HEIGHT;
  //  float render_width = DOOM_WIDTH * scale;
  //  float offset_x = (width - render_width) / (2.0f * scale);
  //  black_bars = offset_x;
  //  fmat16_ortho(-offset_x, DOOM_WIDTH+offset_x, DOOM_HEIGHT, 0, -1, 1, &g_ref.projection);
  screen_width = width / UI_WINDOW_SCALE;
  screen_height = height / UI_WINDOW_SCALE;
  fmat16_ortho(0, screen_width, screen_height, 0, -1, 1, &g_ref.projection);
  fmat16_copy(&g_ref.projection, &g_active_projection);

  update_sprite_projection_uniforms(&g_ref.projection);
  glUseProgram(g_ref.vga_program);
  glUniformMatrix4fv(g_vga.projection, 1, GL_FALSE, fmat16_data(&g_ref.projection));

  return true;
}

void ui_shutdown_prog(void) {
  // Delete shader program and buffers
  SAFE_DELETE_N(g_vga.palette_texture, glDeleteTextures);
  SAFE_DELETE(g_ref.copy_sprite.program, glDeleteProgram);
  SAFE_DELETE(g_ref.gradient_sprite.program, glDeleteProgram);
  SAFE_DELETE(g_ref.rounded_rect_sprite.program, glDeleteProgram);
  SAFE_DELETE(g_ref.vga_program, glDeleteProgram);
  R_MeshDestroy(&g_ref.mesh);
}

void push_sprite_args(int tex, int x, int y, int w, int h, float alpha) {
  if (!g_ref.copy_sprite.program) return;
  glUseProgram(g_ref.copy_sprite.program);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  glUniform1i(g_ref.copy_sprite.tex0_u, 0);
  glUniform2f(g_ref.copy_sprite.offset_u, x, y);
  glUniform2f(g_ref.copy_sprite.scale_u, w, h);
  glUniform1f(g_ref.copy_sprite.alpha_u, alpha);
  glUniform4f(g_ref.copy_sprite.params0_u, 0.0f, 0.0f, 0.0f, 0.0f);
  glUniform4f(g_ref.copy_sprite.params1_u, 0.0f, 0.0f, 0.0f, 0.0f);
  glUniform2f(g_ref.copy_sprite.uv_offset_u, 0.0f, 0.0f);
  glUniform2f(g_ref.copy_sprite.uv_scale_u, 1.0f, 1.0f);
  glUniform4f(g_ref.copy_sprite.tint_u, 1.0f, 1.0f, 1.0f, 1.0f);
}

void set_projection(int x, int y, int w, int h) {
  if (!g_ref.vga_program) return;
  fmat16_t projection;
  fmat16_ortho(x, w, h, y, -1, 1, &projection);
  fmat16_copy(&projection, &g_active_projection);
  fmat16_copy(&projection, &g_ref.projection);
  update_sprite_projection_uniforms(&projection);
}

float *get_sprite_matrix(void) {
  return (float*)fmat16_data(&g_ref.projection);
}

// Draw a sprite at the specified screen position
void draw_rect_ex(int tex, irect16_t r, int type, float alpha) {
  if (!g_ref.vga_program) return;
  push_sprite_args(tex, r.x, r.y, r.w, r.h, alpha);
  
  // Source-over alpha keeps opaque window surfaces opaque under faded sprites.
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  // Disable depth testing for UI elements
  glDisable(GL_DEPTH_TEST);
  
  // Use the appropriate drawing mode
  g_ref.mesh.draw_mode = type ? GL_LINE_LOOP : GL_TRIANGLE_FAN;
  R_MeshDraw(&g_ref.mesh);
  
  // Reset state
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
}

// Draw a sprite at the specified screen position
void draw_rect(int tex, irect16_t r) {
  draw_rect_ex(tex, r, false, 1);
}

// Draw a sub-region of a sprite sheet at the specified screen position.
// uv packs normalized texture coordinates as floats: x=u0, y=v0, w=u1, h=v1.
void draw_sprite_region(int tex, irect16_t r,
                        frect_t const *uv,
                        uint32_t color, uint32_t flags) {
  if (!g_ref.vga_program) return;
  const sprite_program_t *prog = &g_ref.copy_sprite;
  if (!prog || !prog->program) return;
  float u0 = uv ? uv->x : 0.0f;
  float v0 = uv ? uv->y : 0.0f;
  float u1 = uv ? uv->w : 1.0f;
  float v1 = uv ? uv->h : 1.0f;

  float alpha = ((color >> 24) & 0xFF) / 255.0f;
  if (flags & DRAW_SPRITE_NO_ALPHA)
    alpha = 1.0f;
  push_sprite_args(tex, r.x, r.y, r.w, r.h, alpha);

  float tr = ((color      ) & 0xFF) / 255.0f;
  float tg = ((color >>  8) & 0xFF) / 255.0f;
  float tb = ((color >> 16) & 0xFF) / 255.0f;
  float ta = 1.0f;
  glUniform4f(prog->tint_u, tr, tg, tb, ta);

  glUniform2f(prog->uv_offset_u, u0, v0);
  glUniform2f(prog->uv_scale_u, u1 - u0, v1 - v0);
  if (flags & DRAW_SPRITE_NO_ALPHA) {
    glDisable(GL_BLEND);
  } else {
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  }
  glDisable(GL_DEPTH_TEST);
  g_ref.mesh.draw_mode = GL_TRIANGLE_FAN;
  R_MeshDraw(&g_ref.mesh);
  glEnable(GL_DEPTH_TEST);
  if (!(flags & DRAW_SPRITE_NO_ALPHA))
    glDisable(GL_BLEND);
}

void draw_rect_gradient(int tex, int x, int y, int w, int h,
                        const ui_render_effect_params_t *params) {
  static const ui_render_effect_params_t kZeroParams = {{0}};
  const ui_render_effect_params_t *p = params ? params : &kZeroParams;
  if (!g_ref.vga_program || !g_ref.gradient_sprite.program) return;
  glUseProgram(g_ref.gradient_sprite.program);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  glUniform1i(g_ref.gradient_sprite.tex0_u, 0);
  glUniform2f(g_ref.gradient_sprite.offset_u, x, y);
  glUniform2f(g_ref.gradient_sprite.scale_u, w, h);
  glUniform1f(g_ref.gradient_sprite.alpha_u, 1.0f);
  glUniform4f(g_ref.gradient_sprite.params0_u, p->f[0], p->f[1], p->f[2], p->f[3]);
  glUniform4f(g_ref.gradient_sprite.params1_u, p->f[4], p->f[5], p->f[6], p->f[7]);
  glUniform2f(g_ref.gradient_sprite.uv_offset_u, 0.0f, 0.0f);
  glUniform2f(g_ref.gradient_sprite.uv_scale_u, 1.0f, 1.0f);
  glUniform4f(g_ref.gradient_sprite.tint_u, 1.0f, 1.0f, 1.0f, 1.0f);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);
  g_ref.mesh.draw_mode = GL_TRIANGLE_FAN;
  R_MeshDraw(&g_ref.mesh);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
}

void draw_rect_program_params_blend(int tex, int x, int y, int w, int h,
                                    float alpha, ui_layer_blend_t blend,
                                    uint32_t program, float mix_amount,
                                    const ui_render_effect_params_t *params) {
  if (!g_ref.vga_program || !program) return;
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  switch (blend) {
    case UI_LAYER_BLEND_MULTIPLY:
      glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
      break;
    case UI_LAYER_BLEND_SCREEN:
      glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
      break;
    case UI_LAYER_BLEND_ADD:
      glBlendFunc(GL_SRC_ALPHA, GL_ONE);
      break;
    case UI_LAYER_BLEND_NORMAL:
    default:
      glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      break;
  }
  draw_rect_program_common(tex, x, y, w, h, alpha, program, mix_amount, params);
  glDisable(GL_BLEND);
}

void draw_rect_blend(int tex, int x, int y, int w, int h, float alpha,
                     ui_layer_blend_t blend) {
  if (!g_ref.vga_program) return;
  push_sprite_args(tex, x, y, w, h, alpha);
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  switch (blend) {
    case UI_LAYER_BLEND_MULTIPLY:
      glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
      break;
    case UI_LAYER_BLEND_SCREEN:
      glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
      break;
    case UI_LAYER_BLEND_ADD:
      glBlendFunc(GL_SRC_ALPHA, GL_ONE);
      break;
    case UI_LAYER_BLEND_NORMAL:
    default:
      glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      break;
  }
  glDisable(GL_DEPTH_TEST);
  g_ref.mesh.draw_mode = GL_TRIANGLE_FAN;
  R_MeshDraw(&g_ref.mesh);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
}

static void draw_rect_program_common(int tex, int x, int y, int w, int h,
                                     float alpha, uint32_t program,
                                     float mix_amount,
                                     const ui_render_effect_params_t *params) {
  if (!g_ref.vga_program || !program) return;
  glUseProgram(program);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  GLint tex0_u = glGetUniformLocation(program, "tex0");
  GLint projection_u = glGetUniformLocation(program, "projection");
  GLint alpha_u = glGetUniformLocation(program, "alpha");
  GLint tint_u = glGetUniformLocation(program, "tint");
  GLint mix_u = glGetUniformLocation(program, "u_mix");
  GLint offset_u = glGetUniformLocation(program, "offset");
  GLint scale_u = glGetUniformLocation(program, "scale");
  GLint uv_offset_u = glGetUniformLocation(program, "uv_offset");
  GLint uv_scale_u = glGetUniformLocation(program, "uv_scale");
  GLint params0_u = glGetUniformLocation(program, "params0");
  GLint params1_u = glGetUniformLocation(program, "params1");
  if (tex0_u >= 0) glUniform1i(tex0_u, 0);
  if (projection_u >= 0)
    glUniformMatrix4fv(projection_u, 1, GL_FALSE, get_sprite_matrix());
  if (alpha_u >= 0) glUniform1f(alpha_u, alpha);
  if (tint_u >= 0) glUniform4f(tint_u, 1.0f, 1.0f, 1.0f, 1.0f);
  if (mix_u >= 0) glUniform1f(mix_u, mix_amount);
  if (offset_u >= 0) glUniform2f(offset_u, (float)x, (float)y);
  if (scale_u >= 0) glUniform2f(scale_u, (float)w, (float)h);
  if (uv_offset_u >= 0) glUniform2f(uv_offset_u, 0.0f, 0.0f);
  if (uv_scale_u >= 0) glUniform2f(uv_scale_u, 1.0f, 1.0f);
  if (params) {
    if (params0_u >= 0)
      glUniform4f(params0_u, params->f[0], params->f[1], params->f[2], params->f[3]);
    if (params1_u >= 0)
      glUniform4f(params1_u, params->f[4], params->f[5], params->f[6], params->f[7]);
  }
  glDisable(GL_DEPTH_TEST);
  g_ref.mesh.draw_mode = GL_TRIANGLE_FAN;
  R_MeshDraw(&g_ref.mesh);
  glEnable(GL_DEPTH_TEST);
}

void draw_rect_program_blend(int tex, int x, int y, int w, int h, float alpha,
                             ui_layer_blend_t blend, uint32_t program,
                             float mix_amount) {
  draw_rect_program_params_blend(tex, x, y, w, h, alpha, blend,
                                 program, mix_amount, NULL);
}

void draw_rect_program_params(int tex, int x, int y, int w, int h,
                              uint32_t program, float mix_amount,
                              const ui_render_effect_params_t *params) {
  draw_rect_program_params_blend(tex, x, y, w, h, 1.0f, UI_LAYER_BLEND_NORMAL,
                                 program, mix_amount, params);
}

void draw_rect_program(int tex, int x, int y, int w, int h, uint32_t program,
                       float mix_amount) {
  draw_rect_program_blend(tex, x, y, w, h, 1.0f, UI_LAYER_BLEND_NORMAL,
                          program, mix_amount);
}

static bool bake_texture_program_common(int src_tex, int w, int h,
                                        uint32_t program, float mix_amount,
                                        const ui_render_effect_params_t *params,
                                        uint32_t *out_tex) {
  if (!g_ref.vga_program || src_tex == 0 || w <= 0 || h <= 0 || !program || !out_tex)
    return false;

  GLuint tex = R_CreateTextureRGBA(w, h, NULL, R_FILTER_LINEAR, R_WRAP_CLAMP);
  if (!tex) return false;

  GLuint fbo = 0;
  GLint prev_fbo = 0;
  GLint prev_view[4] = {0};
  GLint prev_scissor[4] = {0};
  GLint prev_prog = 0;
  fmat16_t prev_proj;
  memcpy(&prev_proj, get_sprite_matrix(), sizeof(prev_proj));
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
  glGetIntegerv(GL_VIEWPORT, prev_view);
  glGetIntegerv(GL_SCISSOR_BOX, prev_scissor);
  glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);

  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    glDeleteFramebuffers(1, &fbo);
    R_DeleteTexture(tex);
    return false;
  }

  const GLenum draw_buffer = GL_COLOR_ATTACHMENT0;
  glDrawBuffers(1, &draw_buffer);
  glViewport(0, 0, w, h);
  glScissor(0, 0, w, h);
  set_projection(0, 0, w, h);
  draw_rect_program_common(src_tex, 0, 0, w, h, 1.0f, program, mix_amount, params);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
  glDeleteFramebuffers(1, &fbo);
  glUseProgram((GLuint)prev_prog);
  glViewport(prev_view[0], prev_view[1], prev_view[2], prev_view[3]);
  glScissor(prev_scissor[0], prev_scissor[1], prev_scissor[2], prev_scissor[3]);
  fmat16_copy(&prev_proj, &g_active_projection);
  fmat16_copy(&prev_proj, &g_ref.projection);
  update_sprite_projection_uniforms(&prev_proj);
  *out_tex = tex;
  return true;
}

bool bake_texture_program_params(int src_tex, int w, int h, uint32_t program,
                                 float mix_amount,
                                 const ui_render_effect_params_t *params,
                                 uint32_t *out_tex) {
  return bake_texture_program_common(src_tex, w, h, program, mix_amount, params, out_tex);
}

bool bake_texture_program(int src_tex, int w, int h, uint32_t program,
                          float mix_amount, uint32_t *out_tex) {
  return bake_texture_program_common(src_tex, w, h, program, mix_amount, NULL, out_tex);
}

void draw_program_rect(int tex, irect16_t r, uint32_t program, float mix_amount) {
  draw_rect_program(tex, r.x, r.y, r.w, r.h, program, mix_amount);
}

// Draw a texture with SDF rounded-corner masking.
// tex     — source texture (typically an FBO color attachment).
// r       — destination rectangle in logical coordinates.
// win_w/h — window size in pixels (used for SDF computation).
// radius  — corner radius in pixels.
// alpha   — overall opacity multiplier.
void render_rounded_rect(int tex, irect16_t r, int win_w, int win_h,
                                float radius, float alpha, uint32_t color) {
  if (!g_ref.rounded_rect_sprite.program || !tex) return;
  glUseProgram(g_ref.rounded_rect_sprite.program);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
  glUniform1i(g_ref.rounded_rect_sprite.tex0_u, 0);
  glUniform2f(g_ref.rounded_rect_sprite.offset_u, (float)r.x, (float)r.y);
  glUniform2f(g_ref.rounded_rect_sprite.scale_u, (float)r.w, (float)r.h);
  glUniform1f(g_ref.rounded_rect_sprite.alpha_u, alpha);
  glUniform4f(g_ref.rounded_rect_sprite.params0_u, 0.0f, 0.0f, 0.0f, 0.0f);
  glUniform4f(g_ref.rounded_rect_sprite.params1_u, 0.0f, 0.0f, 0.0f, 0.0f);
  glUniform2f(g_ref.rounded_rect_sprite.uv_offset_u, 0.0f, 1.0f);
  glUniform2f(g_ref.rounded_rect_sprite.uv_scale_u, 1.0f, -1.0f);
  glUniform4f(g_ref.rounded_rect_sprite.tint_u,
              (color & 255) / 255.0f, ((color >> 8) & 255) / 255.0f,
              ((color >> 16) & 255) / 255.0f, (color >> 24) / 255.0f);
  // SDF-specific uniforms.
  glUniform2f(g_rounded_rect.size_u, (float)win_w, (float)win_h);
  glUniform1f(g_rounded_rect.radius_u, MAX(0.0f, MIN(radius, MIN(win_w, win_h) * 0.5f)));
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);
  g_ref.mesh.draw_mode = GL_TRIANGLE_FAN;
  R_MeshDraw(&g_ref.mesh);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
}

void draw_rounded_rect(int tex, irect16_t r, int win_w, int win_h,
                       float radius, float alpha) {
  render_rounded_rect(tex, r, win_w, win_h, radius, alpha, 0xffffffff);
}

bool read_texture_rgba(int src_tex, int w, int h, uint8_t *out_rgba) {
  if (!g_ref.vga_program || src_tex == 0 || w <= 0 || h <= 0 || !out_rgba)
    return false;

  GLuint fbo = 0;
  GLint prev_fbo = 0;
  GLint prev_view[4] = {0};
  GLint prev_scissor[4] = {0};

  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
  glGetIntegerv(GL_VIEWPORT, prev_view);
  glGetIntegerv(GL_SCISSOR_BOX, prev_scissor);

  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, (GLuint)src_tex, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    glDeleteFramebuffers(1, &fbo);
    glViewport(prev_view[0], prev_view[1], prev_view[2], prev_view[3]);
    glScissor(prev_scissor[0], prev_scissor[1], prev_scissor[2], prev_scissor[3]);
    return false;
  }

  const GLenum draw_buffer = GL_COLOR_ATTACHMENT0;
  glDrawBuffers(1, &draw_buffer);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  size_t row_sz = (size_t)w * 4;
  uint8_t *tmp = malloc((size_t)h * row_sz);
  if (!tmp) {
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    glDeleteFramebuffers(1, &fbo);
    glViewport(prev_view[0], prev_view[1], prev_view[2], prev_view[3]);
    glScissor(prev_scissor[0], prev_scissor[1], prev_scissor[2], prev_scissor[3]);
    return false;
  }
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
  for (int y = 0; y < h; y++) {
    memcpy(out_rgba + (size_t)y * row_sz,
           tmp + (size_t)(h - 1 - y) * row_sz,
           row_sz);
  }
  free(tmp);

  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
  glDeleteFramebuffers(1, &fbo);
  glViewport(prev_view[0], prev_view[1], prev_view[2], prev_view[3]);
  glScissor(prev_scissor[0], prev_scissor[1], prev_scissor[2], prev_scissor[3]);
  return true;
}

bool capture_framebuffer_rgba(int w, int h, uint8_t *out_rgba) {
  if (w <= 0 || h <= 0 || !out_rgba)
    return false;

  GLint prev_fbo = 0;
  GLint prev_read = 0;
  GLint prev_view[4] = {0};
  GLint prev_scissor[4] = {0};

  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
  glGetIntegerv(GL_READ_BUFFER, &prev_read);
  glGetIntegerv(GL_VIEWPORT, prev_view);
  glGetIntegerv(GL_SCISSOR_BOX, prev_scissor);

  glViewport(0, 0, w, h);
  glScissor(0, 0, w, h);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  size_t row_sz = (size_t)w * 4;
  uint8_t *tmp = malloc((size_t)h * row_sz);
  if (!tmp) {
    glViewport(prev_view[0], prev_view[1], prev_view[2], prev_view[3]);
    glScissor(prev_scissor[0], prev_scissor[1], prev_scissor[2], prev_scissor[3]);
    glReadBuffer((GLenum)prev_read);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    return false;
  }
  glReadBuffer(prev_fbo == 0 ? GL_BACK : GL_COLOR_ATTACHMENT0);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
  for (int y = 0; y < h; y++) {
    memcpy(out_rgba + (size_t)y * row_sz,
           tmp + (size_t)(h - 1 - y) * row_sz,
           row_sz);
  }
  free(tmp);

  glViewport(prev_view[0], prev_view[1], prev_view[2], prev_view[3]);
  glScissor(prev_scissor[0], prev_scissor[1], prev_scissor[2], prev_scissor[3]);
  glReadBuffer((GLenum)prev_read);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
  return true;
}

int ui_get_system_metrics(ui_system_metrics_t metric) {
  switch (metric) {
    case kSystemMetricScreenWidth:
      return screen_width;
    case kSystemMetricScreenHeight:
      return screen_height;
    default:
      return 0;
  }
}

void ui_update_screen_size(int width, int height) {
  screen_width = width / UI_WINDOW_SCALE;
  screen_height = height / UI_WINDOW_SCALE;
  fmat16_ortho(0, screen_width, screen_height, 0, -1, 1, &g_ref.projection);
  fmat16_copy(&g_ref.projection, &g_active_projection);
  update_sprite_projection_uniforms(&g_ref.projection);
}
uint32_t R_CreateTextureRGBA(int w, int h, const void *rgba,
                              R_TextureFilter filter, R_TextureWrap wrap) {
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  GLenum gl_filter = (filter == R_FILTER_LINEAR) ? GL_LINEAR : GL_NEAREST;
  GLenum gl_wrap   = (wrap   == R_WRAP_REPEAT)   ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl_wrap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl_wrap);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  return (uint32_t)tex;
}

int R_GetMaxTextureSize(void) {
  GLint limit = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
  if (limit <= 0) { fprintf(stderr, "[renderer] texture limit unavailable\n"); fflush(stderr); }
  return limit;
}

uint32_t R_CreateTextureR8(int w, int h, const void *pixels,
                            R_TextureFilter filter, R_TextureWrap wrap) {
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  GLenum gl_filter = (filter == R_FILTER_LINEAR) ? GL_LINEAR : GL_NEAREST;
  GLenum gl_wrap   = (wrap   == R_WRAP_REPEAT)   ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl_wrap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl_wrap);
  GLint swizzle_mask[] = {GL_ONE, GL_ONE, GL_ONE, GL_RED};
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, swizzle_mask[0]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, swizzle_mask[1]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, swizzle_mask[2]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, swizzle_mask[3]);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED,
               GL_UNSIGNED_BYTE, pixels);
  GLenum error = glGetError();
  if (error != GL_NO_ERROR) {
    fprintf(stderr, "[renderer] R8 texture failed size=%dx%d error=0x%x\n", w, h, error);
    fflush(stderr);
    glDeleteTextures(1, &tex);
    return 0;
  }
  return (uint32_t)tex;
}

uint32_t R_CreateTextureRG8(int w, int h, const void *rg,
                             R_TextureFilter filter, R_TextureWrap wrap) {
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  GLenum gl_filter = (filter == R_FILTER_LINEAR) ? GL_LINEAR : GL_NEAREST;
  GLenum gl_wrap   = (wrap   == R_WRAP_REPEAT)   ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl_wrap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl_wrap);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, w, h, 0, GL_RG, GL_UNSIGNED_BYTE, rg);
  return (uint32_t)tex;
}

bool R_UpdateTextureRG8(uint32_t tex, int x, int y, int w, int h,
                        const void *rg) {
  if (!tex || !rg || w <= 0 || h <= 0)
    return false;
  glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RG, GL_UNSIGNED_BYTE, rg);
  return true;
}

bool R_UpdateTextureRGBA(uint32_t tex, int x, int y, int w, int h,
                         const void *rgba) {
  if (!tex || !rgba || w <= 0 || h <= 0)
    return false;
  glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  return true;
}

void R_DeleteTexture(uint32_t id) {
  if (id == 0) return;
  GLuint tex = (GLuint)id;
  glDeleteTextures(1, &tex);
}

void R_SetBlendMode(bool enabled) {
  if (enabled) {
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
  } else {
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
  }
}

bool R_DrawVGABuffer(const R_VgaBuffer *buf,
                     int x, int y,
                     int dst_w_px, int dst_h_px,
                     const R_FontSheet *font,
                     const uint32_t palette256[256]) {
  if (!g_ref.vga_program || !buf || !buf->vga_buffer || !font ||
      !font->texture || font->cell_w <= 0 || font->cell_h <= 0 ||
      !palette256 || buf->width <= 0 || buf->height <= 0 ||
      dst_w_px <= 0 || dst_h_px <= 0)
    return false;
  uint8_t pal[256 * 4];
  for (int i = 0; i < 256; i++) {
    pal[i * 4 + 0] = (uint8_t)(palette256[i] >> 16);
    pal[i * 4 + 1] = (uint8_t)(palette256[i] >> 8);
    pal[i * 4 + 2] = (uint8_t)palette256[i];
    pal[i * 4 + 3] = (uint8_t)(palette256[i] >> 24);
  }

  glUseProgram(g_ref.vga_program);
  glUniformMatrix4fv(g_vga.projection, 1, GL_FALSE, fmat16_data(&g_active_projection));
  glUniform2f(g_vga.offset, (float)x, (float)y);
  glUniform2f(g_vga.scale, (float)dst_w_px, (float)dst_h_px);
  glUniform2f(g_vga.uv_offset, 0.0f, 0.0f);
  glUniform2f(g_vga.uv_scale, 1.0f, 1.0f);
  glUniform2f(g_vga.grid_size, (float)buf->width, (float)buf->height);
  glUniform2f(g_vga.cell_size, (float)font->cell_w, (float)font->cell_h);
  glUniform1i(g_vga.cell_tex, 0);
  glUniform1i(g_vga.font_tex, 1);
  glUniform1i(g_vga.palette_tex, 2);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, (GLuint)buf->vga_buffer);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, (GLuint)font->texture);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, g_vga.palette_texture);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, pal);

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glBindVertexArray(g_ref.mesh.vao);
  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
  glBindVertexArray(0);
  glEnable(GL_DEPTH_TEST);
  return true;
}

// ── Per-window render-target helpers ────────────────────────────────────────

bool R_EnsureWindowTarget(uint32_t *fbo, uint32_t *tex,
                          int *cur_w, int *cur_h,
                          int req_w, int req_h) {
  if (!fbo || !tex || !cur_w || !cur_h) return false;
  if (req_w <= 0 || req_h <= 0) return false;

  // Already correct size — nothing to do.
  if (*fbo != 0 && *cur_w == req_w && *cur_h == req_h)
    return true;

  // Destroy previous target if dimensions changed.
  if (*fbo != 0)
    R_DestroyWindowTarget(fbo, tex, cur_w, cur_h);

  GLuint new_tex = R_CreateTextureRGBA(req_w, req_h, NULL,
                                        R_FILTER_LINEAR, R_WRAP_CLAMP);
  if (!new_tex) return false;

  GLuint new_fbo = 0;
  glGenFramebuffers(1, &new_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, new_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, new_tex, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &new_fbo);
    R_DeleteTexture(new_tex);
    return false;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  *fbo   = new_fbo;
  *tex   = new_tex;
  *cur_w = req_w;
  *cur_h = req_h;
  return true;
}

void R_DestroyWindowTarget(uint32_t *fbo, uint32_t *tex,
                           int *w, int *h) {
  if (fbo && *fbo != 0) {
    GLuint id = (GLuint)*fbo;
    glDeleteFramebuffers(1, &id);
    *fbo = 0;
  }
  if (tex && *tex != 0) {
    R_DeleteTexture(*tex);
    *tex = 0;
  }
  if (w) *w = 0;
  if (h) *h = 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(_WIN32) || defined(_WIN64)
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#else
#  include <unistd.h>
#endif

#include <orion/ui.h>
#include <orion/user/gl_compat.h>
#include <orion/user/color.h>
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
  sprite_program_t present_sprite;
  sprite_program_t indexed_sprite;
  GLuint indexed_palette;
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

typedef struct {
  GLuint fbo;
  GLuint tex;
  int width, height;
  R_ScreenCompositionMode requested;
  R_ScreenCompositionMode active;
} screen_composition_t;

static screen_composition_t g_screen_composition = {
  .requested = R_SCREEN_COMPOSITION_AUTO,
  .active = R_SCREEN_COMPOSITION_SRGB8,
};
static int g_fp16_blend_capability = -1;

typedef struct texture_metadata_t {
  GLuint id;
  R_TextureFormat format;
  int width, height;
  bool premultiplied;
  struct texture_metadata_t *next;
} texture_metadata_t;

static texture_metadata_t *g_texture_metadata;

static uint8_t *premultiply_srgba8(const uint8_t *rgba, int w, int h) {
  if (!rgba || w <= 0 || h <= 0 ||
      (size_t)w > SIZE_MAX / 4 / (size_t)h)
    return NULL;
  size_t bytes = (size_t)w * (size_t)h * 4;
  uint8_t *out = malloc(bytes);
  if (!out) return NULL;
  for (size_t i = 0; i < bytes; i += 4) {
    uint8_t a = rgba[i + 3];
    float alpha = (float)a / 255.0f;
    out[i + 0] = ui_linear_to_srgb8(ui_srgb8_to_linear(rgba[i + 0]) * alpha);
    out[i + 1] = ui_linear_to_srgb8(ui_srgb8_to_linear(rgba[i + 1]) * alpha);
    out[i + 2] = ui_linear_to_srgb8(ui_srgb8_to_linear(rgba[i + 2]) * alpha);
    out[i + 3] = a;
  }
  return out;
}

static bool register_texture_metadata(GLuint id, R_TextureFormat format,
                                     int width, int height) {
  texture_metadata_t *meta = malloc(sizeof(*meta));
  if (!meta) return false;
  meta->id = id;
  meta->format = format;
  meta->width = width;
  meta->height = height;
  meta->premultiplied = format == R_TEXTURE_SRGBA8_COLOR ||
                        format == R_TEXTURE_RGBA16F_LINEAR;
  meta->next = g_texture_metadata;
  g_texture_metadata = meta;
  return true;
}

static texture_metadata_t *find_texture_metadata(uint32_t id) {
  for (texture_metadata_t *meta = g_texture_metadata; meta; meta = meta->next)
    if (meta->id == (GLuint)id) return meta;
  return NULL;
}

static void draw_rect_program_common(int tex, int x, int y, int w, int h,
                                     float alpha, uint32_t program,
                                     float mix_amount,
                                     const ui_render_effect_params_t *params,
                                     bool premultiplied_output);

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

  g_ref.present_sprite.program = load_program_from_files("sprite_present.frag.glsl",
                                                         "position", "texcoord", "color");
  if (!g_ref.present_sprite.program) {
    ui_shutdown_prog();
    return false;
  }
  cache_sprite_uniforms(&g_ref.present_sprite);

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
  g_vga.palette_texture = R_CreateTextureSRGBA8(256, 1, NULL,
                                                R_FILTER_NEAREST, R_WRAP_CLAMP);
  if (!g_vga.palette_texture) {
    fprintf(stderr, "[renderer] VGA palette texture allocation failed\n");
    fflush(stderr);
    ui_shutdown_prog();
    return false;
  }

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

  const char *composition = getenv("ORION_SCREEN_COMPOSITION");
  if (composition && strcmp(composition, "srgb8") == 0)
    R_SetScreenCompositionMode(R_SCREEN_COMPOSITION_SRGB8);
  else if (composition && strcmp(composition, "fp16") == 0)
    R_SetScreenCompositionMode(R_SCREEN_COMPOSITION_FP16);
  else if (composition && strcmp(composition, "auto") == 0)
    R_SetScreenCompositionMode(R_SCREEN_COMPOSITION_AUTO);
  else if (composition && composition[0]) {
    fprintf(stderr, "[renderer] unknown screen composition mode=%s; using auto\n", composition);
    fflush(stderr);
    R_SetScreenCompositionMode(R_SCREEN_COMPOSITION_AUTO);
  }

  return true;
}

void ui_shutdown_prog(void) {
  // Delete shader program and buffers
  R_DestroyScreenComposition();
  R_DeleteTexture(g_vga.palette_texture);
  SAFE_DELETE(g_ref.copy_sprite.program, glDeleteProgram);
  SAFE_DELETE(g_ref.present_sprite.program, glDeleteProgram);
  SAFE_DELETE(g_ref.indexed_sprite.program, glDeleteProgram);
  R_DeleteTexture(g_ref.indexed_palette);
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

void end_draw_transform(const float saved[16]) {
  memcpy(&g_ref.projection, saved, sizeof(g_ref.projection));
  fmat16_copy(&g_ref.projection, &g_active_projection);
  update_sprite_projection_uniforms(&g_ref.projection);
}

void begin_draw_transform(const view_matrix_t *view, float saved[16]) {
  memcpy(saved, get_sprite_matrix(), sizeof(float) * 16);
  float c = view->a, s = view->b, x = view->tx, y = view->ty, transformed[16];
  memcpy(transformed, saved, sizeof(transformed));
  for (int row = 0; row < 4; row++) {
    transformed[row] = saved[row] * c + saved[4 + row] * s;
    transformed[4 + row] = -saved[row] * s + saved[4 + row] * c;
    transformed[12 + row] = saved[row] * x + saved[4 + row] * y + saved[12 + row];
  }
  end_draw_transform(transformed);
}

// Draw a sprite at the specified screen position
void draw_rect_ex(int tex, irect16_t r, int type, float alpha) {
  if (!g_ref.vga_program) return;
  push_sprite_args(tex, r.x, r.y, r.w, r.h, alpha);
  bool premultiplied = R_TextureIsPremultiplied((uint32_t)tex);
  glUniform4f(g_ref.copy_sprite.params1_u, premultiplied ? 1.0f : 0.0f,
              0.0f, 0.0f, 0.0f);
  
  // Source-over alpha keeps opaque window surfaces opaque under faded sprites.
  glEnable(GL_BLEND);
  if (premultiplied) R_BlendPremultiplied();
  else glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                           GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
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

void draw_indexed_rect(uint32_t tex, irect16_t r, const uint32_t palette[256], int transparent, float alpha) {
  if (!tex || !palette || transparent < 0 || transparent > 255 || r.w <= 0 || r.h <= 0) {
    fprintf(stderr, "[renderer] indexed draw rejected tex=%u transparent=%d rect=%d,%d,%d,%d\n",
            tex, transparent, r.x, r.y, r.w, r.h);
    fflush(stderr);
    return;
  }
  sprite_program_t *prog = &g_ref.indexed_sprite;
  if (!prog->program) {
    prog->program = load_program_from_files("sprite_indexed.frag.glsl", "position", "texcoord", "color");
    if (!prog->program) {
      fprintf(stderr, "[renderer] indexed shader unavailable\n");
      fflush(stderr);
      return;
    }
    cache_sprite_uniforms(prog);
  }
  uint8_t rgba[256 * 4];
  for (int i = 0; i < 256; i++) {
    rgba[i * 4] = (uint8_t)palette[i]; rgba[i * 4 + 1] = (uint8_t)(palette[i] >> 8);
    rgba[i * 4 + 2] = (uint8_t)(palette[i] >> 16);
    rgba[i * 4 + 3] = i == transparent ? 0 : (uint8_t)(palette[i] >> 24);
  }
  glActiveTexture(GL_TEXTURE1);
  if (!g_ref.indexed_palette)
    g_ref.indexed_palette = R_CreateTextureSRGBA8(256, 1, rgba, R_FILTER_NEAREST, R_WRAP_CLAMP);
  else R_UpdateTextureRGBA(g_ref.indexed_palette, 0, 0, 256, 1, rgba);
  glBindTexture(GL_TEXTURE_2D, g_ref.indexed_palette);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  glUseProgram(prog->program);
  glUniformMatrix4fv(prog->projection_u, 1, GL_FALSE, fmat16_data(&g_active_projection));
  glUniform2f(prog->offset_u, r.x, r.y);
  glUniform2f(prog->scale_u, r.w, r.h);
  glUniform2f(prog->uv_offset_u, 0, 0);
  glUniform2f(prog->uv_scale_u, 1, 1);
  glUniform1f(prog->alpha_u, alpha);
  glUniform1i(prog->tex0_u, 0);
  glUniform1i(glGetUniformLocation(prog->program, "palette_tex"), 1);
  R_BlendPremultiplied();
  g_ref.mesh.draw_mode = GL_TRIANGLE_FAN;
  R_MeshDraw(&g_ref.mesh);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
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
  bool premultiplied = R_TextureIsPremultiplied((uint32_t)tex);
  glUniform4f(prog->params1_u, premultiplied ? 1.0f : 0.0f,
              0.0f, 0.0f, 0.0f);

  float tr = ((color      ) & 0xFF) / 255.0f;
  float tg = ((color >>  8) & 0xFF) / 255.0f;
  float tb = ((color >> 16) & 0xFF) / 255.0f;
  float ta = 1.0f;
  glUniform4f(prog->tint_u, tr, tg, tb, ta);

  glUniform2f(prog->uv_offset_u, u0, v0);
  glUniform2f(prog->uv_scale_u, u1 - u0, v1 - v0);
  if (flags & DRAW_SPRITE_NO_ALPHA) {
    glDisable(GL_BLEND);
  } else if (premultiplied) {
    R_BlendPremultiplied();
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
      glBlendFuncSeparate(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA,
                          GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      break;
    case UI_LAYER_BLEND_SCREEN:
      glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR,
                          GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      break;
    case UI_LAYER_BLEND_ADD:
      glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      break;
    case UI_LAYER_BLEND_NORMAL:
    default:
      glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA,
                          GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      break;
  }
  draw_rect_program_common(tex, x, y, w, h, alpha, program, mix_amount,
                           params, true);
  glDisable(GL_BLEND);
}

void draw_rect_blend(int tex, int x, int y, int w, int h, float alpha,
                     ui_layer_blend_t blend) {
  if (!g_ref.vga_program) return;
  push_sprite_args(tex, x, y, w, h, alpha);
  bool premultiplied = R_TextureIsPremultiplied((uint32_t)tex);
  glUniform4f(g_ref.copy_sprite.params1_u, premultiplied ? 1.0f : 0.0f,
              0.0f, 0.0f, 0.0f);
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  switch (blend) {
    case UI_LAYER_BLEND_MULTIPLY:
      glBlendFuncSeparate(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA,
                          GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      break;
    case UI_LAYER_BLEND_SCREEN:
      glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR,
                          GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      break;
    case UI_LAYER_BLEND_ADD:
      glBlendFuncSeparate(premultiplied ? GL_ONE : GL_SRC_ALPHA, GL_ONE,
                          GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      break;
    case UI_LAYER_BLEND_NORMAL:
    default:
      glBlendFuncSeparate(premultiplied ? GL_ONE : GL_SRC_ALPHA,
                          GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
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
                                     const ui_render_effect_params_t *params,
                                     bool premultiplied_output) {
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
  GLint source_premultiplied_u = glGetUniformLocation(program, "source_premultiplied");
  GLint output_premultiplied_u = glGetUniformLocation(program, "output_premultiplied");
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
  if (source_premultiplied_u >= 0)
    glUniform1f(source_premultiplied_u,
                R_TextureIsPremultiplied((uint32_t)tex) ? 1.0f : 0.0f);
  if (output_premultiplied_u >= 0)
    glUniform1f(output_premultiplied_u, premultiplied_output ? 1.0f : 0.0f);
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
  draw_rect_program_common(src_tex, 0, 0, w, h, 1.0f, program, mix_amount,
                           params, false);
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
static void render_rounded_box(int tex, irect16_t r, int win_w, int win_h,
                                float radius, float alpha, uint32_t color,
                                float blur, float padding, bool premultiplied) {
  if (!g_ref.rounded_rect_sprite.program || (!tex && blur <= 0)) return;
  glUseProgram(g_ref.rounded_rect_sprite.program);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
  glUniform1i(g_ref.rounded_rect_sprite.tex0_u, 0);
  glUniform2f(g_ref.rounded_rect_sprite.offset_u, (float)r.x, (float)r.y);
  glUniform2f(g_ref.rounded_rect_sprite.scale_u, (float)r.w, (float)r.h);
  glUniform1f(g_ref.rounded_rect_sprite.alpha_u, alpha);
  glUniform4f(g_ref.rounded_rect_sprite.params0_u, blur, padding, 0.0f, 0.0f);
  glUniform2f(g_ref.rounded_rect_sprite.uv_offset_u, 0.0f, 1.0f);
  glUniform2f(g_ref.rounded_rect_sprite.uv_scale_u, 1.0f, -1.0f);
  glUniform4f(g_ref.rounded_rect_sprite.tint_u,
              (color & 255) / 255.0f, ((color >> 8) & 255) / 255.0f,
              ((color >> 16) & 255) / 255.0f, (color >> 24) / 255.0f);
  // SDF-specific uniforms.
  glUniform2f(g_rounded_rect.size_u, (float)win_w, (float)win_h);
  glUniform1f(g_rounded_rect.radius_u, MAX(0.0f, MIN(radius, MIN(win_w, win_h) * 0.5f)));
  premultiplied = premultiplied || R_TextureIsPremultiplied((uint32_t)tex);
  glUniform4f(g_ref.rounded_rect_sprite.params1_u, premultiplied ? 1.0f : 0.0f,
              0.0f, 0.0f, 0.0f);
  R_BlendPremultiplied();
  g_ref.mesh.draw_mode = GL_TRIANGLE_FAN;
  R_MeshDraw(&g_ref.mesh);
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
}

void render_rounded_rect(int tex, irect16_t r, int win_w, int win_h,
                          float radius, float alpha, uint32_t color) {
  render_rounded_box(tex, r, win_w, win_h, radius, alpha, color, 0, 0, false);
}

void draw_rect_shadow(irect16_t r, float radius, float blur, ipoint16_t offset, uint32_t color) {
  if (r.w <= 0 || r.h <= 0 || !(color >> 24) || blur == 0) return;
  if (!(blur > 0 && blur <= 256) || !(radius >= 0)) {
    fprintf(stderr, "[renderer] invalid shadow radius=%g blur=%g\n", radius, blur);
    fflush(stderr);
    return;
  }
  int padding = (int)(blur * 3 + 1);
  irect16_t bounds = rect_inset(rect_offset(r, offset.x, offset.y), -padding);
  render_rounded_box(0, bounds, r.w, r.h, radius, 1, color, blur, padding, false);
}

void draw_rounded_rect(int tex, irect16_t r, int win_w, int win_h,
                       float radius, float alpha) {
  render_rounded_rect(tex, r, win_w, win_h, radius, alpha, 0xffffffff);
}

void draw_rounded_rect_premultiplied(int tex, irect16_t r, int win_w, int win_h,
                                     float radius, float alpha) {
  render_rounded_box(tex, r, win_w, win_h, radius, alpha, 0xffffffff,
                     0, 0, true);
}

bool read_texture_rgba(int src_tex, int w, int h, uint8_t *out_rgba) {
  if (!g_ref.vga_program || src_tex == 0 || w <= 0 || h <= 0 || !out_rgba) {
    fprintf(stderr, "[renderer] texture readback rejected tex=%d size=%dx%d\n",
            src_tex, w, h);
    fflush(stderr);
    return false;
  }

  GLuint fbo = 0;
  GLint prev_fbo = 0;
  GLint prev_view[4] = {0};
  GLint prev_scissor[4] = {0};
  GLint prev_pack = 4;

  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
  glGetIntegerv(GL_VIEWPORT, prev_view);
  glGetIntegerv(GL_SCISSOR_BOX, prev_scissor);
  glGetIntegerv(GL_PACK_ALIGNMENT, &prev_pack);

  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, (GLuint)src_tex, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    glDeleteFramebuffers(1, &fbo);
    glViewport(prev_view[0], prev_view[1], prev_view[2], prev_view[3]);
    glScissor(prev_scissor[0], prev_scissor[1], prev_scissor[2], prev_scissor[3]);
    glPixelStorei(GL_PACK_ALIGNMENT, prev_pack);
    fprintf(stderr, "[renderer] texture readback framebuffer incomplete tex=%d\n", src_tex);
    fflush(stderr);
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
    glPixelStorei(GL_PACK_ALIGNMENT, prev_pack);
    fprintf(stderr, "[renderer] texture readback allocation failed tex=%d size=%dx%d\n",
            src_tex, w, h);
    fflush(stderr);
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
  glPixelStorei(GL_PACK_ALIGNMENT, prev_pack);
  return true;
}

static bool read_texture_rgba_float(int src_tex, int w, int h, float *out_rgba) {
  if (src_tex == 0 || w <= 0 || h <= 0 || !out_rgba ||
      (size_t)w > SIZE_MAX / (sizeof(float) * 4) / (size_t)h) {
    fprintf(stderr, "[renderer] float texture readback rejected tex=%d size=%dx%d\n",
            src_tex, w, h);
    fflush(stderr);
    return false;
  }
  GLuint fbo = 0;
  GLint prev_fbo = 0, prev_view[4] = {0}, prev_scissor[4] = {0}, prev_pack = 4;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
  glGetIntegerv(GL_VIEWPORT, prev_view);
  glGetIntegerv(GL_SCISSOR_BOX, prev_scissor);
  glGetIntegerv(GL_PACK_ALIGNMENT, &prev_pack);
  glGenFramebuffers(1, &fbo);
  if (!fbo) {
    fprintf(stderr, "[renderer] float readback framebuffer allocation failed tex=%d\n", src_tex);
    fflush(stderr);
    return false;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, (GLuint)src_tex, 0);
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "[renderer] float readback framebuffer incomplete tex=%d status=0x%x\n",
            src_tex, status);
    fflush(stderr);
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
  size_t row_size = (size_t)w * 4 * sizeof(float);
  float *tmp = malloc((size_t)h * row_size);
  if (!tmp) {
    fprintf(stderr, "[renderer] float readback allocation failed tex=%d size=%dx%d\n",
            src_tex, w, h);
    fflush(stderr);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    glDeleteFramebuffers(1, &fbo);
    glViewport(prev_view[0], prev_view[1], prev_view[2], prev_view[3]);
    glScissor(prev_scissor[0], prev_scissor[1], prev_scissor[2], prev_scissor[3]);
    glPixelStorei(GL_PACK_ALIGNMENT, prev_pack);
    return false;
  }
  glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, tmp);
  for (int y = 0; y < h; y++)
    memcpy(out_rgba + (size_t)y * row_size,
           tmp + (size_t)(h - 1 - y) * row_size, row_size);
  free(tmp);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
  glDeleteFramebuffers(1, &fbo);
  glViewport(prev_view[0], prev_view[1], prev_view[2], prev_view[3]);
  glScissor(prev_scissor[0], prev_scissor[1], prev_scissor[2], prev_scissor[3]);
  glPixelStorei(GL_PACK_ALIGNMENT, prev_pack);
  return true;
}

bool R_ReadTextureSRGBA8(uint32_t tex, int w, int h, uint8_t *out_rgba) {
  if (!tex || w <= 0 || h <= 0 || !out_rgba ||
      (size_t)w > SIZE_MAX / 4 / (size_t)h) {
    fprintf(stderr, "[renderer] sRGB readback rejected tex=%u size=%dx%d\n", tex, w, h);
    fflush(stderr);
    return false;
  }
  texture_metadata_t *meta = find_texture_metadata(tex);
  if (meta && (w > meta->width || h > meta->height)) {
    fprintf(stderr, "[renderer] sRGB readback out of range tex=%u request=%dx%d size=%dx%d\n",
            tex, w, h, meta->width, meta->height);
    fflush(stderr);
    return false;
  }
  R_TextureFormat format = meta ? meta->format : R_TEXTURE_RGBA8_DATA;
  if (format == R_TEXTURE_RGBA8_DATA)
    return read_texture_rgba((int)tex, w, h, out_rgba);

  size_t pixel_count = (size_t)w * (size_t)h;
  float *linear = NULL;
  uint8_t *encoded = NULL;
  if (format == R_TEXTURE_RGBA16F_LINEAR) {
    if (pixel_count > SIZE_MAX / (4 * sizeof(float))) {
      fprintf(stderr, "[renderer] sRGB float readback size overflow tex=%u size=%dx%d\n",
              tex, w, h);
      fflush(stderr);
      return false;
    }
    linear = malloc(pixel_count * 4 * sizeof(float));
    if (!linear) {
      fprintf(stderr, "[renderer] sRGB float readback allocation failed tex=%u size=%dx%d\n",
              tex, w, h);
      fflush(stderr);
      return false;
    }
    if (!read_texture_rgba_float((int)tex, w, h, linear)) {
      free(linear);
      return false;
    }
  } else {
    encoded = malloc(pixel_count * 4);
    if (!encoded) {
      fprintf(stderr, "[renderer] sRGB readback allocation failed tex=%u size=%dx%d\n",
              tex, w, h);
      fflush(stderr);
      return false;
    }
    if (!read_texture_rgba((int)tex, w, h, encoded)) {
      free(encoded);
      return false;
    }
  }

  for (size_t i = 0; i < pixel_count; i++) {
    float alpha = format == R_TEXTURE_RGBA16F_LINEAR
                ? linear[i * 4 + 3] : (float)encoded[i * 4 + 3] / 255.0f;
    if (!isfinite(alpha) || alpha <= 0.0f) {
      out_rgba[i * 4] = out_rgba[i * 4 + 1] = out_rgba[i * 4 + 2] = 0;
      out_rgba[i * 4 + 3] = 0;
      continue;
    }
    for (int c = 0; c < 3; c++) {
      float premultiplied = format == R_TEXTURE_RGBA16F_LINEAR
                          ? linear[i * 4 + c]
                          : ui_srgb8_to_linear(encoded[i * 4 + c]);
      out_rgba[i * 4 + c] = ui_linear_to_srgb8(premultiplied / alpha);
    }
    if (alpha >= 1.0f) out_rgba[i * 4 + 3] = 255;
    else out_rgba[i * 4 + 3] = (uint8_t)(alpha * 255.0f + 0.5f);
  }
  free(linear);
  free(encoded);
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
uint32_t R_CreateTexture(int w, int h, R_TextureFormat format,
                         const void *pixels, R_TextureFilter filter,
                         R_TextureWrap wrap) {
  if (w <= 0 || h <= 0 || format < R_TEXTURE_RGBA8_DATA ||
      format > R_TEXTURE_RGBA16F_LINEAR ||
      filter < R_FILTER_NEAREST || filter > R_FILTER_LINEAR ||
      wrap < R_WRAP_CLAMP || wrap > R_WRAP_REPEAT) {
    fprintf(stderr, "[renderer] RGBA texture rejected size=%dx%d format=%d filter=%d wrap=%d\n",
            w, h, format, filter, wrap);
    fflush(stderr);
    return 0;
  }
  size_t bytes_per_pixel = format == R_TEXTURE_RGBA16F_LINEAR ? 16 : 4;
  if ((size_t)w > SIZE_MAX / bytes_per_pixel / (size_t)h) {
    fprintf(stderr, "[renderer] RGBA texture size overflow size=%dx%d format=%d\n",
            w, h, format);
    fflush(stderr);
    return 0;
  }
  const void *upload = pixels;
  uint8_t *premultiplied = NULL;
  if (format == R_TEXTURE_SRGBA8_COLOR && pixels) {
    premultiplied = premultiply_srgba8(pixels, w, h);
    if (!premultiplied) {
      fprintf(stderr, "[renderer] sRGB premultiplication allocation failed size=%dx%d\n",
              w, h);
      fflush(stderr);
      return 0;
    }
    upload = premultiplied;
  }
  GLenum internal_format = format == R_TEXTURE_SRGBA8_COLOR ? GL_SRGB8_ALPHA8 :
                           format == R_TEXTURE_RGBA16F_LINEAR ? GL_RGBA16F : GL_RGBA;
  GLenum data_type = format == R_TEXTURE_RGBA16F_LINEAR ? GL_FLOAT : GL_UNSIGNED_BYTE;
  GLuint tex = 0;
  GLint previous_unpack = 4;
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &previous_unpack);
  glGenTextures(1, &tex);
  if (!tex) {
    fprintf(stderr, "[renderer] RGBA texture allocation failed size=%dx%d format=%d\n",
            w, h, format);
    fflush(stderr);
    free(premultiplied);
    return 0;
  }
  glBindTexture(GL_TEXTURE_2D, tex);
  GLenum gl_filter = (filter == R_FILTER_LINEAR) ? GL_LINEAR : GL_NEAREST;
  GLenum gl_wrap   = (wrap   == R_WRAP_REPEAT)   ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl_wrap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl_wrap);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, internal_format, w, h, 0, GL_RGBA, data_type, upload);
  glPixelStorei(GL_UNPACK_ALIGNMENT, previous_unpack);
  free(premultiplied);
  GLenum error = glGetError();
  if (error != GL_NO_ERROR) {
    fprintf(stderr, "[renderer] RGBA texture failed size=%dx%d format=%d error=0x%x\n",
            w, h, format, error);
    fflush(stderr);
    glDeleteTextures(1, &tex);
    return 0;
  }
  if ((format == R_TEXTURE_SRGBA8_COLOR || format == R_TEXTURE_RGBA16F_LINEAR) &&
      !register_texture_metadata(tex, format, w, h)) {
    fprintf(stderr, "[renderer] texture metadata allocation failed tex=%u size=%dx%d\n",
            tex, w, h);
    fflush(stderr);
    glDeleteTextures(1, &tex);
    return 0;
  }
  return (uint32_t)tex;
}

uint32_t R_CreateTextureRGBA(int w, int h, const void *rgba,
                             R_TextureFilter filter, R_TextureWrap wrap) {
  return R_CreateTexture(w, h, R_TEXTURE_RGBA8_DATA, rgba, filter, wrap);
}

uint32_t R_CreateTextureSRGBA8(int w, int h, const void *rgba,
                               R_TextureFilter filter, R_TextureWrap wrap) {
  return R_CreateTexture(w, h, R_TEXTURE_SRGBA8_COLOR, rgba, filter, wrap);
}

bool R_TextureIsPremultiplied(uint32_t tex) {
  texture_metadata_t *meta = find_texture_metadata(tex);
  return meta ? meta->premultiplied : false;
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
  if (!tex || !rgba || w <= 0 || h <= 0 || x < 0 || y < 0) {
    fprintf(stderr, "[renderer] RGBA texture update rejected tex=%u rect=%d,%d,%d,%d\n",
            tex, x, y, w, h);
    fflush(stderr);
    return false;
  }
  texture_metadata_t *meta = find_texture_metadata(tex);
  if (meta && (w > meta->width - x || h > meta->height - y)) {
    fprintf(stderr, "[renderer] RGBA texture update out of range tex=%u rect=%d,%d,%d,%d size=%dx%d\n",
            tex, x, y, w, h, meta->width, meta->height);
    fflush(stderr);
    return false;
  }
  if (meta && meta->format == R_TEXTURE_RGBA16F_LINEAR) {
    fprintf(stderr, "[renderer] RGBA8 update rejected for float texture tex=%u rect=%d,%d,%d,%d\n",
            tex, x, y, w, h);
    fflush(stderr);
    return false;
  }
  const void *upload = rgba;
  uint8_t *premultiplied = NULL;
  if (meta && meta->format == R_TEXTURE_SRGBA8_COLOR) {
    premultiplied = premultiply_srgba8(rgba, w, h);
    if (!premultiplied) {
      fprintf(stderr, "[renderer] sRGB update conversion failed tex=%u size=%dx%d\n",
              tex, w, h);
      fflush(stderr);
      return false;
    }
    upload = premultiplied;
  }
  glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
  GLint previous_unpack = 4;
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &previous_unpack);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, upload);
  glPixelStorei(GL_UNPACK_ALIGNMENT, previous_unpack);
  free(premultiplied);
  GLenum error = glGetError();
  if (error != GL_NO_ERROR) {
    fprintf(stderr, "[renderer] RGBA texture update failed tex=%u rect=%d,%d,%d,%d error=0x%x\n",
            tex, x, y, w, h, error);
    fflush(stderr);
    return false;
  }
  return true;
}

void R_DeleteTexture(uint32_t id) {
  if (id == 0) return;
  texture_metadata_t **link = &g_texture_metadata;
  while (*link) {
    if ((*link)->id == (GLuint)id) {
      texture_metadata_t *old = *link;
      *link = old->next;
      free(old);
      break;
    }
    link = &(*link)->next;
  }
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

void R_SetFramebufferSRGB(bool enabled) {
#ifndef ORION_OPENGL_ES
  if (enabled) glEnable(GL_FRAMEBUFFER_SRGB);
  else glDisable(GL_FRAMEBUFFER_SRGB);
#else
  // OpenGL ES 3 applies sRGB decoding/encoding to sRGB texture/framebuffer
  // formats as part of the format contract, without a global enable bit.
  (void)enabled;
#endif
}

void R_BlendPremultiplied(void) {
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA,
                      GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);
}

void R_SetScreenCompositionMode(R_ScreenCompositionMode mode) {
  if (mode < R_SCREEN_COMPOSITION_AUTO || mode > R_SCREEN_COMPOSITION_FP16) {
    fprintf(stderr, "[renderer] screen composition mode rejected mode=%d\n", mode);
    fflush(stderr);
    return;
  }
  g_screen_composition.requested = mode;
  fprintf(stderr, "[renderer] screen composition requested mode=%s\n",
          mode == R_SCREEN_COMPOSITION_AUTO ? "auto" :
          mode == R_SCREEN_COMPOSITION_FP16 ? "fp16" : "srgb8");
  fflush(stderr);
}

R_ScreenCompositionMode R_GetScreenCompositionMode(void) {
  return g_screen_composition.active;
}

#ifdef ORION_OPENGL_ES
static bool gl_has_extension(const char *wanted) {
  GLint count = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &count);
  for (GLint i = 0; i < count; i++) {
    const char *extension = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
    if (extension && strcmp(extension, wanted) == 0) return true;
  }
  return false;
}
#endif

static bool fp16_composition_supported(void) {
#ifdef ORION_OPENGL_ES
  if (g_fp16_blend_capability >= 0)
    return g_fp16_blend_capability != 0;
  GLint major = 0, minor = 0;
  glGetIntegerv(GL_MAJOR_VERSION, &major);
  glGetIntegerv(GL_MINOR_VERSION, &minor);
  bool float_blend = (major > 3 || (major == 3 && minor >= 2)) ||
                     gl_has_extension("GL_EXT_float_blend");
  if (!float_blend) {
    fprintf(stderr, "[renderer] FP16 composition unavailable: float blending unsupported\n");
    fflush(stderr);
    g_fp16_blend_capability = 0;
    return false;
  }
  g_fp16_blend_capability = 1;
#endif
  return true;
}

static bool screen_target_create(int width, int height,
                                 R_ScreenCompositionMode mode,
                                 GLuint *out_fbo, GLuint *out_tex) {
  R_TextureFormat format = mode == R_SCREEN_COMPOSITION_FP16
                         ? R_TEXTURE_RGBA16F_LINEAR : R_TEXTURE_SRGBA8_COLOR;
  GLuint tex = R_CreateTexture(width, height, format, NULL,
                               R_FILTER_LINEAR, R_WRAP_CLAMP);
  if (!tex) return false;
  GLint previous_fbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_fbo);
  GLuint fbo = 0;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, tex, 0);
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous_fbo);
  if (!fbo || status != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "[renderer] screen target rejected mode=%s size=%dx%d fbo=%u status=0x%x\n",
            mode == R_SCREEN_COMPOSITION_FP16 ? "fp16" : "srgb8",
            width, height, fbo, status);
    fflush(stderr);
    if (fbo) glDeleteFramebuffers(1, &fbo);
    R_DeleteTexture(tex);
    return false;
  }
  *out_fbo = fbo;
  *out_tex = tex;
  uint64_t bytes_per_pixel = mode == R_SCREEN_COMPOSITION_FP16 ? 8u : 4u;
  uint64_t pixels = (uint64_t)(unsigned)width * (uint64_t)(unsigned)height;
  uint64_t bytes = pixels > UINT64_MAX / bytes_per_pixel
                 ? UINT64_MAX : pixels * bytes_per_pixel;
  fprintf(stderr, "[renderer] screen target ready mode=%s size=%dx%d bytes_per_pixel=%llu allocation_bytes=%llu\n",
          mode == R_SCREEN_COMPOSITION_FP16 ? "fp16" : "srgb8",
          width, height, (unsigned long long)bytes_per_pixel,
          (unsigned long long)bytes);
  fflush(stderr);
  return true;
}

static bool ensure_screen_target(int width, int height) {
  R_ScreenCompositionMode mode = g_screen_composition.requested;
  if (mode == R_SCREEN_COMPOSITION_AUTO || mode == R_SCREEN_COMPOSITION_FP16) {
    if (fp16_composition_supported()) mode = R_SCREEN_COMPOSITION_FP16;
    else mode = R_SCREEN_COMPOSITION_SRGB8;
  }
  if (g_screen_composition.fbo && g_screen_composition.width == width &&
      g_screen_composition.height == height && g_screen_composition.active == mode)
    return true;

  GLuint fbo = 0, tex = 0;
  if (!screen_target_create(width, height, mode, &fbo, &tex) &&
      mode == R_SCREEN_COMPOSITION_FP16) {
    fprintf(stderr, "[renderer] FP16 composition allocation failed size=%dx%d; falling back to sRGB8\n",
            width, height);
    fflush(stderr);
    mode = R_SCREEN_COMPOSITION_SRGB8;
    if (g_screen_composition.fbo && g_screen_composition.width == width &&
        g_screen_composition.height == height && g_screen_composition.active == mode)
      return true;
    if (!screen_target_create(width, height, mode, &fbo, &tex)) return false;
  } else if (!fbo) {
    return false;
  }

  if (g_screen_composition.fbo) glDeleteFramebuffers(1, &g_screen_composition.fbo);
  if (g_screen_composition.tex) R_DeleteTexture(g_screen_composition.tex);
  g_screen_composition.fbo = fbo;
  g_screen_composition.tex = tex;
  g_screen_composition.width = width;
  g_screen_composition.height = height;
  g_screen_composition.active = mode;
  return true;
}

bool R_BeginScreenComposition(int width, int height, uint32_t clear_color) {
  if (width <= 0 || height <= 0) {
    fprintf(stderr, "[renderer] screen composition rejected size=%dx%d\n", width, height);
    fflush(stderr);
    return false;
  }
  if (!ensure_screen_target(width, height)) {
    fprintf(stderr, "[renderer] screen composition target unavailable size=%dx%d\n",
            width, height);
    fflush(stderr);
    return false;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, g_screen_composition.fbo);
  glViewport(0, 0, width, height);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDepthMask(GL_TRUE);
  R_SetFramebufferSRGB(g_screen_composition.active == R_SCREEN_COMPOSITION_SRGB8);
  float r = ui_srgb8_to_linear((uint8_t)clear_color);
  float g = ui_srgb8_to_linear((uint8_t)(clear_color >> 8));
  float b = ui_srgb8_to_linear((uint8_t)(clear_color >> 16));
  glClearColor(r, g, b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  return true;
}

void R_PresentScreenComposition(int width, int height) {
  if (!g_screen_composition.tex || !g_ref.present_sprite.program ||
      width <= 0 || height <= 0) {
    fprintf(stderr, "[renderer] screen presentation rejected tex=%u size=%dx%d\n",
            g_screen_composition.tex, width, height);
    fflush(stderr);
    return;
  }
  glViewport(0, 0, width, height);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDepthMask(GL_TRUE);
  R_SetFramebufferSRGB(false);
#ifdef ORION_OPENGL_ES
  GLint target_fbo = 0, encoding = GL_LINEAR;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &target_fbo);
  GLenum attachment = target_fbo ? GL_COLOR_ATTACHMENT0 : GL_BACK;
  glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, attachment,
                                         GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING,
                                         &encoding);
  float encode_srgb = encoding == GL_SRGB ? 0.0f : 1.0f;
#else
  float encode_srgb = 1.0f;
#endif
  sprite_program_t *prog = &g_ref.present_sprite;
  glUseProgram(prog->program);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, g_screen_composition.tex);
  glUniform1i(prog->tex0_u, 0);
  glUniformMatrix4fv(prog->projection_u, 1, GL_FALSE,
                     fmat16_data(&g_active_projection));
  glUniform2f(prog->offset_u, 0.0f, 0.0f);
  glUniform2f(prog->scale_u, (float)screen_width, (float)screen_height);
  glUniform2f(prog->uv_offset_u, 0.0f, 1.0f);
  glUniform2f(prog->uv_scale_u, 1.0f, -1.0f);
  glUniform1f(prog->alpha_u, 1.0f);
  glUniform4f(prog->params0_u, encode_srgb, 0.0f, 0.0f, 0.0f);
  glUniform4f(prog->tint_u, 1.0f, 1.0f, 1.0f, 1.0f);
  g_ref.mesh.draw_mode = GL_TRIANGLE_FAN;
  R_MeshDraw(&g_ref.mesh);
  glEnable(GL_DEPTH_TEST);
}

void R_DestroyScreenComposition(void) {
  if (g_screen_composition.fbo) glDeleteFramebuffers(1, &g_screen_composition.fbo);
  if (g_screen_composition.tex) R_DeleteTexture(g_screen_composition.tex);
  g_screen_composition.fbo = 0;
  g_screen_composition.tex = 0;
  g_screen_composition.width = 0;
  g_screen_composition.height = 0;
  g_screen_composition.active = R_SCREEN_COMPOSITION_SRGB8;
  g_fp16_blend_capability = -1;
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
  if (!R_UpdateTextureRGBA(g_vga.palette_texture, 0, 0, 256, 1, pal)) {
    fprintf(stderr, "[renderer] VGA palette texture update failed\n");
    fflush(stderr);
    return false;
  }

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
  if (!fbo || !tex || !cur_w || !cur_h || req_w <= 0 || req_h <= 0) {
    fprintf(stderr, "[renderer] window target rejected fbo=%p tex=%p size=%dx%d\n",
            (void *)fbo, (void *)tex, req_w, req_h);
    fflush(stderr);
    return false;
  }

  // Already correct size — nothing to do.
  if (*fbo != 0 && *cur_w == req_w && *cur_h == req_h)
    return true;

  // Destroy previous target if dimensions changed.
  if (*fbo != 0)
    R_DestroyWindowTarget(fbo, tex, cur_w, cur_h);

  GLuint new_tex = R_CreateTextureSRGBA8(req_w, req_h, NULL,
                                         R_FILTER_LINEAR, R_WRAP_CLAMP);
  if (!new_tex) {
    fprintf(stderr, "[renderer] window target texture failed size=%dx%d\n", req_w, req_h);
    fflush(stderr);
    return false;
  }

  GLint previous_fbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_fbo);
  GLuint new_fbo = 0;
  glGenFramebuffers(1, &new_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, new_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, new_tex, 0);
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous_fbo);
  if (!new_fbo || status != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "[renderer] window target incomplete size=%dx%d fbo=%u status=0x%x\n",
            req_w, req_h, new_fbo, status);
    fflush(stderr);
    glDeleteFramebuffers(1, &new_fbo);
    R_DeleteTexture(new_tex);
    return false;
  }

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

void R_ClearWindowTarget(uint32_t fbo) {
  GLint previous;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
  GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glDisable(GL_SCISSOR_TEST);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  if (scissor) glEnable(GL_SCISSOR_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous);
}

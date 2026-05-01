#include "../ui.h"
#include "../user/gl_compat.h"

#if __has_include(<cglm/cglm.h>)
#  include <cglm/cglm.h>
#  if __has_include(<cglm/struct.h>)
#    include <cglm/struct.h>
#  endif
#else
#  include "cglm_compat.h"
#endif

#define OFFSET_OF(type, field) (void*)((size_t)&(((type *)0)->field))

static int screen_width, screen_height;

void ui_shutdown_prog(void);

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
  sprite_program_t sprite[UI_RENDER_EFFECT_COUNT];
  GLuint vga_program;    // VGA text renderer program
  R_Mesh mesh;           // Sprite mesh for drawing quads
  mat4 projection;       // Orthographic projection matrix
} renderer_system_t;

renderer_system_t g_ref = {0};
static mat4 g_active_projection;

typedef struct {
  GLuint program;
  GLint projection;
  GLint offset;
  GLint scale;
  GLint grid_size;
  GLint cell_tex;
  GLint font_tex;
  GLint ega_palette;
} vga_renderer_t;

static vga_renderer_t g_vga = {0};

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
  snprintf(path, sizeof(path), "%s/../share/imageeditor/shaders/%s",
           ui_get_exe_dir(), name);
  return read_text_file(path);
}

// Compile a shader
GLuint compile_shader(GLenum type, const char* src) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &src, 0);
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
  return g_ref.sprite[UI_RENDER_EFFECT_COPY].program;
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
  g_vga.grid_size   = glGetUniformLocation(g_ref.vga_program, "gridSize");
  g_vga.cell_tex    = glGetUniformLocation(g_ref.vga_program, "cellTex");
  g_vga.font_tex    = glGetUniformLocation(g_ref.vga_program, "fontTex");
  g_vga.ega_palette = glGetUniformLocation(g_ref.vga_program, "egaPalette[0]");
}

static const sprite_program_t *sprite_program_for_effect(ui_render_effect_t effect) {
  if (effect < 0 || effect >= UI_RENDER_EFFECT_COUNT)
    effect = UI_RENDER_EFFECT_COPY;
  return &g_ref.sprite[(int)effect];
}

static void update_sprite_projection_uniforms(const mat4 projection) {
  GLint prev_prog = 0;
  glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
  for (int i = 0; i < UI_RENDER_EFFECT_COUNT; i++) {
    if (!g_ref.sprite[i].program || g_ref.sprite[i].projection_u < 0)
      continue;
    glUseProgram(g_ref.sprite[i].program);
    glUniformMatrix4fv(g_ref.sprite[i].projection_u, 1, GL_FALSE, projection[0]);
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

  const char *sprite_fs[UI_RENDER_EFFECT_COUNT] = {
    "sprite_copy.frag.glsl",
    "sprite_mask.frag.glsl",
    "sprite_levels.frag.glsl",
    "sprite_invert.frag.glsl",
    "sprite_threshold.frag.glsl",
    "sprite_gradient.frag.glsl",
  };

  for (int i = 0; i < UI_RENDER_EFFECT_COUNT; i++) {
    g_ref.sprite[i].program = load_program_from_files(sprite_fs[i],
                                                       "position", "texcoord", "color");
    if (!g_ref.sprite[i].program) {
      ui_shutdown_prog();
      return false;
    }
    cache_sprite_uniforms(&g_ref.sprite[i]);
  }

  g_ref.vga_program = load_program_from_files("vga.frag.glsl",
                                              "position", "texcoord", NULL);
  if (!g_ref.vga_program) {
    ui_shutdown_prog();
    return false;
  }
  cache_vga_uniforms();

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
  //  glm_ortho(-offset_x, DOOM_WIDTH+offset_x, DOOM_HEIGHT, 0, -1, 1, g_ref.projection);
  screen_width = width / UI_WINDOW_SCALE;
  screen_height = height / UI_WINDOW_SCALE;
  glm_ortho(0, screen_width, ui_get_system_metrics(kSystemMetricScreenHeight), 0, -1, 1, g_ref.projection);
  glm_mat4_copy(g_ref.projection, g_active_projection);

  update_sprite_projection_uniforms(g_ref.projection);
  glUseProgram(g_ref.vga_program);
  glUniformMatrix4fv(g_vga.projection, 1, GL_FALSE, g_ref.projection[0]);

  return true;
}

void ui_shutdown_prog(void) {
  // Delete shader program and buffers
  for (int i = 0; i < UI_RENDER_EFFECT_COUNT; i++)
    SAFE_DELETE(g_ref.sprite[i].program, glDeleteProgram);
  SAFE_DELETE(g_ref.vga_program, glDeleteProgram);
  R_MeshDestroy(&g_ref.mesh);
}

void push_sprite_args(int tex, int x, int y, int w, int h, float alpha) {
  push_sprite_effect_args(tex, x, y, w, h, alpha, UI_RENDER_EFFECT_COPY, NULL);
}

void push_sprite_effect_args(int tex, int x, int y, int w, int h, float alpha,
                             ui_render_effect_t effect,
                             const ui_render_effect_params_t *params) {
  static const ui_render_effect_params_t kZeroParams = {{0}};
  const ui_render_effect_params_t *p = params ? params : &kZeroParams;
  const sprite_program_t *prog = sprite_program_for_effect(effect);
  if (!prog || !prog->program) return;

  glUseProgram(prog->program);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  glUniform1i(prog->tex0_u, 0);
  glUniform2f(prog->offset_u, x, y);
  glUniform2f(prog->scale_u, w, h);
  glUniform1f(prog->alpha_u, alpha);
  glUniform4f(prog->params0_u, p->f[0], p->f[1], p->f[2], p->f[3]);
  glUniform4f(prog->params1_u, p->f[4], p->f[5], p->f[6], p->f[7]);
  glUniform2f(prog->uv_offset_u, 0.0f, 0.0f);
  glUniform2f(prog->uv_scale_u, 1.0f, 1.0f);
  glUniform4f(prog->tint_u, 1.0f, 1.0f, 1.0f, 1.0f);
}

void set_projection(int x, int y, int w, int h) {
  if (!g_ui_runtime.running) return;
  mat4 projection;
  glm_ortho(x, w, h, y, -1, 1, projection);
  glm_mat4_copy(projection, g_active_projection);
  glm_mat4_copy(projection, g_ref.projection);
  update_sprite_projection_uniforms(projection);
}

float *get_sprite_matrix(void) {
  return (float*)&g_ref.projection;
}

// Draw a sprite at the specified screen position
void draw_rect_ex(int tex, irect16_t r, int type, float alpha) {
  if (!g_ui_runtime.running) return;
  push_sprite_args(tex, r.x, r.y, r.w, r.h, alpha);
  
  // Enable blending for transparency
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
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
  if (!g_ui_runtime.running) return;
  const sprite_program_t *prog = sprite_program_for_effect(UI_RENDER_EFFECT_COPY);
  if (!prog || !prog->program) return;
  float u0 = uv ? uv->x : 0.0f;
  float v0 = uv ? uv->y : 0.0f;
  float u1 = uv ? uv->w : 1.0f;
  float v1 = uv ? uv->h : 1.0f;

  float alpha = ((color >> 24) & 0xFF) / 255.0f;
  if (flags & DRAW_SPRITE_NO_ALPHA)
    alpha = 1.0f;
  push_sprite_args(tex, r.x, r.y, r.w, r.h, alpha);

  float tr = ((color >> 16) & 0xFF) / 255.0f;
  float tg = ((color >> 8) & 0xFF) / 255.0f;
  float tb = (color & 0xFF) / 255.0f;
  float ta = 1.0f;
  glUniform4f(prog->tint_u, tr, tg, tb, ta);

  glUniform2f(prog->uv_offset_u, u0, v0);
  glUniform2f(prog->uv_scale_u, u1 - u0, v1 - v0);
  if (flags & DRAW_SPRITE_NO_ALPHA) {
    glDisable(GL_BLEND);
  } else {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }
  glDisable(GL_DEPTH_TEST);
  g_ref.mesh.draw_mode = GL_TRIANGLE_FAN;
  R_MeshDraw(&g_ref.mesh);
  glEnable(GL_DEPTH_TEST);
  if (!(flags & DRAW_SPRITE_NO_ALPHA))
    glDisable(GL_BLEND);
}

void draw_rect_effect(int tex, int x, int y, int w, int h,
                      ui_render_effect_t effect,
                      const ui_render_effect_params_t *params) {
  if (!g_ui_runtime.running) return;
  push_sprite_effect_args(tex, x, y, w, h, 1.0f, effect, params);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);
  g_ref.mesh.draw_mode = GL_TRIANGLE_FAN;
  R_MeshDraw(&g_ref.mesh);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
}

void draw_rect_effect_blend(int tex, int x, int y, int w, int h, float alpha,
                            ui_layer_blend_t blend,
                            ui_render_effect_t effect,
                            const ui_render_effect_params_t *params) {
  if (!g_ui_runtime.running) return;
  push_sprite_effect_args(tex, x, y, w, h, alpha, effect, params);
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
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      break;
  }
  glDisable(GL_DEPTH_TEST);
  g_ref.mesh.draw_mode = GL_TRIANGLE_FAN;
  R_MeshDraw(&g_ref.mesh);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
}

void draw_rect_blend(int tex, int x, int y, int w, int h, float alpha,
                     ui_layer_blend_t blend) {
  if (!g_ui_runtime.running) return;
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
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
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
                                     float mix_amount) {
  if (!g_ui_runtime.running || !program) return;
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
  glDisable(GL_DEPTH_TEST);
  g_ref.mesh.draw_mode = GL_TRIANGLE_FAN;
  R_MeshDraw(&g_ref.mesh);
  glEnable(GL_DEPTH_TEST);
}

void draw_rect_program_blend(int tex, int x, int y, int w, int h, float alpha,
                             ui_layer_blend_t blend, uint32_t program,
                             float mix_amount) {
  if (!g_ui_runtime.running || !program) return;
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
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      break;
  }
  draw_rect_program_common(tex, x, y, w, h, alpha, program, mix_amount);
  glDisable(GL_BLEND);
}

void draw_rect_program(int tex, int x, int y, int w, int h, uint32_t program,
                       float mix_amount) {
  draw_rect_program_blend(tex, x, y, w, h, 1.0f, UI_LAYER_BLEND_NORMAL,
                          program, mix_amount);
}

bool bake_texture_effect(int src_tex, int w, int h,
                         ui_render_effect_t effect,
                         const ui_render_effect_params_t *params,
                         uint32_t *out_tex) {
  if (!g_ui_runtime.running || w <= 0 || h <= 0 || !out_tex) return false;

  GLuint tex = 0;
  GLuint fbo = 0;
  GLint prev_fbo = 0, prev_prog = 0;
  GLint prev_view[4] = {0};
  GLint prev_scissor[4] = {0};
  float prev_proj[16];
  memcpy(prev_proj, get_sprite_matrix(), sizeof(prev_proj));

  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
  glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
  glGetIntegerv(GL_VIEWPORT, prev_view);
  glGetIntegerv(GL_SCISSOR_BOX, prev_scissor);

  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, NULL);

  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, tex, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    glUseProgram((GLuint)prev_prog);
    glViewport(prev_view[0], prev_view[1], prev_view[2], prev_view[3]);
    glScissor(prev_scissor[0], prev_scissor[1], prev_scissor[2], prev_scissor[3]);
    update_sprite_projection_uniforms(prev_proj);
    glUseProgram((GLuint)prev_prog);
    return false;
  }

  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  glViewport(0, 0, w, h);
  glScissor(0, 0, w, h);
  set_projection(0, 0, w, h);
  draw_rect_effect(src_tex, 0, 0, w, h, effect, params);

  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
  glDeleteFramebuffers(1, &fbo);
  glUseProgram((GLuint)prev_prog);
  glViewport(prev_view[0], prev_view[1], prev_view[2], prev_view[3]);
  glScissor(prev_scissor[0], prev_scissor[1], prev_scissor[2], prev_scissor[3]);
  update_sprite_projection_uniforms(prev_proj);
  glUseProgram((GLuint)prev_prog);

  *out_tex = tex;
  return true;
}

bool bake_texture_program(int src_tex, int w, int h, uint32_t program,
                          float mix_amount, uint32_t *out_tex) {
  if (!g_ui_runtime.running || src_tex == 0 || w <= 0 || h <= 0 || !program || !out_tex)
    return false;

  GLuint tex = R_CreateTextureRGBA(w, h, NULL, R_FILTER_LINEAR, R_WRAP_CLAMP);
  if (!tex) return false;

  GLuint fbo = 0;
  GLint prev_fbo = 0;
  GLint prev_view[4] = {0};
  GLint prev_scissor[4] = {0};
  GLint prev_prog = 0;
  mat4 prev_proj;
  memcpy(prev_proj, get_sprite_matrix(), sizeof(prev_proj));
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

  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  glViewport(0, 0, w, h);
  glScissor(0, 0, w, h);
  set_projection(0, 0, w, h);
  draw_rect_program_common(src_tex, 0, 0, w, h, 1.0f, program, mix_amount);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
  glDeleteFramebuffers(1, &fbo);
  glUseProgram((GLuint)prev_prog);
  glViewport(prev_view[0], prev_view[1], prev_view[2], prev_view[3]);
  glScissor(prev_scissor[0], prev_scissor[1], prev_scissor[2], prev_scissor[3]);
  update_sprite_projection_uniforms(prev_proj);
  *out_tex = tex;
  return true;
}

void draw_program_rect(int tex, irect16_t r, uint32_t program, float mix_amount) {
  draw_rect_program(tex, r.x, r.y, r.w, r.h, program, mix_amount);
}

bool read_texture_rgba(int src_tex, int w, int h, uint8_t *out_rgba) {
  if (!g_ui_runtime.running || src_tex == 0 || w <= 0 || h <= 0 || !out_rgba)
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

  glDrawBuffer(GL_COLOR_ATTACHMENT0);
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
  glm_ortho(0, screen_width, screen_height, 0, -1, 1, g_ref.projection);
  glm_mat4_copy(g_ref.projection, g_active_projection);
  update_sprite_projection_uniforms(g_ref.projection);
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

void R_DeleteTexture(uint32_t id) {
  if (id == 0) return;
  GLuint tex = (GLuint)id;
  glDeleteTextures(1, &tex);
}

void R_SetBlendMode(bool enabled) {
  if (enabled) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
  } else {
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
  }
}

bool R_DrawVGABuffer(const R_VgaBuffer *buf,
                     int x, int y,
                     int dst_w_px, int dst_h_px,
                     uint32_t font_tex,
                     const uint32_t palette16[16]) {
  if (!g_ui_runtime.running || !buf || !buf->vga_buffer || !font_tex ||
      !palette16 || buf->width <= 0 || buf->height <= 0 ||
      dst_w_px <= 0 || dst_h_px <= 0)
    return false;
  float pal[16 * 4];
  for (int i = 0; i < 16; i++) {
    pal[i * 4 + 0] = ((palette16[i] >> 16) & 0xFF) / 255.0f;
    pal[i * 4 + 1] = ((palette16[i] >> 8) & 0xFF) / 255.0f;
    pal[i * 4 + 2] = (palette16[i] & 0xFF) / 255.0f;
    pal[i * 4 + 3] = ((palette16[i] >> 24) & 0xFF) / 255.0f;
  }

  glUseProgram(g_ref.vga_program);
  glUniformMatrix4fv(g_vga.projection, 1, GL_FALSE, g_active_projection[0]);
  glUniform2f(g_vga.offset, (float)x, (float)y);
  glUniform2f(g_vga.scale, (float)dst_w_px, (float)dst_h_px);
  glUniform2f(g_vga.grid_size, (float)buf->width, (float)buf->height);
  glUniform4fv(g_vga.ega_palette, 16, pal);
  glUniform1i(g_vga.cell_tex, 0);
  glUniform1i(g_vga.font_tex, 1);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, (GLuint)buf->vga_buffer);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, (GLuint)font_tex);

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glBindVertexArray(g_ref.mesh.vao);
  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
  glBindVertexArray(0);
  glEnable(GL_DEPTH_TEST);
  return true;
}

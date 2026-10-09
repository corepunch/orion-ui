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
#include "vendor/gl_shader/gl_shader.c"

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

typedef struct {
  float projection[16], offset[2], scale[2], uv_offset[2], uv_scale[2];
  float tint[4], alpha, params0[4], params1[4], size[2], radius, edge[4];
  float glyph_uv[4], glyph_box[4], shadow_color[4], material[4], disabled;
  float grid_size[2], cell_size[2];
  int tex0, palette_tex, cell_tex, font_tex, vga_palette_tex;
} sprite_state_t;

typedef struct {
  shaderProg_t shader;
  shader_desc_t desc;
  sprite_state_t state;
} sprite_program_t;

typedef struct {
  sprite_program_t copy_sprite, present_sprite, indexed_sprite;
  GLuint indexed_palette;
  sprite_program_t gradient_sprite, plastic_sprite, rounded_rect_sprite;
  R_Mesh mesh;
  fmat16_t projection;
} renderer_system_t;

renderer_system_t g_ref = {0};
static fmat16_t g_active_projection;
static struct { sprite_program_t program; GLuint palette_texture; } g_vga;

#define SPRITE_UNIFORM(field, name, type) {offsetof(sprite_state_t, field), name, type, PRECISION_DEFAULT}
static const shaderUniform_t sprite_uniforms[] = {
  SPRITE_UNIFORM(projection, "projection", UT_FLOAT_MAT4),
  SPRITE_UNIFORM(offset, "offset", UT_FLOAT_VEC2), SPRITE_UNIFORM(scale, "scale", UT_FLOAT_VEC2),
  SPRITE_UNIFORM(uv_offset, "uv_offset", UT_FLOAT_VEC2), SPRITE_UNIFORM(uv_scale, "uv_scale", UT_FLOAT_VEC2),
  SPRITE_UNIFORM(tint, "tint", UT_FLOAT_VEC4), SPRITE_UNIFORM(alpha, "alpha", UT_FLOAT),
  SPRITE_UNIFORM(params0, "params0", UT_FLOAT_VEC4), SPRITE_UNIFORM(params1, "params1", UT_FLOAT_VEC4),
  SPRITE_UNIFORM(tex0, "tex0", UT_SAMPLER_2D), SPRITE_UNIFORM(palette_tex, "palette_tex", UT_SAMPLER_2D),
  SPRITE_UNIFORM(size, "size", UT_FLOAT_VEC2), SPRITE_UNIFORM(radius, "radius", UT_FLOAT),
  SPRITE_UNIFORM(edge, "edge", UT_FLOAT_VEC4), SPRITE_UNIFORM(glyph_uv, "glyph_uv", UT_FLOAT_VEC4),
  SPRITE_UNIFORM(glyph_box, "glyph_box", UT_FLOAT_VEC4), SPRITE_UNIFORM(shadow_color, "shadow_color", UT_FLOAT_VEC4), SPRITE_UNIFORM(material, "material", UT_FLOAT_VEC4),
  SPRITE_UNIFORM(disabled, "disabled", UT_FLOAT), SPRITE_UNIFORM(grid_size, "gridSize", UT_FLOAT_VEC2),
  SPRITE_UNIFORM(cell_size, "cellSize", UT_FLOAT_VEC2), SPRITE_UNIFORM(cell_tex, "cellTex", UT_SAMPLER_2D),
  SPRITE_UNIFORM(font_tex, "fontTex", UT_SAMPLER_2D), SPRITE_UNIFORM(vga_palette_tex, "paletteTex", UT_SAMPLER_2D),
};
#undef SPRITE_UNIFORM

static gs_options_t renderer_shader_options(void) {
  return (gs_options_t){.dialect =
#ifdef ORION_OPENGL_ES
    GLSL_DIALECT_ES3
#else
    GLSL_DIALECT_150
#endif
  };
}
static void sprite_vec2(float *v, float x, float y) { v[0] = x; v[1] = y; }
static void sprite_vec4(float *v, float x, float y, float z, float w) { v[0] = x; v[1] = y; v[2] = z; v[3] = w; }
static void delete_sprite_program(sprite_program_t *prog) {
  gs_delete(&prog->shader);
  free((void *)prog->desc.VertexBody);
  free((void *)prog->desc.FragmentBody);
  memset(prog, 0, sizeof(*prog));
}

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

int get_sprite_prog(void) {
  return g_ref.copy_sprite.shader.progid;
}

int get_sprite_vao(void) {
  return g_ref.mesh.vao;
}

static void update_sprite_projection_uniforms(const fmat16_t *projection) {
  sprite_program_t *programs[] = {&g_ref.copy_sprite, &g_ref.present_sprite, &g_ref.gradient_sprite,
    &g_ref.plastic_sprite, &g_ref.rounded_rect_sprite, &g_ref.indexed_sprite, &g_vga.program};
  for (size_t i = 0; i < ARRAY_LEN(programs); i++)
    memcpy(programs[i]->state.projection, fmat16_data(projection), sizeof(programs[i]->state.projection));
}

bool ui_load_program_with_attributes(const char *vs_src, const char *fs_src,
                                      const char *const *names, size_t count, uint32_t *out_program) {
  if (!out_program || (!names && count) || count > MAX_SHADER_ATTRIBS) {
    fprintf(stderr, "[renderer] invalid source program output or attribute count=%zu\n", count);
    fflush(stderr);
    return false;
  }
  shaderAttrib_t attrs[MAX_SHADER_ATTRIBS] = {0};
  for (size_t i = 0; i < count; i++) attrs[i] = (shaderAttrib_t){names[i], (uint32_t)i};
  gs_options_t options = renderer_shader_options();
  GLuint id = gs_link_sources(vs_src, fs_src, attrs, count, &options);
  if (!id) return false;
  *out_program = id;
  return true;
}
bool ui_load_program_from_source(const char *vs_src, const char *fs_src,
                                 const char *attrib0, const char *attrib1,
                                 const char *attrib2, uint32_t *out_program) {
  const char *names[] = {attrib0, attrib1, attrib2};
  return ui_load_program_with_attributes(vs_src, fs_src, names, ARRAY_LEN(names), out_program);
}
void ui_delete_program(uint32_t program) { if (program) glDeleteProgram(program); }

static bool load_sprite_program(sprite_program_t *prog, const char *name) {
  prog->desc = (shader_desc_t){.Name = name,
    .Attributes = {{"position", 0, UT_FLOAT_VEC2}, {"texcoord", 1, UT_FLOAT_VEC2}, {"color", 2, UT_COLOR}},
    .Shared = {{"tex", UT_FLOAT_VEC2}, {"col", UT_COLOR}},
    .VertexBody = read_shader_file("sprite.vert.glsl"), .FragmentBody = read_shader_file(name)};
  memcpy(prog->desc.Uniforms, sprite_uniforms, sizeof(sprite_uniforms));
  gs_options_t options = renderer_shader_options();
  if (!gs_load(&prog->shader, &prog->desc, &prog->state, sizeof(prog->state), &options)) {
    delete_sprite_program(prog);
    return false;
  }
  memcpy(prog->state.projection, fmat16_data(&g_active_projection), sizeof(prog->state.projection));
  return true;
}

// Initialize the sprite system
bool ui_init_prog(void) {
  memset(&g_ref, 0, sizeof(g_ref));
  memset(&g_vga, 0, sizeof(g_vga));

  static const struct { size_t offset; const char *name; } programs[] = {
    {offsetof(renderer_system_t, copy_sprite), "sprite_copy.frag.glsl"},
    {offsetof(renderer_system_t, present_sprite), "sprite_present.frag.glsl"},
    {offsetof(renderer_system_t, gradient_sprite), "sprite_gradient.frag.glsl"},
    {offsetof(renderer_system_t, plastic_sprite), "sprite_plastic.frag.glsl"},
    {offsetof(renderer_system_t, rounded_rect_sprite), "sprite_rounded_rect.frag.glsl"},
  };
  for (size_t i = 0; i < ARRAY_LEN(programs); i++) {
    if (!load_sprite_program((sprite_program_t *)((char *)&g_ref + programs[i].offset), programs[i].name)) {
      ui_shutdown_prog();
      return false;
    }
  }
  if (!load_sprite_program(&g_vga.program, "vga.frag.glsl")) { ui_shutdown_prog(); return false; }
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

  memcpy(g_vga.program.state.projection, fmat16_data(&g_ref.projection), sizeof(g_vga.program.state.projection));

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
  delete_sprite_program(&g_ref.copy_sprite);
  delete_sprite_program(&g_ref.present_sprite);
  delete_sprite_program(&g_ref.indexed_sprite);
  R_DeleteTexture(g_ref.indexed_palette);
  delete_sprite_program(&g_ref.gradient_sprite);
  delete_sprite_program(&g_ref.plastic_sprite);
  delete_sprite_program(&g_ref.rounded_rect_sprite);
  delete_sprite_program(&g_vga.program);
  R_MeshDestroy(&g_ref.mesh);
}

static void prepare_sprite_args(int tex, int x, int y, int w, int h, float alpha) {
  if (!g_ref.copy_sprite.shader.progid) return;

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  g_ref.copy_sprite.state.tex0 = 0;
  sprite_vec2(g_ref.copy_sprite.state.offset, x, y);
  sprite_vec2(g_ref.copy_sprite.state.scale, w, h);
  g_ref.copy_sprite.state.alpha = alpha;
  sprite_vec4(g_ref.copy_sprite.state.params0, 0.0f, 0.0f, 0.0f, 0.0f);
  sprite_vec4(g_ref.copy_sprite.state.params1, 0.0f, 0.0f, 0.0f, 0.0f);
  sprite_vec2(g_ref.copy_sprite.state.uv_offset, 0.0f, 0.0f);
  sprite_vec2(g_ref.copy_sprite.state.uv_scale, 1.0f, 1.0f);
  sprite_vec4(g_ref.copy_sprite.state.tint, 1.0f, 1.0f, 1.0f, 1.0f);
}

void push_sprite_args(int tex, int x, int y, int w, int h, float alpha) {
  if (!g_ref.copy_sprite.shader.progid) return;
  prepare_sprite_args(tex, x, y, w, h, alpha);
  gs_apply(&g_ref.copy_sprite.shader, &g_ref.copy_sprite.state);
}

void set_projection(int x, int y, int w, int h) {
  if (!g_vga.program.shader.progid) return;
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
  if (!g_vga.program.shader.progid) return;
  prepare_sprite_args(tex, r.x, r.y, r.w, r.h, alpha);
  bool premultiplied = R_TextureIsPremultiplied((uint32_t)tex);
  sprite_vec4(g_ref.copy_sprite.state.params1, premultiplied ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
  
  // Source-over alpha keeps opaque window surfaces opaque under faded sprites.
  glEnable(GL_BLEND);
  if (premultiplied) R_BlendPremultiplied();
  else glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                           GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  // Disable depth testing for UI elements
  glDisable(GL_DEPTH_TEST);
  
  // Use the appropriate drawing mode
  g_ref.mesh.draw_mode = type ? GL_LINE_LOOP : GL_TRIANGLE_FAN;
  if (!gs_apply(&g_ref.copy_sprite.shader, &g_ref.copy_sprite.state)) return;
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
  if (!prog->shader.progid) {
    load_sprite_program(prog, "sprite_indexed.frag.glsl");
    if (!prog->shader.progid) {
      fprintf(stderr, "[renderer] indexed shader unavailable\n");
      fflush(stderr);
      return;
    }
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

  memcpy(prog->state.projection, fmat16_data(&g_active_projection), sizeof(prog->state.projection));
  sprite_vec2(prog->state.offset, r.x, r.y);
  sprite_vec2(prog->state.scale, r.w, r.h);
  sprite_vec2(prog->state.uv_offset, 0, 0);
  sprite_vec2(prog->state.uv_scale, 1, 1);
  prog->state.alpha = alpha;
  prog->state.tex0 = 0;
  prog->state.palette_tex = 1;
  R_BlendPremultiplied();
  g_ref.mesh.draw_mode = GL_TRIANGLE_FAN;
  if (!gs_apply(&prog->shader, &prog->state)) return;
  R_MeshDraw(&g_ref.mesh);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
}

// Draw a sub-region of a sprite sheet at the specified screen position.
// uv packs normalized texture coordinates as floats: x=u0, y=v0, w=u1, h=v1.
void draw_sprite_region(int tex, irect16_t r,
                        frect_t const *uv,
                        uint32_t color, uint32_t flags) {
  if (!g_vga.program.shader.progid) return;
  sprite_program_t *prog = &g_ref.copy_sprite;
  if (!prog || !prog->shader.progid) return;
  float u0 = uv ? uv->x : 0.0f;
  float v0 = uv ? uv->y : 0.0f;
  float u1 = uv ? uv->w : 1.0f;
  float v1 = uv ? uv->h : 1.0f;

  float alpha = ((color >> 24) & 0xFF) / 255.0f;
  if (flags & DRAW_SPRITE_NO_ALPHA)
    alpha = 1.0f;
  prepare_sprite_args(tex, r.x, r.y, r.w, r.h, alpha);
  bool premultiplied = R_TextureIsPremultiplied((uint32_t)tex);
  sprite_vec4(prog->state.params1, premultiplied ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);

  float tr = ((color      ) & 0xFF) / 255.0f;
  float tg = ((color >>  8) & 0xFF) / 255.0f;
  float tb = ((color >> 16) & 0xFF) / 255.0f;
  float ta = 1.0f;
  sprite_vec4(prog->state.tint, tr, tg, tb, ta);

  sprite_vec2(prog->state.uv_offset, u0, v0);
  sprite_vec2(prog->state.uv_scale, u1 - u0, v1 - v0);
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
  if (!gs_apply(&prog->shader, &prog->state)) return;
  R_MeshDraw(&g_ref.mesh);
  glEnable(GL_DEPTH_TEST);
  if (!(flags & DRAW_SPRITE_NO_ALPHA))
    glDisable(GL_BLEND);
}

void render_plastic_surface(irect16_t r, float radius, float bevel, float shadow,
                            const plastic_look_t *look, uint32_t color, uint32_t shadow_color,
                            uint32_t icon_tex, const frect_t *icon_uv, ipoint16_t icon_size) {
  sprite_program_t *program = &g_ref.plastic_sprite;
  if (r.w <= 0 || r.h <= 0) return;
  if (!look) {
    fprintf(stderr, "[renderer] plastic surface rejected rect=%d,%d,%d,%d: missing look\n", r.x, r.y, r.w, r.h);
    fflush(stderr);
    return;
  }
  if (!program->shader.progid) {
    fprintf(stderr, "[renderer] plastic shader unavailable rect=%d,%d,%d,%d\n", r.x, r.y, r.w, r.h);
    fflush(stderr);
    return;
  }
  frect_t uv = icon_uv ? *icon_uv : (frect_t){0, 0, 1, 1};
  shadow = CLAMP(shadow, 0, MAX(0, MIN(r.w, r.h) * 0.5f - 1));
  float glyph_w = MIN(MAX(0, icon_size.x), MAX(0, r.w - 2 * shadow - 4));
  float glyph_h = MIN(MAX(0, icon_size.y), MAX(0, r.h - 2 * shadow - 4));

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, icon_tex ? icon_tex : g_vga.palette_texture);
  program->state.tex0 = 0;
  sprite_vec2(program->state.offset, r.x, r.y);
  sprite_vec2(program->state.scale, r.w, r.h);
  sprite_vec2(program->state.uv_offset, 0, 0);
  sprite_vec2(program->state.uv_scale, 1, 1);
  sprite_vec4(program->state.tint, (color & 255) / 255.0f, ((color >> 8) & 255) / 255.0f, ((color >> 16) & 255) / 255.0f, (color >> 24) / 255.0f);
  sprite_vec4(program->state.params0, r.w, r.h, MAX(0, radius), MAX(0, bevel));
  sprite_vec4(program->state.params1, shadow, look->pressed, look->hover, look->selected);
  sprite_vec4(program->state.material, look->gloss, look->rim, look->ink, look->lift);
  program->state.disabled = look->disabled;
  sprite_vec4(program->state.shadow_color, ui_srgb8_to_linear(shadow_color & 255), ui_srgb8_to_linear((shadow_color >> 8) & 255), ui_srgb8_to_linear((shadow_color >> 16) & 255), (shadow_color >> 24) / 255.0f);
  sprite_vec4(program->state.glyph_uv, uv.x, uv.y, uv.w, uv.h);
  sprite_vec4(program->state.glyph_box, (r.w - glyph_w) * 0.5f, (r.h - glyph_h) * 0.5f, icon_tex ? glyph_w : 0, icon_tex ? glyph_h : 0);
  R_BlendPremultiplied();
  g_ref.mesh.draw_mode = GL_TRIANGLE_FAN;
  if (!gs_apply(&program->shader, &program->state)) return;
  R_MeshDraw(&g_ref.mesh);
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
}

void draw_rect_gradient(int tex, int x, int y, int w, int h,
                        const ui_render_effect_params_t *params) {
  static const ui_render_effect_params_t kZeroParams = {{0}};
  const ui_render_effect_params_t *p = params ? params : &kZeroParams;
  if (!g_vga.program.shader.progid || !g_ref.gradient_sprite.shader.progid) return;

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  g_ref.gradient_sprite.state.tex0 = 0;
  sprite_vec2(g_ref.gradient_sprite.state.offset, x, y);
  sprite_vec2(g_ref.gradient_sprite.state.scale, w, h);
  g_ref.gradient_sprite.state.alpha = 1.0f;
  sprite_vec4(g_ref.gradient_sprite.state.params0, p->f[0], p->f[1], p->f[2], p->f[3]);
  sprite_vec4(g_ref.gradient_sprite.state.params1, p->f[4], p->f[5], p->f[6], p->f[7]);
  sprite_vec2(g_ref.gradient_sprite.state.uv_offset, 0.0f, 0.0f);
  sprite_vec2(g_ref.gradient_sprite.state.uv_scale, 1.0f, 1.0f);
  sprite_vec4(g_ref.gradient_sprite.state.tint, 1.0f, 1.0f, 1.0f, 1.0f);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);
  g_ref.mesh.draw_mode = GL_TRIANGLE_FAN;
  if (!gs_apply(&g_ref.gradient_sprite.shader, &g_ref.gradient_sprite.state)) return;
  R_MeshDraw(&g_ref.mesh);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
}

void draw_rect_program_params_blend(int tex, int x, int y, int w, int h,
                                    float alpha, ui_layer_blend_t blend,
                                    uint32_t program, float mix_amount,
                                    const ui_render_effect_params_t *params) {
  if (!g_vga.program.shader.progid || !program) return;
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
  if (!g_vga.program.shader.progid) return;
  prepare_sprite_args(tex, x, y, w, h, alpha);
  bool premultiplied = R_TextureIsPremultiplied((uint32_t)tex);
  sprite_vec4(g_ref.copy_sprite.state.params1, premultiplied ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
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
  if (!gs_apply(&g_ref.copy_sprite.shader, &g_ref.copy_sprite.state)) return;
  R_MeshDraw(&g_ref.mesh);
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
}

static void draw_rect_program_common(int tex, int x, int y, int w, int h,
                                     float alpha, uint32_t program,
                                     float mix_amount,
                                     const ui_render_effect_params_t *params,
                                     bool premultiplied_output) {
  if (!g_vga.program.shader.progid || !program) return;
  sprite_program_t *builtins[] = {&g_ref.copy_sprite, &g_ref.present_sprite, &g_ref.indexed_sprite,
    &g_ref.gradient_sprite, &g_ref.plastic_sprite, &g_ref.rounded_rect_sprite};
  for (size_t i = 0; i < ARRAY_LEN(builtins); i++)
    if (builtins[i]->shader.progid == program) gs_invalidate(&builtins[i]->shader);
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
  if (!g_vga.program.shader.progid || src_tex == 0 || w <= 0 || h <= 0 || !program || !out_tex)
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
                                float blur, float padding, bool premultiplied,
                                uint32_t edge_color, float edge_width, float stroke, int fill_style, float tail, float tail_x) {
  if (!g_ref.rounded_rect_sprite.shader.progid || (!tex && blur <= 0 && fill_style < 2)) return;

  glActiveTexture(GL_TEXTURE0);
  // A shadow passes tex 0 and never samples. Binding the default name makes
  // the macOS GL layer warn and substitute a zero texture.
  if (tex) glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
  g_ref.rounded_rect_sprite.state.tex0 = 0;
  sprite_vec2(g_ref.rounded_rect_sprite.state.offset, (float)r.x, (float)r.y);
  sprite_vec2(g_ref.rounded_rect_sprite.state.scale, (float)r.w, (float)r.h);
  g_ref.rounded_rect_sprite.state.alpha = alpha;
  sprite_vec4(g_ref.rounded_rect_sprite.state.params0, blur, padding, (float)fill_style, tail);
  sprite_vec2(g_ref.rounded_rect_sprite.state.uv_offset, 0.0f, 1.0f);
  sprite_vec2(g_ref.rounded_rect_sprite.state.uv_scale, 1.0f, -1.0f);
  sprite_vec4(g_ref.rounded_rect_sprite.state.tint, (color & 255) / 255.0f, ((color >> 8) & 255) / 255.0f, ((color >> 16) & 255) / 255.0f, (color >> 24) / 255.0f);
  // SDF-specific uniforms.
  sprite_vec2(g_ref.rounded_rect_sprite.state.size, (float)win_w, (float)win_h);
  g_ref.rounded_rect_sprite.state.radius = MAX(0.0f, MIN(radius, MIN(win_w, win_h) * 0.5f));
  premultiplied = premultiplied || R_TextureIsPremultiplied((uint32_t)tex);
  sprite_vec4(g_ref.rounded_rect_sprite.state.params1, premultiplied ? 1.0f : 0.0f, edge_width, MAX(0.0f, stroke), tail_x);
  sprite_vec4(g_ref.rounded_rect_sprite.state.edge, (edge_color & 255) / 255.0f, ((edge_color >> 8) & 255) / 255.0f, ((edge_color >> 16) & 255) / 255.0f, (edge_color >> 24) / 255.0f);
  R_BlendPremultiplied();
  g_ref.mesh.draw_mode = GL_TRIANGLE_FAN;
  if (!gs_apply(&g_ref.rounded_rect_sprite.shader, &g_ref.rounded_rect_sprite.state)) return;
  R_MeshDraw(&g_ref.mesh);
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
}

void render_rounded_rect(int tex, irect16_t r, int win_w, int win_h,
                          float radius, float alpha, uint32_t color) {
  render_rounded_box(tex, r, win_w, win_h, radius, alpha, color, 0, 0, false, 0, 0, 0, 0, 0, 0);
}

void render_tooltip_bubble(irect16_t r, isize16_t face_size, float radius, float tail,
                          float tail_offset, float padding, tooltip_tail_side_t side, uint32_t color) {
  if (side < TOOLTIP_TAIL_TOP || side > TOOLTIP_TAIL_RIGHT) {
    fprintf(stderr, "[renderer] invalid tooltip tail side=%d\n", side);
    fflush(stderr);
    return;
  }
  bool horizontal = side == TOOLTIP_TAIL_LEFT || side == TOOLTIP_TAIL_RIGHT;
  if (side == TOOLTIP_TAIL_BOTTOM || side == TOOLTIP_TAIL_RIGHT) tail = -tail;
  render_rounded_box(0, r, face_size.w, face_size.h, radius, 1, color,
                     0, padding, false, 0, 0, 0, horizontal ? 3 : 2, tail, tail_offset);
}

void render_rounded_rect_gradient(int tex, irect16_t r, int pixel_w, int pixel_h,
                                  float radius, uint32_t top, uint32_t bottom) {
  render_rounded_box(tex, r, pixel_w, pixel_h, radius, 1, top, 0, 0, false, bottom, 0, 0, 1, 0, 0);
}

void render_rounded_rect_stroke(int tex, irect16_t r, int win_w, int win_h,
                                float radius, float alpha, uint32_t color, float stroke) {
  render_rounded_box(tex, r, win_w, win_h, radius, alpha, color, 0, 0, false, 0, 0, stroke, 0, 0, 0);
}

void render_rounded_rect_edged(int tex, irect16_t r, int win_w, int win_h, float radius,
                               float alpha, uint32_t color, uint32_t edge_color, float edge_width) {
  render_rounded_box(tex, r, win_w, win_h, radius, alpha, color, 0, 0, false, edge_color, edge_width, 0, 0, 0, 0);
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
  render_rounded_box(0, bounds, r.w, r.h, radius, 1, color, blur, padding, false, 0, 0, 0, 0, 0, 0);
}

void draw_rounded_rect(int tex, irect16_t r, int win_w, int win_h,
                       float radius, float alpha) {
  render_rounded_rect(tex, r, win_w, win_h, radius, alpha, 0xffffffff);
}

void draw_rounded_rect_premultiplied(int tex, irect16_t r, int win_w, int win_h,
                                     float radius, float alpha) {
  render_rounded_box(tex, r, win_w, win_h, radius, alpha, 0xffffffff,
                     0, 0, true, 0, 0, 0, 0, 0, 0);
}

bool read_texture_rgba(int src_tex, int w, int h, uint8_t *out_rgba) {
  if (!g_vga.program.shader.progid || src_tex == 0 || w <= 0 || h <= 0 || !out_rgba) {
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

// Clear error flags left by earlier GL calls so the glGetError() check that
// follows reports only its own calls. Bounded: without a usable context
// glGetError() can keep returning an error.
static void drain_stale_gl_errors(const char *before) {
  for (int i = 0; i < 16; i++) {
    GLenum error = glGetError();
    if (error == GL_NO_ERROR) return;
    fprintf(stderr, "[renderer] stale GL error before %s error=0x%x\n", before, error);
    fflush(stderr);
  }
}

bool capture_framebuffer_rgba(int w, int h, uint8_t *out_rgba) {
  if (w <= 0 || h <= 0 || !out_rgba) {
    fprintf(stderr, "[renderer] framebuffer capture rejected size=%dx%d out=%p\n",
            w, h, (void *)out_rgba);
    fflush(stderr);
    return false;
  }

  GLint prev_fbo = 0;
  GLint prev_read = 0;
  GLint source = GL_NONE;
  GLint prev_view[4] = {0};
  GLint prev_scissor[4] = {0};

  drain_stale_gl_errors("framebuffer capture");
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
  glGetIntegerv(GL_READ_BUFFER, &prev_read);
  glGetIntegerv(GL_VIEWPORT, prev_view);
  glGetIntegerv(GL_SCISSOR_BOX, prev_scissor);

  size_t row_sz = (size_t)w * 4;
  uint8_t *tmp = malloc((size_t)h * row_sz);
  if (!tmp) {
    fprintf(stderr, "[renderer] framebuffer capture allocation failed size=%dx%d\n", w, h);
    fflush(stderr);
    return false;
  }
  glViewport(0, 0, w, h);
  glScissor(0, 0, w, h);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  // Read the colour buffer the frame was drawn into. On the default
  // framebuffer that is GL_BACK only when the context is double-buffered; a
  // single-buffered one has just GL_FRONT and rejects GL_BACK.
  glGetIntegerv(GL_DRAW_BUFFER0, &source);
  glReadBuffer((GLenum)source);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
  GLenum error = glGetError();

  glViewport(prev_view[0], prev_view[1], prev_view[2], prev_view[3]);
  glScissor(prev_scissor[0], prev_scissor[1], prev_scissor[2], prev_scissor[3]);
  glReadBuffer((GLenum)prev_read);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
  if (error != GL_NO_ERROR) {
    fprintf(stderr, "[renderer] framebuffer capture failed fbo=%d buffer=0x%x size=%dx%d error=0x%x\n",
            prev_fbo, source, w, h, error);
    fflush(stderr);
    free(tmp);
    return false;
  }
  for (int y = 0; y < h; y++) {
    memcpy(out_rgba + (size_t)y * row_sz,
           tmp + (size_t)(h - 1 - y) * row_sz,
           row_sz);
  }
  free(tmp);
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
  drain_stale_gl_errors("RGBA texture creation");
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
  drain_stale_gl_errors("R8 texture creation");
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
  drain_stale_gl_errors("RGBA texture update");
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
  if (!g_screen_composition.tex || !g_ref.present_sprite.shader.progid ||
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

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, g_screen_composition.tex);
  prog->state.tex0 = 0;
  memcpy(prog->state.projection, fmat16_data(&g_active_projection), sizeof(prog->state.projection));
  sprite_vec2(prog->state.offset, 0.0f, 0.0f);
  sprite_vec2(prog->state.scale, (float)screen_width, (float)screen_height);
  sprite_vec2(prog->state.uv_offset, 0.0f, 1.0f);
  sprite_vec2(prog->state.uv_scale, 1.0f, -1.0f);
  prog->state.alpha = 1.0f;
  sprite_vec4(prog->state.params0, encode_srgb, 0.0f, 0.0f, 0.0f);
  sprite_vec4(prog->state.tint, 1.0f, 1.0f, 1.0f, 1.0f);
  g_ref.mesh.draw_mode = GL_TRIANGLE_FAN;
  if (!gs_apply(&prog->shader, &prog->state)) return;
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
  if (!g_vga.program.shader.progid || !buf || !buf->vga_buffer || !font ||
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

  memcpy(g_vga.program.state.projection, fmat16_data(&g_active_projection), sizeof(g_vga.program.state.projection));
  sprite_vec2(g_vga.program.state.offset, (float)x, (float)y);
  sprite_vec2(g_vga.program.state.scale, (float)dst_w_px, (float)dst_h_px);
  sprite_vec2(g_vga.program.state.uv_offset, 0.0f, 0.0f);
  sprite_vec2(g_vga.program.state.uv_scale, 1.0f, 1.0f);
  sprite_vec2(g_vga.program.state.grid_size, (float)buf->width, (float)buf->height);
  sprite_vec2(g_vga.program.state.cell_size, (float)font->cell_w, (float)font->cell_h);
  g_vga.program.state.cell_tex = 0;
  g_vga.program.state.font_tex = 1;
  g_vga.program.state.vga_palette_tex = 2;

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
  if (!gs_apply(&g_vga.program.shader, &g_vga.program.state)) return false;
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

// ── Raster state and window targets: the only GL the window system reaches ─────────────────

void R_BindWindowTarget(uint32_t fbo) { glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo); }
void R_SetViewport(int x, int y, int w, int h) { glViewport(x, y, w, h); }
void R_SetScissor(int x, int y, int w, int h) { glEnable(GL_SCISSOR_TEST); glScissor(x, y, w, h); }
void R_DisableScissor(void) { glDisable(GL_SCISSOR_TEST); }

void R_PrintDeviceInfo(void) {
  printf("GL_VERSION  : %s\n", glGetString(GL_VERSION));
  printf("GLSL_VERSION: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
}

// ── Compositor ────────────────────────────────────────────────────────────────────────────
// The window system describes each redirected surface (position, shape, shadow, border); the
// compositor owns how they reach the screen: one reusable physical-pixel target, shadows, SDF
// rounded corners, then presentation. Windows never see any of it.

void R_Composite(const R_CompositeLayer *layers, int count, uint32_t clear_color,
                 int logical_w, int logical_h,
                 void (*draw_border)(const R_CompositeLayer *layer)) {
  if (count < 0 || (count > 0 && !layers) || logical_w <= 0 || logical_h <= 0) {
    fprintf(stderr, "[renderer] composite rejected layers=%p count=%d logical=%dx%d\n",
            (const void *)layers, count, logical_w, logical_h);
    fflush(stderr);
    return;
  }
  // iOS and offscreen hosts present a platform-owned, nonzero framebuffer.
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  axBindFramebuffer();
  struct AXsize size;
  axGetSize(&size);
  float scale = axGetScaling();
  int screen_w = (int)((float)size.width * scale + 0.5f);
  int screen_h = (int)((float)size.height * scale + 0.5f);
  bool composed = R_BeginScreenComposition(screen_w, screen_h, clear_color);
  if (!composed) {
    glViewport(0, 0, screen_w, screen_h);
    R_SetFramebufferSRGB(true);
  }
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glViewport(0, 0, screen_w, screen_h);
  set_projection(0, 0, logical_w, logical_h);

  for (int i = 0; i < count; i++) {
    const R_CompositeLayer *l = &layers[i];
    if (!l->tex) continue;
    float radius = MIN(l->corner_radius, (float)MIN(l->w, l->h) / 2);   // physical pixels
    if (l->shadow)
      draw_rect_shadow(l->frame, l->shadow_radius, l->shadow_blur, l->shadow_offset, l->shadow_color);
    draw_rounded_rect_premultiplied((int)l->tex, l->frame, l->w, l->h, radius, 1.0f);
    if (l->border && draw_border) draw_border(l);
  }

  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
  if (composed) {
    axBindFramebuffer();
    R_PresentScreenComposition(screen_w, screen_h);
  }
}

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
  GLuint program;        // Shader program
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

// Sprite shader sources
const char* sprite_vs_src = "#version 150 core\n"
"in vec2 position;\n"
"in vec2 texcoord;\n"
"in vec4 color;\n"
"out vec2 tex;\n"
"out vec4 col;\n"
"uniform mat4 projection;\n"
"uniform vec2 offset;\n"
"uniform vec2 scale;\n"
"uniform vec2 uv_offset;\n"
"uniform vec2 uv_scale;\n"
"void main() {\n"
"  col = color;\n"
"  tex = texcoord * uv_scale + uv_offset;\n"
"  gl_Position = projection * vec4(position * scale + offset, 0.0, 1.0);\n"
"}";

const char* sprite_fs_src = "#version 150 core\n"
"in vec2 tex;\n"
"in vec4 col;\n"
"out vec4 outColor;\n"
"uniform sampler2D tex0;\n"
"uniform vec4 tint;\n"
"uniform float alpha;\n"
"void main() {\n"
"  outColor = texture(tex0, tex) * col * tint;\n"
"  outColor.a *= alpha;\n"
"  if(outColor.a < 0.1) discard;\n"
"}";

const char* vga_vs_src = "#version 150 core\n"
"in vec2 position;\n"
"in vec2 texcoord;\n"
"uniform mat4 projection;\n"
"uniform vec2 offset;\n"
"uniform vec2 scale;\n"
"out vec2 uv;\n"
"void main() {\n"
"  uv = texcoord;\n"
"  gl_Position = projection * vec4(position * scale + offset, 0.0, 1.0);\n"
"}";

const char* vga_fs_src = "#version 150 core\n"
"in vec2 uv;\n"
"out vec4 outColor;\n"
"uniform sampler2D cellTex;\n"
"uniform sampler2D fontTex;\n"
"uniform vec2 gridSize;\n"
"uniform vec4 egaPalette[16];\n"
"void main() {\n"
"  vec2 g = uv * gridSize;\n"
"  vec2 cell = floor(g);\n"
"  vec2 fracCell = fract(g);\n"
"  vec2 cellUv = (cell + vec2(0.5)) / gridSize;\n"
"  vec2 packed = texture(cellTex, cellUv).rg;\n"
"  float ch = floor(packed.r * 255.0 + 0.5);\n"
"  float c = floor(packed.g * 255.0 + 0.5);\n"
"  int fg = int(mod(c, 16.0));\n"
"  int bg = int(floor(c / 16.0));\n"
"  float col = mod(ch, 16.0);\n"
"  float row = floor(ch / 16.0);\n"
"  float px = floor(fracCell.x * 8.0);\n"
"  float py = floor(fracCell.y * 16.0);\n"
"  vec2 fuv = vec2((col * 8.0 + px + 0.5) / 128.0,\n"
"                  (row * 16.0 + py + 0.5) / 256.0);\n"
"  float a = texture(fontTex, fuv).a;\n"
"  outColor = mix(egaPalette[bg], egaPalette[fg], a);\n"
"}";

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
  return g_ref.program;
}

int get_sprite_vao(void) {
  return g_ref.mesh.vao;
}

static bool ensure_vga_program(void) {
  if (g_vga.program)
    return true;

  GLuint vs = compile_shader(GL_VERTEX_SHADER, vga_vs_src);
  GLuint fs = compile_shader(GL_FRAGMENT_SHADER, vga_fs_src);
  if (!vs || !fs) {
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    return false;
  }

  g_vga.program = glCreateProgram();
  if (!g_vga.program) {
    glDeleteShader(vs);
    glDeleteShader(fs);
    return false;
  }

  glAttachShader(g_vga.program, vs);
  glAttachShader(g_vga.program, fs);
  glBindAttribLocation(g_vga.program, 0, "position");
  glBindAttribLocation(g_vga.program, 1, "texcoord");
  glLinkProgram(g_vga.program);
  glDeleteShader(vs);
  glDeleteShader(fs);

  GLint linked = GL_FALSE;
  glGetProgramiv(g_vga.program, GL_LINK_STATUS, &linked);
  if (linked != GL_TRUE) {
    GLint n = 0;
    glGetProgramiv(g_vga.program, GL_INFO_LOG_LENGTH, &n);
    if (n > 1) {
      char *log = malloc((size_t)n);
      if (log) {
        glGetProgramInfoLog(g_vga.program, n, NULL, log);
        printf("VGA shader link error: %s\n", log);
        free(log);
      }
    }
    glDeleteProgram(g_vga.program);
    g_vga.program = 0;
    return false;
  }

  g_vga.projection  = glGetUniformLocation(g_vga.program, "projection");
  g_vga.offset      = glGetUniformLocation(g_vga.program, "offset");
  g_vga.scale       = glGetUniformLocation(g_vga.program, "scale");
  g_vga.grid_size   = glGetUniformLocation(g_vga.program, "gridSize");
  g_vga.cell_tex    = glGetUniformLocation(g_vga.program, "cellTex");
  g_vga.font_tex    = glGetUniformLocation(g_vga.program, "fontTex");
  g_vga.ega_palette = glGetUniformLocation(g_vga.program, "egaPalette[0]");
  return true;
}

// Initialize the sprite system
bool ui_init_prog(void) {
  // Create shader program
  GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, sprite_vs_src);
  GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, sprite_fs_src);
  
  g_ref.program = glCreateProgram();
  glAttachShader(g_ref.program, vertex_shader);
  glAttachShader(g_ref.program, fragment_shader);
  glBindAttribLocation(g_ref.program, 0, "position");
  glBindAttribLocation(g_ref.program, 1, "texcoord");
  glBindAttribLocation(g_ref.program, 2, "color");
  glLinkProgram(g_ref.program);
  
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
    
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);
  
  return true;
}

void ui_shutdown_prog(void) {
  // Delete shader program and buffers
  SAFE_DELETE(g_vga.program, glDeleteProgram);
  SAFE_DELETE(g_ref.program, glDeleteProgram);
  R_MeshDestroy(&g_ref.mesh);
}

void push_sprite_args(int tex, int x, int y, int w, int h, float alpha) {
  // Bind sprite texture
  glUseProgram(g_ref.program);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  glUniform1i(glGetUniformLocation(g_ref.program, "tex0"), 0);
  glUniform2f(glGetUniformLocation(g_ref.program, "offset"), x, y);
  glUniform2f(glGetUniformLocation(g_ref.program, "scale"), w, h);
  glUniform1f(glGetUniformLocation(g_ref.program, "alpha"), alpha);
  glUniform2f(glGetUniformLocation(g_ref.program, "uv_offset"), 0.0f, 0.0f);
  glUniform2f(glGetUniformLocation(g_ref.program, "uv_scale"), 1.0f, 1.0f);
  glUniform4f(glGetUniformLocation(g_ref.program, "tint"), 1.0f, 1.0f, 1.0f, 1.0f);
}

void set_projection(int x, int y, int w, int h) {
  if (!g_ui_runtime.running) return;
  mat4 projection;
  glm_ortho(x, w, h, y, -1, 1, projection);
  glm_mat4_copy(projection, g_active_projection);
  glUseProgram(get_sprite_prog());
  glUniformMatrix4fv(glGetUniformLocation(g_ref.program, "projection"), 1, GL_FALSE, projection[0]);
}

float *get_sprite_matrix(void) {
  return (float*)&g_ref.projection;
}

// Draw a sprite at the specified screen position
void draw_rect_ex(int tex, rect_t r, int type, float alpha) {
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
void draw_rect(int tex, rect_t r) {
  draw_rect_ex(tex, r, false, 1);
}

// Draw a sub-region of a sprite sheet at the specified screen position.
// uv packs normalized texture coordinates as floats: x=u0, y=v0, w=u1, h=v1.
void draw_sprite_region(int tex, rect_t r,
                        frect_t const *uv,
                        uint32_t color, uint32_t flags) {
  if (!g_ui_runtime.running) return;
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
  glUniform4f(glGetUniformLocation(g_ref.program, "tint"), tr, tg, tb, ta);

  glUniform2f(glGetUniformLocation(g_ref.program, "uv_offset"), u0, v0);
  glUniform2f(glGetUniformLocation(g_ref.program, "uv_scale"), u1 - u0, v1 - v0);
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
  glUseProgram(g_ref.program);
  glUniformMatrix4fv(glGetUniformLocation(g_ref.program, "projection"), 1, GL_FALSE, g_ref.projection[0]);
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
  if (!ensure_vga_program())
    return false;

  float pal[16 * 4];
  for (int i = 0; i < 16; i++) {
    pal[i * 4 + 0] = ((palette16[i] >> 16) & 0xFF) / 255.0f;
    pal[i * 4 + 1] = ((palette16[i] >> 8) & 0xFF) / 255.0f;
    pal[i * 4 + 2] = (palette16[i] & 0xFF) / 255.0f;
    pal[i * 4 + 3] = ((palette16[i] >> 24) & 0xFF) / 255.0f;
  }

  glUseProgram(g_vga.program);
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

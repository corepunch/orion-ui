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
"uniform float alpha;\n"
"void main() {\n"
"  outColor = texture(tex0, tex) * col;\n"
"  outColor.a *= alpha;\n"
"  if(outColor.a < 0.1) discard;\n"
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
    
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);
  
  return true;
}

void ui_shutdown_prog(void) {
  // Delete shader program and buffers
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
}

void set_projection(int x, int y, int w, int h) {
  if (!g_ui_runtime.running) return;
  mat4 projection;
  glm_ortho(x, w, h, y, -1, 1, projection);
  glUseProgram(get_sprite_prog());
  glUniformMatrix4fv(glGetUniformLocation(g_ref.program, "projection"), 1, GL_FALSE, projection[0]);
}

float *get_sprite_matrix(void) {
  return (float*)&g_ref.projection;
}

// Draw a sprite at the specified screen position
void draw_rect_ex(int tex, int x, int y, int w, int h, int type, float alpha) {
  if (!g_ui_runtime.running) return;
  push_sprite_args(tex, x, y, w, h, alpha);
  
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
void draw_rect(int tex, int x, int y, int w, int h) {
  draw_rect_ex(tex, x, y, w, h, false, 1);
}

// Draw a sub-region of a sprite sheet at the specified screen position.
// (u0,v0)-(u1,v1) are normalized texture coordinates selecting the icon.
void draw_sprite_region(int tex, int x, int y, int w, int h,
                        float u0, float v0, float u1, float v1, float alpha) {
  if (!g_ui_runtime.running) return;
  push_sprite_args(tex, x, y, w, h, alpha);
  glUniform2f(glGetUniformLocation(g_ref.program, "uv_offset"), u0, v0);
  glUniform2f(glGetUniformLocation(g_ref.program, "uv_scale"), u1 - u0, v1 - v0);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);
  g_ref.mesh.draw_mode = GL_TRIANGLE_FAN;
  R_MeshDraw(&g_ref.mesh);
  glEnable(GL_DEPTH_TEST);
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

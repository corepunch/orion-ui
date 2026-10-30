#include <mapview/gl_compat.h>
#include <cglm/cglm.h>
#include <cglm/cglm.h>
#include <math.h>

#include <mapview/map.h>
#include <mapview/sprites.h>
#include <mapview/console.h>
#include <editor/editor.h>
#include <orion/kernel/joystick.h>

// External references to context (defined in ui/kernel)
// Global mouse position (updated from mouse events)
int g_mouse_x = 0, g_mouse_y = 0;

const char* vs_src = "#version 150 core\n"
"in vec3 pos;\n"
"in vec2 uv;\n"
"in vec3 norm;\n"
"in vec4 color;\n"
"out vec2 tex;\n"
"out vec3 normal;\n"
"out vec3 fragPos;\n"
"out vec4 col;\n"
"uniform vec2 tex0_size;\n"
"uniform mat4 mvp;\n"
"void main() {\n"
"  tex = uv / tex0_size;\n"
"  normal = norm;\n"
"  col = vec4(1)-color;\n"
"  fragPos = pos;\n"
"  gl_Position = mvp * vec4(pos, 1.0);\n"
"}";

const char* fs_src = "#version 150 core\n"
"in vec2 tex;\n"
"in vec3 normal;\n"
"in vec3 fragPos;\n"
"out vec4 outColor;\n"
"uniform float light;\n"
"uniform vec3 viewPos;\n"
"uniform sampler2D tex0;\n"
"uniform mat4 mvp;\n"
"void main() {\n"
"  // Extract view position from inverse MVP matrix\n"
"  vec3 viewDir = normalize(viewPos - fragPos);\n"
"  float distance = smoothstep(500,0,distance(viewPos, fragPos));\n"
"  float facingFactor = abs(dot(normalize(normal), viewDir));\n"
"  float fading = mix(distance * light, 1.0, light * light);\n"
"  if (viewDir.z < -10000) { outColor = texture(tex0, tex) * light; return; }"
"  outColor = texture(tex0, tex) * mix(facingFactor, 1.0, 0.5) * fading * 1.5;\n"
"  if(outColor.a < 0.1) discard;\n"
"}";

const char* fs_unlit_src = "#version 150 core\n"
"in vec2 tex;\n"
"in vec4 col;\n"
"out vec4 outColor;\n"
"uniform vec4 color;\n"
"uniform sampler2D tex0;\n"
"void main() {\n"
"  outColor = texture(tex0, tex) * color * col;\n"
"}";

GLuint compile(GLenum type, const char* src) {
  GLuint s = R_GpuCreateShader(type);
  R_GpuShaderSource(s, 1, &src, 0);
  R_GpuCompileShader(s);
  return s;
}

GLuint make_1bit_tex(void *data, int width, int height);

// Global variables
GLuint world_prog, ui_prog;
GLuint white_tex, black_tex, selection_tex, no_tex;

int world_prog_mvp;
int world_prog_viewPos;
int world_prog_tex0_size;
int world_prog_tex0;
int world_prog_light;

int ui_prog_mvp;
int ui_prog_tex0_size;
int ui_prog_tex0;
int ui_prog_color;

bool mode = false;
unsigned frame = 0;

palette_entry_t *palette;

editor_state_t *get_editor(void) {
  return g_game ? &g_game->state : NULL;
}

// Initialize OpenGL resources and create shaders
bool init_resources(void) {  
  GLuint vs, fs;

  vs = compile(GL_VERTEX_SHADER, vs_src);
  fs = compile(GL_FRAGMENT_SHADER, fs_src);
  world_prog = R_GpuCreateProgram();
  R_GpuAttachShader(world_prog, vs);
  R_GpuAttachShader(world_prog, fs);
  R_GpuBindAttribLocation(world_prog, 0, "pos");
  R_GpuBindAttribLocation(world_prog, 1, "uv");
  R_GpuBindAttribLocation(world_prog, 2, "norm");
  R_GpuBindAttribLocation(world_prog, 3, "color");
  R_GpuLinkProgram(world_prog);
  R_GpuUseProgram(world_prog);
  R_GpuDeleteShader(vs);
  R_GpuDeleteShader(fs);
  
  world_prog_mvp = R_GpuGetUniformLocation(world_prog, "mvp");
  world_prog_viewPos = R_GpuGetUniformLocation(world_prog, "viewPos");
  world_prog_tex0_size = R_GpuGetUniformLocation(world_prog, "tex0_size");
  world_prog_tex0 = R_GpuGetUniformLocation(world_prog, "tex0");
  world_prog_light = R_GpuGetUniformLocation(world_prog, "light");
  R_GpuUniform1i(world_prog_tex0, 0);

  vs = compile(GL_VERTEX_SHADER, vs_src);
  fs = compile(GL_FRAGMENT_SHADER, fs_unlit_src);
  ui_prog = R_GpuCreateProgram();
  R_GpuAttachShader(ui_prog, vs);
  R_GpuAttachShader(ui_prog, fs);
  R_GpuBindAttribLocation(ui_prog, 0, "pos");
  R_GpuBindAttribLocation(ui_prog, 1, "uv");
  R_GpuBindAttribLocation(ui_prog, 2, "norm");
  R_GpuBindAttribLocation(ui_prog, 3, "color");
  R_GpuLinkProgram(ui_prog);
  R_GpuUseProgram(ui_prog);
  R_GpuDeleteShader(vs);
  R_GpuDeleteShader(fs);
  
  ui_prog_mvp = R_GpuGetUniformLocation(ui_prog, "mvp");
  ui_prog_tex0_size = R_GpuGetUniformLocation(ui_prog, "tex0_size");
  ui_prog_tex0 = R_GpuGetUniformLocation(ui_prog, "tex0");
  ui_prog_color = R_GpuGetUniformLocation(ui_prog, "color");
  R_GpuUniform1i(ui_prog_tex0, 0);

  R_GpuEnable(GL_CULL_FACE);
  R_GpuCullFace(GL_BACK);
  
  R_GpuEnable(GL_DEPTH_TEST);
  R_GpuDepthMask(GL_TRUE);
  
  // 1x1 white texture
  R_GpuGenTextures(1, &white_tex);
  R_GpuBindTexture(GL_TEXTURE_2D, white_tex);
  R_GpuTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &(int){-1});

  R_GpuGenTextures(1, &black_tex);
  R_GpuBindTexture(GL_TEXTURE_2D, black_tex);
  R_GpuTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &(int){0xff000000});
  
  R_GpuGenTextures(1, &selection_tex);
  R_GpuBindTexture(GL_TEXTURE_2D, selection_tex);
  R_GpuTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &(int){0xff00ffff});
  
  R_GpuGenTextures(1, &no_tex);
  R_GpuBindTexture(GL_TEXTURE_2D, no_tex);
  R_GpuTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &(int){0xffffff00});

  return true;
}

void GetMouseInVirtualCoords(int* vx, int* vy) {
  *vx = g_mouse_x / 2;
  *vy = g_mouse_y / 2;
}

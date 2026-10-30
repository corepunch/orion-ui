#include <mapview/gl_compat.h>
#include <cglm/cglm.h>
#include <string.h>
#include <math.h>
#include <mapview/map.h>

// Structure to hold sky texture information
typedef struct {
  GLuint texture_id;
  int width;
  int height;
  bool initialized;
} sky_texture_t;

// Global sky texture
static sky_texture_t g_sky = {0};

// Sky box vertices and buffers
static GLuint sky_vao = 0;
static GLuint sky_vbo = 0;
static int sky_vertex_count = 0;

// External reference to unlit shader
extern GLuint ui_prog;

#define SKY_RADIUS 300.0f
#define SKY_HEIGHT 400.0f
#define SKY_SEGMENTS 32

// Function to load sky texture from WAD
bool load_sky_texture(map_data_t const *map) {
  // Try to find a sky texture - common names in Doom/Doom2
  const char* sky_names[] = {"SKY1", "SKY2", "SKY3", "RSKY1", "RSKY2", "RSKY3", NULL};
  filelump_t *sky_lump = NULL;
  
  // Find the first available sky texture
  for (int i = 0; sky_names[i] != NULL; i++) {
    sky_lump = find_lump(sky_names[i]);
    if (sky_lump) {
      doom_ed_debug("Found sky texture: %s\n", sky_names[i]);
      break;
    }
  }
  
  // If no sky texture found, try to find it in texture directories
  if (!sky_lump) {
    mapside_texture_t const* sky_tex = get_texture("SKY1");
    if (sky_tex) {
      g_sky.texture_id = sky_tex->texture;
      g_sky.width = sky_tex->width;
      g_sky.height = sky_tex->height;
      g_sky.initialized = true;
      return true;
    }
    
    fprintf(stderr, "Warning: No sky texture found\n");
    return false;
  }
  
  // Sky textures are typically 256x128 or similar wide format
  // We'll need to determine dimensions from the texture itself
  
  mapside_texture_t *
  find_and_load_sky_texture(const char* sky_name);

  // Get the texture data
  mapside_texture_t *sky_tex = find_and_load_sky_texture(sky_lump->name);

  if (sky_tex) {
    g_sky.texture_id = sky_tex->texture;
    g_sky.width = sky_tex->width;
    g_sky.height = sky_tex->height;
    g_sky.initialized = true;
    return true;
  }
  
  // If we couldn't get it from the texture cache, try to create a default sky
  // This is a fallback in case the primary methods fail
  doom_ed_debug("Creating default sky texture\n");
  
  const int width = 256;
  const int height = 128;
  
  // Create a simple gradient sky texture
  uint8_t* sky_data = malloc(width * height * 4);
  if (!sky_data) return false;
  
  // Fill with a blue to black gradient
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      float blue = 0.5f + 0.5f * (1.0f - (float)y / height);
      sky_data[(y * width + x) * 4 + 0] = 100;  // R
      sky_data[(y * width + x) * 4 + 1] = 150;  // G
      sky_data[(y * width + x) * 4 + 2] = (uint8_t)(blue * 255.0f);  // B
      sky_data[(y * width + x) * 4 + 3] = 255;  // A
    }
  }
  
  // Create OpenGL texture
  R_GpuGenTextures(1, &g_sky.texture_id);
  R_GpuBindTexture(GL_TEXTURE_2D, g_sky.texture_id);
  
  R_GpuTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  R_GpuTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  R_GpuTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  R_GpuTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  
  R_GpuTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, sky_data);
  
  free(sky_data);
  
  g_sky.width = width;
  g_sky.height = height;
  g_sky.initialized = true;
  
  return true;
}

// Initialize sky geometry (cylinder)
void init_sky_geometry(void) {
  if (sky_vao != 0) return;  // Already initialized
  
  // We'll create a cylinder for the sky
  // This works well with Doom's 2:1 aspect ratio sky textures
  
  // Calculate number of vertices needed
  // 2 vertices per segment for the cylinder walls
  sky_vertex_count = (SKY_SEGMENTS+1) * 2;
  
  // Allocate memory for vertices
  wall_vertex_t* vertices = malloc(sky_vertex_count * sizeof(wall_vertex_t));
  if (!vertices) {
    fprintf(stderr, "Failed to allocate memory for sky vertices\n");
    return;
  }
  
  // Create vertices for cylinder
  for (int i = 0, j = 0; i <= SKY_SEGMENTS; i++) {
    float angle1 = (float)i / SKY_SEGMENTS * 2.0f * M_PI;
    float angle2 = (float)(i + 1) / SKY_SEGMENTS * 2.0f * M_PI;
    
    float x1 = cosf(angle1) * SKY_RADIUS;
    float z1 = sinf(angle1) * SKY_RADIUS;
    float x2 = cosf(angle2) * SKY_RADIUS;
    float z2 = sinf(angle2) * SKY_RADIUS;
    
    // UV mapping: u wraps around based on angle, v is constant
    // Doom's sky texture is typically 2:1, meaning it wraps twice around
    float u1 = (float)i / SKY_SEGMENTS * 2.0f;
    float u2 = (float)(i + 1) / SKY_SEGMENTS * 2.0f;
    
    // Bottom vertex
    vertices[j++] = (wall_vertex_t) {
      .x = (int16_t)x2,
      .y = (int16_t)z2,
      .z = (int16_t)SKY_HEIGHT * -0.2,
      .u = (int16_t)(u2 * 256.0f),
      .v = (int16_t)(1.0f * 128.0f),
      .nx = (int8_t)(x2 / SKY_RADIUS * 127.0f),
      .ny = 0,
      .nz = (int8_t)(z2 / SKY_RADIUS * 127.0f),
    };

    // Top vertex
    vertices[j++] = (wall_vertex_t) {
      .x = (int16_t)x1,
      .y = (int16_t)z1,
      .z = (int16_t)SKY_HEIGHT,
      .u = (int16_t)(u1 * 256.0f),
      .v = (int16_t)(0.0f * 128.0f),
      .nx = (int8_t)(x1 / SKY_RADIUS * 127.0f),
      .ny = 0,
      .nz = (int8_t)(z1 / SKY_RADIUS * 127.0f),
    };
  }
  
  // Create and bind VAO
  R_GpuGenVertexArrays(1, &sky_vao);
  R_GpuBindVertexArray(sky_vao);
  
  // Create and bind VBO
  R_GpuGenBuffers(1, &sky_vbo);
  R_GpuBindBuffer(GL_ARRAY_BUFFER, sky_vbo);
  R_GpuBufferData(GL_ARRAY_BUFFER, sky_vertex_count * sizeof(wall_vertex_t), vertices, GL_STATIC_DRAW);
  
  // Set up vertex attributes
  R_GpuEnableVertexAttribArray(0);  // Position
  R_GpuVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), (void*)0);
  
  R_GpuEnableVertexAttribArray(1);  // Texture coordinates
  R_GpuVertexAttribPointer(1, 2, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), (void*)offsetof(wall_vertex_t, u));
  
  R_GpuEnableVertexAttribArray(2);  // Normal
  R_GpuVertexAttribPointer(2, 3, GL_BYTE, GL_TRUE, sizeof(wall_vertex_t), (void*)offsetof(wall_vertex_t, nx));
  
  R_GpuEnableVertexAttribArray(3);  // Color
  R_GpuVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(wall_vertex_t), (void*)offsetof(wall_vertex_t, color));
  
  // Unbind
  R_GpuBindVertexArray(0);
  R_GpuBindBuffer(GL_ARRAY_BUFFER, 0);
  
  free(vertices);
}

// Draw the sky with the current view
void draw_sky(map_data_t const *map, player_t const *player, mat4 mvp) {
  if (!g_sky.initialized) return;
  
  // Save current OpenGL state
  GLboolean depth_test_enabled;
  R_GpuGetBooleanv(GL_DEPTH_TEST, &depth_test_enabled);
  
  // Disable depth testing for sky
  R_GpuDisable(GL_DEPTH_TEST);
  R_GpuDepthMask(GL_FALSE);
  
  // Use unlit shader
  R_GpuUseProgram(ui_prog);
  
//  // Calculate model matrix to position sky centered on player but only rotating horizontally
//  float model_matrix[16] = {
//    1.0f, 0.0f, 0.0f, 0.0f,
//    0.0f, 1.0f, 0.0f, 0.0f,
//    0.0f, 0.0f, 1.0f, 0.0f,
//    player->x, 0.0f, player->y, 1.0f  // Only use player's X and Z position
//  };
  
  mat4 skyboxView;
  glm_mat4_copy(mvp, skyboxView);
  
  // Remove translation (set the last column to 0,0,0,1)
  skyboxView[3][0] = 0.0f;
  skyboxView[3][1] = 0.0f;
  skyboxView[3][2] = 0.0f;
  skyboxView[3][3] = 1.0f;
  
  // Set uniforms
  R_GpuUniformMatrix4fv(ui_prog_mvp, 1, GL_FALSE, (float*)skyboxView);
  
  // Bind texture
  R_GpuActiveTexture(GL_TEXTURE0);
  R_GpuBindTexture(GL_TEXTURE_2D, g_sky.texture_id);
  R_GpuUniform1i(ui_prog_tex0, 0);

  R_GpuUniform2f(ui_prog_tex0_size, g_sky.width, g_sky.height);

  // Set color to white (full brightness)
  float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  R_GpuUniform4fv(ui_prog_color, 1, color);

  // Bind VAO and draw
  R_GpuBindVertexArray(sky_vao);
  R_GpuDrawArrays(GL_TRIANGLE_STRIP, 0, sky_vertex_count);
  R_GpuBindVertexArray(0);
  
  // Restore OpenGL state
  if (depth_test_enabled) {
    R_GpuEnable(GL_DEPTH_TEST);
  }
  R_GpuDepthMask(GL_TRUE);
}

// Initialize sky renderer
bool init_sky(map_data_t const *map) {
  // Load the sky texture
  if (!load_sky_texture(map)) {
    fprintf(stderr, "Warning: Failed to load sky texture\n");
    return false;
  }
  
  // Initialize the geometry
  init_sky_geometry();
  
  return true;
}

// Clean up sky resources
void cleanup_sky(void) {
  if (sky_vao != 0) {
    R_GpuDeleteVertexArrays(1, &sky_vao);
    sky_vao = 0;
  }
  
  if (sky_vbo != 0) {
    R_GpuDeleteBuffers(1, &sky_vbo);
    sky_vbo = 0;
  }
  
  // Note: We don't delete the texture here as it might be managed by the texture cache
}

#include <mapview/gl_compat.h>
#include <cglm/cglm.h>
#include <cglm/cglm.h>
#include <string.h>
#include <math.h>
#include <mapview/map.h>
#include <mapview/sprites.h>
#include <editor/editor.h>

#ifdef HEXEN
#include <hexen/info.h>
#else
#include <doom/info.h>
#endif

// Thing shader sources - we'll use view-aligned quads
const char* thing_vs_src = "#version 150 core\n"
"in vec2 position; in vec2 texcoord;\n"
"out vec2 tex;\n"
"uniform mat4 mvp;\n"
"uniform vec2 scale;\n"
"void main() {\n"
"  tex = texcoord;\n"
"  gl_Position = mvp * vec4(position * scale, 0.0, 1.0);\n"
"}";

const char* thing_fs_src = "#version 150 core\n"
"in vec2 tex;\n"
"out vec4 outColor;\n"
"uniform sampler2D tex0;\n"
"uniform float light;\n"
"void main() {\n"
"  outColor = texture(tex0, tex);\n"
"  outColor.rgb *= light;\n"
"  if(outColor.a < 0.1) discard;\n"
"}";

// Vertex data for a quad
float thing_verts[] = {
  // pos.x  pos.y    tex.x tex.y
  -0.5f,   -0.5f,    0.0f, 1.0f, // bottom left
  -0.5f,   0.5f,    0.0f, 0.0f, // top left
  0.5f,    0.5f,    1.0f, 0.0f, // top right
  0.5f,    -0.5f,    1.0f, 1.0f, // bottom right
};

// Thing rendering system
typedef struct {
  GLuint program;
  GLuint vao;
  GLuint vbo;
} thing_renderer_t;

typedef struct {
  bool rotate;
  sprite_t *angle[8];
  bool flip[8];
} spriteframe_t;

typedef struct  {
  int num_frames;
  spriteframe_t spriteframes[24];
} spritedef_t;

static thing_renderer_t g_thing_renderer = {0};
static spritedef_t sprites[NUMSPRITES];

// Forward declarations
GLuint compile_thing_shader(GLenum type, const char* src);
bool point_in_frustum(vec3 point, vec4 const planes[6]);

// Initialize the thing rendering system
bool init_things(void) {
  thing_renderer_t* renderer = &g_thing_renderer;
  
  // Create shader program
  GLuint vertex_shader = compile_thing_shader(GL_VERTEX_SHADER, thing_vs_src);
  GLuint fragment_shader = compile_thing_shader(GL_FRAGMENT_SHADER, thing_fs_src);
  
  renderer->program = R_GpuCreateProgram();
  R_GpuAttachShader(renderer->program, vertex_shader);
  R_GpuAttachShader(renderer->program, fragment_shader);
  R_GpuBindAttribLocation(renderer->program, 0, "position");
  R_GpuBindAttribLocation(renderer->program, 1, "texcoord");
  R_GpuLinkProgram(renderer->program);
  
  // Check for linking errors
  GLint success;
  R_GpuGetProgramiv(renderer->program, GL_LINK_STATUS, &success);
  if (!success) {
    char infoLog[512];
    R_GpuGetProgramInfoLog(renderer->program, 512, NULL, infoLog);
    fprintf(stderr, "Thing shader program linking failed: %s\n", infoLog);
    return false;
  }
  
  // Create VAO and VBO
  R_GpuGenVertexArrays(1, &renderer->vao);
  R_GpuBindVertexArray(renderer->vao);
  
  R_GpuGenBuffers(1, &renderer->vbo);
  R_GpuBindBuffer(GL_ARRAY_BUFFER, renderer->vbo);
  R_GpuBufferData(GL_ARRAY_BUFFER, sizeof(thing_verts), thing_verts, GL_STATIC_DRAW);
  
  // Set up vertex attributes
  R_GpuEnableVertexAttribArray(0);
  R_GpuVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
  R_GpuEnableVertexAttribArray(1);
  R_GpuVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
  
  R_GpuDeleteShader(vertex_shader);
  R_GpuDeleteShader(fragment_shader);
  
  memset(sprites, 0, sizeof(sprites));
  
  extern sprite_t* find_sprite6(const char* name);
  
  for (int i = 0; i < NUMSPRITES; i++) {
    // Determine angle frame (A-H based on 45-degree increments)
    // Doom uses 8 angle sprites, with 0 being east, 45 northeast, etc.
    for (int j = 0; j < 16; j++) { //((angle + 22) % 360) / 45;
      char frame_char = 'A' + j;
      char sprite_name[8]={0};
      const char *prefix = sprnames[i];
      // Format sprite name (typically PREFIX + FRAME + ANGLE, e.g., "TROOA1")
      snprintf(sprite_name, sizeof(sprite_name), "%s%c1", prefix, frame_char);
      
      if (find_sprite6(sprite_name)) {
        for (int k = 0; k < 8; k++) {
          snprintf(sprite_name, sizeof(sprite_name), "%s%c%d", prefix, frame_char, k+1);
          sprites[i].spriteframes[j].angle[k] = find_sprite6(sprite_name);
        }
        sprites[i].spriteframes[j].rotate = true;
      } else {
        snprintf(sprite_name, sizeof(sprite_name), "%s%c0", prefix, frame_char);
        sprites[i].spriteframes[j].angle[0] = find_sprite6(sprite_name);
      }
      
      if (!sprites[i].spriteframes[j].angle[0]) {
        break;
      }
      
      sprites[i].num_frames++;
    }
  }
  
  return true;
}

// Compile thing shader
GLuint compile_thing_shader(GLenum type, const char* src) {
  GLuint shader = R_GpuCreateShader(type);
  R_GpuShaderSource(shader, 1, &src, 0);
  R_GpuCompileShader(shader);
  
  // Check for errors
  GLint status;
  R_GpuGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE) {
    GLint log_length;
    R_GpuGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
    char* log = malloc(log_length);
    R_GpuGetShaderInfoLog(shader, log_length, NULL, log);
    fprintf(stderr, "Thing shader compilation error: %s\n", log);
    free(log);
  }
  
  return shader;
}

// Helper function to get sprite name based on thing type and angle
sprite_t *get_thing_sprite_name(uint16_t thing_type, uint16_t angle) {
  static sprite_t empty = {
    .width = 8,
    .height = 8,
    .texture = 1,
  };
  for (int i = 0; i < NUMMOBJTYPES; i++) {
    if (mobjinfo[i].doomednum == thing_type) {
      state_t const *s = &states[mobjinfo[i].spawnstate];
      spritedef_t const *sdef = &sprites[s->sprite];
      uint16_t frame = s->frame&0x7fff;
      if (sdef->num_frames == 0) {
        return &empty;
      }
      if (sdef->num_frames <= frame) {
        frame = 0;
      }
      spriteframe_t const *sf = &sdef->spriteframes[frame];
      sprite_t *sprite = sf->rotate ? sf->angle[angle%8] : sf->angle[0];
      return sprite ? sprite : &empty;
    }
  }
  return &empty;
}

// Returns a value from 1 to 8 for sprite angle selection
int GetSpriteRotationIndex(int thingAngleDeg, int playerAngleDeg) {
  // Normalize both to 0–359
  while (playerAngleDeg < 0) {
    playerAngleDeg += 360;
  }
  while (thingAngleDeg < 0) {
    thingAngleDeg += 360;
  }
  int relAngle = (playerAngleDeg - thingAngleDeg + 360) % 360;
  
  // Divide the circle into 8 equal 45-degree sectors
  // Add 22 to center each sector around its angle
  int spriteIndex = ((relAngle + 22) % 360) / 45;
  
  return spriteIndex;
}

// Draw things in the map
void draw_things(map_data_t const *map, viewdef_t const *viewdef, bool rotate) {
  thing_renderer_t* renderer = &g_thing_renderer;
  
  R_GpuDisable(GL_CULL_FACE);
  
  // Enable blending for transparency
  R_GpuEnable(GL_BLEND);
  R_GpuBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  
  // Use thing shader program
  R_GpuUseProgram(renderer->program);
  
  // Bind VAO
  R_GpuBindVertexArray(renderer->vao);
  
  // Set uniforms that don't change per-thing
  R_GpuUniformMatrix4fv(R_GpuGetUniformLocation(renderer->program, "mvp"), 1, GL_FALSE, viewdef->mvp[0]);

  // Loop through all things in the map
  for (int i = 0; i < map->num_things; i++) {
    mapthing_t const *thing = &map->things[i];
    
    // Skip player
#ifdef HEXEN
    if (thing->type == MT_MAPSPOT)
#else
    if (thing->type == MT_PLAYER)
#endif
      continue;
    
    // Find the sector this thing is in to get light level
    if (thing->height == -1) {
      continue; // Skip if not in any sector
    }
    
    float z_pos = 0;
    float light = 1;

    if (thing->height < map->num_sectors) {
      mapsector_t const *sector = &map->sectors[thing->height];
      
      // Calculate distance to player (for potential culling)
      //    float dist_to_player = dist_sq(player->x, player->y, thing->x, thing->y);
      
      // Simple culling - skip if too far away
      //    if (dist_to_player > 100000.0f) { // Adjust threshold as needed
      //      continue;
      //    }
      
      // Calculate thing height (based on floor height)
      z_pos = sector->floorheight;
      light = sector->lightlevel / 255.0f;
    }
    
    // Get appropriate sprite name based on thing type and angle
    int angle = GetSpriteRotationIndex(thing->angle, viewdef->player.angle);
    sprite_t* sprite = get_thing_sprite_name(thing->type, rotate?angle:0);
    // Set up matrices for this thing
    mat4 model, mv;
    glm_mat4_identity(model);
    
    // Translate to thing position
    glm_translate(model, (vec3){thing->x, thing->y, z_pos+sprite->offsety-sprite->height/2});
    
    // Rotate to face the defined angle
//    float angle_rad = glm_rad(thing->angle);
//    glm_rotate_z(model, angle_rad, model);
    
    float dx = viewdef->viewpos[0] - thing->x;
    float dy = viewdef->viewpos[1] - thing->y;

    if (rotate) {
      glm_rotate_z(model, atan2f(dy, dx)-M_PI_2, model);
      glm_rotate_x(model, M_PI_2, model);
    }
    
    if (!point_in_frustum((vec3){thing->x, thing->y, z_pos}, viewdef->frustum))
      continue;

    glm_mat4_mul(viewdef->mvp, model, mv);

    R_GpuUniformMatrix4fv(R_GpuGetUniformLocation(renderer->program, "mvp"), 1, GL_FALSE, (const float*)mv);
    
    // Set scale uniform
    R_GpuUniform2f(R_GpuGetUniformLocation(renderer->program, "scale"), sprite->width, sprite->height);
    
    // Set light level
    R_GpuUniform1f(R_GpuGetUniformLocation(renderer->program, "light"), rotate?light*1.5:1.25);
    
    // Bind texture
    R_GpuActiveTexture(GL_TEXTURE0);
    R_GpuBindTexture(GL_TEXTURE_2D, sprite->texture);
    R_GpuUniform1i(R_GpuGetUniformLocation(renderer->program, "tex0"), 0);
    
    // Draw the thing
    R_GpuDrawArrays(GL_TRIANGLE_FAN, 0, 4);
  }
    
  // Reset state
  R_GpuDisable(GL_BLEND);
  R_GpuEnable(GL_CULL_FACE);
}

// Cleanup thing rendering resources
void cleanup_things(void) {
  thing_renderer_t* renderer = &g_thing_renderer;
  
  R_GpuDeleteProgram(renderer->program);
  R_GpuDeleteVertexArrays(1, &renderer->vao);
  R_GpuDeleteBuffers(1, &renderer->vbo);
}

void assign_thing_sector(map_data_t const *map, mapthing_t *thing) {
  mapsector_t const *sector = find_player_sector(map, thing->x, thing->y);
  if(sector) {
    thing->height = sector - map->sectors;
  } else {
    thing->height = -1;
  }
}

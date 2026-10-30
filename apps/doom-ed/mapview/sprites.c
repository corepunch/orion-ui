#include <mapview/gl_compat.h>
#include <cglm/cglm.h>
#include <cglm/cglm.h>
#include <string.h>
#include <ctype.h>
#include <mapview/map.h>
#include <mapview/sprites.h>

#define MAX_SPRITES 10000

// Sprite system state
typedef struct {
  sprite_t all_sprites[MAX_SPRITES];
  int num_sprites;
  GLuint crosshair_texture; // Custom crosshair texture (if needed)
} sprite_system_t;

sprite_system_t g_sprite_system = {0};

// Forward declarations
GLuint compile_shader(GLenum type, const char* src);
GLuint load_sprite_texture(void *data, int* width, int* height, int* offsetx, int* offsety);
GLuint generate_crosshair_texture(int size);

int load_sprite(const char *name) {
  int width, height, offsetx, offsety;
  GLuint texture = load_sprite_texture(cache_lump(name), &width, &height, &offsetx, &offsety);
  if (texture) {
    sprite_t* sprite = &g_sprite_system.all_sprites[g_sprite_system.num_sprites];
    strncpy(sprite->name, name, 16);
    sprite->texture = texture;
    sprite->width = width;
    sprite->height = height;
    sprite->offsetx = offsetx;
    sprite->offsety = offsety;
//    printf("Loaded sprite: %s (%dx%d)\n", sprite_lump->name, width, height);
    return g_sprite_system.num_sprites++;
  } else {
    return -1;
  }
}

// Initialize the sprite system
bool init_sprites(void) {
  sprite_system_t* sys = &g_sprite_system;
    
  // Find and preload weapon sprites (starting with SHT)
  sys->num_sprites = 0;
  
  int s_start = find_lump_num("S_START");
  int s_end = find_lump_num("S_END");
  if (s_start >= 0 && s_end >= 0 && s_start < s_end) {
    // Iterate through all sprites
    for (int i = s_start + 1; i < s_end; i++) {
      load_sprite(get_lump_name(i));
    }
  }

//  // If no sprite markers, try loading directly by name
//  int shotgun_sprite = find_lump("SHTGA0");
//  if (shotgun_sprite >= 0) {
//    load_sprite(wad_file, &directory[shotgun_sprite]);
//  }
//  
//  // Try loading crosshair directly
//  int crosshair_sprite = find_lump("CROSA0");
//  if (crosshair_sprite >= 0) {
//    load_sprite(wad_file, &directory[crosshair_sprite]);
//  }
//  
//  int stbar_lump = find_lump("STBAR");
//  if (stbar_lump >= 0) {
//    load_sprite(wad_file, &directory[stbar_lump]);
//  }
  
  
  // Initialize the crosshair texture to 0 (will be generated on demand if needed)
  sys->crosshair_texture = 0;
  
  return sys->num_sprites > 0;
}

// Sprite header structure (same as patch_t)
typedef struct {
  int16_t width;      // width of the sprite
  int16_t height;     // height of the sprite
  int16_t leftoffset; // left offset of sprite
  int16_t topoffset;  // top offset of sprite
  int32_t columnofs[]; // column offsets (flexible array member)
} spriteheader_t;

// Load a sprite texture from memory
GLuint load_sprite_texture(void *data, int* width, int* height, int* offsetx, int* offsety) {
  if (!data) return 0;
  
  // Cast to sprite structure for header access
  spriteheader_t* sprite_header = data;
  
  // Get dimensions and offsets
  *width = sprite_header->width;
  *height = sprite_header->height;
  *offsetx = sprite_header->leftoffset;
  *offsety = sprite_header->topoffset;
  
  // Allocate memory for texture data (RGBA)
  uint8_t* tex_data = calloc(sprite_header->width * sprite_header->height * 4, 1);
  if (!tex_data) {
    return 0;
  }
  
  // Column offsets are directly accessible in the sprite header
  int32_t* columnofs = sprite_header->columnofs;
  
  // Base address for byte calculations
  uint8_t* sprite_bytes = (uint8_t*)data;
  
  // Process each column
  for (int x = 0; x < sprite_header->width; x++) {
    // Calculate position of this column's data
    uint8_t* column_data = sprite_bytes + columnofs[x];
    
    // Process each post
    while (1) {
      uint8_t top_delta = *column_data++;
      
      if (top_delta == 0xFF) break; // End of column
      
      uint8_t length = *column_data++;
      
      // Skip dummy byte
      column_data++;
      
      // Read pixel data
      for (int y = 0; y < length; y++) {
        uint8_t color_index = *column_data++;
        
        int pos = ((top_delta + y) * sprite_header->width + x) * 4;
        tex_data[pos] = palette[color_index].r;
        tex_data[pos + 1] = palette[color_index].g;
        tex_data[pos + 2] = palette[color_index].b;
        tex_data[pos + 3] = (color_index == 247) ? 0 : 255; // 247 is transparent in DOOM sprites
      }
      
      // Skip dummy byte
      column_data++;
    }
  }
  
  // Create OpenGL texture
  GLuint texture_id;
  R_GpuGenTextures(1, &texture_id);
  R_GpuBindTexture(GL_TEXTURE_2D, texture_id);
  
  // Set texture parameters
  R_GpuTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  R_GpuTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  
  // Upload texture data
  R_GpuTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sprite_header->width, sprite_header->height,
               0, GL_RGBA, GL_UNSIGNED_BYTE, tex_data);
  
  // Free temporary data
  free(tex_data);
  
  return texture_id;
}

// Helper function to find sprite in cache
sprite_t* find_sprite(const char* name) {
  sprite_system_t* sys = &g_sprite_system;
  
  for (int i = 0; i < sys->num_sprites; i++) {
    if (strncmp(sys->all_sprites[i].name, name, 4) == 0) {
      return &sys->all_sprites[i];
    }
  }
  
//  for (int i = 0; i < sys->num_sprites; i++) {
//    if (strncmp(sys->all_sprites[i].name, name, 4) == 0) {
//      return &sys->all_sprites[i];
//    }
//  }
  
  return NULL;
}

sprite_t* find_sprite6(const char* name) {
  sprite_system_t* sys = &g_sprite_system;
  
  for (int i = 0; i < sys->num_sprites; i++) {
    if (strncmp(sys->all_sprites[i].name, name, 6) == 0) {
      return &sys->all_sprites[i];
    }
  }
  
  return NULL;
}

// Find lump from file (helper function)
int find_lump_from_file(FILE* wad_file, const char* name) {
  // Get file size
  fseek(wad_file, 0, SEEK_END);
//  long file_size = ftell(wad_file);
  fseek(wad_file, 0, SEEK_SET);
  
  // Read WAD header
  wadheader_t header;
  fread(&header, sizeof(wadheader_t), 1, wad_file);
  
  // Seek to directory
  fseek(wad_file, header.infotableofs, SEEK_SET);
  
  // Return the file position of the matching lump
  for (uint32_t i = 0; i < header.numlumps; i++) {
    long lump_pos = ftell(wad_file);
    
    filelump_t lump;
    fread(&lump, sizeof(filelump_t), 1, wad_file);
    
    if (strncmp(lump.name, name, 8) == 0) {
      return (int)lump_pos;
    }
  }
  
  return -1;
}

// Generate a simple crosshair texture
GLuint generate_crosshair_texture(int size) {
  // Clamp size to minimum of 8
  if (size < 8) size = 8;
  
  uint32_t* crosshair_data = calloc(size * size * 4, 1);
  if (!crosshair_data) return 0;

  for (int i = 0; i < size-1; i++) {
    crosshair_data[i+size*(size/2-1)] = -1;
    crosshair_data[i*size+(size/2-1)] = -1;
  }
  
  crosshair_data[(1+size)*(size/2-1)] = 0;
  crosshair_data[(1+size)*(size/2-1)-1] = 0;
  crosshair_data[(1+size)*(size/2-1)+1] = 0;
  crosshair_data[(1+size)*(size/2-1)-size] = 0;
  crosshair_data[(1+size)*(size/2-1)+size] = 0;

  // Create OpenGL texture
  GLuint texture;
  R_GpuGenTextures(1, &texture);
  R_GpuBindTexture(GL_TEXTURE_2D, texture);
  
  R_GpuTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  R_GpuTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  R_GpuTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  R_GpuTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  
  R_GpuTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, crosshair_data);
  
  free(crosshair_data);
  return texture;
}

// Draw a sprite at the specified screen position
void draw_sprite(const char* name, float x, float y, float scale, float alpha) {
  sprite_t* sprite = find_sprite(name);
  
  if (!sprite) {
    fprintf(stderr, "Sprite not found: %s\n", name);
    return;
  }
  
  // Enable blending for transparency
  R_GpuEnable(GL_BLEND);
  R_GpuBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  
  // Disable depth testing for UI elements
  R_GpuDisable(GL_DEPTH_TEST);
  
  // Use sprite shader program
  R_GpuUseProgram(get_sprite_prog());
  
  // Set uniforms
//  R_GpuUniformMatrix4fv(R_GpuGetUniformLocation(sys->program, "projection"), 1, GL_FALSE, (const float*)sys->projection);
  R_GpuUniform2f(R_GpuGetUniformLocation(get_sprite_prog(), "offset"), x-sprite->offsetx*scale, y-sprite->offsety*scale);
  R_GpuUniform2f(R_GpuGetUniformLocation(get_sprite_prog(), "scale"), sprite->width * scale, sprite->height * scale);
  R_GpuUniform1f(R_GpuGetUniformLocation(get_sprite_prog(), "alpha"), alpha);
  
  // Bind sprite texture
  R_GpuActiveTexture(GL_TEXTURE0);
  R_GpuBindTexture(GL_TEXTURE_2D, sprite->texture);
  R_GpuUniform1i(R_GpuGetUniformLocation(get_sprite_prog(), "tex0"), 0);
  
  // Bind VAO and draw
  R_GpuBindVertexArray(get_sprite_vao());
  R_GpuDrawArrays(GL_TRIANGLE_FAN, 0, 4);
  
  // Reset state
  R_GpuEnable(GL_DEPTH_TEST);
  R_GpuDisable(GL_BLEND);
}

void get_weapon_wobble_offset(int* offset_x, int* offset_y, float speed) {
  unsigned ticks = (unsigned)axGetMilliseconds();
  float time = ticks / 1000.0f;
  
  float freq = 6.0f;             // How fast the bob oscillates
  float amp_x = 4.0f;            // Side-to-side movement
  float amp_y = 2.0f;            // Vertical bobbing
  
  float phase = time * freq;
  
  *offset_x = (int)(sin(phase) * amp_x * speed);
  *offset_y = (int)(fabs(sin(phase)) * amp_y * speed); // U-shaped vertical bob
}

// Draw the shotgun at the bottom center of the screen
void draw_weapon(player_t const *player, float k) {
  set_projection(0, 0, k * VGA_HEGHT, VGA_HEGHT);
  
  // Find appropriate shotgun sprite frame (usually "SHTGA0")
#ifdef HEXEN
//  int tick = SDL_GetTicks();
//  char shotgun_sprite[64]={0};
//  sprintf(shotgun_sprite, "CFLM%c0", 'A'+tick%8);
  const char* shotgun_sprite = "MSTFA0";
#else
  const char* shotgun_sprite = "SHTGA0";
#endif
  sprite_t* sprite = find_sprite(shotgun_sprite);
  
//  if (!sprite) {
//    // Try alternative names if not found
//    const char* alternatives[] = {"SHTFA0", "SHTGB0", "SHTFC0"};
//    for (int i = 0; i < 3; i++) {
//      sprite = find_sprite(alternatives[i]);
//      if (sprite) {
//        shotgun_sprite = alternatives[i];
//        break;
//      }
//    }
//  }

  if (sprite) {
    // Draw with full opacity
    
    int x, y;
    get_weapon_wobble_offset(&x, &y, MAX(fabs(player->vel_x), fabs(player->vel_y))/75);
    
//#ifndef HEXEN
//    y += 20;
//#else
    y += 50;
//#endif
    
    draw_sprite(shotgun_sprite, (k * VGA_HEGHT - VGA_WIDTH)/2 + x, y, 1, 1.0f);

  }
}

#define CROSSHAIR_SIZE 8

// Draw a crosshair in the center of the screen
void draw_crosshair(float k) {
  sprite_system_t* sys = &g_sprite_system;

  set_projection(0, 0, k * VGA_HEGHT, VGA_HEGHT);

  // First try to find a pre-existing crosshair sprite
  sprite_t* sprite = find_sprite("CROSA0");
  
  if (sprite) {
    // Use the existing crosshair sprite
    float x = k * VGA_HEGHT/2;
    float y = VGA_HEGHT/2;
    float scale = 1.0f;
    
    draw_sprite("CROSA0", x, y, scale, 1);
  } else {
    // No crosshair sprite found, generate one on demand
    if (sys->crosshair_texture == 0) {
      // Generate a crosshair texture of 16x16 pixels
      sys->crosshair_texture = generate_crosshair_texture(CROSSHAIR_SIZE);
      
      if (sys->crosshair_texture == 0) {
        fprintf(stderr, "Failed to generate crosshair texture\n");
        return;
      }
      
      // Add it to our sprite cache
      if (sys->num_sprites < MAX_SPRITES) {
        sprite_t* new_sprite = &sys->all_sprites[sys->num_sprites];
        strncpy(new_sprite->name, "CROSSH", 16);
        new_sprite->texture = sys->crosshair_texture;
        new_sprite->width = CROSSHAIR_SIZE;
        new_sprite->height = CROSSHAIR_SIZE;
        new_sprite->offsetx = CROSSHAIR_SIZE/2;
        new_sprite->offsety = CROSSHAIR_SIZE/2;
        sys->num_sprites++;
        
        doom_ed_debug("Generated crosshair sprite (16x16)\n");
      }
    }
    
//    // Draw the generated crosshair
//    sprite = find_sprite("CROSSH");
//    if (sprite) {
//      float x = SCREEN_WIDTH;
//      float y = SCREEN_HEIGHT;
//      float alpha = 1.0f;
//      
//      // Enable blending for transparency
//      R_GpuEnable(GL_BLEND);
//      R_GpuBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
//      
//      // Disable depth testing for UI elements
//      R_GpuDisable(GL_DEPTH_TEST);
//      
//      // Use sprite shader program
//      R_GpuUseProgram(sys->program);
//      
//      // Set uniforms
//      R_GpuUniformMatrix4fv(R_GpuGetUniformLocation(sys->program, "projection"), 1, GL_FALSE, (const float*)sys->projection);
//      R_GpuUniform2f(R_GpuGetUniformLocation(sys->program, "offset"), x, y);
//      R_GpuUniform2f(R_GpuGetUniformLocation(sys->program, "scale"), sprite->width, sprite->height);
//      R_GpuUniform1f(R_GpuGetUniformLocation(sys->program, "alpha"), alpha);
//      
//      // Bind sprite texture
//      R_GpuActiveTexture(GL_TEXTURE0);
//      R_GpuBindTexture(GL_TEXTURE_2D, sprite->texture);
//      R_GpuUniform1i(R_GpuGetUniformLocation(sys->program, "tex0"), 0);
//      
//      // Bind VAO and draw
//      R_GpuBindVertexArray(sys->vao);
//      R_GpuDrawArrays(GL_TRIANGLE_FAN, 0, 4);
//      
//      // Reset state
//      R_GpuEnable(GL_DEPTH_TEST);
//      R_GpuDisable(GL_BLEND);
//    }
  }
}

// Clean up sprite system resources
void cleanup_sprites(void) {
  sprite_system_t* sys = &g_sprite_system;
  
  // Delete textures
  for (int i = 0; i < sys->num_sprites; i++) {
    R_GpuDeleteTextures(1, &sys->all_sprites[i].texture);
  }
    
  // Reset sprite count
  sys->num_sprites = 0;
}


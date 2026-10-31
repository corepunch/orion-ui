#include <mapview/gl_compat.h>
#include <string.h>
#include <ctype.h>
#include <mapview/map.h>

#define MAX_TEXTURES 256
#define MAX_TEXDIR 8

// PNAMES lump structure
typedef struct {
  int32_t nummappatches;      // Number of patches
  lumpname_t name[1];         // Array of patch names (variable size)
} mappatchnames_t;

// Post (column segment) structure
typedef struct {
  uint8_t topdelta;           // Top delta
  uint8_t length;             // Length of the post
                              // followed by length bytes of data and a 0 byte
} post_t;

// Structure for a patch reference in a texture
typedef struct {
  int16_t originx;            // X offset
  int16_t originy;            // Y offset
  int16_t patch;              // Patch number (index into PNAMES)
  int16_t stepdir;            // Step direction (unused)
  int16_t colormap;           // Colormap (unused)
} mappatch_t;

// Structure for a texture definition in TEXTURE1/2
typedef struct {
  lumpname_t name;            // Texture name
  uint32_t masked;            // Masked flag (bit 3)
  uint16_t width;             // Width of the texture
  uint16_t height;            // Height of the texture
  uint32_t columndirectory;   // Column directory (unused, obsolete)
  uint16_t patchcount;        // Number of patches in this texture
  mappatch_t patches[1];      // Array of patches (variable size)
} maptexture_t;

// Texture directory structure for TEXTURE1/2
typedef struct {
  uint32_t numtextures;       // Number of textures
  uint32_t offsets[1];        // Array of offsets to maptexture_t
} texture_directory_t;

texture_cache_t *flat_cache = NULL;
texture_cache_t *texture_cache = NULL;

const char *get_selected_texture(void) {
  return texture_cache->selected;
}

void set_selected_texture(const char *str) {
  memcpy(texture_cache->selected, str, sizeof(texname_t));
}

const char *get_selected_flat_texture(void) {
  return flat_cache->selected;
}

void set_selected_flat_texture(const char *str) {
  memcpy(flat_cache->selected, str, sizeof(texname_t));
}

// Convenience function to get flat texture
mapside_texture_t const *get_flat_texture(const char* name);
result_t win_textures(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

uint8_t* load_patch(void* patch_lump, int* width, int* height) {
  if (!patch_lump) return NULL;
  
  // Cast to patch structure for header access
  patch_t* header = (patch_t*)patch_lump;
  
  *width = header->width;
  *height = header->height;
  
  // Allocate memory for the patch data (RGBA)
  uint8_t* patch_data = calloc(*width * *height * 4, 1);
  if (!patch_data) {
    return NULL;
  }
  
  // Column offsets are directly accessible in the patch header
  int32_t* columnofs = header->columnofs;
  
  // Base address for byte calculations
  uint8_t* lump_bytes = (uint8_t*)patch_lump;
  
  // Process each column
  for (int x = 0; x < header->width; x++) {
    // Calculate position of this column's data
    uint8_t* column_data = lump_bytes + columnofs[x];
    
    // Process posts in this column
    while (1) {
      uint8_t topdelta = *column_data++;
      
      // 0xFF indicates end of column
      if (topdelta == 0xFF) break;
      
      uint8_t length = *column_data++;
      
      // Skip unused byte (padding)
      column_data++;
      
      // Read post data
      for (int y = 0; y < length; y++) {
        uint8_t color = *column_data++;
        
        // Store in RGBA format (alpha set to opaque)
        int pos = ((topdelta + y) * header->width + x) * 4;
        patch_data[pos] = color;     // R (temporarily storing color index)
        patch_data[pos + 1] = color; // G (you might want to map to palette later)
        patch_data[pos + 2] = color; // B (you might want to map to palette later)
        patch_data[pos + 3] = 255;   // A (opaque)
      }
      
      // Skip unused byte (padding)
      column_data++;
    }
  }
  
  return patch_data;
}

// Create a GL texture from the given texture definition and patches
GLuint
create_texture_from_definition(maptexture_t* tex_def,
                               mappatchnames_t* pnames)
{
  int width = tex_def->width;
  int height = tex_def->height;
  
  // Allocate memory for the composed texture (RGBA)
  uint8_t* texture_data = calloc(width * height * 4, 1);
  if (!texture_data) return 0;
  
  // Process each patch in the texture
  for (int i = 0; i < tex_def->patchcount; i++) {
    mappatch_t* patch_ref = &tex_def->patches[i];
    
    // Get patch name from PNAMES
    if (patch_ref->patch >= pnames->nummappatches) {
      free(texture_data);
      return 0;
    }
    
    char* patch_name = pnames->name[patch_ref->patch];
    
    // Find patch lump
    void *patch_lump = cache_lump(patch_name);
    if (!patch_lump) {
      fprintf(stderr, "Warning: Could not find patch: %s\n", patch_name);
      continue;
    }

    // Load patch data
    int patch_width, patch_height;
    uint8_t* patch_data = load_patch(patch_lump, &patch_width, &patch_height);
    if (!patch_data) continue;
    
    // Composite patch into texture at specified position
    int originx = patch_ref->originx;
    int originy = patch_ref->originy;
    
    for (int y = 0; y < patch_height; y++) {
      int tex_y = originy + y;
      if (tex_y < 0 || tex_y >= height) continue;
      
      for (int x = 0; x < patch_width; x++) {
        int tex_x = originx + x;
        if (tex_x < 0 || tex_x >= width) continue;
        
        int patch_pos = (y * patch_width + x) * 4;
        int tex_pos = (tex_y * width + tex_x) * 4;
        
        // Only copy if pixel is opaque (has alpha = 255)
        if (patch_data[patch_pos + 3] == 255) {
          uint8_t color_index = patch_data[patch_pos];
          
          // Convert color index to RGB using palette
          texture_data[tex_pos] = palette[color_index].r;
          texture_data[tex_pos + 1] = palette[color_index].g;
          texture_data[tex_pos + 2] = palette[color_index].b;
          texture_data[tex_pos + 3] = 255; // Opaque
//          texture_data[tex_pos + 3] = color_index > 220 ? 0 : 255;
        }
      }
    }
    
    free(patch_data);
  }
  
  // Create OpenGL texture
  GLuint tex;
  R_GpuGenTextures(1, &tex);
  R_GpuBindTexture(GL_TEXTURE_2D, tex);
  
  R_GpuTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
  R_GpuTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  
  R_GpuTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture_data);
  
  R_GpuGenerateMipmap(GL_TEXTURE_2D);
  
  free(texture_data);
  return tex;
}

// Find a texture in the directory by name
maptexture_t*
find_texture(texture_directory_t* directory,
             texname_t name)
{
  if (!directory) return NULL;
  for (uint32_t i = 0; i < directory->numtextures; i++) {
    maptexture_t *tex = ((void*)directory)+directory->offsets[i];
    if (strncmp(tex->name, name, sizeof(texname_t)) == 0) {
      return tex;
    }
  }
  return NULL;
}

// Try to load texture from texture directories
static mapside_texture_t*
load_texture_from_directories(texname_t const tex_name,
                              texture_directory_t* tex_dirs[],
                              mappatchnames_t* pnames)
{
  static mapside_texture_t tmp;
  maptexture_t* tex_def = NULL;
  texname_t uppercase = {0};

  for (int i = 0; i < sizeof(texname_t); i++) {
    uppercase[i] = toupper(tex_name[i]);
  }
  
  // Try each texture directory until we find the texture
  for (int i = 0; i < MAX_TEXDIR; i++) {
    tex_def = find_texture(tex_dirs[i], uppercase);
    if (tex_def) break;
  }
  
  // If texture definition found, create OpenGL texture
  if (tex_def) {
    tmp.texture = create_texture_from_definition(tex_def, pnames);
    tmp.width = tex_def->width;
    tmp.height = tex_def->height;
    strncpy(tmp.name, tex_name, sizeof(texname_t));
    return &tmp;
  }
  
  return NULL;
}

// Try to load texture
static void maybe_load_texture(texture_cache_t* cache, texname_t const tex_name,
                               texture_directory_t* tex_dirs[], mappatchnames_t* pnames) {
  if (tex_name[0] == '-' || tex_name[0] == '\0') return;
  if (cache->num_textures == MAX_TEXTURES) return;
  
  // Check if texture is already in cache
  for (int j = 0; j < cache->num_textures; j++) {
    if (strncmp(cache->textures[j].name, tex_name, 8) == 0) {
      return;  // Already cached
    }
  }
  
  // Try to load from texture directories
  mapside_texture_t *tex =
  load_texture_from_directories(tex_name, tex_dirs, pnames);
  
  if (tex) {
    cache->textures[cache->num_textures++] = *tex;
  }
}

// Check if a lump is a marker (like F_START, F_END)
bool is_marker(const char* name) {
  if (name[0] == 'F' && name[1] == '_' && strcmp(name + 2, "START") == 0) return true;
  if (name[0] == 'F' && name[1] == '_' && strcmp(name + 2, "END") == 0) return true;
  if (strcmp(name, "FF_START") == 0) return true;
  if (strcmp(name, "FF_END") == 0) return true;
  return false;
}

// Main function to allocate textures for map sides
int allocate_mapside_textures(void) {
  texture_cache = malloc(sizeof(texture_cache_t) + sizeof(texture_cache_t) * MAX_TEXTURES);
  memset(texture_cache, 0, sizeof(texture_cache_t));

  // Array of texture directories
  texture_directory_t *tex_dirs[MAX_TEXDIR]={0};
  int dir_count = 0;
  
  // Find and load all texture directories (TEXTURE1, TEXTURE2, etc.)
  for (int i = 0; i < MAX_TEXDIR; i++) {
    char tex_lump_name[sizeof(lumpname_t)+1]={0};
    snprintf(tex_lump_name, sizeof(tex_lump_name), "TEXTURE%d", i+1);
    texture_directory_t *td = cache_lump(tex_lump_name);
    if (td) {
      tex_dirs[dir_count++] = td;
    }
  }
  
  if (dir_count == 0) {
    fprintf(stderr, "Error: No texture directories found (TEXTURE1, TEXTURE2)\n");
    return 0;
  }
    
  // Load PNAMES
  mappatchnames_t* pnames = cache_lump("PNAMES");
  if (!pnames) {
    fprintf(stderr, "Error: Failed to load PNAMES lump\n");
    return 0;
  }
  
  for (int j = 0; j < dir_count; j++) {
    texture_directory_t* directory = tex_dirs[j];
    for (int i = 0; i < directory->numtextures; i++) {
      maptexture_t *tex = ((void*)directory)+directory->offsets[i];
      if (strstr(tex->name, "SKY")||strstr(tex->name, "CLOCK")) {
        continue;
      }
      maybe_load_texture(texture_cache, tex->name, tex_dirs, pnames);
    }
  }
  
  // Process each sidedef
//  for (int i = 0; i < map->num_sidedefs; i++) {
//    mapsidedef_t* side = &map->sidedefs[i];
//    
//    maybe_load_texture(texture_cache, side->toptexture, tex_dirs, pnames);
//    maybe_load_texture(texture_cache, side->midtexture, tex_dirs, pnames);
//    maybe_load_texture(texture_cache, side->bottomtexture, tex_dirs, pnames);
//  }
  
  create_window("Textures", WINDOW_VSCROLL, MAKERECT(20, 20, 256, 256), NULL, win_textures, 0, texture_cache);
  
  if (texture_cache->num_textures) {
    memcpy(texture_cache->selected, texture_cache->textures->name, sizeof(texname_t));
  }
  
  memcpy(texture_cache->selected, "CASTLE07", 8);

  return texture_cache->num_textures;
}

// Free texture cache
void free_texture_cache(texture_cache_t* cache) {
  if (!cache) return;
  
  for (int i = 0; i < cache->num_textures; i++) {
    if (cache->textures[i].texture) {
      R_GpuDeleteTextures(1, &cache->textures[i].texture);
    }
  }
  
  cache->num_textures = 0;
}

// Get texture from cache by name
mapside_texture_t *get_texture_from_cache(texture_cache_t* cache, const char* name) {
  for (int i = 0; i < cache->num_textures; i++) {
    if (strncmp(cache->textures[i].name, name, 8) == 0) {
      return &cache->textures[i];
    }
  }
  
  return NULL;
}

// Convenience function to get texture
mapside_texture_t const *get_texture(const char* name) {
  return get_texture_from_cache(texture_cache, name);
}

/*
 Flat textures
 */

// Load a single flat texture and create an OpenGL texture
mapside_texture_t *
load_flat_texture(texname_t const floorpic)
{
  filelump_t *flat_lump = find_lump(floorpic);
  static mapside_texture_t tmp = {0};
  
  // Check size - flats should be 64x64 (4096 bytes)
  if (flat_lump->size != 4096) {
    fprintf(stderr, "Warning: Flat %.8s has unexpected size: %d bytes\n", flat_lump->name, flat_lump->size);
    if (flat_lump->size < 4096) return 0;
  }
  
  // Flats are 64x64 pixels
  const int width = 64;
  const int height = 64;
  
  // Read raw flat data (just color indices)
  uint8_t *raw_flat = cache_lump(floorpic);
  uint8_t *flat_data = malloc(width*height*4);

  // Convert color indices to RGBA using palette
  for (int i = 0; i < width * height; i++) {
    uint8_t index = raw_flat[i];
    flat_data[i * 4 + 0] = palette[index].r; // R
    flat_data[i * 4 + 1] = palette[index].g; // G
    flat_data[i * 4 + 2] = palette[index].b; // B
    flat_data[i * 4 + 3] = 255;              // A (always opaque)
  }
  
  // Create OpenGL texture
  GLuint tex;
  R_GpuGenTextures(1, &tex);
  R_GpuBindTexture(GL_TEXTURE_2D, tex);
  
  R_GpuTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
  R_GpuTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  
  R_GpuTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, flat_data);
  
  R_GpuGenerateMipmap(GL_TEXTURE_2D);
  
  tmp.texture = tex;
  tmp.width = width;
  tmp.height = height;
  strncpy(tmp.name, floorpic, sizeof(texname_t));
  
  free(flat_data);

  return &tmp;
}

// Main function to allocate flat textures for map
int allocate_flat_textures(void) {
  flat_cache = malloc(sizeof(texture_cache_t) + MAX_TEXTURES * sizeof(mapside_texture_t));
  memset(flat_cache, 0, sizeof(texture_cache_t));
  
  // Find F_START and F_END markers
  int f_start = find_lump_num("F_START"), f_end = find_lump_num("F_END");
  int ff_start = find_lump_num("FF_START"), ff_end = find_lump_num("FF_END");
  
  if (f_start < 0 || f_end < 0 || f_start >= f_end) {
    fprintf(stderr, "Error: Could not find flat markers (F_START/F_END)\n");
    return 0;
  }
  
  // Load flats between F_START and F_END
  for (int i = f_start + 1; i < f_end; i++) {
    // Skip markers
    if (is_marker(get_lump_name(i))) continue;
    
    // Check if we're at capacity
    if (flat_cache->num_textures >= MAX_TEXTURES) break;
    
    // Check if flat is already in cache
    bool already_cached = false;
    for (int j = 0; j < flat_cache->num_textures; j++) {
      if (strncmp(flat_cache->textures[j].name, get_lump_name(i), 8) == 0) {
        already_cached = true;
        break;
      }
    }
    if (already_cached) continue;
    
    // Load the flat texture
    mapside_texture_t *tex = load_flat_texture(get_lump_name(i));
    if (tex) {
      flat_cache->textures[flat_cache->num_textures++] = *tex;
    }
  }
  
  // If FF_START/FF_END exist, load those flats too (DOOM II)
  if (ff_start >= 0 && ff_end >= 0 && ff_start < ff_end) {
    for (int i = ff_start + 1; i < ff_end; i++) {
      // Skip markers
      if (is_marker(get_lump_name(i))) continue;

      // Check if we're at capacity
      if (flat_cache->num_textures >= MAX_TEXTURES) break;
      
      // Check if flat is already in cache
      bool already_cached = false;
      for (int j = 0; j < flat_cache->num_textures; j++) {
        if (strncmp(flat_cache->textures[j].name, get_lump_name(i), 8) == 0) {
          already_cached = true;
          break;
        }
      }
      if (already_cached) continue;
      
      // Load the flat texture
      mapside_texture_t *tex = load_flat_texture(get_lump_name(i));
      if (tex) {
        flat_cache->textures[flat_cache->num_textures++] = *tex;
      }
    }
  }
  
//  // Process each sector to preload required flat textures
//  for (int i = 0; i < map->num_sectors; i++) {
//    mapsector_t* sector = &map->sectors[i];
//    
//    // If not loaded yet, try to find and load it
//    if (!get_flat_texture(sector->floorpic)) {
//      mapside_texture_t *tex = load_flat_texture(sector->floorpic);
//      if (tex && flat_cache->num_textures < MAX_TEXTURES) {
//        flat_cache->textures[flat_cache->num_textures++] = *tex;
//      }
//    }
//    
//    // If not loaded yet, try to find and load it
//    if (!get_flat_texture(sector->ceilingpic)) {
//      mapside_texture_t *tex = load_flat_texture(sector->ceilingpic);
//      if (tex && flat_cache->num_textures < MAX_TEXTURES) {
//        flat_cache->textures[flat_cache->num_textures++] = *tex;
//      }
//    }
//  }
  
  create_window("Flats", WINDOW_VSCROLL, MAKERECT(ui_get_system_metrics(kSystemMetricScreenWidth)-148, 20, 128, 256), NULL, win_textures, 0, flat_cache);
  
  if (flat_cache->num_textures) {
    memcpy(flat_cache->selected, flat_cache->textures->name, sizeof(texname_t));
  }
  
  return flat_cache->num_textures;
}

// Free flat texture cache
void free_flat_texture_cache(texture_cache_t* cache) {
  if (!cache) return;
  
  for (int i = 0; i < cache->num_textures; i++) {
    if (cache->textures[i].texture) {
      R_GpuDeleteTextures(1, &cache->textures[i].texture);
    }
  }
  
  cache->num_textures = 0;
}

// Get flat texture from cache by name
mapside_texture_t const *
get_flat_texture_from_cache(texture_cache_t* cache, const char* name) {
  for (int i = 0; i < cache->num_textures; i++) {
    if (strncmp(cache->textures[i].name, name, 8) == 0) {
      return &cache->textures[i];
    }
  }
  
  return NULL;
}

// Convenience function to get flat texture
mapside_texture_t const *
get_flat_texture(const char* name) {
  return get_flat_texture_from_cache(flat_cache, name);
}

char const* get_texture_name(int i) {
  return texture_cache->textures[i%texture_cache->num_textures].name;
}

char const* get_flat_texture_name(int i) {
  return flat_cache->textures[i%flat_cache->num_textures].name;
}

int get_texture_index(char const* name) {
  for (int i = 0; i < texture_cache->num_textures; i++) {
    if (!strncmp(name, texture_cache->textures[i].name, sizeof(texname_t))) {
      return i;
    }
  }
  return -1;
}

int get_flat_texture_index(char const* name) {
  for (int i = 0; i < flat_cache->num_textures; i++) {
    if (!strncmp(name, flat_cache->textures[i].name, sizeof(texname_t))) {
      return i;
    }
  }
  return -1;
}

// Load a sky texture and create an OpenGL texture
mapside_texture_t *
find_and_load_sky_texture(texname_t const skypic)
{
  static mapside_texture_t tmp = {0};
  
  // Sky textures in Doom are stored as patches
  int width, height;
  uint8_t* patch_data = load_patch(cache_lump(skypic), &width, &height);
  
  if (!patch_data) {
    fprintf(stderr, "Error: Failed to load sky texture %s\n", skypic);
    return 0;
  }
  
  // Now convert the color indices to RGBA using the palette
  uint8_t* sky_data = malloc(width * height * 4);
  if (!sky_data) {
    free(patch_data);
    return 0;
  }
  
  // Convert color indices to RGBA using palette
  for (int i = 0; i < width * height; i++) {
    if (patch_data[i * 4 + 3] == 255) { // Only copy opaque pixels
      uint8_t index = patch_data[i * 4]; // Color index is stored in the red channel
      sky_data[i * 4] = palette[index].r;     // R
      sky_data[i * 4 + 1] = palette[index].g; // G
      sky_data[i * 4 + 2] = palette[index].b; // B
      
      // For sky textures, use transparency for index 0 (typically black)
      // This allows parts of the sky to be transparent if needed
      sky_data[i * 4 + 3] = (index == 0) ? 0 : 255;
    } else {
      // Keep transparent pixels transparent
      sky_data[i * 4] = 0;
      sky_data[i * 4 + 1] = 0;
      sky_data[i * 4 + 2] = 0;
      sky_data[i * 4 + 3] = 0;
    }
  }
  
  // Create OpenGL texture
  GLuint tex;
  R_GpuGenTextures(1, &tex);
  R_GpuBindTexture(GL_TEXTURE_2D, tex);
  
  // For sky textures, we want different parameters:
  // - Use linear filtering for smoother appearance
  // - Enable horizontal wrapping for scrolling effect
  R_GpuTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  R_GpuTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  R_GpuTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  R_GpuTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  
  R_GpuTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, sky_data);
  
  R_GpuGenerateMipmap(GL_TEXTURE_2D);
  
  free(patch_data);
  free(sky_data);
  
  tmp.texture = tex;
  tmp.width = width;
  tmp.height = height;
  strncpy(tmp.name, skypic, sizeof(texname_t));
  
  return &tmp;
}

// Function to load all sky textures for a map
// This should be called during map loading
int find_and_load_sky_textures(map_data_t* map)
{
  // Common sky texture names in Doom 2
  const char* sky_names[] = {
    "SKY1", "SKY2", "SKY3", "SKY4", "RSKY1", "RSKY2", "RSKY3", NULL
  };
  
  int sky_count = 0;
  
  // Try to load each sky texture
  for (int i = 0; sky_names[i] != NULL; i++) {
    mapside_texture_t* sky = find_and_load_sky_texture(sky_names[i]);
    if (sky) {
      // Add to the texture cache
      if (texture_cache->num_textures < MAX_TEXTURES) {
        texture_cache->textures[texture_cache->num_textures++] = *sky;
        sky_count++;
      }
    }
  }
  
  return sky_count;
}

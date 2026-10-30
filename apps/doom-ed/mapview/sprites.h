#ifndef __SPRITES__
#define __SPRITES__

#include <stdio.h>
#include <stdbool.h>
#include <mapview/gl_compat.h>

// Sprite cache structure
typedef struct {
  char name[16];         // Sprite name (e.g., "SHTGA0")
  GLuint texture;        // OpenGL texture ID
  int width;             // Sprite width
  int height;            // Sprite height
  int offsetx;           // X offset for centering
  int offsety;           // Y offset for centering
} sprite_t;

typedef struct player_s player_t;

float *get_sprite_matrix(void);

// Initialize sprite system
bool init_sprites(void);
void init_intermission(void);

// Draw a sprite at the specified screen position
void draw_sprite(const char* name, float x, float y, float scale, float alpha);

// Draw the weapon sprite at the bottom center of screen
void draw_weapon(player_t const *player, float aspect);

// Clean up sprite system resources
void cleanup_sprites(void);

// Draw a crosshair in the center of the screen
void draw_crosshair(float aspect);

// Helper function to find a lump directly from file
int find_lump_from_file(FILE* wad_file, const char* name);


int load_sprite(const char *name);
sprite_t* find_sprite(const char* name);
void set_projection(int x, int y, int w, int h);

// things


bool init_things(void);
sprite_t* get_thing_sprite_name(uint16_t thing_type, uint16_t angle);
void cleanup_things(void);

#endif /* __SPRITES__ */

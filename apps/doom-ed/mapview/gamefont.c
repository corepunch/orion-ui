// Game font rendering implementation for DOOM/Hexen fonts
// Moved from ui/user/text.c to keep game-specific code in mapview

#include <mapview/gl_compat.h>
#include "../debug.h"
#include <orion/user/rect.h>
#include <orion/user/draw.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <mapview/gamefont.h>

#define CONSOLE_FONT_HEIGHT 8
#define CONSOLE_FONT_WIDTH 8

// Font character structure for DOOM/Hexen fonts
typedef struct {
  GLuint texture;    // OpenGL texture ID
  int x, y;
  int width;         // Width of this character
  int height;        // Height of this character
} font_char_t;

// Game font state
static struct {
  font_char_t font[128];     // ASCII characters for DOOM/Hexen fonts
} gamefont_state = {0};

#ifdef HEXEN
// Hexen font lump name
static const char* HEXEN_FONT_LUMP = "FONTAY_S";
#else
// Doom font lump names (original Doom font is stored in patches named STCFN*)
static const char* FONT_LUMPS_PREFIX = "STCFN";
#endif

// Forward declarations for external functions
extern GLuint white_tex;

// Forward declarations for WAD file functions
extern int find_lump_num(const char* name);
extern void* cache_lump_num(int lump);
extern void* cache_lump(const char* name);
extern GLuint load_sprite_texture(void *data, int* width, int* height, int* offsetx, int* offsety);

// Forward declarations
static bool load_font_char(int font, int char_code);

// Initialize game font system
void init_gamefont(void) {
  memset(&gamefont_state, 0, sizeof(gamefont_state));
}

// Load a specific font character for Doom
static bool load_font_char(int font, int char_code) {
  int width, height, leftoffset, topoffset;
  
#ifdef HEXEN
  int texture = load_sprite_texture(cache_lump_num(font+char_code-32), &width, &height, &leftoffset, &topoffset);
#else
  // Construct font lump name (e.g., "STCFN065" for 'A')
  char lump_name[16];
  snprintf(lump_name, sizeof(lump_name), "%s%03d", FONT_LUMPS_PREFIX, char_code);
  int texture = load_sprite_texture(cache_lump(lump_name), &width, &height, &leftoffset, &topoffset);
#endif
  
  // Store character data
  gamefont_state.font[char_code].texture = texture;
  gamefont_state.font[char_code].x = leftoffset;
  gamefont_state.font[char_code].y = topoffset;
  gamefont_state.font[char_code].width = width;
  gamefont_state.font[char_code].height = height;
  
  return true;
}

// Load the appropriate font (Doom or Hexen)
bool load_console_font(void) {
  bool success = true;
  
#ifdef HEXEN
  // Load Hexen font
  int fontdata = find_lump_num(HEXEN_FONT_LUMP);
  if (fontdata == -1) {
    fprintf(stderr, "Error: Could not load Hexen font lump %s\n", HEXEN_FONT_LUMP);
    return false;
  }
  
  for (int i = 33; i <= 95; i++) {
    if (!load_font_char(fontdata, i)) {
      // Not all characters might be present, that's fine
      fprintf(stderr, "Warning: Could not load font character %c (%d)\n", (char)i, i);
    }
  }
  
  doom_ed_debug("Game font initialized with Hexen font\n");
#else
  // Doom: Find and load all STCFN## font characters
  // In Doom, only certain ASCII characters are stored (33-95, i.e., ! through _)
  for (int i = 33; i <= 95; i++) {
    if (!load_font_char(0, i)) {
      // Not all characters might be present, that's fine
      fprintf(stderr, "Warning: Could not load font character %c (%d)\n", (char)i, i);
    }
  }
  
  doom_ed_debug("Game font initialized with DOOM font\n");
#endif
  
  return success;
}

// Draw a single character using DOOM/Hexen font
static void draw_char_gl3(char c, int x, int y, float alpha) {
#ifdef HEXEN
  int char_code = toupper(c);
#else
  int char_code = (int)(c);
#endif
  
  // Only handle characters we have textures for
  if (char_code < 0 || char_code >= 128 || gamefont_state.font[char_code].texture == 0) {
    // Fallback for unsupported characters - draw a solid rectangle
    if (c != ' ') {  // Skip spaces
      draw_rect(white_tex, R(x, y, CONSOLE_FONT_WIDTH, CONSOLE_FONT_HEIGHT));
    }
    return;
  }
  
  font_char_t* ch = &gamefont_state.font[char_code];
  
  draw_rect(ch->texture, R(x - gamefont_state.font[char_code].x, y - gamefont_state.font[char_code].y, ch->width, ch->height));
}

// Draw text string using GL3 with DOOM/Hexen font
void draw_text_gl3(const char* text, int x, int y, float alpha) {
  int cursor_x = x;
  
  // Set up GL state
  R_GpuEnable(GL_BLEND);
  R_GpuBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  R_GpuDisable(GL_DEPTH_TEST);
  
  // Draw each character
#ifdef HEXEN
  // Hexen font is already uppercase
  for (int i = 0; text[i] != '\0'; i++) {
    char c = text[i];
#else
  // Doom font is uppercase only
  for (int i = 0; text[i] != '\0'; i++) {
    char c = toupper(text[i]);
#endif
    draw_char_gl3(c, cursor_x, y, alpha);
    
    // Advance cursor
    if (c >= 32 && c > 0) {
      // Use character width if available, otherwise use default
      int char_width = gamefont_state.font[(unsigned char)c].width;
      cursor_x += (char_width > 0 ? char_width : CONSOLE_FONT_WIDTH);
    } else {
      cursor_x += CONSOLE_FONT_WIDTH;  // Default width for unsupported chars
    }
  }
  
  // Restore GL state
  R_GpuEnable(GL_DEPTH_TEST);
  R_GpuDisable(GL_BLEND);
}

// Get width of text rendered with DOOM/Hexen font
int get_text_width(const char* text) {
  int cursor_x = 0;
  // Draw each character
#ifdef HEXEN
  // Hexen font is already uppercase
  for (int i = 0; text[i] != '\0'; i++) {
    char c = text[i];
#else
  // Doom font is uppercase only
  for (int i = 0; text[i] != '\0'; i++) {
    char c = toupper(text[i]);
#endif
    
    // Advance cursor
    if (c >= 32 && c > 0) {
      // Use character width if available, otherwise use default
      int char_width = gamefont_state.font[(unsigned char)c].width;
      cursor_x += (char_width > 0 ? char_width : CONSOLE_FONT_WIDTH);
    } else {
      cursor_x += CONSOLE_FONT_WIDTH;  // Default width for unsupported chars
    }
  }
  return cursor_x;
}

// Clean up game font resources
void shutdown_gamefont(void) {
  // Delete all font textures
  for (int i = 0; i < 128; i++) {
    if (gamefont_state.font[i].texture != 0) {
      R_GpuDeleteTextures(1, &gamefont_state.font[i].texture);
      gamefont_state.font[i].texture = 0;
    }
  }
}

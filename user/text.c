// Text rendering implementation
// Extracted from mapview/windows/console.c
// Contains only the small embedded font rendering (console_font_6x8: 6-bit wide, 8 pixels tall)

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"
#include "user.h"

#define FONT_TEX_SIZE 128
#define MAX_TEXT_LENGTH 4096  // Increased for terminal output
#define SMALL_FONT_WIDTH 8
#define SMALL_FONT_HEIGHT 8
#define VERTICES_PER_CHAR 6  // 2 triangles = 6 vertices

typedef struct {
  int16_t x, y;
  float u, v;
  uint32_t col;
} text_vertex_t;

// Font atlas structure
typedef struct {
  R_Mesh mesh;       // Mesh for rendering text
  R_Texture texture; // Atlas texture
  uint8_t char_from[256];    // Start position of each character in pixels
  uint8_t char_to[256];      // End position of each character in pixels
  uint8_t char_height;       // Height of each character in pixels
  uint8_t chars_per_row;     // Number of characters per row in atlas
  uint8_t total_chars;       // Total number of characters in atlas
} font_atlas_t;

// Text rendering state
static struct {
  font_atlas_t small_font;   // Small 6x8 font atlas
} text_state = {0};

// Helper to get character width (internal fast path)
static inline int get_char_width(unsigned char c) {
  return text_state.small_font.char_to[c] - text_state.small_font.char_from[c];
}

// Public API: pixel width of one glyph (0 when text system not initialized).
int char_width(unsigned char c) {
  if (text_state.small_font.char_height == 0) return 0;
  return get_char_width(c);
}

// Forward declarations for external functions
extern void push_sprite_args(int tex, int x, int y, int w, int h, float alpha);

// Create texture atlas for the small 6x8 font
static bool create_font_atlas(void) {
  extern unsigned char console_font_6x8[];
  // Font atlas dimensions
  const int glyph_w = SMALL_FONT_WIDTH;
  const int glyph_h = SMALL_FONT_HEIGHT;
  const int chars_per_row = 16;
  const int rows = 8;      // 16 * 8 = 128 ASCII characters (0-127)
  
  // Create a buffer for the atlas texture
  unsigned char* atlas_data = (unsigned char*)calloc(FONT_TEX_SIZE * FONT_TEX_SIZE, sizeof(unsigned char));
  if (!atlas_data) {
    printf("Error: Could not allocate memory for font atlas\n");
    return false;
  }
  
  // Fill the atlas with character data from the font_6x8 array
  for (int c = 0; c < 128; c++) {
    int atlas_x = (c % chars_per_row) * glyph_w;
    int atlas_y = (c / chars_per_row) * glyph_h;
    // Copy character bits from font data to atlas
    text_state.small_font.char_to[c] = 0;
    text_state.small_font.char_from[c] = 0xff;
    for (int y = 0; y < glyph_h; y++) {
      for (int x = 0; x < glyph_w; x++) {
        // Get bit from font data (assuming 1 byte per row, 8 rows per character)
        int bit_pos = x;
        int font_byte = console_font_6x8[c * glyph_h + y];
        int bit_value = ((font_byte >> (glyph_w - 1 - bit_pos)) & 1);
        // Set corresponding pixel in atlas (convert 1-bit to 8-bit)
        atlas_data[(atlas_y + y) * FONT_TEX_SIZE + atlas_x + x] = bit_value ? 255 : 0;
        if (bit_value) {
          text_state.small_font.char_from[c] = (x < text_state.small_font.char_from[c]) ? x : text_state.small_font.char_from[c];
          text_state.small_font.char_to[c] = ((x+2) > text_state.small_font.char_to[c]) ? (x+2) : text_state.small_font.char_to[c];
        }
      }
    }
  }
  
  extern unsigned char icons_bits[];
  size_t half = FONT_TEX_SIZE * FONT_TEX_SIZE / 2;
  memcpy(atlas_data + half, icons_bits, half);
  
  for (int i = 128; i < 256; i++) {
    text_state.small_font.char_to[i] = 8;
    text_state.small_font.char_from[i] = 0;
  }
  
  // Store atlas information
  text_state.small_font.texture.width = FONT_TEX_SIZE;
  text_state.small_font.texture.height = FONT_TEX_SIZE;
  text_state.small_font.texture.format = GL_RED;
  text_state.small_font.char_height = glyph_h;
  text_state.small_font.chars_per_row = chars_per_row;
  text_state.small_font.total_chars = chars_per_row * rows;

  // Create OpenGL texture for the atlas
  R_AllocateFontTexture(&text_state.small_font.texture, atlas_data);  
  
  // Free temporary buffer
  free(atlas_data);
  
  printf("Small font atlas created successfully\n");
  
  // Initialize mesh for text rendering
  // Vertex attribute layout: 0 = Position, 1 = UV, 2 = Color
  R_VertexAttrib attribs[] = {
    {0, 2, GL_SHORT, GL_FALSE, offsetof(text_vertex_t, x)},  // Position
    {1, 2, GL_FLOAT, GL_FALSE, offsetof(text_vertex_t, u)},  // UV
    {2, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(text_vertex_t, col)} // Color
  };
  R_MeshInit(&text_state.small_font.mesh, attribs, 3, sizeof(text_vertex_t), GL_TRIANGLES);

  return true;
}

// Initialize text rendering system
void init_text_rendering(void) {
  memset(&text_state, 0, sizeof(text_state));
  create_font_atlas();
}

// Get width of text substring with small font
int strnwidth(const char* text, int text_length) {
  if (!text || !*text) return 0; // Early return for empty strings
  
  if (text_length > MAX_TEXT_LENGTH) text_length = MAX_TEXT_LENGTH;
  
  int cursor_x = 0;
  
  // Pre-calculate all vertices for the entire string
  for (int i = 0; i < text_length; i++) {
    unsigned char c = text[i];
    if (c == ' ') {
      cursor_x += SPACE_WIDTH;
      continue;
    }
    // Advance cursor position
    cursor_x += text_state.small_font.char_to[c] - text_state.small_font.char_from[c];
  }
  return cursor_x;
}

// Get width of text with small font
int strwidth(const char* text) {
  if (!text || !*text) return 0; // Early return for empty strings
  return strnwidth(text, (int)strlen(text));
}

// Draw text using small bitmap font
void draw_text_small(const char* text, int x, int y, uint32_t col) {
  if (!text || !*text) return; // Early return for empty strings
  
  // Skip drawing if graphics aren't initialized (e.g., in tests)
  if (!g_ui_runtime.running) return;
  
  int text_length = (int)strlen(text);
  if (text_length > MAX_TEXT_LENGTH) text_length = MAX_TEXT_LENGTH;
  
  static text_vertex_t buffer[MAX_TEXT_LENGTH * VERTICES_PER_CHAR];
  int vertex_count = 0;
  
  int cursor_x = x;
  
  // Pre-calculate all vertices for the entire string
  for (int i = 0; i < text_length; i++) {
    unsigned char c = text[i];
    
    if (c == ' ') {
      cursor_x += SPACE_WIDTH;
      continue;
    }
    if (c == '\n') {
      cursor_x = x;
      y += SMALL_LINE_HEIGHT;
      continue;
    }
    
    // Calculate texture coordinates
    int atlas_x = (c % text_state.small_font.chars_per_row) * SMALL_FONT_WIDTH;
    int atlas_y = (c / text_state.small_font.chars_per_row) * SMALL_FONT_HEIGHT;
    
    // Convert to normalized UV coordinates (0-255 range for uint8_t)
    float u1 = (atlas_x + text_state.small_font.char_from[c])/(float)FONT_TEX_SIZE;
    float v1 = (atlas_y)/(float)FONT_TEX_SIZE;
    float u2 = (atlas_x + text_state.small_font.char_to[c])/(float)FONT_TEX_SIZE;
    float v2 = (atlas_y + SMALL_FONT_HEIGHT)/(float)FONT_TEX_SIZE;
    
    uint8_t w = text_state.small_font.char_to[c] - text_state.small_font.char_from[c];
    uint8_t h = SMALL_FONT_HEIGHT;
    
    // Skip spaces to save rendering effort
    if (c != ' ') {
      // First triangle (bottom-left, top-left, bottom-right)
      buffer[vertex_count++] = (text_vertex_t) { cursor_x, y, u1, v1, col };
      buffer[vertex_count++] = (text_vertex_t) { cursor_x, y + h, u1, v2, col };
      buffer[vertex_count++] = (text_vertex_t) { cursor_x + w, y, u2, v1, col };
      
      // Second triangle (top-left, top-right, bottom-right)
      buffer[vertex_count++] = (text_vertex_t) { cursor_x, y + h, u1, v2, col };
      buffer[vertex_count++] = (text_vertex_t) { cursor_x + w, y + h, u2, v2, col };
      buffer[vertex_count++] = (text_vertex_t) { cursor_x + w, y, u2, v1, col };
    }
    
    // Advance cursor position
    cursor_x += w;
  }
  
  // Early return if nothing to draw
  if (vertex_count == 0) return;
  
  // Set up blend state for 2-D UI text rendering.
  R_SetBlendMode(true);
  
  // Get locations for shader uniforms
  push_sprite_args(text_state.small_font.texture.id, 0, 0, 1, 1, 1);
  
  // Bind font texture and draw
  R_TextureBind(&text_state.small_font.texture);
  R_MeshDrawDynamic(&text_state.small_font.mesh, buffer, vertex_count);
  
  // Restore GL state
  // Note: Commented out to match original behavior
  // glEnable(GL_DEPTH_TEST);
  // glDisable(GL_BLEND);
}

// Calculate total height of text with wrapping
int calc_text_height(const char* text, int width) {
  if (!text || !*text || width <= 0) return 0;
  
  // Check if text_state is initialized
  if (text_state.small_font.char_height == 0) return 0;
  
  int lines = 1, x = 0;
  for (const char* p = text; *p; p++) {
    if (*p == '\n') {
      lines++;
      x = 0;
    } else if (*p == ' ') {
      x += SPACE_WIDTH;
    } else {
      int cw = get_char_width((unsigned char)*p);
      if (x + cw > width) {
        lines++;
        x = cw;
      } else {
        x += cw;
      }
    }
  }
  return lines * SMALL_LINE_HEIGHT;
}

// Draw text with wrapping and viewport clipping
void draw_text_wrapped(const char* text, rect_t const *viewport, uint32_t col) {
  if (!text || !*text || !g_ui_runtime.running || !viewport) return;
  
  // Check if text_state is initialized
  if (text_state.small_font.char_height == 0) return;
  
  int x = viewport->x;
  int y = viewport->y;
  int width = viewport->w;
  // int height = viewport->h; // Currently not used since we skip per-character visibility checks for performance
  
  static text_vertex_t buffer[MAX_TEXT_LENGTH * VERTICES_PER_CHAR];
  int vertex_count = 0, cx = x, cy = y;
  
  for (const char* p = text; *p && vertex_count < MAX_TEXT_LENGTH * VERTICES_PER_CHAR - VERTICES_PER_CHAR; p++) {
    unsigned char c = *p;
    if (c == '\n') {
      cx = x;
      cy += SMALL_LINE_HEIGHT;
      continue;
    }
    if (c == ' ') {
      cx += 3;
      continue;
    }
    
    int cw = get_char_width(c);
    if (cx + cw > x + width) {
      cx = x;
      cy += SMALL_LINE_HEIGHT;
    }
    
    // Show character if any part of it is visible in viewport
    // if (cy + SMALL_FONT_HEIGHT > y && cy < y + height) {
      int ax = (c % text_state.small_font.chars_per_row) * SMALL_FONT_WIDTH;
      int ay = (c / text_state.small_font.chars_per_row) * SMALL_FONT_HEIGHT;
      float u1 = (ax + text_state.small_font.char_from[c]) / (float)FONT_TEX_SIZE;
      float v1 = ay / (float)FONT_TEX_SIZE;
      float u2 = (ax + text_state.small_font.char_to[c]) / (float)FONT_TEX_SIZE;
      float v2 = (ay + SMALL_FONT_HEIGHT) / (float)FONT_TEX_SIZE;
      
      buffer[vertex_count++] = (text_vertex_t){cx, cy, u1, v1, col};
      buffer[vertex_count++] = (text_vertex_t){cx, cy + SMALL_FONT_HEIGHT, u1, v2, col};
      buffer[vertex_count++] = (text_vertex_t){cx + cw, cy, u2, v1, col};
      buffer[vertex_count++] = (text_vertex_t){cx, cy + SMALL_FONT_HEIGHT, u1, v2, col};
      buffer[vertex_count++] = (text_vertex_t){cx + cw, cy + SMALL_FONT_HEIGHT, u2, v2, col};
      buffer[vertex_count++] = (text_vertex_t){cx + cw, cy, u2, v1, col};
    // }
    cx += cw;
  }
  
  if (vertex_count == 0) return;
  
  R_SetBlendMode(true);
  push_sprite_args(text_state.small_font.texture.id, 0, 0, 1, 1, 1);
  R_TextureBind(&text_state.small_font.texture);
  R_MeshDrawDynamic(&text_state.small_font.mesh, buffer, vertex_count);
}

// Clean up text rendering resources
void shutdown_text_rendering(void) {
  // Delete small font resources
  R_DeleteTexture((uint32_t)text_state.small_font.texture.id);
  text_state.small_font.texture.id = 0;
  R_MeshDestroy(&text_state.small_font.mesh);
  
  // Clear the entire state
  memset(&text_state, 0, sizeof(text_state));
}

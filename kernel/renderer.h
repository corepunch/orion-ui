/* Renderer API - High-level abstraction for OpenGL rendering
 * Provides clean API for mesh and texture management to reduce boilerplate
 */
#ifndef __UI_RENDERER_H__
#define __UI_RENDERER_H__

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "../user/gl_compat.h"

// Texture filter mode
typedef enum {
  R_FILTER_NEAREST = 0,   // Nearest-neighbour (pixel art / UI)
  R_FILTER_LINEAR  = 1,   // Bilinear (smooth)
} R_TextureFilter;

// Texture wrap mode
typedef enum {
  R_WRAP_CLAMP  = 0,      // Clamp to edge (sprites, icons)
  R_WRAP_REPEAT = 1,      // Tile (patterns, selection dashes)
} R_TextureWrap;

// Vertex attribute description
typedef struct {
  GLuint index;           // Attribute index (location in shader)
  GLint size;             // Number of components (1-4)
  GLenum type;            // Data type (GL_FLOAT, GL_SHORT, GL_UNSIGNED_BYTE, etc.)
  GLboolean normalized;   // Whether to normalize fixed-point data
  size_t offset;          // Offset in vertex structure
} R_VertexAttrib;

// Mesh/drawable object - encapsulates VAO, VBO, and vertex format
typedef struct {
  GLuint vao;             // Vertex array object
  GLuint vbo;             // Vertex buffer object
  GLuint ibo;             // Index buffer object (0 if unused)
  size_t vertex_size;     // Size of a single vertex in bytes
  size_t vertex_count;    // Number of vertices currently in buffer
  GLenum draw_mode;       // Drawing mode (GL_TRIANGLES, GL_LINES, etc.)
} R_Mesh;

// Texture object - encapsulates texture state
typedef struct {
  GLuint id;              // OpenGL texture ID
  int width;              // Texture width in pixels
  int height;             // Texture height in pixels
  GLenum format;          // Texture format (GL_RGBA, GL_RED, etc.)
} R_Texture;

// VGA text buffer descriptor (one texel per character cell).
// Texture format is expected to be RG8:
//   R = character index (0..255)
//   G = packed color nibble (bg<<4 | fg)
typedef struct {
  uint32_t vga_buffer;
  int width;    // character columns
  int height;   // character rows
} R_VgaBuffer;

// Mesh management functions
// Initialize a mesh with vertex attributes and drawing mode
void R_MeshInit(R_Mesh* mesh, const R_VertexAttrib* attribs, size_t attrib_count, 
                size_t vertex_size, GLenum draw_mode);

// Upload vertex data to mesh buffer (for static or dynamic geometry)
void R_MeshUpload(R_Mesh* mesh, const void* data, size_t vertex_count);

// Draw the mesh using its current vertex data
void R_MeshDraw(R_Mesh* mesh);

// Upload and draw in one call (efficient for dynamic geometry that changes every frame)
void R_MeshDrawDynamic(R_Mesh* mesh, const void* data, size_t vertex_count);

// Destroy mesh and free GPU resources
void R_MeshDestroy(R_Mesh* mesh);

// Texture management functions
// Bind texture to current texture unit
void R_TextureBind(R_Texture* texture);

// Unbind texture from current texture unit
void R_TextureUnbind(void);

// Low-level vertex attribute helpers
// Enable and configure vertex attributes
void R_SetVertexAttribs(const R_VertexAttrib* attribs, size_t count, size_t vertex_size);

// Disable vertex attributes
void R_ClearVertexAttribs(size_t count);

// Allocate a font texture with given dimensions and format
GLuint R_AllocateFontTexture(R_Texture* texture, void *data);

// High-level texture helpers (no GL knowledge required in callers)
// Create an RGBA texture from pixel data.  Returns the texture ID, or 0 on failure.
uint32_t R_CreateTextureRGBA(int w, int h, const void *rgba,
                              R_TextureFilter filter, R_TextureWrap wrap);

// Create/update RG8 textures for text-cell buffers.
uint32_t R_CreateTextureRG8(int w, int h, const void *rg,
                             R_TextureFilter filter, R_TextureWrap wrap);
bool R_UpdateTextureRG8(uint32_t tex, int x, int y, int w, int h,
                        const void *rg);

// Delete a texture by its ID (no-op when id == 0).
void R_DeleteTexture(uint32_t id);

// Blend state
// Enable/disable standard alpha blending (SRC_ALPHA / ONE_MINUS_SRC_ALPHA)
// and pair it with depth-test disable/enable for 2-D UI rendering.
void R_SetBlendMode(bool enabled);

// Draw a VGA text buffer using 3 sources:
//   - buf->vga_buffer : RG8 cell buffer (char + packed colors)
//   - font_tex        : 16x16 atlas of 8x16 glyphs
//   - palette16       : EGA-like 16-color palette (0xAARRGGBB)
// dst_w_px/dst_h_px are output size in screen pixels.
bool R_DrawVGABuffer(const R_VgaBuffer *buf,
                     int x, int y,
                     int dst_w_px, int dst_h_px,
                     uint32_t font_tex,
                     const uint32_t palette16[16]);

#endif /* __UI_RENDERER_H__ */

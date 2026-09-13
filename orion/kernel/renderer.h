/* Renderer API - High-level abstraction for OpenGL rendering
 * Provides clean API for mesh and texture management to reduce boilerplate
 */
#ifndef __UI_RENDERER_H__
#define __UI_RENDERER_H__

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <orion/user/gl_compat.h>

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
// Texture format is RGBA (4 bytes/cell):
//   R = glyph index low byte  (uint16_t & 0xFF)
//   G = glyph index high byte (uint16_t >> 8)
//   B = foreground color index (0..255)
//   A = background color index (0..255)
typedef struct {
  uint32_t vga_buffer;
  int width;    // character columns
  int height;   // character rows
} R_VgaBuffer;

// Font sheet layout for VGA rendering.
typedef struct {
  uint32_t texture;   // glyph sheet texture (256x256 grid of cells)
  int cell_w;          // pixel width of one glyph cell
  int cell_h;          // pixel height of one glyph cell
  int sheet_w;         // total texture width  (= 256 * cell_w)
  int sheet_h;         // total texture height (= 256 * cell_h)
} R_FontSheet;

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

int R_GetMaxTextureSize(void);

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

// High-level texture helpers (no GL knowledge required in callers)
// Create an RGBA texture from pixel data.  Returns the texture ID, or 0 on failure.
uint32_t R_CreateTextureRGBA(int w, int h, const void *rgba,
                              R_TextureFilter filter, R_TextureWrap wrap);
uint32_t R_CreateTextureR8(int w, int h, const void *pixels,
                           R_TextureFilter filter, R_TextureWrap wrap);
bool R_UpdateTextureR8(uint32_t tex, int x, int y, int w, int h,
                       const void *pixels);

// Create/update RG8 textures for text-cell buffers.
uint32_t R_CreateTextureRG8(int w, int h, const void *rg,
                             R_TextureFilter filter, R_TextureWrap wrap);
bool R_UpdateTextureRG8(uint32_t tex, int x, int y, int w, int h,
                        const void *rg);

// Update a sub-region of an existing RGBA texture.
bool R_UpdateTextureRGBA(uint32_t tex, int x, int y, int w, int h,
                         const void *rgba);

// Delete a texture by its ID (no-op when id == 0).
void R_DeleteTexture(uint32_t id);

// Per-window render-target helpers.
// Create or resize an FBO+texture pair.  Returns true when *fbo/*tex are
// valid and sized to req_w × req_h.  If they already match, this is a no-op.
bool R_EnsureWindowTarget(uint32_t *fbo, uint32_t *tex,
                          int *cur_w, int *cur_h,
                          int req_w, int req_h);

// Destroy a window render target (no-op when *fbo == 0).
void R_DestroyWindowTarget(uint32_t *fbo, uint32_t *tex,
                           int *w, int *h);

// Blend state
// Enable/disable standard alpha blending (SRC_ALPHA / ONE_MINUS_SRC_ALPHA)
// and pair it with depth-test disable/enable for 2-D UI rendering.
void R_SetBlendMode(bool enabled);

// Draw a VGA text buffer using the cell buffer and font sheet.
//   - buf       : RGBA cell buffer (glyph_hi<<8|glyph_lo, fg, bg)
//   - font      : font sheet description (texture, cell/sheet dimensions)
//   - palette256: full 256-color palette (0xAARRGGBB)
// dst_w_px/dst_h_px are output size in screen pixels.
bool R_DrawVGABuffer(const R_VgaBuffer *buf,
                     int x, int y,
                     int dst_w_px, int dst_h_px,
                     const R_FontSheet *font,
                     const uint32_t palette256[256]);

#endif /* __UI_RENDERER_H__ */

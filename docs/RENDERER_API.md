# Renderer API

## Overview

The Renderer API provides a high-level abstraction over OpenGL rendering calls to reduce boilerplate code and improve maintainability. All renderer functions use the `R_` prefix for easy identification.

## Key Benefits

- **Cleaner Code**: Reduces ~25 lines of OpenGL boilerplate to 2-3 lines
- **Maintainability**: Single place for OpenGL state management
- **Error Handling**: Centralized validation and error checking
- **Future-Proof**: Foundation for state caching, batching, and multi-backend support

## Data Structures

### R_VertexAttrib

Describes a single vertex attribute for shader input:

```c
typedef struct {
  GLuint index;           // Attribute index (location in shader)
  GLint size;             // Number of components (1-4)
  GLenum type;            // Data type (GL_FLOAT, GL_SHORT, GL_UNSIGNED_BYTE, etc.)
  GLboolean normalized;   // Whether to normalize fixed-point data
  size_t offset;          // Offset in vertex structure
} R_VertexAttrib;
```

**Example:**
```c
R_VertexAttrib attribs[] = {
  {0, 2, GL_SHORT, GL_FALSE, offsetof(vertex_t, x)},    // Position
  {1, 2, GL_FLOAT, GL_FALSE, offsetof(vertex_t, u)},    // UV coordinates
  {2, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(vertex_t, col)}  // Color
};
```

### R_Mesh

Encapsulates a VAO, VBO, and vertex format:

```c
typedef struct {
  GLuint vao;             // Vertex array object
  GLuint vbo;             // Vertex buffer object
  GLuint ibo;             // Index buffer object (0 if unused)
  size_t vertex_size;     // Size of a single vertex in bytes
  size_t vertex_count;    // Number of vertices currently in buffer
  GLenum draw_mode;       // Drawing mode (GL_TRIANGLES, GL_LINES, etc.)
} R_Mesh;
```

### R_Texture

Wraps texture state and metadata:

```c
typedef struct {
  GLuint id;              // OpenGL texture ID
  int width;              // Texture width in pixels
  int height;             // Texture height in pixels
  GLenum format;          // Texture format (GL_RGBA, GL_RED, etc.)
} R_Texture;
```

### R_VgaBuffer

Describes a character-cell buffer texture for VGA text composition:

```c
typedef struct {
  uint32_t vga_buffer;
  int width;    // character columns
  int height;   // character rows
} R_VgaBuffer;
```

The underlying texture is expected to be `RG8`:
- `R` = character index (0..255)
- `G` = packed color nibble (`bg << 4 | fg`)

## API Functions

### Mesh Management

#### R_MeshInit
```c
void R_MeshInit(R_Mesh* mesh, const R_VertexAttrib* attribs, size_t attrib_count, 
                size_t vertex_size, GLenum draw_mode);
```

Initializes a mesh with vertex attributes and drawing mode. This should be called once during initialization.

**Parameters:**
- `mesh`: Pointer to mesh structure to initialize
- `attribs`: Array of vertex attribute descriptions
- `attrib_count`: Number of attributes in the array
- `vertex_size`: Size of a single vertex in bytes (use `sizeof(your_vertex_t)`)
- `draw_mode`: OpenGL drawing mode (`GL_TRIANGLES`, `GL_LINES`, etc.)

**Example:**
```c
R_Mesh text_mesh;
R_VertexAttrib attribs[] = {
  {0, 2, GL_SHORT, GL_FALSE, offsetof(text_vertex_t, x)},
  {1, 2, GL_FLOAT, GL_FALSE, offsetof(text_vertex_t, u)},
  {2, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(text_vertex_t, col)}
};
R_MeshInit(&text_mesh, attribs, 3, sizeof(text_vertex_t), GL_TRIANGLES);
```

#### R_MeshUpload
```c
void R_MeshUpload(R_Mesh* mesh, const void* data, size_t vertex_count);
```

Uploads vertex data to the mesh buffer. Use this for static or semi-static geometry that doesn't change every frame.

**Parameters:**
- `mesh`: Pointer to initialized mesh
- `data`: Pointer to vertex data
- `vertex_count`: Number of vertices to upload

#### R_MeshDraw
```c
void R_MeshDraw(R_Mesh* mesh);
```

Draws the mesh using its current vertex data. The mesh must have been uploaded with `R_MeshUpload` first.

#### R_MeshDrawDynamic
```c
void R_MeshDrawDynamic(R_Mesh* mesh, const void* data, size_t vertex_count);
```

Uploads and draws vertex data in one call. Optimized for dynamic geometry that changes every frame.

**Parameters:**
- `mesh`: Pointer to initialized mesh
- `data`: Pointer to vertex data
- `vertex_count`: Number of vertices to upload and draw

**Example:**
```c
// Dynamic text rendering every frame
text_vertex_t buffer[MAX_TEXT_LENGTH * 6];
int vertex_count = build_text_vertices(buffer, "Hello, World!");
R_MeshDrawDynamic(&text_mesh, buffer, vertex_count);
```

#### R_MeshDestroy
```c
void R_MeshDestroy(R_Mesh* mesh);
```

Destroys a mesh and frees GPU resources. Always call during cleanup.

### Texture Management

#### R_TextureBind
```c
void R_TextureBind(R_Texture* texture);
```

Binds a texture to the current texture unit. Call before drawing with the texture.

#### R_TextureUnbind
```c
void R_TextureUnbind(void);
```

Unbinds the current texture (binds texture 0).

#### R_CreateTextureRG8
```c
uint32_t R_CreateTextureRG8(int w, int h, const void *rg,
                             R_TextureFilter filter, R_TextureWrap wrap);
```

Creates an `RG8` texture for text-cell or other packed two-channel data.

#### R_UpdateTextureRG8
```c
bool R_UpdateTextureRG8(uint32_t tex, int x, int y, int w, int h,
                        const void *rg);
```

Uploads a sub-region into an existing `RG8` texture.

#### R_DrawVGABuffer
```c
bool R_DrawVGABuffer(const R_VgaBuffer *buf,
                     int x, int y,
                     int dst_w_px, int dst_h_px,
                     uint32_t font_tex,
                     const uint32_t palette16[16]);
```

Draws a VGA text buffer using:
- `buf->vga_buffer` as RG8 character/attribute data
- `font_tex` as 16x16 glyph atlas (8x16 cells)
- `palette16` as 16 ARGB colors

This keeps all OpenGL state setup and shader composition in the renderer.

### Low-Level Helpers

These are used internally by `R_MeshInit` but are available if needed:

#### R_SetVertexAttribs
```c
void R_SetVertexAttribs(const R_VertexAttrib* attribs, size_t count, size_t vertex_size);
```

Enables and configures vertex attributes. Typically not called directly.

#### R_ClearVertexAttribs
```c
void R_ClearVertexAttribs(size_t count);
```

Disables vertex attributes. Typically not called directly.

## Usage Examples

### Before (Raw OpenGL)

```c
// Font atlas structure (old way)
typedef struct {
  GLuint vao, vbo;
  GLuint texture;
} font_atlas_t;

// Initialize (10+ lines)
glGenVertexArrays(1, &font.vao);
glGenBuffers(1, &font.vbo);
glBindVertexArray(font.vao);
glBindBuffer(GL_ARRAY_BUFFER, font.vbo);
// ... vertex attribute setup ...

// Draw text (25+ lines every frame)
glBindTexture(GL_TEXTURE_2D, font.texture);
glBindVertexArray(font.vao);
glBindBuffer(GL_ARRAY_BUFFER, font.vbo);
glBufferData(GL_ARRAY_BUFFER, vertex_count * sizeof(text_vertex_t), buffer, GL_DYNAMIC_DRAW);
glEnableVertexAttribArray(0);
glEnableVertexAttribArray(1);
glEnableVertexAttribArray(2);
glVertexAttribPointer(0, 2, GL_SHORT, GL_FALSE, sizeof(text_vertex_t), (void*)offsetof(text_vertex_t, x));
glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(text_vertex_t), (void*)offsetof(text_vertex_t, u));
glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(text_vertex_t), (void*)offsetof(text_vertex_t, col));
glDrawArrays(GL_TRIANGLES, 0, vertex_count);
glDisableVertexAttribArray(0);
glDisableVertexAttribArray(1);
glDisableVertexAttribArray(2);

// Cleanup (3+ lines)
glDeleteTextures(1, &font.texture);
glDeleteVertexArrays(1, &font.vao);
glDeleteBuffers(1, &font.vbo);
```

### After (Renderer API)

```c
// Font atlas structure (new way)
typedef struct {
  R_Mesh mesh;
  R_Texture texture;
} font_atlas_t;

// Initialize (5 lines)
R_VertexAttrib attribs[] = {
  {0, 2, GL_SHORT, GL_FALSE, offsetof(text_vertex_t, x)},
  {1, 2, GL_FLOAT, GL_FALSE, offsetof(text_vertex_t, u)},
  {2, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(text_vertex_t, col)}
};
R_MeshInit(&font.mesh, attribs, 3, sizeof(text_vertex_t), GL_TRIANGLES);

// Draw text (2 lines every frame)
R_TextureBind(&font.texture);
R_MeshDrawDynamic(&font.mesh, buffer, vertex_count);

// Cleanup (1 line - texture cleanup handled separately)
R_MeshDestroy(&font.mesh);
```

## Migration Guide

To convert existing OpenGL code to use the Renderer API:

1. **Replace VAO/VBO members** with `R_Mesh`:
   ```c
   // Before
   GLuint vao, vbo;
   
   // After
   R_Mesh mesh;
   ```

2. **Replace texture IDs** with `R_Texture`:
   ```c
   // Before
   GLuint texture;
   
   // After
   R_Texture texture;
   ```

3. **Convert initialization code**:
   - Create `R_VertexAttrib` array from your vertex format
   - Call `R_MeshInit` instead of `glGenVertexArrays` + setup

4. **Simplify drawing code**:
   - Use `R_TextureBind` instead of `glBindTexture`
   - Use `R_MeshDrawDynamic` instead of bind + upload + configure + draw + cleanup

5. **Simplify cleanup code**:
   - Call `R_MeshDestroy` instead of manual deletion

## Performance Notes

- **R_MeshDrawDynamic**: Optimized for dynamic geometry that changes every frame (like text)
- **R_MeshUpload + R_MeshDraw**: Better for static or semi-static geometry
- Vertex attributes are configured once during `R_MeshInit`, not every frame
- No state caching yet, but the API allows for future optimization

## Future Enhancements

The Renderer API is designed to allow future improvements:

- State caching to avoid redundant GL calls
- Batch rendering for multiple draw calls
- Multi-backend support (Vulkan, Metal)
- Debug/validation helpers for development builds
- Profiling and performance metrics

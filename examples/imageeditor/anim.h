#ifndef __ANIM_H__
#define __ANIM_H__

// Animation support for the Image Editor.
// Compiled only when IMAGEEDITOR_ANIMATIONS == 1.
//
// Storage model:
//   • All editing is done in RGBA (doc->pixels — unchanged).
//   • On frame commit (frame switch / play / save) the RGBA buffer is
//     quantized into the target frame_format_t and stored in anim_frame_t.
//   • On frame load the compressed bytes are expanded back to RGBA.
//
// Memory cost at 320×200:
//   RGBA      → 256 000 bytes / frame
//   INDEXED   →  65 024 bytes / frame  (1/4)
//   BITMAP_1BIT →  8 000 bytes / frame  (1/32)

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ============================================================
// Frame pixel formats
// ============================================================

typedef enum {
  FRAME_FORMAT_RGBA      = 0,  // 4 bytes/pixel – live editing buffer
  FRAME_FORMAT_INDEXED   = 1,  // 1 byte/pixel + 256-entry RGBA palette
  FRAME_FORMAT_BITMAP_1BIT = 2 // 1 bit/pixel packed into bytes (ceil(w*h/8))
} frame_format_t;

// ============================================================
// Single animation frame
// ============================================================

typedef struct {
  uint8_t       *data;          // compressed pixel bytes
  size_t         data_size;     // byte count of data
  frame_format_t format;
  uint32_t       palette[256];  // RGBA entries (for FRAME_FORMAT_INDEXED only)
  int            delay_ms;      // per-frame display duration in milliseconds
  char           name[32];      // human-readable frame name
} anim_frame_t;

// ============================================================
// Animation timeline
// ============================================================

struct anim_timeline_s {
  anim_frame_t **frames;       // heap array of frame pointers
  int            frame_count;
  int            active_frame; // index of the currently active frame
  int            fps;          // default playback speed (default 12)
  bool           loop;         // whether playback loops
  bool           playing;      // true while animation is running
};

// Quantize raw RGBA pixels to a ≤256-entry palette + index map.
// palette[] receives up to 256 packed RGBA entries (MAKE_COLOR format).
// indices[] receives w*h palette index bytes.
// Returns the number of palette entries produced (≤256), or 0 on failure.
// Declared here so export helpers in separate translation units can call it.
int quantize_rgba_indexed(const uint8_t *rgba, int w, int h,
                          uint32_t *palette, uint8_t *indices);

// ============================================================
// Frame compress / expand
// ============================================================

// Compress raw RGBA pixels into a frame using the given format.
// frame->data is (re)allocated on success; old data is freed first.
// Returns true on success, false on allocation failure.
bool anim_frame_compress(anim_frame_t *frame, const uint8_t *rgba,
                         int w, int h, frame_format_t fmt);

// Expand a compressed frame back to RGBA.
// rgba_out must point to a buffer of at least w*h*4 bytes.
// Returns true on success.
bool anim_frame_expand(const anim_frame_t *frame, uint8_t *rgba_out,
                       int w, int h);

// ============================================================
// Timeline CRUD
// ============================================================

// Allocate and initialise a new timeline with one empty RGBA frame.
anim_timeline_t *anim_timeline_new(int w, int h);

// Free the timeline and all its frames.
void anim_timeline_free(anim_timeline_t *tl);

// Allocate a new frame; pixels start zeroed (transparent black).
anim_frame_t *anim_frame_new(const char *name, int delay_ms);

// Free a single frame.
void anim_frame_free(anim_frame_t *f);

// Insert a new blank frame after position `after` (-1 = prepend).
// Returns the index of the new frame, or -1 on failure.
int anim_timeline_insert_frame(anim_timeline_t *tl, int after);

// Duplicate frame at index `idx`.
// Returns the index of the new frame, or -1 on failure.
int anim_timeline_duplicate_frame(anim_timeline_t *tl, int idx);

// Delete frame at index `idx`.  Returns false if only one frame remains.
bool anim_timeline_delete_frame(anim_timeline_t *tl, int idx);

// Move frame from `from` to `to` (shift frames between them).
void anim_timeline_move_frame(anim_timeline_t *tl, int from, int to);

// Commit the RGBA working buffer to the current active frame's storage,
// then switch to frame `idx`, expanding its compressed data back to RGBA.
// doc->pixels is updated to point to the new RGBA working buffer.
// Returns false on allocation failure (doc state unchanged).
bool anim_timeline_switch_frame(anim_timeline_t *tl, int idx,
                                uint8_t **rgba_working, int w, int h,
                                frame_format_t commit_fmt);

// ============================================================
// GL rendering helpers (anim_render.c)
// ============================================================

// Initialise GL programs for indexed and 1-bit rendering.
// Call once after GL context is available.
bool anim_render_init(void);

// Release GL resources created by anim_render_init.
void anim_render_shutdown(void);

// Create (or update) a GL_RGBA texture representing a compressed frame.
// If *tex is 0 a new texture is allocated; otherwise the existing one is
// updated.  Returns true on success.
bool anim_render_frame_thumbnail(const anim_frame_t *frame, int w, int h,
                                 uint32_t *tex);

#endif // __ANIM_H__

#ifndef __ANIM_H__
#define __ANIM_H__

// Animation support for the Image Editor.
//
// Penciltest stores independent RLE cels; only the active working layers stay
// decoded. Other builds retain their RGBA/indexed/bitmap storage.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ============================================================
// Frame pixel formats
// ============================================================

typedef enum {
  FRAME_FORMAT_RGBA      = 0,  // 4 bytes/pixel – live editing buffer
  FRAME_FORMAT_INDEXED   = 1,  // 1 byte/pixel + 256-entry RGBA palette
  FRAME_FORMAT_BITMAP_1BIT = 2, // 1 bit/pixel packed into bytes (ceil(w*h/8))
  FRAME_FORMAT_RLE_INDEXED = 3 // Independent FLIC BLACK/BRUN/COPY subchunk
} frame_format_t;

// ============================================================
// Single animation frame
// ============================================================

typedef struct {
  uint8_t       *data;          // compressed pixel bytes
  size_t         data_size;     // byte count of data
  uint8_t       *cels;          // Penciltest: three independent RLE chunks: Color, Pencil, FX
  size_t         cels_size;     // encoded byte count; NULL for blank/imported single-plane frames
  frame_format_t format;
  uint32_t       palette[256];  // RGBA entries (for FRAME_FORMAT_INDEXED only)
  int            delay_ms;      // per-frame display duration in milliseconds
  uint32_t       revision;      // bumped on compress; onion-skin cache key
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
  int            playback_start_frame; // selection to restore when playback stops
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

// Compress pixel data into a frame using the given format.
// In RGBA builds: pixels is 4-byte/px RGBA; FRAME_FORMAT_INDEXED quantizes it.
// In IMAGEEDITOR_INDEXED builds: pixels is 1-byte/px indices.
// RLE_INDEXED encodes an independent plane; pack_cels stores all three Penciltest cels.
// frame->data is replaced only after encoding succeeds.
// Returns true on success, false on allocation failure.
bool anim_frame_compress(anim_frame_t *frame, const uint8_t *pixels,
                         int w, int h, frame_format_t fmt);

// Expand a compressed frame back to a working pixel buffer.
// In RGBA builds: pixels_out receives w*h*4 RGBA bytes.
// In IMAGEEDITOR_INDEXED builds: pixels_out receives w*h index bytes.
// pixels_out must be large enough (w*h*DOC_BPP bytes).
// Returns true on success.
bool anim_frame_expand(const anim_frame_t *frame, uint8_t *pixels_out,
                       int w, int h);
bool anim_frame_expand_indices(const anim_frame_t *frame, uint8_t *pixels, int w, int h);
bool anim_frame_pack_cels(anim_frame_t *frame, const uint8_t *const planes[3], int w, int h);
bool anim_frame_expand_cel(const anim_frame_t *frame, int cel, uint8_t *pixels, int w, int h);

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

// Commit the working pixel buffer to the current active frame's storage,
// then switch to frame `idx`, expanding its compressed data back into the
// working buffer.  In RGBA builds the buffer is 4-byte/px; in
// IMAGEEDITOR_INDEXED builds it is 1-byte/px.
// working_buf is updated to point to the new working buffer.
// Returns false on allocation failure (doc state unchanged).
bool anim_timeline_switch_frame(anim_timeline_t *tl, int idx,
                                uint8_t **working_buf, int w, int h,
                                frame_format_t commit_fmt);

// ============================================================
// GL rendering helpers (anim_render.c)
// ============================================================

bool anim_render_pencil_onion(const anim_frame_t *frame, int w, int h, uint32_t *tex);

// Expand a frame to an onion texture tinted while preserving stroke coverage.
bool anim_render_onion(const anim_frame_t *frame, int w, int h,
                                        uint32_t *tex, const uint32_t *palette,
                                        uint32_t tint);

// Replace #000000 in a gray onion with `tint`. Paper (near-white) becomes
// transparent; source alpha is kept. `npx` is the pixel count.
void anim_onion_tint_rgba(uint8_t *rgba, size_t npx, uint32_t tint);

#endif // __ANIM_H__

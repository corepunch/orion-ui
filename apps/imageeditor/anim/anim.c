// Animation support: frame compress/expand and timeline CRUD.

#include "imageeditor.h"
#include "../io/flc_codec.h"

// ============================================================
// Internal helpers
// ============================================================

// Simple luminance from RGBA (BT.601 coefficients, integer arithmetic).
static uint8_t rgba_luminance(const uint8_t *px) {
  return (uint8_t)((px[0] * 77 + px[1] * 150 + px[2] * 29) >> 8);
}

// ============================================================
// Octree colour quantizer  (FRAME_FORMAT_INDEXED)
// ============================================================
//
// Reduced to 8 colours per level, 8 levels (supports up to 256 leaf colours).
// The implementation is intentionally compact (no dynamic OOM handling beyond
// returning false to the caller).

#define OCT_LEVELS 8
#define OCT_CHILDREN 8

typedef struct oct_node_s {
  uint32_t r, g, b, a;   // accumulated colour components
  uint32_t count;         // number of pixels in this node
  bool     is_leaf;
  struct oct_node_s *children[OCT_CHILDREN];
  struct oct_node_s *next_reducible; // linked list per level
} oct_node_t;

typedef struct {
  oct_node_t  *root;
  oct_node_t  *reducible[OCT_LEVELS]; // head of reducible list per level
  int          leaf_count;
} octree_t;

static oct_node_t *oct_alloc_node(void) {
  return calloc(1, sizeof(oct_node_t));
}

static void oct_free(oct_node_t *n) {
  if (!n) return;
  for (int i = 0; i < OCT_CHILDREN; i++)
    oct_free(n->children[i]);
  free(n);
}

static void oct_insert(octree_t *oc, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  oct_node_t *n = oc->root;
  for (int level = 0; level < OCT_LEVELS; level++) {
    int shift = 7 - level;
    int idx = ((r >> shift) & 1) << 2 |
              ((g >> shift) & 1) << 1 |
              ((b >> shift) & 1);
    if (!n->children[idx]) {
      n->children[idx] = oct_alloc_node();
      if (!n->children[idx]) return;
      if (level == OCT_LEVELS - 1) {
        n->children[idx]->is_leaf = true;
        oc->leaf_count++;
      } else {
        // Add to reducible list for this level
        n->children[idx]->next_reducible = oc->reducible[level];
        oc->reducible[level] = n->children[idx];
      }
    }
    n = n->children[idx];
    n->r += r; n->g += g; n->b += b; n->a += a;
    n->count++;
  }
}

// Count the number of leaf nodes in a subtree.
static int oct_count_leaves(const oct_node_t *n) {
  if (!n) return 0;
  if (n->is_leaf) return 1;
  int total = 0;
  for (int i = 0; i < OCT_CHILDREN; i++)
    total += oct_count_leaves(n->children[i]);
  return total;
}

// Reduce the deepest reducible node.
static void oct_reduce(octree_t *oc) {
  // Find deepest non-empty level
  int level = OCT_LEVELS - 2;
  while (level >= 0 && !oc->reducible[level]) level--;
  if (level < 0) return;

  oct_node_t *n = oc->reducible[level];
  oc->reducible[level] = n->next_reducible;

  uint32_t r = 0, g = 0, b = 0, a = 0, cnt = 0;
  for (int i = 0; i < OCT_CHILDREN; i++) {
    if (n->children[i]) {
      r += n->children[i]->r;
      g += n->children[i]->g;
      b += n->children[i]->b;
      a += n->children[i]->a;
      cnt += n->children[i]->count;
      // Decrement by the actual number of leaves in this child subtree
      // (the child may be an internal node with multiple leaf descendants).
      oc->leaf_count -= oct_count_leaves(n->children[i]);
      oct_free(n->children[i]);
      n->children[i] = NULL;
    }
  }
  n->r = r; n->g = g; n->b = b; n->a = a; n->count = cnt;
  n->is_leaf = true;
  oc->leaf_count++; // the merged node becomes one new leaf
}

// Walk the tree and collect up to 256 leaf colours.
static int oct_collect(oct_node_t *n, uint32_t *palette, int *pal_size) {
  if (!n) return 0;
  if (n->is_leaf) {
    if (*pal_size >= 256) return 0;
    uint32_t cnt = n->count > 0 ? n->count : 1;
    uint8_t r = (uint8_t)(n->r / cnt);
    uint8_t g = (uint8_t)(n->g / cnt);
    uint8_t b = (uint8_t)(n->b / cnt);
    uint8_t a = (uint8_t)(n->a / cnt);
    palette[*pal_size] = MAKE_COLOR(r, g, b, a);
    (*pal_size)++;
    return 1;
  }
  for (int i = 0; i < OCT_CHILDREN; i++)
    oct_collect(n->children[i], palette, pal_size);
  return 0;
}

// Find the palette index closest to (r,g,b,a) using Euclidean distance.
static uint8_t find_palette_idx(const uint32_t *palette, int pal_size,
                                uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  int best_idx = 0;
  uint32_t best_dist = UINT32_MAX;
  for (int i = 0; i < pal_size; i++) {
    int dr = (int)r - (int)COLOR_R(palette[i]);
    int dg = (int)g - (int)COLOR_G(palette[i]);
    int db = (int)b - (int)COLOR_B(palette[i]);
    int da = (int)a - (int)COLOR_A(palette[i]);
    uint32_t dist = (uint32_t)(dr*dr + dg*dg + db*db + da*da);
    if (dist < best_dist) {
      best_dist = dist;
      best_idx = i;
    }
  }
  return (uint8_t)best_idx;
}

// Quantize rgba[w*h*4] to a 256-entry palette + index map.
// palette[] receives up to 256 packed RGBA entries.
// indices[] receives w*h index bytes.
// Returns the number of palette entries produced (<=256), or 0 on failure.
int quantize_rgba_indexed(const uint8_t *rgba, int w, int h,
                                  uint32_t *palette, uint8_t *indices) {
  int npx = w * h;
  octree_t oc = {0};
  oc.root = oct_alloc_node();
  if (!oc.root) return 0;

  for (int i = 0; i < npx; i++)
    oct_insert(&oc, rgba[i*4+0], rgba[i*4+1], rgba[i*4+2], rgba[i*4+3]);

  while (oc.leaf_count > 256)
    oct_reduce(&oc);

  int pal_size = 0;
  oct_collect(oc.root, palette, &pal_size);
  oct_free(oc.root);

  if (pal_size == 0) return 0;

  for (int i = 0; i < npx; i++) {
    indices[i] = find_palette_idx(palette, pal_size,
                                  rgba[i*4+0], rgba[i*4+1],
                                  rgba[i*4+2], rgba[i*4+3]);
  }
  return pal_size;
}

// ============================================================
// Frame compress / expand
// ============================================================

bool anim_frame_compress(anim_frame_t *frame, const uint8_t *pixels,
                         int w, int h, frame_format_t fmt) {
  if (!frame || !pixels || w <= 0 || h <= 0) return false;

  size_t npx = (size_t)w * (size_t)h;
  uint8_t *new_data = NULL;
  size_t   new_size = 0;
  uint32_t new_palette[256];
  memset(new_palette, 0, sizeof(new_palette));

  switch (fmt) {
    case FRAME_FORMAT_RLE_INDEXED: {
#if IMAGEEDITOR_INDEXED
      new_data = flc_codec_encode(pixels, NULL, w, h, &new_size);
#else
      uint8_t *indices = malloc(npx);
      if (!indices) return false;
      int colors = quantize_rgba_indexed(pixels, w, h, new_palette, indices);
      if (colors) new_data = flc_codec_encode(indices, NULL, w, h, &new_size);
      free(indices);
#endif
      if (!new_data) return false;
      break;
    }
    case FRAME_FORMAT_RGBA: {
      size_t sz = npx * 4;
      new_data = malloc(sz);
      if (!new_data) return false;
      memcpy(new_data, pixels, sz);
      new_size = sz;
      break;
    }

    case FRAME_FORMAT_INDEXED: {
      uint8_t *indices = malloc(npx);
      if (!indices) return false;
#if IMAGEEDITOR_INDEXED
      // In indexed builds the working buffer is already 1-byte/pixel.
      // Store the indices directly instead of re-quantizing RGBA.
      memcpy(indices, pixels, npx);
      (void)new_palette; // palette is embedded in doc->ipal; not stored per-frame
#else
      int pal_sz = quantize_rgba_indexed(pixels, w, h, new_palette, indices);
      if (pal_sz == 0) { free(indices); return false; }
#endif
      new_data = indices;
      new_size = npx;
      break;
    }

    case FRAME_FORMAT_BITMAP_1BIT: {
      size_t byte_count = (npx + 7) / 8;
      uint8_t *bits = calloc(1, byte_count);
      if (!bits) return false;
      for (size_t i = 0; i < npx; i++) {
        uint8_t lum = rgba_luminance(pixels + i * 4);
        if (lum > 128)
          bits[i / 8] |= (uint8_t)(1u << (7 - (i & 7)));
      }
      new_data = bits;
      new_size = byte_count;
      break;
    }

    default:
      return false;
  }

  // New data allocated successfully — replace the old frame contents.
  free(frame->data);
  frame->data      = new_data;
  frame->data_size = new_size;
  frame->format    = fmt;
  frame->revision++;
  if (fmt == FRAME_FORMAT_INDEXED || fmt == FRAME_FORMAT_RLE_INDEXED)
    memcpy(frame->palette, new_palette, sizeof(new_palette));
  return true;
}

bool anim_frame_expand(const anim_frame_t *frame, uint8_t *pixels_out,
                       int w, int h) {
  if (!frame || !pixels_out || w <= 0 || h <= 0 || !frame->data) return false;

  size_t npx = (size_t)w * (size_t)h;

  switch (frame->format) {
    case FRAME_FORMAT_RLE_INDEXED:
#if IMAGEEDITOR_INDEXED
      return anim_frame_expand_indices(frame, pixels_out, w, h);
#else
      {
        uint8_t *indices = malloc(npx);
        if (!indices) return false;
        bool ok = anim_frame_expand_indices(frame, indices, w, h);
        if (ok) for (size_t i = 0; i < npx; i++) memcpy(pixels_out + i * 4, &frame->palette[indices[i]], 4);
        free(indices);
        return ok;
      }
#endif
    case FRAME_FORMAT_RGBA:
      if (frame->data_size < npx * 4) return false;
      memcpy(pixels_out, frame->data, npx * 4);
      return true;

    case FRAME_FORMAT_INDEXED:
      if (frame->data_size < npx) return false;
#if IMAGEEDITOR_INDEXED
      // In indexed builds the working buffer is already 1-byte/pixel.
      // Copy indices directly; palette lives in doc->ipal.
      memcpy(pixels_out, frame->data, npx);
      return true;
#else
      for (size_t i = 0; i < npx; i++) {
        uint8_t idx = frame->data[i];
        uint32_t col = frame->palette[idx];
        pixels_out[i*4+0] = COLOR_R(col);
        pixels_out[i*4+1] = COLOR_G(col);
        pixels_out[i*4+2] = COLOR_B(col);
        pixels_out[i*4+3] = COLOR_A(col);
      }
      return true;
#endif

    case FRAME_FORMAT_BITMAP_1BIT: {
      size_t byte_count = (npx + 7) / 8;
      if (frame->data_size < byte_count) return false;
      for (size_t i = 0; i < npx; i++) {
        uint8_t bit = (frame->data[i / 8] >> (7 - (i & 7))) & 1;
        uint8_t v = bit ? 255 : 0;
        pixels_out[i*4+0] = v;
        pixels_out[i*4+1] = v;
        pixels_out[i*4+2] = v;
        pixels_out[i*4+3] = 255;
      }
      return true;
    }

    default:
      return false;
  }
}

bool anim_frame_expand_indices(const anim_frame_t *frame, uint8_t *pixels, int w, int h) {
  if (!frame || !pixels || !frame->data || w < 1 || h < 1) return false;
  if (frame->format == FRAME_FORMAT_RLE_INDEXED)
    return flc_codec_decode(frame->data, frame->data_size, pixels, NULL, w, h);
  if (frame->format != FRAME_FORMAT_INDEXED || frame->data_size != (size_t)w * h) return false;
  memcpy(pixels, frame->data, frame->data_size);
  return true;
}

bool anim_frame_pack_cels(anim_frame_t *frame, const uint8_t *const planes[3], int w, int h) {
  if (!frame || !planes) return false;
  uint8_t *chunks[3] = {0}; size_t sizes[3] = {0}, total = 0;
  bool ok = true;
  for (int i = 0; ok && i < 3; i++) {
    chunks[i] = flc_codec_encode(planes[i], NULL, w, h, &sizes[i]);
    ok = chunks[i] != NULL && sizes[i] <= SIZE_MAX - total;
    total += sizes[i];
  }
  uint8_t *packed = ok ? malloc(total) : NULL;
  if (!packed) ok = false;
  size_t offset = 0;
  for (int i = 0; i < 3; i++) {
    if (ok) { memcpy(packed + offset, chunks[i], sizes[i]); offset += sizes[i]; }
    free(chunks[i]);
  }
  if (!ok) { free(packed); return false; }
  free(frame->data); free(frame->cels);
  frame->data = NULL; frame->data_size = 0;
  frame->cels = packed; frame->cels_size = total;
  frame->format = FRAME_FORMAT_RLE_INDEXED; frame->revision++;
  return true;
}

bool anim_frame_expand_cel(const anim_frame_t *frame, int cel, uint8_t *pixels, int w, int h) {
  if (!frame || !frame->cels || !pixels || cel < 0 || cel >= 3 || frame->format != FRAME_FORMAT_RLE_INDEXED) return false;
  size_t offset = 0, selected_size = 0;
  const uint8_t *selected = NULL;
  for (int i = 0; i < 3; i++) {
    if (offset > frame->cels_size || frame->cels_size - offset < 6) return false;
    const uint8_t *chunk = frame->cels + offset;
    size_t size = flc_codec_u32(chunk); uint16_t type = flc_codec_u16(chunk + 4);
    if (size < 6 || size > frame->cels_size - offset || (size & 1) || (type != 13 && type != 15 && type != 16)) return false;
    if (i == cel) { selected = chunk; selected_size = size; }
    offset += size;
  }
  return offset == frame->cels_size && flc_codec_decode(selected, selected_size, pixels, NULL, w, h);
}

// ============================================================
// Frame / timeline lifecycle
// ============================================================

int anim_default_delay(int fps) {
  return fps > 0 ? MAX(1, (1000 + fps / 2) / fps) : 83;
}

anim_frame_t *anim_frame_new(const char *name) {
  anim_frame_t *f = calloc(1, sizeof(*f));
  if (!f) return NULL;
  f->format = IMAGEEDITOR_BW ? FRAME_FORMAT_RLE_INDEXED : FRAME_FORMAT_INDEXED;
  snprintf(f->name, sizeof(f->name), "%s", name ? name : "Drawing");
  return f;
}

void anim_frame_free(anim_frame_t *f) {
  if (!f) return;
  free(f->data); free(f->cels); free(f);
}

anim_frame_t *anim_frame_clone(const anim_frame_t *src) {
  anim_frame_t *copy = malloc(sizeof(*copy));
  if (!copy) return NULL;
  *copy = *src; copy->data = copy->cels = NULL;
  if (src->data_size) {
    copy->data = malloc(src->data_size);
    if (!copy->data) goto fail;
    memcpy(copy->data, src->data, src->data_size);
  }
  if (src->cels_size) {
    copy->cels = malloc(src->cels_size);
    if (!copy->cels) goto fail;
    memcpy(copy->cels, src->cels, src->cels_size);
  }
  return copy;
fail:
  anim_frame_free(copy);
  return NULL;
}

anim_frame_t *anim_find_cel(const anim_timeline_t *tl, uint32_t id) {
  if (tl) for (int i = 0; i < tl->cel_count; i++) if (tl->bank[i]->id == id) return tl->bank[i];
  return NULL;
}

int anim_cel_uses(const anim_timeline_t *tl, uint32_t id) {
  int count = 0;
  if (tl) for (int i = 0; i < tl->frame_count; i++) if (tl->frames[i].cel->id == id) count++;
  return count;
}

bool anim_bank_add(anim_timeline_t *tl, anim_frame_t *cel) {
  if (!tl || !cel || tl->cel_count >= 65535 || (cel->id && anim_find_cel(tl, cel->id))) return false;
  if (!cel->id && (!tl->next_cel_id || tl->next_cel_id >= INT32_MAX)) return false;
  anim_frame_t **bank = realloc(tl->bank, (tl->cel_count + 1) * sizeof(*bank));
  if (!bank) return false;
  tl->bank = bank;
  if (!cel->id) cel->id = tl->next_cel_id++;
  tl->bank[tl->cel_count++] = cel;
  return true;
}

bool anim_bank_delete(anim_timeline_t *tl, uint32_t id) {
  if (!tl || anim_cel_uses(tl, id)) return false;
  for (int i = 0; i < tl->cel_count; i++) if (tl->bank[i]->id == id) {
    anim_frame_free(tl->bank[i]);
    memmove(tl->bank + i, tl->bank + i + 1, (--tl->cel_count - i) * sizeof(*tl->bank));
    return true;
  }
  return false;
}

int anim_timeline_reuse(anim_timeline_t *tl, int after, uint32_t cel_id) {
  anim_frame_t *cel = anim_find_cel(tl, cel_id);
  if (!cel || tl->frame_count >= 65535 || after < -1 || after >= tl->frame_count) return -1;
  anim_exposure_t *frames = realloc(tl->frames, (tl->frame_count + 1) * sizeof(*frames));
  if (!frames) return -1;
  tl->frames = frames;
  int ins = after + 1;
  memmove(frames + ins + 1, frames + ins, (tl->frame_count - ins) * sizeof(*frames));
  frames[ins] = (anim_exposure_t){.cel = cel, .delay_ms = anim_default_delay(tl->fps)};
  tl->frame_count++;
  if (tl->active_frame >= ins) tl->active_frame++;
  return ins;
}

anim_timeline_t *anim_timeline_new(int w, int h) {
  anim_timeline_t *tl = calloc(1, sizeof(*tl));
  if (!tl) return NULL;
  tl->next_cel_id = 1; tl->fps = 12; tl->loop = true;
  if (anim_timeline_insert_frame(tl, -1) < 0) { anim_timeline_free(tl); return NULL; }
  tl->active_frame = 0;
#if !IMAGEEDITOR_BW
  anim_frame_t *f = tl->frames[0].cel;
  f->data_size = (size_t)w * h * 4;
  f->data = calloc(1, f->data_size); f->format = FRAME_FORMAT_RGBA;
  if (!f->data) { anim_timeline_free(tl); return NULL; }
#endif
  return tl;
}

void anim_timeline_free(anim_timeline_t *tl) {
  if (!tl) return;
  for (int i = 0; i < tl->cel_count; i++) anim_frame_free(tl->bank[i]);
  free(tl->bank); free(tl->frames); free(tl);
}

anim_timeline_t *anim_timeline_clone(const anim_timeline_t *src) {
  anim_timeline_t *copy = calloc(1, sizeof(*copy));
  if (!copy) return NULL;
  *copy = *src; copy->bank = NULL; copy->frames = NULL; copy->cel_count = 0; copy->playing = false;
  for (int i = 0; i < src->cel_count; i++) {
    anim_frame_t *cel = anim_frame_clone(src->bank[i]);
    if (!cel || !anim_bank_add(copy, cel)) { anim_frame_free(cel); goto fail; }
  }
  copy->frames = malloc(src->frame_count * sizeof(*copy->frames));
  if (!copy->frames) goto fail;
  memcpy(copy->frames, src->frames, src->frame_count * sizeof(*copy->frames));
  for (int i = 0; i < src->frame_count; i++) copy->frames[i].cel = anim_find_cel(copy, src->frames[i].cel->id);
  return copy;
fail:
  anim_timeline_free(copy);
  return NULL;
}

int anim_timeline_insert_frame(anim_timeline_t *tl, int after) {
  if (!tl || after < -1 || after >= tl->frame_count) return -1;
  char name[32]; snprintf(name, sizeof(name), "Drawing %u", tl->next_cel_id);
  anim_frame_t *cel = anim_frame_new(name);
  if (!cel || !anim_bank_add(tl, cel)) { anim_frame_free(cel); return -1; }
  int index = anim_timeline_reuse(tl, after, cel->id);
  if (index < 0) anim_bank_delete(tl, cel->id);
  return index;
}

int anim_timeline_duplicate_frame(anim_timeline_t *tl, int idx) {
  if (!tl || idx < 0 || idx >= tl->frame_count) return -1;
  anim_frame_t *cel = anim_frame_clone(tl->frames[idx].cel);
  if (!cel) return -1;
  cel->id = 0;
  if (!anim_bank_add(tl, cel)) { anim_frame_free(cel); return -1; }
  int index = anim_timeline_reuse(tl, idx, cel->id);
  if (index < 0) anim_bank_delete(tl, cel->id);
  else {
    tl->frames[index].delay_ms = tl->frames[idx].delay_ms;
    memcpy(tl->frames[index].note, tl->frames[idx].note, sizeof(tl->frames[index].note));
  }
  return index;
}

bool anim_timeline_make_unique(anim_timeline_t *tl, int index) {
  if (!tl || index < 0 || index >= tl->frame_count) return false;
  if (anim_cel_uses(tl, tl->frames[index].cel->id) <= 1) return true;
  anim_frame_t *cel = anim_frame_clone(tl->frames[index].cel);
  if (!cel) return false;
  cel->id = 0;
  if (!anim_bank_add(tl, cel)) { anim_frame_free(cel); return false; }
  tl->frames[index].cel = cel;
  return true;
}

bool anim_timeline_delete_frame(anim_timeline_t *tl, int idx) {
  if (!tl || tl->frame_count <= 1 || idx < 0 || idx >= tl->frame_count) return false;
  memmove(tl->frames + idx, tl->frames + idx + 1, (--tl->frame_count - idx) * sizeof(*tl->frames));
  if (tl->active_frame > idx) tl->active_frame--;
  tl->active_frame = MIN(tl->active_frame, tl->frame_count - 1);
  return true;
}

void anim_timeline_move_frame(anim_timeline_t *tl, int from, int to) {
  if (!tl || from == to || from < 0 || from >= tl->frame_count || to < 0 || to >= tl->frame_count) return;
  anim_exposure_t f = tl->frames[from];
  if (from < to) memmove(tl->frames + from, tl->frames + from + 1, (to - from) * sizeof(f));
  else memmove(tl->frames + to + 1, tl->frames + to, (from - to) * sizeof(f));
  tl->frames[to] = f;
  if (tl->active_frame == from) tl->active_frame = to;
  else if (from < to && tl->active_frame > from && tl->active_frame <= to) tl->active_frame--;
  else if (from > to && tl->active_frame >= to && tl->active_frame < from) tl->active_frame++;
}

bool anim_timeline_switch_frame(anim_timeline_t *tl, int idx,
                                uint8_t **working_buf, int w, int h,
                                frame_format_t commit_fmt) {
  if (!tl || !working_buf || !*working_buf || w <= 0 || h <= 0) return false;
  if (idx < 0 || idx >= tl->frame_count) return false;
  if (idx == tl->active_frame) return true;

  // Playback only displays stored frames; do not recompress.
  if (!tl->playing) {
    anim_frame_t *cur = tl->frames[tl->active_frame].cel;
    if (!anim_frame_compress(cur, *working_buf, w, h, commit_fmt))
      return false;
  }

  // Load the new frame: expand into working_buf.
  anim_frame_t *next = tl->frames[idx].cel;
  if (next->data && next->data_size > 0) {
    if (!anim_frame_expand(next, *working_buf, w, h))
      return false;
  } else {
    // New empty frame — start with transparent pixels.
    memset(*working_buf, 0, (size_t)w * (size_t)h * DOC_BPP);
  }

  tl->active_frame = idx;
  return true;
}

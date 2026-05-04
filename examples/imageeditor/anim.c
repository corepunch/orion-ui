// Animation support: frame compress/expand and timeline CRUD.
// Compiled only when IMAGEEDITOR_ANIMATIONS == 1.

#include "imageeditor.h"

#if IMAGEEDITOR_ANIMATIONS

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
      oct_free(n->children[i]);
      n->children[i] = NULL;
      oc->leaf_count--;
    }
  }
  n->r = r; n->g = g; n->b = b; n->a = a; n->count = cnt;
  n->is_leaf = true;
  oc->leaf_count++;
}

// Walk the tree and collect up to 256 leaf colours.
static int oct_collect(oct_node_t *n, uint32_t *palette, int *pal_size) {
  if (!n) return 0;
  if (n->is_leaf || n->count == 0) {
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
static int quantize_rgba_indexed(const uint8_t *rgba, int w, int h,
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

bool anim_frame_compress(anim_frame_t *frame, const uint8_t *rgba,
                         int w, int h, frame_format_t fmt) {
  if (!frame || !rgba || w <= 0 || h <= 0) return false;

  free(frame->data);
  frame->data      = NULL;
  frame->data_size = 0;
  frame->format    = fmt;

  size_t npx = (size_t)w * (size_t)h;

  switch (fmt) {
    case FRAME_FORMAT_RGBA: {
      size_t sz = npx * 4;
      frame->data = malloc(sz);
      if (!frame->data) return false;
      memcpy(frame->data, rgba, sz);
      frame->data_size = sz;
      return true;
    }

    case FRAME_FORMAT_INDEXED: {
      uint8_t *indices = malloc(npx);
      if (!indices) return false;
      memset(frame->palette, 0, sizeof(frame->palette));
      int pal_sz = quantize_rgba_indexed(rgba, w, h, frame->palette, indices);
      if (pal_sz == 0) { free(indices); return false; }
      frame->data      = indices;
      frame->data_size = npx;
      return true;
    }

    case FRAME_FORMAT_BITMAP_1BIT: {
      size_t byte_count = (npx + 7) / 8;
      uint8_t *bits = calloc(1, byte_count);
      if (!bits) return false;
      for (size_t i = 0; i < npx; i++) {
        uint8_t lum = rgba_luminance(rgba + i * 4);
        if (lum > 128)
          bits[i / 8] |= (uint8_t)(1u << (7 - (i & 7)));
      }
      frame->data      = bits;
      frame->data_size = byte_count;
      return true;
    }

    default:
      return false;
  }
}

bool anim_frame_expand(const anim_frame_t *frame, uint8_t *rgba_out,
                       int w, int h) {
  if (!frame || !rgba_out || w <= 0 || h <= 0 || !frame->data) return false;

  size_t npx = (size_t)w * (size_t)h;

  switch (frame->format) {
    case FRAME_FORMAT_RGBA:
      if (frame->data_size < npx * 4) return false;
      memcpy(rgba_out, frame->data, npx * 4);
      return true;

    case FRAME_FORMAT_INDEXED:
      if (frame->data_size < npx) return false;
      for (size_t i = 0; i < npx; i++) {
        uint8_t idx = frame->data[i];
        uint32_t col = frame->palette[idx];
        rgba_out[i*4+0] = COLOR_R(col);
        rgba_out[i*4+1] = COLOR_G(col);
        rgba_out[i*4+2] = COLOR_B(col);
        rgba_out[i*4+3] = COLOR_A(col);
      }
      return true;

    case FRAME_FORMAT_BITMAP_1BIT: {
      size_t byte_count = (npx + 7) / 8;
      if (frame->data_size < byte_count) return false;
      for (size_t i = 0; i < npx; i++) {
        uint8_t bit = (frame->data[i / 8] >> (7 - (i & 7))) & 1;
        uint8_t v = bit ? 255 : 0;
        rgba_out[i*4+0] = v;
        rgba_out[i*4+1] = v;
        rgba_out[i*4+2] = v;
        rgba_out[i*4+3] = 255;
      }
      return true;
    }

    default:
      return false;
  }
}

// ============================================================
// Frame / timeline lifecycle
// ============================================================

anim_frame_t *anim_frame_new(const char *name, int delay_ms) {
  anim_frame_t *f = calloc(1, sizeof(anim_frame_t));
  if (!f) return NULL;
  f->delay_ms = delay_ms > 0 ? delay_ms : 80; // default ~12 fps
  f->format   = FRAME_FORMAT_INDEXED;
  if (name)
    strncpy(f->name, name, sizeof(f->name) - 1);
  else
    strncpy(f->name, "Frame", sizeof(f->name) - 1);
  return f;
}

void anim_frame_free(anim_frame_t *f) {
  if (!f) return;
  free(f->data);
  free(f);
}

anim_timeline_t *anim_timeline_new(int w, int h) {
  anim_timeline_t *tl = calloc(1, sizeof(anim_timeline_t));
  if (!tl) return NULL;

  tl->frames = malloc(sizeof(anim_frame_t *));
  if (!tl->frames) { free(tl); return NULL; }

  anim_frame_t *f = anim_frame_new("Frame 1", 80);
  if (!f) { free(tl->frames); free(tl); return NULL; }

  // Allocate an initial blank (transparent) RGBA frame so the first
  // frame's pixels are valid RGBA that can be committed later.
  size_t sz = (size_t)w * (size_t)h * 4;
  f->data = calloc(1, sz);
  if (!f->data) { anim_frame_free(f); free(tl->frames); free(tl); return NULL; }
  f->data_size = sz;
  f->format    = FRAME_FORMAT_RGBA;

  tl->frames[0]    = f;
  tl->frame_count  = 1;
  tl->active_frame = 0;
  tl->fps          = 12;
  tl->loop         = true;
  return tl;
}

void anim_timeline_free(anim_timeline_t *tl) {
  if (!tl) return;
  for (int i = 0; i < tl->frame_count; i++)
    anim_frame_free(tl->frames[i]);
  free(tl->frames);
  free(tl);
}

// ============================================================
// Timeline frame manipulation
// ============================================================

int anim_timeline_insert_frame(anim_timeline_t *tl, int after) {
  if (!tl) return -1;
  anim_frame_t **nf = realloc(tl->frames,
                               sizeof(anim_frame_t *) * (tl->frame_count + 1));
  if (!nf) return -1;
  tl->frames = nf;

  char name[32];
  snprintf(name, sizeof(name), "Frame %d", tl->frame_count + 1);
  anim_frame_t *f = anim_frame_new(name, 80);
  if (!f) return -1;

  int ins = (after < 0 || after >= tl->frame_count) ? tl->frame_count : after + 1;
  memmove(&tl->frames[ins + 1], &tl->frames[ins],
          sizeof(anim_frame_t *) * (tl->frame_count - ins));
  tl->frames[ins] = f;
  tl->frame_count++;
  if (tl->active_frame >= ins)
    tl->active_frame++;
  return ins;
}

int anim_timeline_duplicate_frame(anim_timeline_t *tl, int idx) {
  if (!tl || idx < 0 || idx >= tl->frame_count) return -1;
  const anim_frame_t *src = tl->frames[idx];

  anim_frame_t **nf = realloc(tl->frames,
                               sizeof(anim_frame_t *) * (tl->frame_count + 1));
  if (!nf) return -1;
  tl->frames = nf;

  anim_frame_t *copy = anim_frame_new(src->name, src->delay_ms);
  if (!copy) return -1;
  copy->format = src->format;
  memcpy(copy->palette, src->palette, sizeof(src->palette));
  if (src->data_size > 0) {
    copy->data = malloc(src->data_size);
    if (!copy->data) { anim_frame_free(copy); return -1; }
    memcpy(copy->data, src->data, src->data_size);
    copy->data_size = src->data_size;
  }

  int ins = idx + 1;
  memmove(&tl->frames[ins + 1], &tl->frames[ins],
          sizeof(anim_frame_t *) * (tl->frame_count - ins));
  tl->frames[ins] = copy;
  tl->frame_count++;
  if (tl->active_frame >= ins)
    tl->active_frame++;
  return ins;
}

bool anim_timeline_delete_frame(anim_timeline_t *tl, int idx) {
  if (!tl || tl->frame_count <= 1) return false;
  if (idx < 0 || idx >= tl->frame_count) return false;

  anim_frame_free(tl->frames[idx]);
  memmove(&tl->frames[idx], &tl->frames[idx + 1],
          sizeof(anim_frame_t *) * (tl->frame_count - idx - 1));
  tl->frame_count--;

  if (tl->active_frame >= tl->frame_count)
    tl->active_frame = tl->frame_count - 1;
  return true;
}

void anim_timeline_move_frame(anim_timeline_t *tl, int from, int to) {
  if (!tl || from == to) return;
  if (from < 0 || from >= tl->frame_count) return;
  if (to   < 0 || to   >= tl->frame_count) return;

  anim_frame_t *f = tl->frames[from];
  if (from < to) {
    memmove(&tl->frames[from], &tl->frames[from + 1],
            sizeof(anim_frame_t *) * (to - from));
  } else {
    memmove(&tl->frames[to + 1], &tl->frames[to],
            sizeof(anim_frame_t *) * (from - to));
  }
  tl->frames[to] = f;

  if (tl->active_frame == from) {
    tl->active_frame = to;
  } else if (from < to) {
    if (tl->active_frame > from && tl->active_frame <= to)
      tl->active_frame--;
  } else {
    if (tl->active_frame >= to && tl->active_frame < from)
      tl->active_frame++;
  }
}

bool anim_timeline_switch_frame(anim_timeline_t *tl, int idx,
                                uint8_t **rgba_working, int w, int h,
                                frame_format_t commit_fmt) {
  if (!tl || !rgba_working || !*rgba_working || w <= 0 || h <= 0) return false;
  if (idx < 0 || idx >= tl->frame_count) return false;
  if (idx == tl->active_frame) return true;

  // Commit current RGBA to the active frame.
  anim_frame_t *cur = tl->frames[tl->active_frame];
  if (!anim_frame_compress(cur, *rgba_working, w, h, commit_fmt))
    return false;

  // Load the new frame: expand into rgba_working.
  anim_frame_t *next = tl->frames[idx];
  if (next->data && next->data_size > 0) {
    if (!anim_frame_expand(next, *rgba_working, w, h))
      return false;
  } else {
    // New empty frame — start with transparent pixels.
    memset(*rgba_working, 0, (size_t)w * (size_t)h * 4);
  }

  tl->active_frame = idx;
  return true;
}

#endif // IMAGEEDITOR_ANIMATIONS

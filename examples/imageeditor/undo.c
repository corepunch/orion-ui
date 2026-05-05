// Undo/redo history for canvas documents.
// Each history entry is a heap-allocated blob that serializes the full layer
// stack so that undo/redo restores layers, alpha, opacity, visibility, etc.
//
// Blob layout:
//   [snap_header_t]
//   for each layer:
//     [snap_layer_hdr_t]
//     [canvas_w * canvas_h * DOC_BPP]  pixel data

#include "imageeditor.h"

#define SNAP_MAGIC 0x4C595253u  // 'LYRS'

typedef struct {
  uint32_t magic;
  int32_t  layer_count;
  int32_t  active_layer;
  int32_t  canvas_w;
  int32_t  canvas_h;
} snap_header_t;

typedef struct {
  char    name[64];
  uint8_t visible;
  uint8_t opacity;
  uint8_t blend_mode;
  uint8_t _pad;
} snap_layer_hdr_t;

// Free all entries on one stack and reset its count to zero.
static void clear_stack(uint8_t **states, int *count) {
  for (int i = 0; i < *count; i++) {
    free(states[i]);
    states[i] = NULL;
  }
  *count = 0;
}

// Serialize the full layer stack into a heap blob; returns NULL on OOM.
static uint8_t *make_snapshot(const canvas_doc_t *doc) {
  if (!doc || doc->layer.count == 0) return NULL;
  size_t px_sz = (size_t)doc->canvas_w * doc->canvas_h * DOC_BPP;

  size_t total = sizeof(snap_header_t);
  for (int i = 0; i < doc->layer.count; i++) {
    total += sizeof(snap_layer_hdr_t) + px_sz;
  }

  uint8_t *blob = malloc(total);
  if (!blob) return NULL;
  uint8_t *p = blob;

  snap_header_t *hdr = (snap_header_t *)p;
  hdr->magic        = SNAP_MAGIC;
  hdr->layer_count  = doc->layer.count;
  hdr->active_layer = doc->layer.active;
  hdr->canvas_w     = doc->canvas_w;
  hdr->canvas_h     = doc->canvas_h;
  p += sizeof(snap_header_t);

  for (int i = 0; i < doc->layer.count; i++) {
    const layer_t *lay = doc->layer.stack[i];
    snap_layer_hdr_t *lhdr = (snap_layer_hdr_t *)p;
    memcpy(lhdr->name, lay->name, 64);
    lhdr->visible  = lay->visible;
    lhdr->opacity  = lay->opacity;
    lhdr->blend_mode = lay->blend_mode;
    lhdr->_pad      = 0;
    p += sizeof(snap_layer_hdr_t);

    memcpy(p, lay->pixels, px_sz);
    p += px_sz;
  }
  return blob;
}

// Restore the layer stack from a blob produced by make_snapshot.
// Also restores canvas dimensions (needed for undo of canvas_resize).
static bool restore_snapshot(canvas_doc_t *doc, const uint8_t *blob) {
  if (!blob) return false;
  const snap_header_t *hdr = (const snap_header_t *)blob;
  if (hdr->magic != SNAP_MAGIC || hdr->layer_count <= 0) return false;

  int      n     = hdr->layer_count;
  int      new_w = hdr->canvas_w;
  int      new_h = hdr->canvas_h;
  size_t   px_sz = (size_t)new_w * new_h * DOC_BPP;
  const uint8_t *p = blob + sizeof(snap_header_t);

  layer_t **nl = malloc(sizeof(layer_t *) * n);
  if (!nl) return false;

  // Local helper: free the first `count` partially-constructed layers.
  // We use a labelled goto because C doesn't support local lambdas.
  int built = 0;  // number of successfully allocated layers so far

  for (int i = 0; i < n; i++) {
    const snap_layer_hdr_t *lhdr = (const snap_layer_hdr_t *)p;
    p += sizeof(snap_layer_hdr_t);

    layer_t *lay = calloc(1, sizeof(layer_t));
    if (!lay) goto cleanup;
    lay->pixels = malloc(px_sz);
    if (!lay->pixels) { free(lay); goto cleanup; }
    nl[i] = lay;
    built = i + 1;  // lay is now owned by nl[i]

    memcpy(lay->name, lhdr->name, sizeof(lay->name) - 1);
    lay->name[sizeof(lay->name) - 1] = '\0';
    lay->visible = lhdr->visible;
    lay->opacity = lhdr->opacity;
    lay->blend_mode = lhdr->blend_mode < LAYER_BLEND_COUNT ? lhdr->blend_mode : LAYER_BLEND_NORMAL;
    memcpy(lay->pixels, p, px_sz);
    p += px_sz;
  }
  goto restore;

cleanup:
  for (int j = 0; j < built; j++) { free(nl[j]->pixels); free(nl[j]); }
  free(nl);
  return false;

restore:

  // Replace old layer stack (use doc_free_layers to also release GL textures).
  doc_free_layers(doc);

  doc->layer.stack      = nl;
  doc->layer.count = n;
  doc->layer.active = hdr->active_layer < n ? hdr->active_layer : n - 1;
  doc->pixels       = doc->layer.stack[doc->layer.active]->pixels;

  // Restore canvas dimensions if they changed (e.g. after resize).
  if (new_w != doc->canvas_w || new_h != doc->canvas_h) {
    free(doc->layer.composite_buf);
    doc->layer.composite_buf = malloc(px_sz);
    doc->canvas_w = new_w;
    doc->canvas_h = new_h;
  }

  doc->layer.editing_mask = false;
  doc->canvas_dirty = true;
  doc->modified     = true;
  return true;
}

// Push a snapshot onto a stack, dropping the oldest entry when the stack is
// already full.
static void stack_push(uint8_t **states, int *count, uint8_t *snap) {
  if (*count == UNDO_MAX) {
    free(states[0]);
    memmove(states, states + 1, (UNDO_MAX - 1) * sizeof(uint8_t *));
    (*count)--;
  }
  states[(*count)++] = snap;
}

// Save the current layer stack as a new undo state.
// Any existing redo history is discarded if the snapshot is successfully created.
void doc_push_undo(canvas_doc_t *doc) {
  if (!doc) return;
  uint8_t *snap = make_snapshot(doc);
  if (!snap) return;
  clear_stack(doc->redo.states, &doc->redo.count);
  stack_push(doc->undo.states, &doc->undo.count, snap);
}

// Restore the most recent undo state.
// The current state is pushed onto the redo stack first.
// Returns true if an undo was performed.
bool doc_undo(canvas_doc_t *doc) {
  if (!doc || doc->undo.count == 0) return false;
  uint8_t *current = make_snapshot(doc);
  if (!current) return false;
  stack_push(doc->redo.states, &doc->redo.count, current);

  doc->undo.count--;
  bool ok = restore_snapshot(doc, doc->undo.states[doc->undo.count]);
  free(doc->undo.states[doc->undo.count]);
  doc->undo.states[doc->undo.count] = NULL;
  return ok;
}

// Restore the most recently undone state.
// The current state is pushed onto the undo stack first.
// Returns true if a redo was performed.
bool doc_redo(canvas_doc_t *doc) {
  if (!doc || doc->redo.count == 0) return false;
  uint8_t *current = make_snapshot(doc);
  if (!current) return false;
  stack_push(doc->undo.states, &doc->undo.count, current);

  doc->redo.count--;
  bool ok = restore_snapshot(doc, doc->redo.states[doc->redo.count]);
  free(doc->redo.states[doc->redo.count]);
  doc->redo.states[doc->redo.count] = NULL;
  return ok;
}

// Release all undo/redo memory (call when closing a document).
void doc_free_undo(canvas_doc_t *doc) {
  if (!doc) return;
  clear_stack(doc->undo.states, &doc->undo.count);
  clear_stack(doc->redo.states, &doc->redo.count);
}

// Drop the most recently pushed undo entry without applying it.
// Use this when an operation is cancelled after pushing an undo state.
void doc_discard_undo(canvas_doc_t *doc) {
  if (!doc || doc->undo.count == 0) return;
  doc->undo.count--;
  free(doc->undo.states[doc->undo.count]);
  doc->undo.states[doc->undo.count] = NULL;
}

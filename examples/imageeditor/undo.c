// Undo/redo history for canvas documents.
// Each history entry is a heap-allocated blob that serializes the full layer
// stack so that undo/redo restores layers, alpha, opacity, visibility, etc.
//
// Blob layout:
//   [snap_header_t]
//   for each layer:
//     [snap_layer_hdr_t]
//     [canvas_w * canvas_h * 4]  pixel data (RGBA)

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
  uint8_t _pad[2];
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
  if (!doc || doc->layer_count == 0) return NULL;
  size_t px_sz = (size_t)doc->canvas_w * doc->canvas_h * 4;

  size_t total = sizeof(snap_header_t);
  for (int i = 0; i < doc->layer_count; i++) {
    total += sizeof(snap_layer_hdr_t) + px_sz;
  }

  uint8_t *blob = malloc(total);
  if (!blob) return NULL;
  uint8_t *p = blob;

  snap_header_t *hdr = (snap_header_t *)p;
  hdr->magic        = SNAP_MAGIC;
  hdr->layer_count  = doc->layer_count;
  hdr->active_layer = doc->active_layer;
  hdr->canvas_w     = doc->canvas_w;
  hdr->canvas_h     = doc->canvas_h;
  p += sizeof(snap_header_t);

  for (int i = 0; i < doc->layer_count; i++) {
    const layer_t *lay = doc->layers[i];
    snap_layer_hdr_t *lhdr = (snap_layer_hdr_t *)p;
    memcpy(lhdr->name, lay->name, 64);
    lhdr->visible  = lay->visible;
    lhdr->opacity  = lay->opacity;
    lhdr->_pad[0]  = 0;
    lhdr->_pad[1]  = 0;
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
  size_t   px_sz = (size_t)new_w * new_h * 4;
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
    memcpy(lay->pixels, p, px_sz);
    p += px_sz;
  }
  goto restore;

cleanup:
  for (int j = 0; j < built; j++) { free(nl[j]->pixels); free(nl[j]); }
  free(nl);
  return false;

restore:

  // Replace old layer stack.
  for (int i = 0; i < doc->layer_count; i++) {
    free(doc->layers[i]->pixels);
    free(doc->layers[i]);
  }
  free(doc->layers);

  doc->layers      = nl;
  doc->layer_count = n;
  doc->active_layer = hdr->active_layer < n ? hdr->active_layer : n - 1;
  doc->pixels       = doc->layers[doc->active_layer]->pixels;

  // Restore canvas dimensions if they changed (e.g. after resize).
  if (new_w != doc->canvas_w || new_h != doc->canvas_h) {
    free(doc->composite_buf);
    doc->composite_buf = malloc(px_sz);
    if (doc->canvas_tex) {
      glDeleteTextures(1, &doc->canvas_tex);
      doc->canvas_tex = 0;
    }
    doc->canvas_w = new_w;
    doc->canvas_h = new_h;
  }

  doc->editing_mask = false;
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
  clear_stack(doc->redo_states, &doc->redo_count);
  stack_push(doc->undo_states, &doc->undo_count, snap);
}

// Restore the most recent undo state.
// The current state is pushed onto the redo stack first.
// Returns true if an undo was performed.
bool doc_undo(canvas_doc_t *doc) {
  if (!doc || doc->undo_count == 0) return false;
  uint8_t *current = make_snapshot(doc);
  if (!current) return false;
  stack_push(doc->redo_states, &doc->redo_count, current);

  doc->undo_count--;
  bool ok = restore_snapshot(doc, doc->undo_states[doc->undo_count]);
  free(doc->undo_states[doc->undo_count]);
  doc->undo_states[doc->undo_count] = NULL;
  return ok;
}

// Restore the most recently undone state.
// The current state is pushed onto the undo stack first.
// Returns true if a redo was performed.
bool doc_redo(canvas_doc_t *doc) {
  if (!doc || doc->redo_count == 0) return false;
  uint8_t *current = make_snapshot(doc);
  if (!current) return false;
  stack_push(doc->undo_states, &doc->undo_count, current);

  doc->redo_count--;
  bool ok = restore_snapshot(doc, doc->redo_states[doc->redo_count]);
  free(doc->redo_states[doc->redo_count]);
  doc->redo_states[doc->redo_count] = NULL;
  return ok;
}

// Release all undo/redo memory (call when closing a document).
void doc_free_undo(canvas_doc_t *doc) {
  if (!doc) return;
  clear_stack(doc->undo_states, &doc->undo_count);
  clear_stack(doc->redo_states, &doc->redo_count);
}

// Drop the most recently pushed undo entry without applying it.
// Use this when an operation is cancelled after pushing an undo state.
void doc_discard_undo(canvas_doc_t *doc) {
  if (!doc || doc->undo_count == 0) return;
  doc->undo_count--;
  free(doc->undo_states[doc->undo_count]);
  doc->undo_states[doc->undo_count] = NULL;
}

// Undo/redo history for canvas documents.
// Each history entry is a heap-allocated full pixel snapshot.
// Up to UNDO_MAX entries are kept per stack; the oldest is dropped when full.

#include "imageeditor.h"

// Free all entries on one stack and reset its count to zero.
static void clear_stack(uint8_t **states, int *count) {
  for (int i = 0; i < *count; i++) {
    free(states[i]);
    states[i] = NULL;
  }
  *count = 0;
}

// Allocate and return a snapshot of doc->pixels, or NULL on OOM.
static uint8_t *make_snapshot(const canvas_doc_t *doc) {
  size_t sz = (size_t)doc->canvas_w * doc->canvas_h * 4;
  uint8_t *snap = malloc(sz);
  if (snap) memcpy(snap, doc->pixels, sz);
  return snap;
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

// Save the current canvas pixels as a new undo state.
// Any existing redo history is discarded if the new undo snapshot is
// successfully created.
void doc_push_undo(canvas_doc_t *doc) {
  if (!doc) return;

  uint8_t *snap = make_snapshot(doc);
  if (!snap) {
    // Allocation failed; leave existing undo/redo history intact.
    return;
  }

  clear_stack(doc->redo_states, &doc->redo_count);
  stack_push(doc->undo_states, &doc->undo_count, snap);
}

// Restore the most recent undo state.
// The current pixels are pushed onto the redo stack first.
// Returns true if an undo was performed.
bool doc_undo(canvas_doc_t *doc) {
  if (!doc || doc->undo_count == 0) return false;

  uint8_t *current = make_snapshot(doc);
  if (!current) return false;
  stack_push(doc->redo_states, &doc->redo_count, current);

  doc->undo_count--;
  memcpy(doc->pixels, doc->undo_states[doc->undo_count], (size_t)doc->canvas_w * doc->canvas_h * 4);
  free(doc->undo_states[doc->undo_count]);
  doc->undo_states[doc->undo_count] = NULL;

  doc->canvas_dirty = true;
  doc->modified     = true;
  return true;
}

// Restore the most recently undone state.
// The current pixels are pushed onto the undo stack first.
// Returns true if a redo was performed.
bool doc_redo(canvas_doc_t *doc) {
  if (!doc || doc->redo_count == 0) return false;

  uint8_t *current = make_snapshot(doc);
  if (!current) return false;
  stack_push(doc->undo_states, &doc->undo_count, current);

  doc->redo_count--;
  memcpy(doc->pixels, doc->redo_states[doc->redo_count], (size_t)doc->canvas_w * doc->canvas_h * 4);
  free(doc->redo_states[doc->redo_count]);
  doc->redo_states[doc->redo_count] = NULL;

  doc->canvas_dirty = true;
  doc->modified     = true;
  return true;
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

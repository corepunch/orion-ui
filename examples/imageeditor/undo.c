// Undo/redo history for canvas documents.
// Each history entry is a heap-allocated full pixel snapshot (~250 KB).
// Up to UNDO_MAX entries are kept per stack; the oldest is dropped when full.

#include "imageeditor.h"

static size_t kSnapSize = CANVAS_W * CANVAS_H * 4;

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
  uint8_t *snap = malloc(kSnapSize);
  if (snap) memcpy(snap, doc->pixels, kSnapSize);
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
// Any existing redo history is discarded.
void doc_push_undo(canvas_doc_t *doc) {
  if (!doc) return;
  clear_stack(doc->redo_states, &doc->redo_count);
  uint8_t *snap = make_snapshot(doc);
  if (snap) stack_push(doc->undo_states, &doc->undo_count, snap);
}

// Restore the most recent undo state.
// The current pixels are pushed onto the redo stack first.
// Returns true if an undo was performed.
bool doc_undo(canvas_doc_t *doc) {
  if (!doc || doc->undo_count == 0) return false;

  uint8_t *current = make_snapshot(doc);
  if (current) stack_push(doc->redo_states, &doc->redo_count, current);

  doc->undo_count--;
  memcpy(doc->pixels, doc->undo_states[doc->undo_count], kSnapSize);
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
  if (current) stack_push(doc->undo_states, &doc->undo_count, current);

  doc->redo_count--;
  memcpy(doc->pixels, doc->redo_states[doc->redo_count], kSnapSize);
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

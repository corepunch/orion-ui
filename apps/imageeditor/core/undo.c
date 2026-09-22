// Complete document checkpoints. Rendering caches and UI state are never owned by history.
#include "imageeditor.h"

struct doc_snapshot_s {
  canvas_doc_t state;
};

static void free_snapshot(doc_snapshot_t *blob) {
  if (!blob) return;
  canvas_doc_t *s = &blob->state;
  doc_free_layers(s);
  free(s->layer.composite_buf);
  free(s->sel.mask.data);
  free(s->sel.floating.pixels);
  free(s->sel.floating.mask);
  anim_timeline_free(s->anim);
  free(blob);
}

static bool copy_bytes(uint8_t **dst, const uint8_t *src, size_t size) {
  *dst = NULL;
  if (!src || !size) return true;
  *dst = malloc(size);
  if (!*dst) return false;
  memcpy(*dst, src, size);
  return true;
}

static doc_snapshot_t *make_snapshot(const canvas_doc_t *doc) {
  if (!doc || !doc->layer.count) return NULL;
  doc_snapshot_t *snap = calloc(1, sizeof(*snap));
  if (!snap) return NULL;
  canvas_doc_t *s = &snap->state;
  size_t area = (size_t)doc->canvas_w * doc->canvas_h;
  s->canvas_w = doc->canvas_w;
  s->canvas_h = doc->canvas_h;
  s->background = doc->background;
  s->modified = doc->modified;
  s->layer.active = doc->layer.active;
  s->layer.editing_mask = doc->layer.editing_mask;
  s->layer.mask_only_view = doc->layer.mask_only_view;
  s->layer.stack = calloc(doc->layer.count, sizeof(layer_t *));
  s->layer.composite_buf = malloc(area * 4);
  if (!s->layer.stack || !s->layer.composite_buf) goto fail;
  for (int i = 0; i < doc->layer.count; i++) {
    layer_t *lay = calloc(1, sizeof(*lay));
    if (!lay) goto fail;
    s->layer.stack[s->layer.count++] = lay;
    *lay = *doc->layer.stack[i];
    lay->tex = 0;
    lay->preview.active = false;
    if (!copy_bytes(&lay->pixels, doc->layer.stack[i]->pixels, area * DOC_BPP)) goto fail;
  }
  s->sel = doc->sel;
  s->sel.mask.tex = s->sel.floating.tex = 0;
  s->sel.mask.data = s->sel.floating.pixels = s->sel.floating.mask = NULL;
  size_t floating_area = (size_t)doc->sel.floating.rect.w * doc->sel.floating.rect.h;
  if (!copy_bytes(&s->sel.mask.data, doc->sel.mask.data, area) ||
      !copy_bytes(&s->sel.floating.pixels, doc->sel.floating.pixels, floating_area * 4) ||
      !copy_bytes(&s->sel.floating.mask, doc->sel.floating.mask, floating_area)) goto fail;
#if IMAGEEDITOR_INDEXED
  s->ipal = doc->ipal;
#endif
  if (doc->anim) {
    s->anim = calloc(1, sizeof(*s->anim));
    if (!s->anim) goto fail;
    *s->anim = *doc->anim;
    s->anim->frame_count = 0;
    s->anim->playing = false;
    s->anim->frames = calloc(doc->anim->frame_count, sizeof(anim_frame_t *));
    if (!s->anim->frames) goto fail;
    for (int i = 0; i < doc->anim->frame_count; i++) {
      anim_frame_t *frame = calloc(1, sizeof(*frame));
      if (!frame) goto fail;
      s->anim->frames[s->anim->frame_count++] = frame;
      *frame = *doc->anim->frames[i];
      frame->cels = NULL;
      if (!copy_bytes(&frame->data, doc->anim->frames[i]->data, frame->data_size) ||
          !copy_bytes(&frame->cels, doc->anim->frames[i]->cels, frame->cels_size)) goto fail;
    }
  }
  return snap;
fail:
  free_snapshot(snap);
  IE_TRACE("history snapshot allocation failed doc=%p", (void *)doc);
  return NULL;
}

// Transfer prepared storage into the live document; this cannot fail allocation.
static void restore_snapshot(canvas_doc_t *doc, doc_snapshot_t *blob) {
  canvas_doc_t *s = &blob->state;
  doc_free_layers(doc);
  free(doc->layer.composite_buf);
  canvas_discard_move(doc);
  canvas_clear_selection_mask(doc);
  anim_timeline_free(doc->anim);
  free(doc->shape.snapshot);
  doc->shape.snapshot = NULL;
  doc->drawing = doc->poly.active = doc->stroke.active = false;
  doc->poly.count = 0;
  doc->canvas_w = s->canvas_w;
  doc->canvas_h = s->canvas_h;
  doc->background = s->background;
  doc->layer = s->layer;
  doc->sel = s->sel;
  doc->sel.mask.dirty = true;
  doc->anim = s->anim;
  doc->pixels = doc->layer.stack[doc->layer.active]->pixels;
#if IMAGEEDITOR_INDEXED
  doc->ipal = s->ipal;
#endif
  doc->canvas_dirty = true;
  doc->modified = s->modified;
  memset(s, 0, sizeof(*s));
}

static void clear_stack(doc_snapshot_t **states, int *count) {
  for (int i = 0; i < *count; i++) { free_snapshot(states[i]); states[i] = NULL; }
  *count = 0;
}

static void stack_push(doc_snapshot_t **states, int *count, doc_snapshot_t *snap) {
  if (*count == UNDO_MAX) {
    free_snapshot(states[0]);
    memmove(states, states + 1, (UNDO_MAX - 1) * sizeof(*states));
    (*count)--;
  }
  states[(*count)++] = snap;
}

bool doc_begin_command(canvas_doc_t *doc, const char *name) {
  if (!doc || doc->command.before) {
    IE_TRACE("command rejected doc=%p name=%s: operation already active or no document", (void *)doc, name);
    return false;
  }
  doc->command.before = make_snapshot(doc);
  if (!doc->command.before) return false;
  doc->command.name = name;
  IE_TRACE("command begin doc=%p name=%s frame=%d", (void *)doc, name, doc->anim ? doc->anim->active_frame : -1);
  return true;
}

static bool bytes_equal(const void *a, const void *b, size_t size) {
  return a == b || (a && b && memcmp(a, b, size) == 0);
}

static bool snapshot_matches(const canvas_doc_t *doc, const canvas_doc_t *s) {
  if (doc->canvas_w != s->canvas_w || doc->canvas_h != s->canvas_h ||
      doc->layer.count != s->layer.count || doc->layer.active != s->layer.active ||
      doc->layer.editing_mask != s->layer.editing_mask ||
      doc->background.color != s->background.color || doc->background.show != s->background.show ||
      doc->sel.active != s->sel.active || doc->sel.move.active != s->sel.move.active) return false;
  size_t area = (size_t)doc->canvas_w * doc->canvas_h;
  for (int i = 0; i < doc->layer.count; i++) {
    layer_t *a = doc->layer.stack[i], *b = s->layer.stack[i];
    if (strcmp(a->name, b->name) || a->visible != b->visible || a->opacity != b->opacity ||
        a->blend_mode != b->blend_mode || !bytes_equal(a->pixels, b->pixels, area * DOC_BPP)) return false;
  }
  if (!bytes_equal(doc->sel.mask.data, s->sel.mask.data, area)) return false;
  if (doc->sel.active && (memcmp(&doc->sel.start, &s->sel.start, sizeof(ipoint16_t)) ||
      memcmp(&doc->sel.end, &s->sel.end, sizeof(ipoint16_t)) ||
      memcmp(&doc->sel.mask.offset, &s->sel.mask.offset, sizeof(ipoint16_t)))) return false;
  if (doc->sel.move.active) {
    size_t size = (size_t)doc->sel.floating.rect.w * doc->sel.floating.rect.h;
    if (memcmp(&doc->sel.floating.rect, &s->sel.floating.rect, sizeof(irect16_t)) ||
        !bytes_equal(doc->sel.floating.pixels, s->sel.floating.pixels, size * 4) ||
        !bytes_equal(doc->sel.floating.mask, s->sel.floating.mask, size)) return false;
  }
#if IMAGEEDITOR_INDEXED
  if (memcmp(&doc->ipal, &s->ipal, sizeof(doc->ipal))) return false;
#endif
  if (!!doc->anim != !!s->anim) return false;
  if (doc->anim) {
    anim_timeline_t *a = doc->anim, *b = s->anim;
    if (a->frame_count != b->frame_count || a->active_frame != b->active_frame ||
        a->fps != b->fps || a->loop != b->loop) return false;
    for (int i = 0; i < a->frame_count; i++) {
      anim_frame_t *af = a->frames[i], *bf = b->frames[i];
      if (af->format != bf->format || af->data_size != bf->data_size ||
          af->cels_size != bf->cels_size || !bytes_equal(af->cels, bf->cels, af->cels_size) ||
          af->delay_ms != bf->delay_ms || strcmp(af->name, bf->name) ||
          memcmp(af->palette, bf->palette, sizeof(af->palette)) ||
          !bytes_equal(af->data, bf->data, af->data_size)) return false;
    }
  }
  return true;
}

void doc_end_command(canvas_doc_t *doc, bool success) {
  if (!doc || !doc->command.before) return;
  doc_snapshot_t *before = doc->command.before;
  IE_TRACE("command %s doc=%p name=%s", success ? "commit" : "cancel", (void *)doc, doc->command.name);
  doc->command.before = NULL;
  doc->command.name = NULL;
  if (success && snapshot_matches(doc, &before->state)) {
    doc->modified = before->state.modified;
    free_snapshot(before);
    return;
  }
  if (success && doc->anim)
    success = doc_anim_commit(doc);
  if (success) {
    clear_stack(doc->redo.states, &doc->redo.count);
    stack_push(doc->undo.states, &doc->undo.count, before);
    doc->modified = true;
  } else {
    restore_snapshot(doc, before);
    free_snapshot(before);
  }
}

static bool travel(canvas_doc_t *doc, undo_t *from, undo_t *to) {
  if (doc->command.before || !from->count) return false;
  doc_snapshot_t *current = make_snapshot(doc);
  if (!current) return false;
  doc_snapshot_t *target = from->states[--from->count];
  from->states[from->count] = NULL;
  restore_snapshot(doc, target);
  free_snapshot(target);
  stack_push(to->states, &to->count, current);
  doc->modified = true;
  IE_TRACE("history restore doc=%p undo=%d redo=%d frame=%d", (void *)doc, doc->undo.count, doc->redo.count, doc->anim ? doc->anim->active_frame : -1);
  return true;
}

bool doc_undo(canvas_doc_t *doc) { return doc && travel(doc, &doc->undo, &doc->redo); }
bool doc_redo(canvas_doc_t *doc) { return doc && travel(doc, &doc->redo, &doc->undo); }

void doc_free_undo(canvas_doc_t *doc) {
  if (!doc) return;
  free_snapshot(doc->command.before);
  doc->command.before = NULL;
  clear_stack(doc->undo.states, &doc->undo.count);
  clear_stack(doc->redo.states, &doc->redo.count);
}

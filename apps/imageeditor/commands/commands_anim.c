#include "commands.h"

bool cmd_frame_select(canvas_doc_t *doc, int index) {
  if (!doc || !doc->anim || index < 0 || index >= doc->anim->frame_count) return false;
  if (doc->command.before) return false;
  anim_stop_playback(doc);
  if (index == doc->anim->active_frame) return true;
  IE_TRACE("frame select doc=%p from=%d to=%d", (void *)doc, doc->anim->active_frame, index);
  if (!doc_anim_switch(doc, index)) return false;
  doc->layer.stack[doc->layer.active]->pixels = doc->pixels;
  doc->canvas_dirty = true;
  ie_doc_invalidate_all(doc);
  return true;
}

void cmd_frame_add(canvas_doc_t *doc, bool duplicate) {
  if (!doc || !doc->anim) return;
  anim_stop_playback(doc);
  if (!ie_doc_begin_op(doc, duplicate ? "Duplicate Frame" : "New Frame")) return;
  anim_timeline_t *tl = doc->anim;
  bool ok = doc_anim_commit(doc);
  int index = -1;
  if (ok) index = duplicate ? anim_timeline_duplicate_frame(tl, tl->active_frame)
                            : anim_timeline_insert_frame(tl, tl->active_frame);
  ok = index >= 0 && doc_anim_load(doc, index);
  doc->layer.stack[doc->layer.active]->pixels = doc->pixels;
  doc->canvas_dirty = true;
  ie_doc_commit_op(doc, ok);
}

void cmd_frame_delete(canvas_doc_t *doc) {
  if (!doc || !doc->anim || doc->anim->frame_count <= 1) return;
  anim_stop_playback(doc);
  if (!ie_doc_begin_op(doc, "Delete Frame")) return;
  anim_timeline_t *tl = doc->anim;
  bool ok = anim_timeline_delete_frame(tl, tl->active_frame);
  if (ok) ok = doc_anim_load(doc, tl->active_frame);
  doc->canvas_dirty = true;
  ie_doc_commit_op(doc, ok);
}

void cmd_frame_move(canvas_doc_t *doc, int from, int to) {
  if (!doc || !doc->anim || from == to || from < 0 || to < 0 ||
      from >= doc->anim->frame_count || to >= doc->anim->frame_count) return;
  anim_stop_playback(doc);
  if (!ie_doc_begin_op(doc, "Move Frame")) return;
  anim_timeline_move_frame(doc->anim, from, to);
  ie_doc_commit_op(doc, true);
}

#include "commands.h"

bool cmd_frame_select(canvas_doc_t *doc, int index) {
  if (!doc || !doc->anim || index < 0 || index >= doc->anim->frame_count) return false;
  if (doc->command.before) return false;
  anim_stop_playback(doc);
  if (index == doc->anim->active_frame) return true;
  IE_TRACE("frame select doc=%p from=%d to=%d", (void *)doc, doc->anim->active_frame, index);
  if (!anim_timeline_switch_frame(doc->anim, index, &doc->pixels,
                                  doc->canvas_w, doc->canvas_h, IE_FRAME_FORMAT)) return false;
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
  bool ok = anim_frame_compress(tl->frames[tl->active_frame], doc->pixels,
                                doc->canvas_w, doc->canvas_h, IE_FRAME_FORMAT);
  int index = -1;
  if (ok) index = duplicate ? anim_timeline_duplicate_frame(tl, tl->active_frame)
                            : anim_timeline_insert_frame(tl, tl->active_frame);
  ok = index >= 0 && anim_timeline_switch_frame(tl, index, &doc->pixels,
                                               doc->canvas_w, doc->canvas_h, IE_FRAME_FORMAT);
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
  anim_frame_t *frame = tl->frames[tl->active_frame];
  if (ok && frame->data && frame->data_size)
    ok = anim_frame_expand(frame, doc->pixels, doc->canvas_w, doc->canvas_h);
  else if (ok)
    memset(doc->pixels, 0, (size_t)doc->canvas_w * doc->canvas_h * DOC_BPP);
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

#include "commands.h"

bool cmd_frame_select(canvas_doc_t *doc, int index) {
  if (!doc || !doc->anim || index < 0 || index >= doc->anim->frame_count) return false;
  if (doc->command.before) return false;
  anim_stop_playback(doc);
  if (index == doc->anim->active_frame) return true;
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

bool cmd_anim_fps(canvas_doc_t *doc, int fps, bool retime) {
  if (!doc || !doc->anim || fps < 1 || fps > 1000) return false;
  anim_timeline_t *tl = doc->anim;
  if (tl->fps < 1) return false;
  if (retime) for (int i = 0; i < tl->frame_count; i++)
    if (((int64_t)tl->frames[i].delay_ms * tl->fps + fps / 2) / fps > 65535) return false;
  anim_stop_playback(doc);
  if (!ie_doc_begin_op(doc, "Set FPS")) return false;
  if (retime) for (int i = 0; i < tl->frame_count; i++)
    tl->frames[i].delay_ms = MAX(1, ((int64_t)tl->frames[i].delay_ms * tl->fps + fps / 2) / fps);
  tl->fps = fps;
  ie_doc_commit_op(doc, true);
  return true;
}

bool cmd_frame_duration(canvas_doc_t *doc, int delay_ms, bool all) {
  if (!doc || !doc->anim || delay_ms < 1 || delay_ms > 65535) return false;
  anim_stop_playback(doc);
  if (!ie_doc_begin_op(doc, "Set Frame Duration")) return false;
  for (int i = 0; i < doc->anim->frame_count; i++)
    if (all || i == doc->anim->active_frame) doc->anim->frames[i].delay_ms = delay_ms;
  ie_doc_commit_op(doc, true);
  return true;
}

bool cmd_frame_reuse(canvas_doc_t *doc, uint32_t cel_id) {
  if (!doc || !anim_find_cel(doc->anim, cel_id)) return false;
  anim_stop_playback(doc);
  if (!ie_doc_begin_op(doc, "Reuse Cel")) return false;
  bool ok = doc_anim_commit(doc);
  int index = ok ? anim_timeline_reuse(doc->anim, doc->anim->active_frame, cel_id) : -1;
  ok = index >= 0 && doc_anim_load(doc, index);
  ie_doc_commit_op(doc, ok);
  return ok;
}

bool cmd_frame_unique(canvas_doc_t *doc) {
  if (!doc || !doc->anim) return false;
  anim_stop_playback(doc);
  if (!ie_doc_begin_op(doc, "Make Cel Unique")) return false;
  bool ok = doc_anim_commit(doc) && anim_timeline_make_unique(doc->anim, doc->anim->active_frame);
  ie_doc_commit_op(doc, ok);
  return ok;
}

bool cmd_exposure_edit(canvas_doc_t *doc, uint32_t cel_id, int delay_ms, const char *note) {
  if (!doc || !anim_find_cel(doc->anim, cel_id) || delay_ms < 1 || delay_ms > 65535 || !note || strlen(note) >= 128) return false;
  anim_stop_playback(doc);
  if (!ie_doc_begin_op(doc, "Edit Exposure")) return false;
  bool ok = doc_anim_commit(doc);
  if (ok) {
    anim_exposure_t *row = &doc->anim->frames[doc->anim->active_frame];
    row->cel = anim_find_cel(doc->anim, cel_id); row->delay_ms = delay_ms;
    snprintf(row->note, sizeof(row->note), "%s", note);
    ok = doc_anim_load(doc, doc->anim->active_frame);
  }
  ie_doc_commit_op(doc, ok);
  return ok;
}

bool cmd_cel_rename(canvas_doc_t *doc, uint32_t cel_id, const char *name) {
  anim_frame_t *cel = doc ? anim_find_cel(doc->anim, cel_id) : NULL;
  if (!cel || !name || !*name || strlen(name) >= sizeof(cel->name)) return false;
  anim_stop_playback(doc);
  if (!ie_doc_begin_op(doc, "Rename Cel")) return false;
  snprintf(cel->name, sizeof(cel->name), "%s", name);
  ie_doc_commit_op(doc, true);
  return true;
}

bool cmd_cel_delete(canvas_doc_t *doc, uint32_t cel_id) {
  if (!doc || !anim_find_cel(doc->anim, cel_id) || anim_cel_uses(doc->anim, cel_id)) return false;
  anim_stop_playback(doc);
  if (!ie_doc_begin_op(doc, "Delete Unused Cel")) return false;
  bool ok = anim_bank_delete(doc->anim, cel_id);
  ie_doc_commit_op(doc, ok);
  return ok;
}

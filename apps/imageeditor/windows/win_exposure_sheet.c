#include "imageeditor.h"
#include "anim/animation_db.h"
#include <errno.h>

typedef struct { canvas_doc_t *doc; bool refreshing; } exposure_view_state_t;

static bool exposure_number(window_t *win, uint32_t id, int max, int *value) {
  window_t *edit = get_window_item(win, id);
  if (!edit || !*edit->title) return false;
  char *end; errno = 0;
  long parsed = strtol(edit->title, &end, 10);
  if (errno || *end || parsed < 1 || parsed > max) return false;
  *value = parsed;
  return true;
}

static void exposure_sheet_sync_fields(window_t *win, canvas_doc_t *doc) {
  anim_exposure_t *row = &doc->anim->frames[doc->anim->active_frame];
  set_window_item_text(win, ID_EXPOSURE_SHEET_CEL, "%u", row->cel->id);
  set_window_item_text(win, ID_EXPOSURE_SHEET_DURATION, "%d", row->delay_ms);
  set_window_item_text(win, ID_EXPOSURE_SHEET_NOTE, "%s", row->note);
  set_window_item_text(win, ID_EXPOSURE_SHEET_NAME, "%s", row->cel->name);
}

static void exposure_sheet_refresh(window_t *win, canvas_doc_t *doc) {
  exposure_view_state_t *st = win->userdata;
  st->refreshing = true;
  anim_timeline_t *tl = doc->anim;
  uint64_t duration = 0;
  for (int i = 0; i < tl->frame_count; i++) duration += tl->frames[i].delay_ms;
  set_window_item_text(win, ID_EXPOSURE_SHEET_SUMMARY, "%d drawing(s) · %d exposure(s) · %d FPS · %.3f seconds",
    tl->cel_count, tl->frame_count, tl->fps, duration / 1000.0);
  window_t *bank = get_window_item(win, ID_EXPOSURE_SHEET_BANK), *sheet = get_window_item(win, ID_EXPOSURE_SHEET_SHEET);
  send_message(bank, tvRefresh, 0, NULL); send_message(sheet, tvRefresh, 0, NULL);
  send_message(sheet, RVM_SETSELECTION, tl->active_frame, NULL);
  for (int i = 0; i < tl->cel_count; i++) if (tl->bank[i] == tl->frames[tl->active_frame].cel)
    send_message(bank, RVM_SETSELECTION, i, NULL);
  exposure_sheet_sync_fields(win, doc);
  enable_window(get_window_item(win, ID_EXPOSURE_SHEET_UNIQUE), anim_cel_uses(tl, tl->frames[tl->active_frame].cel->id) > 1);
  enable_window(get_window_item(win, ID_EXPOSURE_SHEET_REMOVE), tl->frame_count > 1);
  enable_window(get_window_item(win, ID_EXPOSURE_SHEET_UP), tl->active_frame > 0);
  enable_window(get_window_item(win, ID_EXPOSURE_SHEET_DOWN), tl->active_frame + 1 < tl->frame_count);
  enable_window(get_window_item(win, ID_EXPOSURE_SHEET_UNDO), doc->undo.count > 0);
  enable_window(get_window_item(win, ID_EXPOSURE_SHEET_REDO), doc->redo.count > 0);
  enable_window(get_window_item(win, ID_EXPOSURE_SHEET_DELETE_CEL), false);
  st->refreshing = false;
}

static result_t exposure_sheet_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  exposure_view_state_t *st = win->userdata;
  canvas_doc_t *doc = st ? st->doc : NULL;
  switch (msg) {
    case evCreate:
      st = allocate_window_data(win, sizeof(*st));
      if (!st) return false;
      st->doc = doc = lparam;
      exposure_sheet_refresh(win, doc);
      return true;
    case evPaint: return false;
    case evDestroy: return true;
    case evCommand: {
      window_t *src = lparam;
      uint32_t notification = HIWORD(wparam);
      if (!doc || !src) return false;
      if (notification == RVN_SELCHANGE) {
        if (st->refreshing) return true;
        if (src->id == ID_EXPOSURE_SHEET_SHEET) {
          if (cmd_frame_select(doc, LOWORD(wparam))) exposure_sheet_refresh(win, doc);
        } else if (src->id == ID_EXPOSURE_SHEET_BANK) {
          const animation_row_t *row = (void *)send_message(src, tvGetSelectedRecord, 0, NULL);
          if (row) {
            enable_window(get_window_item(win, ID_EXPOSURE_SHEET_DELETE_CEL), row->uses == 0);
            set_window_item_text(win, ID_EXPOSURE_SHEET_CEL, "%d", row->cel);
            set_window_item_text(win, ID_EXPOSURE_SHEET_NAME, "%s", row->name);
            set_window_item_text(win, ID_EXPOSURE_SHEET_STATUS, "%d exposure(s) use this drawing. Apply Row replaces the current row; Insert Cel adds a reference.", row->uses);
          }
        }
        return true;
      }
      if (notification != btnClicked) return false;
      if (src->id == ID_EXPOSURE_SHEET_CLOSE) { end_dialog(win, 1); return true; }
      int cel = 0, delay = 0, index = doc->anim->active_frame;
      bool valid_cel = exposure_number(win, ID_EXPOSURE_SHEET_CEL, INT32_MAX, &cel);
      bool ok = true;
      switch (src->id) {
        case ID_EXPOSURE_SHEET_APPLY:
          ok = valid_cel && exposure_number(win, ID_EXPOSURE_SHEET_DURATION, 65535, &delay) &&
            cmd_exposure_edit(doc, cel, delay, get_window_item(win, ID_EXPOSURE_SHEET_NOTE)->title);
          break;
        case ID_EXPOSURE_SHEET_INSERT: ok = valid_cel && cmd_frame_reuse(doc, cel); break;
        case ID_EXPOSURE_SHEET_UNIQUE: ok = cmd_frame_unique(doc); break;
        case ID_EXPOSURE_SHEET_REMOVE:
          ok = doc->anim->frame_count > 1;
          if (ok) cmd_frame_delete(doc);
          break;
        case ID_EXPOSURE_SHEET_UP:
          ok = index > 0;
          if (ok) cmd_frame_move(doc, index, index - 1);
          break;
        case ID_EXPOSURE_SHEET_DOWN:
          ok = index + 1 < doc->anim->frame_count;
          if (ok) cmd_frame_move(doc, index, index + 1);
          break;
        case ID_EXPOSURE_SHEET_NEW: cmd_frame_add(doc, false); break;
        case ID_EXPOSURE_SHEET_RENAME:
          ok = valid_cel && cmd_cel_rename(doc, cel, get_window_item(win, ID_EXPOSURE_SHEET_NAME)->title);
          break;
        case ID_EXPOSURE_SHEET_DELETE_CEL:
          ok = valid_cel && cmd_cel_delete(doc, cel);
          break;
        case ID_EXPOSURE_SHEET_UNDO: cmd_undo(doc); break;
        case ID_EXPOSURE_SHEET_REDO: cmd_redo(doc); break;
        default: return false;
      }
      if (ok) exposure_sheet_refresh(win, doc);
      set_window_item_text(win, ID_EXPOSURE_SHEET_STATUS, "%s", ok ? "Changes applied. Editing a shared cel updates every exposure that uses it." :
        "Use an existing cel and 1–65535 ms. Only unused cels can be deleted.");
      return true;
    }
    default: return false;
  }
}

void show_exposure_sheet(window_t *parent, canvas_doc_t *doc) {
  if (!IMAGEEDITOR_BW || !doc || !doc->anim || doc->command.before) return;
  anim_stop_playback(doc);
  animation_db_state_t binding = {.doc = doc};
  database_t *db = animation_db_bind(&binding);
  if (!db) return;
  show_dialog_from_form_ex(&imageeditor_exposure_sheet_form, "Cel Bank & Exposure Sheet", parent,
    WINDOW_DIALOG | WINDOW_NOTRAYBUTTON, exposure_sheet_proc, doc);
  animation_db_unbind(db);
}

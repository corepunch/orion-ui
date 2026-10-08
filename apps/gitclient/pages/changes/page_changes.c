// Changes page — staged/unstaged file list, inline commit panel, diff view.

#include "page_changes.h"
#include "../../gc_actions.h"
#include <ctype.h>

void page_changes_update_commit(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->main_win) return;
  int count = 0, staged = 0, conflicts = 0;
  result_node_t *rows = gc->changes_db ? (result_node_t *)send_db_message(
    gc->changes_db, dbFetch, MAKEDWORD(ID_DB_FILES, 0), NULL) : NULL;
  for (result_node_t *n = rows; n; n = n->next) {
    const db_file_t *file = *(db_file_t **)n->data;
    count++; staged += file->staged; conflicts += file->status[0] == 'U';
  }
  free_result_list(rows);
  window_t *all = get_window_item(gc->main_win, ID_CHANGES_PAGE_STAGE_ALL);
  window_t *commit = get_window_item(gc->main_win, ID_CHANGES_PAGE_COMMIT_NOW);
  window_t *summary = get_window_item(gc->main_win, ID_CHANGES_PAGE_COMMIT_SUMMARY);
  char text[512] = {0};
  if (summary) send_message(summary, edGetText, sizeof(text), text);
  bool have_summary = false;
  for (const unsigned char *p = (const unsigned char *)text; *p; p++)
    if (!isspace(*p)) { have_summary = true; break; }
  if (all) {
    set_window_item_text(gc->main_win, ID_CHANGES_PAGE_STAGE_ALL, "%d changed file%s", count, count == 1 ? "" : "s");
    send_message(all, btnSetCheck, count && staged == count ? btnStateChecked : btnStateUnchecked, NULL);
    enable_window(all, gc->repo && count > 0);
    invalidate_window(all);
  }
  if (commit) {
    const char *tip = !gc->repo ? "Open a repository to commit" : conflicts ? "Resolve conflicts before committing" :
                      !staged ? "Select one or more files to commit" : !have_summary ? "Enter a commit summary" : "Commit staged changes";
    send_message(commit, btnSetTooltip, 0, (void *)tip);
    enable_window(commit, gc->repo && staged > 0 && !conflicts && have_summary);
  }
}

// ──────────────────────────────────────────────────────────────────────────────
// Window proc — captures outlets on evCreate, otherwise transparent to parent.
// ──────────────────────────────────────────────────────────────────────────────

result_t page_changes_proc(window_t *win, uint32_t msg,
                            uint32_t wparam, void *lparam) {
  (void)wparam; (void)lparam;
  if (msg != evCreate) return false;

  gc_state_t *gc = g_gc;
  if (!gc) return false;

  gc->changes_files_win = get_window_item(win, ID_CHANGES_PAGE_CHANGES_FILES);
  gc->changes_diff_win  = get_window_item(win, ID_CHANGES_PAGE_CHANGES_DIFF);

  // Rebind the files tableview to changes_db (the form default is history_db).
  if (gc->changes_files_win && gc->changes_db)
    send_message(gc->changes_files_win, evSetDatabase, 0, gc->changes_db);

  page_changes_update_commit();

  return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Event delegation — called from gc_main_proc when tab 0 is active.
// ──────────────────────────────────────────────────────────────────────────────

bool page_changes_handle(window_t *main_win, uint32_t msg,
                          uint32_t wparam, void *lparam) {
  if (msg != evCommand) return false;
  gc_state_t *gc = g_gc;
  if (!gc) return false;

  uint16_t code = (uint16_t)HIWORD(wparam);
  uint16_t id   = (uint16_t)LOWORD(wparam);
  window_t *src = (window_t *)lparam;

  if ((code == ednChange || code == edUpdate) && id == ID_CHANGES_PAGE_COMMIT_SUMMARY) {
    page_changes_update_commit();
    return true;
  }

  if (code == RVN_SELCHANGE) {
    if (src != gc->changes_files_win) return false;
    int sel = (int)(int16_t)LOWORD(wparam);
    if (sel != gc->selected_file) {
      gc->selected_commit = -1;
      gc->files_win = gc->changes_files_win;
      gc->diff_win  = gc->changes_diff_win;
      gc->selected_file = sel;
      gc_diff_refresh();
    }
    return true;
  }

  if (code == RVN_ITEMCHECK) {
    if (src != gc->changes_files_win) return false;
    int row = (int)(int16_t)LOWORD(wparam);
    db_file_t *file = (db_file_t *)(intptr_t)send_message(
      gc->changes_files_win, tvGetRecord, (uint32_t)row, NULL);
    bool checked = ReportView_GetCheckState(gc->changes_files_win, row);

    if (file) {
      bool ok = checked ? gc_stage_file(file->path) : gc_unstage_file(file->path);
      if (!ok) message_box(main_win, "Operation failed.", "File", MB_OK);
      gc_refresh_all();
    }
    return true;
  }

  if (code == RVN_DBLCLK) {
    if (src != gc->changes_files_win) return false;
    int idx = (int)(int16_t)LOWORD(wparam);
    if (gc->selected_commit < 0 && gc->repo && idx >= 0) {
      result_node_t *files = (result_node_t *)send_db_message(
        gc->changes_db, dbFetch, MAKEDWORD(ID_DB_FILES, 0), (void *)(intptr_t)0);
      result_node_t *node = files;
      for (int i = 0; i < idx && node; i++) node = node->next;
      if (node) {
        db_file_t *f = *(db_file_t **)node->data;
        if (f->staged) gc_unstage_file(f->path);
        else           gc_stage_file(f->path);
      }
      free_result_list(files);
      gc_refresh_all();
    }
    return true;
  }

  if (code == GC_DIFF_TOGGLE_UNIFIED) {
    if (gc->diff_win) {
      gc_diff_state_t *st = (gc_diff_state_t *)gc->diff_win->userdata;
      if (st) { gc->unified_diff = st->unified_mode; gc_diff_refresh(); }
    }
    return true;
  }

  if (code == GC_DIFF_STAGE_HUNK) {
    int hunk_idx = (int)(int16_t)LOWORD(wparam);
    if (gc->diff_win) {
      gc_diff_state_t *st = (gc_diff_state_t *)gc->diff_win->userdata;
      if (st && st->hunk_path[0]) {
        gc_stage_hunk(st->hunk_path, hunk_idx);
        gc_refresh_all();
      }
    }
    return true;
  }

  if (code == btnClicked || code == 0) {
    if (id == ID_CHANGES_PAGE_STAGE_ALL) {
      bool checked = send_message(src, btnGetCheck, 0, NULL) == btnStateChecked;
      if (!(checked ? gc_stage_all() : gc_unstage_all()))
        message_box(main_win, "Could not update staged files.", "Stage Files", MB_OK);
      gc_refresh_all();
      return true;
    }
    if (id == ID_CHANGES_PAGE_COMMIT_NOW) {
      page_changes_update_commit();
      window_t *commit = get_window_item(main_win, ID_CHANGES_PAGE_COMMIT_NOW);
      if (!commit || window_has_state(commit, WINDOW_STATE_DISABLED)) return true;
      char summary[512] = {0}, desc[2048] = {0}, message[2600] = {0};
      window_t *sw = get_window_item(main_win, ID_CHANGES_PAGE_COMMIT_SUMMARY);
      window_t *dw = get_window_item(main_win, ID_CHANGES_PAGE_COMMIT_DESCRIPTION);
      if (sw) send_message(sw, edGetText, sizeof(summary), summary);
      if (dw) send_message(dw, edGetText, sizeof(desc),    desc);
      if (!summary[0]) {
        message_box(main_win, "Enter a commit summary.", "Commit", MB_OK);
        return true;
      }
      char identity_name[128], identity_email[256];
      if (!git_get_identity(gc->repo, identity_name, sizeof(identity_name),
                            identity_email, sizeof(identity_email))) {
        gc_show_identity_dialog(main_win);
        return true;
      }
      snprintf(message, sizeof(message), "%s%s%s",
               summary, desc[0] ? "\n\n" : "", desc);
      if (gc_commit(message, false)) {
        if (sw) send_message(sw, edSetText, 0, "");
        if (dw) send_message(dw, edSetText, 0, "");
        gc_refresh_all();
      } else {
        message_box(main_win,
                    "Commit failed. Check staged files and Git identity.",
                    "Commit", MB_OK);
      }
      return true;
    }

    if (id == ID_CHANGES_PAGE_STASH_INLINE) {
      gc_stash();
      gc_refresh_all();
      return true;
    }
  }

  return false;
}

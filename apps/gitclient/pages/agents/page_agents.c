#include "apps/gitclient/gc_agents.h"
#include <ctype.h>

typedef struct {
  int diff_task;
  bool diff_loaded, diff_truncated;
} gc_agents_view_t;

static const char *agent_phase_name(const gc_agent_task_t *task) {
  switch (task->phase) {
    case GC_AGENT_PREPARING:    return ui_tr("Creating worktree");
    case GC_AGENT_RUNNING:      return ui_tr("Working");
    case GC_AGENT_STOPPING:     return ui_tr("Stopping");
    case GC_AGENT_STOPPED:      return ui_tr("Stopped");
    case GC_AGENT_SNAPSHOTTING: return ui_tr("Saving revision");
    case GC_AGENT_READY:        return ui_tr(task->work ? "Preparing launch" : "Ready for review");
    case GC_AGENT_NO_CHANGES:   return ui_tr("No changes");
    case GC_AGENT_FAILED:       return ui_tr("Failed");
  }
  return "";
}

database_t *gc_agents_db_create(void) {
  DB_CLASS(db_schema_xml);
  database_t *db = create_database_with_schema("agent_queue", "db_schema_xml", &gitclient_agent_queue_schema, NULL);
  if (db) register_database("agent_queue", db);
  return db;
}

static window_t *agent_list(void) {
  return g_gc && g_gc->agents_page_win ? get_window_item(g_gc->agents_page_win, ID_AGENTS_PAGE_TASKS) : NULL;
}

static const gc_agent_task_t *agent_selected(void) {
  window_t *list = agent_list();
  if (!list) return NULL;
  const db_agent_task_t *row = (const db_agent_task_t *)send_message(list, tvGetSelectedRecord, 0, NULL);
  if (!row) return NULL;
  for (const gc_agent_task_t *t = gc_agents_tasks(); t; t = t->next) if (t->id == row->id) return t;
  return NULL;
}

static void agent_button(window_t *page, uint16_t id, bool enabled) {
  window_t *button = get_window_item(page, id);
  if (button) enable_window(button, enabled);
}

static void agents_show_selection(void) {
  window_t *win = g_gc ? g_gc->agents_page_win : NULL;
  if (!win) return;
  gc_agents_view_t *view = win->userdata;
  const gc_agent_task_t *task = agent_selected();
  window_t *diff = get_window_item(win, ID_AGENTS_PAGE_REVIEW_DIFF);
  gc_diff_state_t *st = diff ? diff->userdata : NULL;
  int diff_task = task && task->phase == GC_AGENT_READY ? task->id : 0;
  if (st && view->diff_task != diff_task) {
    view->diff_task = diff_task;
    st->diff_buf[0] = 0;
    view->diff_loaded = diff_task && gc_agent_diff(diff_task, st->diff_buf, sizeof(st->diff_buf));
    view->diff_truncated = view->diff_loaded && strlen(st->diff_buf) >= sizeof(st->diff_buf) - 256;
    if (!view->diff_loaded) st->diff_buf[0] = 0;
    gc_diff_set_text(diff);
  }

  const char *heading = ui_tr("Select a task to see its revision.");
  const char *detail = "";
  char revision[256];
  if (task) {
    if (task->phase == GC_AGENT_READY) {
      snprintf(revision, sizeof(revision), ui_tr("Compare %.12s to %.12s"), task->base, task->revision);
      heading = revision;
      if (task->work) detail = ui_tr("Preparing the checkout at this revision...");
      else if (!view->diff_loaded) detail = ui_tr("Could not read the revision diff.");
      else if (view->diff_truncated) detail = ui_tr("This diff exceeds the viewer limit. The preview is truncated.");
    } else {
      snprintf(revision, sizeof(revision), ui_tr("%s from %.12s"), task->branch, task->base);
      heading = revision;
      detail = agent_phase_name(task);
    }
    if (task->error) detail = ui_tr(task->error);
  }
  set_window_item_text(win, ID_AGENTS_PAGE_REVISION, "%s", heading);
  set_window_item_text(win, ID_AGENTS_PAGE_DETAIL, "%s", detail);
  agent_button(win, ID_AGENTS_START_BATCH, g_gc->repo && gc_agents_supported());
  agent_button(win, ID_AGENTS_RUN_REVIEWED, task && view->diff_loaded && !task->work);
  agent_button(win, ID_AGENTS_OUTPUT, gc_agent_has_output(task));
  agent_button(win, ID_AGENTS_STOP, task && (task->phase == GC_AGENT_RUNNING || task->phase == GC_AGENT_STOPPING));
  window_layout_sync(win);
}

void gc_agents_refresh(void) {
  window_t *list = agent_list();
  database_t *db = g_gc ? g_gc->agent_queue_db : NULL;
  if (!list || !db) return;
  bool changed = false;
  for (const gc_agent_task_t *t = gc_agents_tasks(); t; t = t->next) {
    db_agent_task_t row = {.id = t->id};
    const char *title = t->request.prompt;
    while (*title && isspace((unsigned char)*title)) title++;
    size_t len = MIN(strcspn(title, "\r\n"), sizeof(row.task) - 1);
    while (len && ((unsigned char)title[len] & 0xc0) == 0x80) len--;
    snprintf(row.task, sizeof(row.task), "%.*s", (int)len, title);
    snprintf(row.provider, sizeof(row.provider), "%s", gc_agent_provider_name(t->request.provider));
    snprintf(row.status, sizeof(row.status), "%s", agent_phase_name(t));
    snprintf(row.tone, sizeof(row.tone), "%s", t->phase == GC_AGENT_READY ? "ready" :
             t->phase == GC_AGENT_FAILED ? "failed" : t->phase == GC_AGENT_RUNNING ? "running" : "");
    const db_agent_task_t *old = (const db_agent_task_t *)send_db_message(db, dbFind, TABLE_AGENT_TASKS, (void *)(intptr_t)row.id);
    if (!old) changed |= send_db_message(db, dbInsert, TABLE_AGENT_TASKS, &row) != 0;
    else if (strcmp(old->status, row.status) || strcmp(old->tone, row.tone))
      changed |= send_db_message(db, dbUpdate, TABLE_AGENT_TASKS, &row) != 0;
  }
  if (changed) send_message(list, tvRefresh, 0, NULL);
  agents_show_selection();
}

result_t gc_agents_page_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)wparam; (void)lparam;
  switch (msg) {
    case evCreate:
      if (!allocate_window_data(win, sizeof(gc_agents_view_t))) return false;
      g_gc->agents_page_win = win;
      gc_agents_refresh();
      return true;
    case evDestroy:
      if (g_gc->agents_page_win == win) g_gc->agents_page_win = NULL;
      free(win->userdata);
      win->userdata = NULL;
      return true;
    case evPaint: return false;
    default: return false;
  }
}

bool gc_agents_page_command(uint32_t wparam, void *lparam) {
  if (!g_gc || !g_gc->agents_page_win || !lparam || g_gc->tab != GC_TAB_AGENTS) return false;
  uint16_t code = HIWORD(wparam), id = LOWORD(wparam);
  if (lparam == agent_list() && (code == RVN_SELCHANGE || code == RVN_DBLCLK)) {
    agents_show_selection();
    return true;
  }
  if (code != btnClicked || (id != ID_AGENTS_START_BATCH && id != ID_AGENTS_OUTPUT &&
                             id != ID_AGENTS_STOP && id != ID_AGENTS_RUN_REVIEWED)) return false;
  gc_agents_action(id);
  return true;
}

static const uint16_t agent_prompt_ids[] = {
  ID_AGENT_BATCH_DIALOG_PROMPT_ONE, ID_AGENT_BATCH_DIALOG_PROMPT_TWO, ID_AGENT_BATCH_DIALOG_PROMPT_THREE
};
static const uint16_t agent_provider_ids[] = {
  ID_AGENT_BATCH_DIALOG_PROVIDER_ONE, ID_AGENT_BATCH_DIALOG_PROVIDER_TWO, ID_AGENT_BATCH_DIALOG_PROVIDER_THREE
};

static result_t agent_batch_dialog_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      set_window_item_text(win, ID_AGENT_BATCH_DIALOG_REPOSITORY, "%s", git_repo_path(g_gc->repo));
      for (int i = 0; i < GC_AGENT_BATCH_SIZE; i++) {
        window_t *combo = get_window_item(win, agent_provider_ids[i]);
        send_message(combo, cbAddString, 0, (void *)gc_agent_provider_name(GC_AGENT_CODEX));
        send_message(combo, cbAddString, 0, (void *)gc_agent_provider_name(GC_AGENT_CLAUDE));
        send_message(combo, cbSetCurrentSelection, GC_AGENT_CODEX, NULL);
      }
      return true;
    case evCommand: {
      if (HIWORD(wparam) != btnClicked || !lparam) return false;
      if (LOWORD(wparam) == ID_AGENT_BATCH_DIALOG_CANCEL) { end_dialog(win, 0); return true; }
      if (LOWORD(wparam) != ID_AGENT_BATCH_DIALOG_START) return false;
      gc_agent_request_t requests[GC_AGENT_BATCH_SIZE] = {0};
      for (int i = 0; i < GC_AGENT_BATCH_SIZE; i++) {
        window_t *input = get_window_item(win, agent_prompt_ids[i]);
        char *prompt = requests[i].prompt;
        result_t length = send_message(input, edGetText, 0, NULL);
        send_message(input, edGetText, sizeof(requests[i].prompt), prompt);
        const char *content = prompt;
        while (*content && isspace((unsigned char)*content)) content++;
        if (!*content || length >= GC_AGENT_PROMPT_SIZE) {
          message_box(win, ui_tr("Enter a task of up to 2047 bytes in each prompt."), ui_tr("Start Three Agents"), MB_OK);
          set_focus(input);
          return true;
        }
        window_t *combo = get_window_item(win, agent_provider_ids[i]);
        requests[i].provider = send_message(combo, cbGetCurrentSelection, 0, NULL) == GC_AGENT_CLAUDE
          ? GC_AGENT_CLAUDE : GC_AGENT_CODEX;
      }
      if (!gc_agents_start(git_repo_path(g_gc->repo), axSettingsDirectory(), requests)) {
        message_box(win, ui_tr("Could not start the batch. Check that the repository has a commit and the worktree directory is writable."),
                    ui_tr("Start Three Agents"), MB_OK);
        return true;
      }
      end_dialog(win, 1);
      return true;
    }
    case evDestroy: return true;
    case evPaint: return false;
    default: return false;
  }
}

void gc_agents_new_batch(void) {
  if (!gc_agents_supported() || !g_gc->repo) {
    const char *error = gc_agents_supported() ? ui_tr("Open a repository first.") : ui_tr("Agent runs require a platform with terminal support.");
    message_box(g_gc->main_win, error, ui_tr("Start Three Agents"), MB_OK);
    return;
  }
  show_dialog_from_form(&gitclient_agent_batch_dialog_form, NULL, g_gc->main_win, agent_batch_dialog_proc, NULL);
}

void gc_agents_action(uint16_t id) {
  if (g_gc->tab != GC_TAB_AGENTS) gc_set_view_mode(GC_TAB_AGENTS);
  if (id == ID_AGENTS_START_BATCH) { gc_agents_new_batch(); return; }
  const gc_agent_task_t *task = agent_selected();
  window_t *page = g_gc->agents_page_win;
  gc_agents_view_t *view = page ? page->userdata : NULL;
  bool ok = task && ((id == ID_AGENTS_OUTPUT && gc_agent_output(task->id)) ||
                    (id == ID_AGENTS_STOP && gc_agent_stop(task->id)) ||
                    (id == ID_AGENTS_RUN_REVIEWED && view && view->diff_loaded && gc_agent_launch(task->id)));
  if (!ok) message_box(g_gc->main_win, ui_tr("This action is unavailable for the selected task."), ui_tr("Agents"), MB_OK);
}

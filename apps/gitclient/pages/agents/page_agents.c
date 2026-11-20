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

// A task's state as the list's RowIcon rules and the review badge show it: one tone, icon and colour.
typedef struct { const char *tone, *icon; sys_color_idx_t color; } agent_tone_t;

static agent_tone_t agent_tone(const gc_agent_task_t *task) {
  switch (task->phase) {
    case GC_AGENT_READY:        if (!task->work) return (agent_tone_t){"ready", "gc-check-circle", brTextSuccess};
                                return (agent_tone_t){"waiting", "gc-hourglass", brTextSecondary};
    case GC_AGENT_FAILED:       return (agent_tone_t){"failed", "gc-x-circle", brTextError};
    case GC_AGENT_RUNNING:      return (agent_tone_t){"running", "gc-play", brTextInfo};
    case GC_AGENT_PREPARING:
    case GC_AGENT_STOPPING:
    case GC_AGENT_SNAPSHOTTING: return (agent_tone_t){"waiting", "gc-hourglass", brTextSecondary};
    case GC_AGENT_STOPPED:
    case GC_AGENT_NO_CHANGES:   break;
  }
  return (agent_tone_t){"", "gc-minus-circle", brTextSecondary};
}

// The first line of the prompt, cut on a UTF-8 boundary: the task's title.
static void agent_title(const gc_agent_task_t *task, char *out, size_t cap) {
  const char *title = task->request.prompt;
  while (*title && isspace((unsigned char)*title)) title++;
  size_t len = MIN(strcspn(title, "\r\n"), cap - 1);
  while (len && ((unsigned char)title[len] & 0xc0) == 0x80) len--;
  snprintf(out, cap, "%.*s", (int)len, title);
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
    gc_diff_set_text(diff, st->diff_buf);
  }

  char title[256] = "", source[384] = "";
  const char *detail = "";
  window_t *badge = get_window_item(win, ID_AGENTS_PAGE_STATE);
  if (task) {
    const char *agent = gc_agent_provider_name(task->request.provider);
    agent_title(task, title, sizeof(title));
    if (task->phase == GC_AGENT_READY) {
      snprintf(source, sizeof(source), ui_tr("%s on %s: %.12s compared to %.12s"), agent, task->branch, task->revision, task->base);
      if (task->work) detail = ui_tr("Preparing the checkout at this revision...");
      else if (!view->diff_loaded) detail = ui_tr("Could not read the revision diff.");
      else if (view->diff_truncated) detail = ui_tr("This diff exceeds the viewer limit. The preview is truncated.");
    } else {
      snprintf(source, sizeof(source), ui_tr("%s on %s, from %.12s"), agent, task->branch, task->base);
    }
    if (task->error) detail = ui_tr(task->error);
    agent_tone_t tone = agent_tone(task);
    if (badge) {
      set_window_title(badge, agent_phase_name(task));
      send_message(badge, btnSetIconName, 0, (void *)tone.icon);
      send_message(badge, bdSetColor, tone.color, NULL);
    }
  } else {
    snprintf(title, sizeof(title), "%s", ui_tr("No task selected"));
    snprintf(source, sizeof(source), "%s", ui_tr("Select a task to review the revision its agent saved."));
  }
  if (badge) show_window(badge, task != NULL);
  bool ended = task && (task->phase == GC_AGENT_FAILED || task->phase == GC_AGENT_STOPPED || task->phase == GC_AGENT_NO_CHANGES);
  if (diff) send_message(diff, edSetPlaceholder, 0, (void *)(!task ? ui_tr("Select a task to see the changes its agent made.")
                                                          : ended ? ui_tr("This task saved no revision to review.")
                                                                  : ui_tr("The diff appears here when the task is ready for review.")));
  set_window_item_text(win, ID_AGENTS_PAGE_REVISION, "%s", title);
  set_window_item_text(win, ID_AGENTS_PAGE_SOURCE, "%s", source);
  set_window_item_text(win, ID_AGENTS_PAGE_DETAIL, "%s", detail);
  agent_button(win, ID_AGENTS_START_BATCH, g_gc->repo && gc_agents_supported());
  agent_button(win, ID_AGENTS_RUN_REVIEWED, task && view->diff_loaded && !task->work);
  agent_button(win, ID_AGENTS_OUTPUT, gc_agent_has_output(task));
  agent_button(win, ID_AGENTS_STOP, task && (task->phase == GC_AGENT_RUNNING || task->phase == GC_AGENT_STOPPING));
  window_layout_sync(win);
}

// The list reads like an inbox: what needs a decision first, then what is still working, then what ended.
enum { AGENT_SECTION_ATTENTION, AGENT_SECTION_ACTIVE, AGENT_SECTION_DONE, AGENT_SECTION_COUNT };

static int agent_section(const gc_agent_task_t *task) {
  switch (task->phase) {
    case GC_AGENT_READY:      return task->work ? AGENT_SECTION_ACTIVE : AGENT_SECTION_ATTENTION;
    case GC_AGENT_FAILED:     return AGENT_SECTION_ATTENTION;
    case GC_AGENT_STOPPED:
    case GC_AGENT_NO_CHANGES: return AGENT_SECTION_DONE;
    default:                  return AGENT_SECTION_ACTIVE;
  }
}

static const char *agent_section_name(int section) {
  switch (section) {
    case AGENT_SECTION_ATTENTION: return ui_tr("Needs your attention");
    case AGENT_SECTION_ACTIVE:    return ui_tr("In progress");
    default:                      return ui_tr("Done");
  }
}

static void agent_row(const gc_agent_task_t *t, db_agent_task_t *row) {
  memset(row, 0, sizeof(*row));
  row->id = t->id;
  agent_title(t, row->task, sizeof(row->task));
  snprintf(row->provider, sizeof(row->provider), "%s", gc_agent_provider_name(t->request.provider));
  snprintf(row->status, sizeof(row->status), "%s", agent_phase_name(t));
  snprintf(row->tone, sizeof(row->tone), "%s", agent_tone(t).tone);
  snprintf(row->section, sizeof(row->section), "%s", agent_section_name(agent_section(t)));
}

static bool agent_row_equal(const db_agent_task_t *a, const db_agent_task_t *b) {
  return a->id == b->id && !strcmp(a->task, b->task) && !strcmp(a->provider, b->provider) &&
         !strcmp(a->status, b->status) && !strcmp(a->tone, b->tone) && !strcmp(a->section, b->section);
}

static void agents_show_summary(const int counts[AGENT_SECTION_COUNT]) {
  window_t *win = g_gc->agents_page_win;
  if (!counts[AGENT_SECTION_ATTENTION] && !counts[AGENT_SECTION_ACTIVE] && !counts[AGENT_SECTION_DONE])
    set_window_item_text(win, ID_AGENTS_PAGE_SUMMARY, "%s", "");
  else
    set_window_item_text(win, ID_AGENTS_PAGE_SUMMARY, ui_tr("%d in progress, %d to review, %d done"),
                         counts[AGENT_SECTION_ACTIVE], counts[AGENT_SECTION_ATTENTION], counts[AGENT_SECTION_DONE]);
}

void gc_agents_refresh(void) {
  window_t *list = agent_list();
  database_t *db = g_gc ? g_gc->agent_queue_db : NULL;
  if (!list || !db) return;
  // The rows the list should show, section by section, against the rows it shows now.
  int counts[AGENT_SECTION_COUNT] = {0};
  bool changed = false;
  result_node_t *rows = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(TABLE_AGENT_TASKS, 0), NULL);
  result_node_t *shown = rows;
  for (int section = 0; section < AGENT_SECTION_COUNT; section++)
    for (const gc_agent_task_t *t = gc_agents_tasks(); t; t = t->next) {
      if (agent_section(t) != section) continue;
      counts[section]++;
      db_agent_task_t want;
      agent_row(t, &want);
      if (!shown || !agent_row_equal(*(const db_agent_task_t **)shown->data, &want)) changed = true;
      if (shown) shown = shown->next;
    }
  changed |= shown != NULL;
  free_result_list(rows);
  agents_show_summary(counts);
  if (!changed) { agents_show_selection(); return; }

  const db_agent_task_t *selected = (const db_agent_task_t *)send_message(list, tvGetSelectedRecord, 0, NULL);
  int selected_id = selected ? selected->id : 0;
  rows = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(TABLE_AGENT_TASKS, 0), NULL);
  for (result_node_t *r = rows; r; r = r->next)
    send_db_message(db, dbDelete, TABLE_AGENT_TASKS, (void *)(intptr_t)(*(const db_agent_task_t **)r->data)->id);
  free_result_list(rows);
  for (int section = 0; section < AGENT_SECTION_COUNT; section++)
    for (const gc_agent_task_t *t = gc_agents_tasks(); t; t = t->next) {
      if (agent_section(t) != section) continue;
      db_agent_task_t row;
      agent_row(t, &row);
      if (!send_db_message(db, dbInsert, TABLE_AGENT_TASKS, &row)) {
        fprintf(stderr, "[gitclient] agent row insert failed task=%d\n", t->id);
        fflush(stderr);
      }
    }
  send_message(list, tvRefresh, 0, NULL);
  for (int i = 0; selected_id; i++) {   // the selection follows its task to the task's new place
    const db_agent_task_t *row = (const db_agent_task_t *)send_message(list, tvGetRecord, (uint32_t)i, NULL);
    if (!row) break;
    if (row->id == selected_id) { send_message(list, RVM_SETSELECTION, (uint32_t)i, NULL); break; }
  }
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

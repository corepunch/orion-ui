// CONTROLLER: Application state and business logic.

#include "taskmanager.h"

// ============================================================
// Global app state pointer
// ============================================================

app_state_t *g_app = NULL;

// ============================================================
// app_init
// ============================================================

app_state_t *app_init(void) {
  app_state_t *app = (app_state_t *)calloc(1, sizeof(app_state_t));
  if (!app) return NULL;
  app->next_x = DOC_START_X;
  app->next_y = DOC_START_Y;
  return app;
}

task_doc_t *doc_state_init(void) {
  task_doc_t *doc = (task_doc_t *)calloc(1, sizeof(task_doc_t));
  if (!doc) return NULL;

  doc->task_capacity = TASK_CAPACITY_INIT;
  doc->tasks = (task_t **)calloc((size_t)doc->task_capacity, sizeof(task_t *));
  if (!doc->tasks) {
    free(doc);
    return NULL;
  }

  doc->next_id = 1;
  doc->selected_idx = -1;
  return doc;
}

void doc_state_shutdown(task_doc_t *doc) {
  if (!doc) return;
  for (int i = 0; i < doc->task_count; i++)
    task_free(doc->tasks[i]);
  free(doc->tasks);
  free(doc);
}

// ============================================================
// app_shutdown — free all tasks and the state struct
// ============================================================

void app_shutdown(app_state_t *app) {
  if (!app) return;
  while (app->docs)
    close_document(app->docs);
  if (app->accel)
    free_accelerators(app->accel);
  free(app);
}

// ============================================================
// app_add_task — append a task, grow the array if needed
// ============================================================

bool app_add_task(task_doc_t *doc, task_t *task) {
  if (!doc || !task) return false;
  if (doc->task_count >= doc->task_capacity) {
    int new_cap = doc->task_capacity * 2;
    task_t **newbuf = (task_t **)realloc(doc->tasks,
                                          (size_t)new_cap * sizeof(task_t *));
    if (!newbuf) return false;
    doc->tasks         = newbuf;
    doc->task_capacity = new_cap;
  }
  task->id = doc->next_id++;
  doc->tasks[doc->task_count++] = task;
  doc->modified = true;
  return true;
}

// ============================================================
// app_delete_task — remove task at index and free it
// ============================================================

bool app_delete_task(task_doc_t *doc, int index) {
  if (!doc || index < 0 || index >= doc->task_count) return false;
  task_free(doc->tasks[index]);
  for (int i = index; i < doc->task_count - 1; i++)
    doc->tasks[i] = doc->tasks[i + 1];
  doc->task_count--;
  doc->modified = true;
  if (doc->selected_idx >= doc->task_count)
    doc->selected_idx = doc->task_count - 1;
  return true;
}

// ============================================================
// app_get_task — bounds-checked accessor
// ============================================================

task_t *app_get_task(task_doc_t *doc, int index) {
  if (!doc || index < 0 || index >= doc->task_count) return NULL;
  return doc->tasks[index];
}

// ============================================================
// app_update_status — refresh status bar with task count
// ============================================================

void app_update_status(task_doc_t *doc) {
  if (!doc || !doc->win) return;
  char buf[64];
  snprintf(buf, sizeof(buf), "%d task%s",
           doc->task_count, doc->task_count == 1 ? "" : "s");
  send_message(doc->win, evStatusBar, 0, buf);
}

task_doc_t *doc_from_window(window_t *win) {
  window_t *root;
  if (!win) return NULL;
  root = get_root_window(win);
  return root ? (task_doc_t *)root->userdata : NULL;
}

task_doc_t *app_find_document_by_path(const char *path) {
  task_doc_t *doc;

  if (!g_app || !path || path[0] == '\0') return NULL;

  for (doc = g_app->docs; doc; doc = doc->next) {
    if (doc->filename[0] != '\0' && strcmp(doc->filename, path) == 0)
      return doc;
  }

  return NULL;
}

bool app_close_all_documents(window_t *parent_win) {
  while (g_app && g_app->docs) {
    if (!doc_confirm_close(g_app->docs, parent_win))
      return false;
  }
  return true;
}

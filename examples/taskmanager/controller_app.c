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

  app->task_capacity = TASK_CAPACITY_INIT;
  app->tasks = (task_t **)calloc((size_t)app->task_capacity, sizeof(task_t *));
  if (!app->tasks) {
    free(app);
    return NULL;
  }

  app->task_count  = 0;
  app->next_id     = 1;
  app->selected_idx = -1;
  app->modified    = false;
  return app;
}

// ============================================================
// app_shutdown — free all tasks and the state struct
// ============================================================

void app_shutdown(app_state_t *app) {
  if (!app) return;
  for (int i = 0; i < app->task_count; i++)
    task_free(app->tasks[i]);
  free(app->tasks);
  if (app->accel)
    free_accelerators(app->accel);
  free(app);
}

// ============================================================
// app_add_task — append a task, grow the array if needed
// ============================================================

bool app_add_task(app_state_t *app, task_t *task) {
  if (!app || !task) return false;
  if (app->task_count >= app->task_capacity) {
    int new_cap = app->task_capacity * 2;
    task_t **newbuf = (task_t **)realloc(app->tasks,
                                          (size_t)new_cap * sizeof(task_t *));
    if (!newbuf) return false;
    app->tasks         = newbuf;
    app->task_capacity = new_cap;
  }
  task->id = app->next_id++;
  app->tasks[app->task_count++] = task;
  app->modified = true;
  return true;
}

// ============================================================
// app_delete_task — remove task at index and free it
// ============================================================

bool app_delete_task(app_state_t *app, int index) {
  if (!app || index < 0 || index >= app->task_count) return false;
  task_free(app->tasks[index]);
  for (int i = index; i < app->task_count - 1; i++)
    app->tasks[i] = app->tasks[i + 1];
  app->task_count--;
  app->modified = true;
  if (app->selected_idx >= app->task_count)
    app->selected_idx = app->task_count - 1;
  return true;
}

// ============================================================
// app_get_task — bounds-checked accessor
// ============================================================

task_t *app_get_task(app_state_t *app, int index) {
  if (!app || index < 0 || index >= app->task_count) return NULL;
  return app->tasks[index];
}

// ============================================================
// app_update_status — refresh status bar with task count
// ============================================================

void app_update_status(app_state_t *app) {
  if (!app || !app->main_win) return;
  char buf[64];
  snprintf(buf, sizeof(buf), "%d task%s",
           app->task_count, app->task_count == 1 ? "" : "s");
  send_message(app->main_win, kWindowMessageStatusBar, 0, buf);
}

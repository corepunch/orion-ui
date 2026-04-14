// MODEL: Task data structures and CRUD operations.

#include "taskmanager.h"

// ============================================================
// Portable string duplicate helper
// ============================================================

static char *task_strdup(const char *s) {
  if (!s) s = "";
  size_t len = strlen(s);
  char *copy = (char *)malloc(len + 1);
  if (copy) memcpy(copy, s, len + 1);
  return copy;
}

// ============================================================
// Priority and status labels
// ============================================================

const char *priority_to_string(task_priority_t p) {
  switch (p) {
    case PRIORITY_LOW:    return "Low";
    case PRIORITY_NORMAL: return "Normal";
    case PRIORITY_HIGH:   return "High";
    case PRIORITY_URGENT: return "Urgent";
    default:              return "?";
  }
}

const char *status_to_string(task_status_t s) {
  switch (s) {
    case STATUS_TODO:       return "Todo";
    case STATUS_INPROGRESS: return "In Progress";
    case STATUS_COMPLETED:  return "Completed";
    case STATUS_CANCELLED:  return "Cancelled";
    default:                return "?";
  }
}

// ============================================================
// task_create — allocate and initialise a new task
// ============================================================

task_t *task_create(const char *title, const char *desc,
                    task_priority_t prio, task_status_t status,
                    uint32_t due) {
  task_t *t = (task_t *)calloc(1, sizeof(task_t));
  if (!t) return NULL;
  t->title       = task_strdup(title);
  t->description = task_strdup(desc);
  if (!t->title || !t->description) {
    free(t->title);
    free(t->description);
    free(t);
    return NULL;
  }
  t->priority     = prio;
  t->status       = status;
  t->created_date = (uint32_t)time(NULL);
  t->due_date     = due;
  return t;
}

// ============================================================
// task_free — release all memory owned by a task
// ============================================================

void task_free(task_t *task) {
  if (!task) return;
  free(task->title);
  free(task->description);
  free(task);
}

// ============================================================
// task_update — modify an existing task in-place
// ============================================================

void task_update(task_t *task, const char *title, const char *desc,
                 task_priority_t prio, task_status_t status,
                 uint32_t due) {
  if (!task) return;

  if (title) {
    char *t = task_strdup(title);
    if (t) { free(task->title); task->title = t; }
  }
  if (desc) {
    char *d = task_strdup(desc);
    if (d) { free(task->description); task->description = d; }
  }
  task->priority = prio;
  task->status   = status;
  task->due_date = due;
}

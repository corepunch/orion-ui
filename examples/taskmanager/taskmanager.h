#ifndef __TASKMANAGER_H__
#define __TASKMANAGER_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "../../ui.h"
#include "../../commctl/columnview.h"
#include "../../commctl/menubar.h"
#include "../../user/accel.h"
#include "../../user/icons.h"

// ============================================================
// Layout constants
// ============================================================

#define SCREEN_W   640
#define SCREEN_H   480

#define MAIN_WIN_X  60
// frame.y is now the window top (title bar top), not the client area top.
#define MAIN_WIN_Y  (MENUBAR_HEIGHT + 8)
#define MAIN_WIN_W  (SCREEN_W - MAIN_WIN_X - 4)
#define MAIN_WIN_H  (SCREEN_H - MAIN_WIN_Y - 4)

// ============================================================
// Menu item IDs
// ============================================================

#define ID_FILE_NEW      1
#define ID_FILE_OPEN     2
#define ID_FILE_SAVE     3
#define ID_FILE_SAVEAS   4
#define ID_FILE_QUIT     5

#define ID_TASK_NEW      10
#define ID_TASK_EDIT     11
#define ID_TASK_DELETE   12

#define ID_VIEW_REFRESH  20

#define ID_HELP_ABOUT    100

// ============================================================
// Task dialog control IDs
// ============================================================

#define ID_TASK_TITLE_CTRL    1001
#define ID_TASK_DESC_CTRL     1002
#define ID_TASK_PRIORITY_CTRL 1003
#define ID_TASK_STATUS_CTRL   1004
#define ID_TASK_DUEDATE_CTRL  1005
#define ID_OK                 1
#define ID_CANCEL             2

// ============================================================
// Task data model
// ============================================================

typedef enum {
  PRIORITY_LOW    = 0,
  PRIORITY_NORMAL = 1,
  PRIORITY_HIGH   = 2,
  PRIORITY_URGENT = 3,
} task_priority_t;

typedef enum {
  STATUS_TODO       = 0,
  STATUS_INPROGRESS = 1,
  STATUS_COMPLETED  = 2,
  STATUS_CANCELLED  = 3,
} task_status_t;

typedef struct {
  int             id;
  char           *title;
  char           *description;
  task_priority_t priority;
  task_status_t   status;
  uint32_t        created_date;
  uint32_t        due_date;
} task_t;

// ============================================================
// Application state (controller)
// ============================================================

#define TASK_CAPACITY_INIT  16

typedef struct {
  task_t       **tasks;
  int            task_count;
  int            task_capacity;
  int            next_id;
  int            selected_idx;
  bool           modified;
  char           filename[512];

  window_t      *main_win;
  window_t      *list_win;
  window_t      *menubar_win;
  hinstance_t    hinstance;  // owning app instance

  accel_table_t *accel;
} app_state_t;

extern app_state_t *g_app;

// ============================================================
// Model functions (model_task.c)
// ============================================================

task_t *task_create(const char *title, const char *desc,
                    task_priority_t prio, task_status_t status,
                    uint32_t due);
void    task_free(task_t *task);
void    task_update(task_t *task, const char *title, const char *desc,
                    task_priority_t prio, task_status_t status,
                    uint32_t due);
const char *priority_to_string(task_priority_t p);
const char *status_to_string(task_status_t s);

// ============================================================
// Controller functions (controller_app.c)
// ============================================================

app_state_t *app_init(void);
void         app_shutdown(app_state_t *app);
bool         app_add_task(app_state_t *app, task_t *task);
bool         app_delete_task(app_state_t *app, int index);
task_t      *app_get_task(app_state_t *app, int index);
void         app_update_status(app_state_t *app);

// ============================================================
// View — menu bar (view_menubar.c)
// ============================================================

extern menu_def_t  kMenus[];
extern const int   kNumMenus;
void handle_menu_command(uint16_t id);
result_t app_menubar_proc(window_t *win, uint32_t msg,
                          uint32_t wparam, void *lparam);
void create_menubar(void);

// ============================================================
// View — main window (view_main.c)
// ============================================================

result_t main_win_proc(window_t *win, uint32_t msg,
                       uint32_t wparam, void *lparam);

// ============================================================
// View — task list (view_tasklist.c)
// ============================================================

// Column widths (status and priority are fixed; title stretches)
#define TASKLIST_STATUS_W    70
#define TASKLIST_PRIORITY_W  60
#define TASKLIST_ROW_H       13
#define TASKLIST_PADDING      4
#define TASKLIST_HEADER_H    13

// Message to add a row (lparam = tasklist_row_t *)
#define TLVM_ADDROW       (kWindowMessageUser + 200)
#define TASKLIST_TITLE_LEN 256

typedef struct {
  char     title[TASKLIST_TITLE_LEN];
  char     priority[32];
  char     status[32];
  uint32_t task_idx;
  uint32_t color;
} tasklist_row_t;

result_t tasklist_proc(window_t *win, uint32_t msg,
                       uint32_t wparam, void *lparam);
void     tasklist_refresh(window_t *list_win);

// ============================================================
// View — task dialog (view_dlg_task.c)
// ============================================================

bool show_task_dialog(window_t *parent, task_t *task);

// ============================================================
// View — about dialog (view_dlg_about.c)
// ============================================================

void show_about_dialog(window_t *parent);

// ============================================================
// File I/O (file_io.c)
// ============================================================

bool task_file_save(const char *path, app_state_t *app);
bool task_file_load(const char *path, app_state_t *app);

#endif // __TASKMANAGER_H__

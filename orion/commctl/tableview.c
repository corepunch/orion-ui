// tableview.c — Database-backed table view control
//
// Automatically populates a reportview from database records using the Zero
// Wrapper Structs API. No manual population code needed.
//
// Creation parameters (lparam):
//   tableview_params_t (see commctl/commctl.h)
//
// Messages:
//   tvRefresh - Refresh from database (wparam=0, lparam=0)
//   tvSetFilter - Change filter (wparam=filter_field, lparam=filter_value)
//
// Example usage:
//   tableview_params_t params = {
//     .db = g_db,
//     .table_id = TABLE_POSTS,
//     .filter_field = 0,
//     .filter_value = 0,
//     .field_names = (const char *[]){"title", "author", "like_count", NULL},
//     .column_titles = (const char *[]){"Title", "Author", "Likes", NULL},
//     .column_widths = (const int[]){0, 80, 50, -1}
//   };
//   window_t *tv = create_window("", WINDOW_NOTITLE | WINDOW_VSCROLL,
//                                MAKERECT(0, 0, w, h), parent,
//                                win_tableview, &params);

#include <orion/ui.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "columnview_internal.h"
#include "menubar.h"

#ifndef TABLEVIEW_DEBUG
#define TABLEVIEW_DEBUG 0
#endif

#if TABLEVIEW_DEBUG
#define TV_LOG(...) do {                                                      \
  fprintf(stderr, "[tableview] " __VA_ARGS__);                               \
  fputc('\n', stderr);                                                        \
  axLog("[tableview] " __VA_ARGS__);                                         \
} while (0)
#else
#define TV_LOG(...) ((void)0)
#endif

typedef struct {
  database_t *db;
  int table_id;
  int filter_field;
  intptr_t filter_value;
  
  // Column metadata (copied from params)
  reportview_cell_style_t cell_style;
  char **field_names;
  char **column_titles;
  int *column_widths;
  int *column_min_widths;
  int column_count;
  char *check_field;
  
  // DDX-style field extraction
  db_object_proc_t obj_proc;          // Object handler proc for this table
  const db_field_msg_binding_t *bindings;  // Field bindings for this table
  int binding_count;
  uint32_t master_id;
  int master_filter_field;
  char *master_key;
  void **rows;
  int row_count;
} tableview_state_t;

// Forward declarations
static void tv_refresh(window_t *win, tableview_state_t *s);

// ══════════════════════════════════════════════════════════════════════════
// Helper: Count NULL-terminated string array
// ══════════════════════════════════════════════════════════════════════════

static int count_strings(const char **strs) {
  if (!strs) return 0;
  int count = 0;
  while (strs[count]) count++;
  return count;
}

// ══════════════════════════════════════════════════════════════════════════
// Helper: Copy string array
// ══════════════════════════════════════════════════════════════════════════

static char **copy_string_array(const char **src, int count) {
  if (!src || count <= 0) return NULL;
  char **dst = calloc((size_t)(count + 1), sizeof(char *));
  if (!dst) return NULL;
  for (int i = 0; i < count; i++) {
    dst[i] = src[i] ? strdup(src[i]) : NULL;
  }
  dst[count] = NULL;
  return dst;
}

// ══════════════════════════════════════════════════════════════════════════
// Helper: Copy int array
// ══════════════════════════════════════════════════════════════════════════

static int *copy_int_array(const int *src, int count) {
  if (count <= 0) return NULL;
  int *dst = calloc((size_t)count, sizeof(int));
  if (!dst) return NULL;
  if (src) {
    memcpy(dst, src, (size_t)count * sizeof(int));
  }
  return dst;
}

// ══════════════════════════════════════════════════════════════════════════
// Helper: Free string array
// ══════════════════════════════════════════════════════════════════════════

static void free_string_array(char **arr) {
  if (!arr) return;
  for (int i = 0; arr[i]; i++)
    free(arr[i]);
  free(arr);
}

// ══════════════════════════════════════════════════════════════════════════
// Helper: Get field text from record via DDX-style field interrogation
// ══════════════════════════════════════════════════════════════════════════

static bool tv_get_field_text(tableview_state_t *s, void *record,
                                 const char *field, char *buf, size_t buf_sz) {
  if (!s || !record || !field || !buf || buf_sz == 0) return false;
  
  // Use DDX-style field extraction via database object proc
  return db_object_get_field_text(s->bindings, s->binding_count,
                                  s->obj_proc, record, field, buf, buf_sz);
}

// Removed: tv_apply_column_widths() - now inherited from reportview evResize

// ══════════════════════════════════════════════════════════════════════════
// Refresh: Fetch records from database and populate reportview
// ══════════════════════════════════════════════════════════════════════════

static void tv_refresh(window_t *win, tableview_state_t *s) {
  if (!win || !s || !s->db) {
    TV_LOG("refresh skipped: win=%p state=%p db=%p", (void *)win, (void *)s,
           s ? (void *)s->db : NULL);
    return;
  }

  TV_LOG("refresh begin: id=%u table=%d columns=%d filter=%d:%ld",
         (unsigned)win->id, s->table_id, s->column_count, s->filter_field,
         (long)s->filter_value);
  
  // Clear existing items
  send_message(win, RVM_CLEAR, 0, NULL);
  free(s->rows);
  s->rows = NULL;
  s->row_count = 0;
  
  // Setup columns (on first refresh or if column count changed)
  int existing_cols = (int)send_message(win, RVM_GETCOLUMNCOUNT, 0, NULL);
  if (existing_cols != s->column_count) {
    send_message(win, RVM_CLEARCOLUMNS, 0, NULL);
    for (int i = 0; i < s->column_count; i++) {
      reportview_column_t col = {
        .title = s->column_titles[i] ? s->column_titles[i] : s->field_names[i],
        .width = (uint32_t)((s->column_widths && s->column_widths[i] > 0) ? s->column_widths[i] : 0),
        .min_width = (uint32_t)((s->column_min_widths && s->column_min_widths[i] > 0) ? s->column_min_widths[i] : 0),
      };
      send_message(win, RVM_ADDCOLUMN, 0, &col);
    }
    // Column widths will be calculated by reportview's evResize when window is sized
  }
  
  // Fetch records from database (Zero Wrapper Structs API!)
  result_node_t *results = (result_node_t *)send_db_message(s->db, dbFetch,
    MAKEDWORD(s->table_id, s->filter_field), (void *)s->filter_value);
  
  if (!results) {
    TV_LOG("fetch returned no rows: id=%u table=%d", (unsigned)win->id,
           s->table_id);
    return;
  }
  
  // Allocate cell buffers
  char (*cell_buf)[256] = calloc((size_t)s->column_count, sizeof(*cell_buf));
  if (!cell_buf) {
    free_result_list(results);
    return;
  }
  
  // Add each record as a row
  int row_idx = 0;
  for (result_node_t *n = results; n; n = (result_node_t *)n->next) {
    void *record = *(void **)n->data;
    if (!record) continue;
    void **grown = realloc(s->rows, (size_t)(s->row_count + 1) * sizeof(void *));
    if (!grown) break;
    s->rows = grown;
    s->rows[s->row_count++] = record;
    
    // Extract field values for all columns
    for (int col = 0; col < s->column_count; col++) {
      if (!tv_get_field_text(s, record, s->field_names[col],
                             cell_buf[col], sizeof(cell_buf[col]))) {
        cell_buf[col][0] = '\0';
      }
    }
    
    // Create row
    reportview_item_t item = {
      .text = cell_buf[0],
      .icon = -1,
      .color = get_sys_color(brTextNormal),
      .userdata = (uint32_t)row_idx,
      .subitem_count = (uint32_t)((s->column_count > 1) ? (s->column_count - 1) : 0),
    };
    if (s->check_field) {
      char checked[16] = {0};
      tv_get_field_text(s, record, s->check_field, checked, sizeof(checked));
      item.state = RV_INDEXTOSTATEIMAGEMASK(atoi(checked) ? 2 : 1);
    }
    
    for (int col = 1; col < s->column_count; col++) {
      item.subitems[col - 1] = cell_buf[col];
    }
    
    send_message(win, RVM_ADDITEM, 0, &item);
    row_idx++;
  }
  
  free(cell_buf);
  free_result_list(results);
  TV_LOG("refresh complete: id=%u table=%d rows=%d",
         (unsigned)win->id, s->table_id, row_idx);

  // Auto-select first row to trigger cascading updates in dependent views
  if (row_idx > 0) {
    reportview_data_t *rv = (reportview_data_t *)win->userdata2;
    send_message(win, RVM_SETSELECTION, 0, NULL);
    rv_notify(win, rv, 0, RVN_SELCHANGE);
  }
}

// ══════════════════════════════════════════════════════════════════════════
// Window Procedure
// ══════════════════════════════════════════════════════════════════════════

result_t win_tableview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  tableview_state_t *s = (tableview_state_t *)win->userdata;
  
  switch (msg) {
    case evRightButtonDown:
      if (win->context_menu && win->context_menu_count > 0) {
        int x = (int16_t)LOWORD(wparam) - (int)win->hscroll.pos;
        int y = (int16_t)HIWORD(wparam) - (int)win->vscroll.pos;
        return show_popup_menu(get_root_window(win), win->context_menu,
                               win->context_menu_count,
                               window_screen_x(win) + x, window_screen_y(win) + y);
      }
      return false;
    case evCreate: {
      if (!win_reportview(win, evCreate, 0, NULL)) {
        return false;
      }

      // Parse creation parameters from form_ctrl_def_t or raw tableview_params_t*
      tableview_params_t *params = NULL;
      if (lparam && (uintptr_t)lparam > 0x1000) {
        // Check if it's a form_ctrl_def_t* (has class_name field at offset 0)
        const form_ctrl_def_t *cd = (const form_ctrl_def_t *)lparam;
        if (cd->lparam) {
          params = (tableview_params_t *)cd->lparam;
        }
      } else {
        // Legacy: direct tableview_params_t* (for non-form creation)
        params = (tableview_params_t *)lparam;
      }
      
      if (!params || !params->field_names) {
        TV_LOG("create has no table params: id=%u lparam=%p",
               (unsigned)win->id, lparam);
        // Keep the control alive as a plain reportview to avoid forwarding
        // messages into an uninitialized base state.
        return true;
      }
      
      // Allocate state
      s = calloc(1, sizeof(tableview_state_t));
      if (!s) {
        return true;
      }
      
      win->userdata = s;
      s->db = params->db;  // May be NULL - set later via evSetDatabase
      s->table_id = params->table_id;
      s->filter_field = params->filter_field;
      s->filter_value = params->filter_value;
      s->master_id = params->master_id;
      s->master_filter_field = params->master_filter_field;
      s->master_key = params->master_key && params->master_key[0] ? strdup(params->master_key) : NULL;
      s->check_field = params->check_field && params->check_field[0] ? strdup(params->check_field) : NULL;
      s->cell_style = params->cell_style;
      TV_LOG("create: id=%u table=%d db=%p columns=%d", (unsigned)win->id,
             s->table_id, (void *)s->db, count_strings(params->field_names));
      
      // Get object proc and field bindings from database (DDX pattern!)
      // Skip if db is NULL (will be set later)
      if (s->db) {
        s->obj_proc = (db_object_proc_t)send_db_message(s->db, dbGetObjectProc,
                                                         (uint32_t)s->table_id, NULL);
        s->bindings = (const db_field_msg_binding_t *)send_db_message(s->db, dbGetFieldBindings,
                                                                       (uint32_t)s->table_id, &s->binding_count);
        
        if (!s->obj_proc || !s->bindings) {
          TV_LOG("create metadata missing: id=%u table=%d object_proc=%p bindings=%p count=%d",
                 (unsigned)win->id, s->table_id, (void *)s->obj_proc,
                 (void *)s->bindings, s->binding_count);
          free(s->master_key);
          free(s->check_field);
          free(s);
          win->userdata = NULL;
          return true;
        }
      }
      
      // Copy column metadata
      s->column_count = count_strings(params->field_names);
      if (s->column_count <= 0) {
        free(s->master_key);
        free(s->check_field);
        free(s);
        win->userdata = NULL;
        return true;
      }
      
      s->field_names = copy_string_array(params->field_names, s->column_count);
      s->column_titles = params->column_titles
        ? copy_string_array(params->column_titles, s->column_count)
        : copy_string_array(params->field_names, s->column_count);  // Use field names as titles
      s->column_widths = copy_int_array(params->column_widths, s->column_count);
      s->column_min_widths = copy_int_array(params->column_min_widths, s->column_count);
      
      if (!s->field_names || !s->column_titles) {
        free_string_array(s->field_names);
        free_string_array(s->column_titles);
        free(s->column_widths);
        free(s->column_min_widths);
        free(s->master_key);
        free(s->check_field);
        free(s->rows);
        free(s);
        win->userdata = NULL;
        return true;
      }
      
      // Setup as reportview
      send_message(win, RVM_SETVIEWMODE, RVM_VIEW_REPORT, NULL);
      send_message(win, RVM_SETCELLSTYLE, s->cell_style, NULL);
      send_message(win, RVM_SETCOLUMNTITLESVISIBLE, !params->hide_column_titles, NULL);
      if (s->check_field)
        send_message(win, RVM_SETEXTENDEDSTYLE, RVS_EX_CHECKBOXES,
                     (void *)(uintptr_t)RVS_EX_CHECKBOXES);
      
      // Initial refresh (only if db is set)
      if (s->db) {
        tv_refresh(win, s);
      }
      
      return true;
    }
    
    case evDestroy:
      if (s) {
        free_string_array(s->field_names);
        free_string_array(s->column_titles);
        free(s->column_widths);
        free(s->column_min_widths);
        free(s->master_key);
        free(s->check_field);
        free(s->rows);
        free(s);
        win->userdata = NULL;
      }
      win_reportview(win, evDestroy, 0, NULL);
      return false;
    
    case tvRefresh:
      if (s) {
        tv_refresh(win, s);
      }
      return true;
    
    case evSetDatabase:
      if (s && lparam) {
        s->db = (database_t *)lparam;
        // Get object proc and field bindings
        s->obj_proc = (db_object_proc_t)send_db_message(s->db, dbGetObjectProc,
                                                         (uint32_t)s->table_id, NULL);
        s->bindings = (const db_field_msg_binding_t *)send_db_message(s->db, dbGetFieldBindings,
                                                                       (uint32_t)s->table_id, &s->binding_count);
        TV_LOG("database attached: id=%u table=%d db=%p object_proc=%p bindings=%p count=%d",
               (unsigned)win->id, s->table_id, (void *)s->db,
               (void *)s->obj_proc, (void *)s->bindings, s->binding_count);
        // Refresh from database
        if (s->obj_proc && s->bindings) {
          tv_refresh(win, s);
        } else {
          TV_LOG("database API incomplete: id=%u table=%d", (unsigned)win->id,
                 s->table_id);
        }
      } else {
        TV_LOG("database attach skipped: id=%u state=%p db=%p",
               (unsigned)win->id, (void *)s, lparam);
      }
      return true;
    
    case tvSetFilter:
      if (s) {
        s->filter_field = (int)wparam;
        s->filter_value = (intptr_t)lparam;
        tv_refresh(win, s);
      }
      return true;

    case tvGetSelectedRecord: {
      int row = (int)send_message(win, RVM_GETSELECTION, 0, NULL);
      return (s && row >= 0 && row < s->row_count)
        ? (result_t)(intptr_t)s->rows[row] : 0;
    }

    case tvGetRecord:
      return (s && (int)wparam >= 0 && (int)wparam < s->row_count)
        ? (result_t)(intptr_t)s->rows[wparam] : 0;

    // evArrange and evResize now inherited from reportview
    // (reportview evResize automatically recalculates column widths)
    
    // Forward all other messages to reportview
    default:
      return win_reportview(win, msg, wparam, lparam);
  }
}

static bool tv_selected_field(window_t *win, const char *field,
                              char *buf, size_t buf_sz) {
  tableview_state_t *s = win ? (tableview_state_t *)win->userdata : NULL;
  int row = win ? (int)send_message(win, RVM_GETSELECTION, 0, NULL) : -1;
  return s && row >= 0 && row < s->row_count &&
         tv_get_field_text(s, s->rows[row], field, buf, buf_sz);
}

static void tv_refresh_dependents(window_t *win, window_t *master) {
  for (window_t *child = win ? win->children : NULL; child; child = child->next) {
    tableview_state_t *s = (tableview_state_t *)child->userdata;
    if (child->proc == win_tableview && s && s->master_id == master->id) {
      char value[64] = {0};
      if (tv_selected_field(master, s->master_key ? s->master_key : "id",
                            value, sizeof(value))) {
        // LIMITATION: strtol assumes the master key is an integer.
        // Non-integer FKs (UUIDs, hashes, etc.) will produce a zero value.
        send_message(child, tvSetFilter, (uint32_t)s->master_filter_field,
                     (void *)(intptr_t)strtol(value, NULL, 10));
      } else {
        send_message(child, RVM_CLEAR, 0, NULL);
      }
    }
    tv_refresh_dependents(child, master);
  }
}

void tableview_handle_master_selection(window_t *root, window_t *master) {
  if (root && master && master->proc == win_tableview)
    tv_refresh_dependents(root, master);
}

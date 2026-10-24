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
#include <orion/commdlg/msgbox.h>

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
  
  uint32_t master_id;
  int master_filter_field;
  char *master_key;
  void **rows;
  int row_count;
  const form_def_t *record_form;
  bool blank_zeros;
  const db_field_meta_t **metas;  // field metadata per column (NULL unknown), resolved with the database
  tableview_row_icon_t *row_icons;
  int row_icon_count;
  bool *totals;        // per column, NULL = no totals row
  char *totals_label;
  char *totals_skip;
} tableview_state_t;

// Forward declarations
static void tv_refresh(window_t *win, tableview_state_t *s, int keep_row);
static void tv_refresh_dependents(window_t *win, window_t *master);
static bool tv_selected_field(window_t *win, const char *field, char *buf, size_t buf_sz);
static bool tv_record_action(window_t *win, tableview_state_t *s, uint32_t msg, const char *defaults);

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
  
  return db_get_field_text(s->db, s->table_id, record, field, buf, buf_sz);
}

static bool tv_zero_text(const char *text) {
  bool digit = false;
  for (const char *p = text; *p; p++) {
    if (*p >= '1' && *p <= '9') return false;
    digit |= *p == '0';
  }
  return digit;
}

// Booleans read "Yes" or nothing instead of 1 and 0; with zeros="blank",
// numeric zeros are left empty.
static int tv_type(const tableview_state_t *s, int col) {
  return s->metas && s->metas[col] ? (int)s->metas[col]->type : -1;
}

static bool tv_numeric(int type) { return type == DB_TYPE_INT || type == DB_TYPE_FLOAT || type == DB_TYPE_DOUBLE; }

static void tv_present_cell(tableview_state_t *s, int col, char *text, size_t sz) {
  int type = tv_type(s, col);
  if (type == DB_TYPE_BOOL) snprintf(text, sz, "%s", atoi(text) ? ui_tr("Yes") : "");
  else if (s->blank_zeros && tv_numeric(type) && tv_zero_text(text))
    text[0] = '\0';
}

// A field is set when it is non-empty and not zero or false.
static bool tv_field_set(tableview_state_t *s, void *record, const char *field) {
  char text[256] = "";
  db_get_field_text(s->db, s->table_id, record, field, text, sizeof(text));
  return text[0] && !tv_zero_text(text) && strcmp(text, "false");
}

static const tableview_row_icon_t *tv_row_icon(tableview_state_t *s, void *record) {
  for (int i = 0; i < s->row_icon_count; i++) {
    const tableview_row_icon_t *rule = &s->row_icons[i];
    char text[256] = "";
    if (!rule->field) return rule;
    if (!rule->value) { if (tv_field_set(s, record, rule->field)) return rule; continue; }
    db_get_field_text(s->db, s->table_id, record, rule->field, text, sizeof(text));
    if (!strcmp(text, rule->value)) return rule;
  }
  return NULL;
}

// The totals row: sums in their columns, the caption in the first other one.
static void tv_set_footer(window_t *win, tableview_state_t *s, const double *sums) {
  if (!s->totals) return;
  char (*text)[64] = calloc((size_t)s->column_count, sizeof(*text));
  const char **texts = calloc((size_t)s->column_count, sizeof(*texts));
  if (!text || !texts) {
    fprintf(stderr, "[tableview] totals allocation failed win=%u columns=%d\n", win->id, s->column_count);
    fflush(stderr);
    free(text); free(texts); return;
  }
  bool labelled = false;
  for (int col = 0; col < s->column_count; col++) {
    texts[col] = text[col];
    if (!s->totals[col]) {
      if (!labelled) snprintf(text[col], sizeof(text[col]), "%s", s->totals_label ? s->totals_label : ui_tr("Totals"));
      labelled = true;
      continue;
    }
    const db_field_meta_t *meta = s->metas ? s->metas[col] : NULL;
    int decimals = !meta || meta->type == DB_TYPE_INT ? 0 : meta->precision > 0 ? meta->precision : 2;
    ui_format_number(sums ? sums[col] : 0, decimals, text[col], sizeof(text[col]));
    if (s->blank_zeros && tv_zero_text(text[col])) text[col][0] = '\0';
  }
  send_message(win, RVM_SETFOOTER, (uint32_t)s->column_count, texts);
  free(text); free(texts);
}

// The database can format this table: schema-driven or object-proc adaptor.
static bool tv_db_supports(database_t *db, int table_id) {
  if (!db) return false;
  const db_schema_def_t *schema = (const db_schema_def_t *)send_db_message(db, dbGetSchema, 0, NULL);
  const db_table_schema_t *t = db_schema_table(schema, table_id);
  if (t && t->meta) return true;
  return send_db_message(db, dbGetObjectProc, (uint32_t)table_id, NULL) &&
         send_db_message(db, dbGetFieldBindings, (uint32_t)table_id, NULL);
}

// Removed: tv_apply_column_widths() - now inherited from reportview evResize

// ══════════════════════════════════════════════════════════════════════════
// Refresh: Fetch records from database and populate reportview
// ══════════════════════════════════════════════════════════════════════════

// keep_row: row to reselect after reloading (clamped), or 0 for the first.
static void tv_refresh(window_t *win, tableview_state_t *s, int keep_row) {
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
  
  // Setup columns (on first refresh or if column count changed). Numbers
  // align right, as in a ledger.
  int existing_cols = (int)send_message(win, RVM_GETCOLUMNCOUNT, 0, NULL);
  if (existing_cols != s->column_count) {
    if (s->column_count > MAX_REPORTVIEW_COLUMNS) {
      fprintf(stderr, "[tableview] columns beyond the limit are not shown win=%u table=%d columns=%d max=%d\n",
              win->id, s->table_id, s->column_count, MAX_REPORTVIEW_COLUMNS);
      fflush(stderr);
      s->column_count = MAX_REPORTVIEW_COLUMNS;
    }
    const db_schema_def_t *schema = (const db_schema_def_t *)send_db_message(s->db, dbGetSchema, 0, NULL);
    const db_table_schema_t *table = db_schema_table(schema, s->table_id);
    free(s->metas);
    s->metas = calloc((size_t)s->column_count, sizeof(*s->metas));
    send_message(win, RVM_CLEARCOLUMNS, 0, NULL);
    for (int i = 0; i < s->column_count; i++) {
      if (s->metas && table) s->metas[i] = db_schema_field_meta(s->db, table, s->field_names[i]);
      bool number = tv_numeric(tv_type(s, i));
      reportview_column_t col = {
        .title = s->column_titles[i] ? s->column_titles[i] : s->field_names[i],
        .width = (uint32_t)((s->column_widths && s->column_widths[i] > 0) ? s->column_widths[i] : 0),
        .min_width = (uint32_t)((s->column_min_widths && s->column_min_widths[i] > 0) ? s->column_min_widths[i] : 0),
        .align = number ? TEXT_ALIGN_RIGHT : 0,
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
    tv_set_footer(win, s, NULL);
    return;
  }
  
  // Allocate cell buffers
  char (*cell_buf)[256] = calloc((size_t)s->column_count, sizeof(*cell_buf));
  double *sums = s->totals ? calloc((size_t)s->column_count, sizeof(double)) : NULL;
  if (!cell_buf || (s->totals && !sums)) {
    fprintf(stderr, "[tableview] row buffer allocation failed win=%u columns=%d\n", win->id, s->column_count);
    fflush(stderr);
    free(cell_buf); free(sums);
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
    bool counted = !s->totals_skip || !tv_field_set(s, record, s->totals_skip);
    for (int col = 0; col < s->column_count; col++) {
      if (!tv_get_field_text(s, record, s->field_names[col],
                             cell_buf[col], sizeof(cell_buf[col]))) {
        cell_buf[col][0] = '\0';
      }
      if (sums && s->totals[col] && counted) sums[col] += ui_parse_number(cell_buf[col]);
      tv_present_cell(s, col, cell_buf[col], sizeof(cell_buf[col]));
    }
    
    // Create row
    reportview_item_t item = {
      .text = cell_buf[0],
      .icon = -1,
      .color = get_sys_color(brTextNormal),
      .userdata = (uint32_t)row_idx,
      .subitem_count = (uint32_t)((s->column_count > 1) ? (s->column_count - 1) : 0),
    };
    const tableview_row_icon_t *rule = s->row_icons ? tv_row_icon(s, record) : NULL;
    if (rule) {
      item.icon_name = rule->icon;
      item.icon_color = rule->color >= 0 && rule->color < brCount ? get_sys_color((sys_color_idx_t)rule->color) : 0;
    }
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
  
  tv_set_footer(win, s, sums);
  free(cell_buf);
  free(sums);
  free_result_list(results);
  TV_LOG("refresh complete: id=%u table=%d rows=%d",
         (unsigned)win->id, s->table_id, row_idx);

  // Select a row to trigger cascading updates in dependent views
  if (row_idx > 0) {
    reportview_data_t *rv = (reportview_data_t *)win->userdata2;
    int row = keep_row < 0 ? 0 : keep_row >= row_idx ? row_idx - 1 : keep_row;
    send_message(win, RVM_SETSELECTION, (uint32_t)row, NULL);
    rv_notify(win, rv, row, RVN_SELCHANGE);
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
      s->record_form = params->record_form;
      s->blank_zeros = params->blank_zeros;
      TV_LOG("create: id=%u table=%d db=%p columns=%d", (unsigned)win->id,
             s->table_id, (void *)s->db, count_strings(params->field_names));
      
      if (s->db && !tv_db_supports(s->db, s->table_id)) {
        TV_LOG("create metadata missing: id=%u table=%d", (unsigned)win->id, s->table_id);
        free(s->master_key);
        free(s->check_field);
        free(s);
        win->userdata = NULL;
        return true;
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
      for (int i = 0; s->column_titles && i < s->column_count; i++) {
        const char *title = ui_tr(s->column_titles[i]);
        if (title && title != s->column_titles[i]) { free(s->column_titles[i]); s->column_titles[i] = strdup(title); }
      }
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
      
      if (params->row_icon_count > 0 && params->row_icons) {
        s->row_icons = calloc((size_t)params->row_icon_count, sizeof(*s->row_icons));
        for (int i = 0; s->row_icons && i < params->row_icon_count; i++) {
          const tableview_row_icon_t *src = &params->row_icons[i];
          s->row_icons[i] = (tableview_row_icon_t){ src->field ? strdup(src->field) : NULL, src->value ? strdup(src->value) : NULL,
                                                    src->icon ? strdup(src->icon) : NULL, src->color };
          s->row_icon_count++;
        }
      }
      if (params->column_totals) {
        s->totals = calloc((size_t)s->column_count, sizeof(bool));
        if (s->totals) memcpy(s->totals, params->column_totals, (size_t)s->column_count * sizeof(bool));
        s->totals_label = params->totals_label ? strdup(ui_tr(params->totals_label)) : NULL;
        s->totals_skip = params->totals_skip ? strdup(params->totals_skip) : NULL;
      }
      if ((params->row_icon_count > 0 && !s->row_icons) || (params->column_totals && !s->totals)) {
        fprintf(stderr, "[tableview] row icon/totals allocation failed win=%u\n", win->id);
        fflush(stderr);
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
        tv_refresh(win, s, 0);
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
        free(s->metas);
        for (int i = 0; i < s->row_icon_count; i++) {
          free((char *)s->row_icons[i].field); free((char *)s->row_icons[i].value); free((char *)s->row_icons[i].icon);
        }
        free(s->row_icons);
        free(s->totals);
        free(s->totals_label);
        free(s->totals_skip);
        free(s);
        win->userdata = NULL;
      }
      win_reportview(win, evDestroy, 0, NULL);
      return false;
    
    case tvRefresh:
      if (s) {
        tv_refresh(win, s, (int)send_message(win, RVM_GETSELECTION, 0, NULL));
      }
      return true;
    
    case evSetDatabase:
      if (s && lparam) {
        s->db = (database_t *)lparam;
        TV_LOG("database attached: id=%u table=%d db=%p", (unsigned)win->id,
               s->table_id, (void *)s->db);
        window_t *master = s->master_id ? get_window_item(get_root_window(win), s->master_id) : NULL;
        if (!tv_db_supports(s->db, s->table_id)) {
          TV_LOG("database API incomplete: id=%u table=%d", (unsigned)win->id,
                 s->table_id);
        } else if (master && master->userdata) {
          // A detail bound after its master follows the master's selection.
          tv_refresh_dependents(get_root_window(win), master);
        } else {
          tv_refresh(win, s, 0);
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
        tv_refresh(win, s, 0);
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

    case tvGetTable:
      if (s && lparam) *(int *)lparam = s->table_id;
      return s != NULL;
    case tvInsertRecord: case tvEditRecord: case tvDeleteRecord:
      return s && tv_record_action(win, s, msg, (const char *)lparam);

    // evArrange and evResize now inherited from reportview
    // (reportview evResize automatically recalculates column widths)
    
    // Forward all other messages to reportview
    default:
      return win_reportview(win, msg, wparam, lparam);
  }
}

// ── Record actions ─────────────────────────────────────────────────────────

static int tv_record_key(tableview_state_t *s, const void *record) {
  char key[32];
  return record && tv_get_field_text(s, record, "id", key, sizeof(key)) ? atoi(key) : 0;
}

static bool tv_record_action(window_t *win, tableview_state_t *s, uint32_t msg, const char *defaults) {
  window_t *root = get_root_window(win);
  void *selected = (void *)(intptr_t)send_message(win, tvGetSelectedRecord, 0, NULL);
  int id = tv_record_key(s, selected);
  bool changed = false;
  if (msg == tvDeleteRecord) {
    if (!id || !s->db) return false;
    int referrers = (int)send_db_message(s->db, dbCountReferrers, (uint32_t)s->table_id, (void *)(intptr_t)id);
    char text[256];
    if (referrers > 0) {
      snprintf(text, sizeof(text), ui_tr("This record is used by %d other record(s) and cannot be deleted."), referrers);
      message_box(root, text, ui_tr("Delete"), MB_OK);
      return false;
    }
    if (message_box(root, ui_tr("Delete the selected record?"), ui_tr("Delete"), MB_YESNO) != IDYES) return false;
    changed = send_db_message(s->db, dbDelete, (uint32_t)s->table_id, (void *)(intptr_t)id) != 0;
  } else {
    if (!s->record_form) {
      fprintf(stderr, "[tableview] record action rejected win=%u table=%d: no record form (form=)\n", win->id, s->table_id);
      fflush(stderr);
      return false;
    }
    if (msg == tvEditRecord && !id) return false;
    // A detail view's filter is its master's key: new records take it.
    const db_field_meta_t *meta = NULL;
    int count = 0;
    if (s->db) meta = (const db_field_meta_t *)send_db_message(s->db, dbGetFieldMeta, (uint32_t)s->table_id, &count);
    db_dialog_opts_t opts = { .defaults = defaults };
    if (s->filter_field > 0 && meta && s->filter_field < count) {
      opts.fk_field = meta[s->filter_field].name;
      opts.fk_value = (int)s->filter_value;
    }
    changed = show_db_dialog_opts(s->record_form, NULL, root, msg == tvEditRecord ? id : 0, &opts) != 0;
  }
  if (!changed) return false;
  tv_refresh(win, s, (int)send_message(win, RVM_GETSELECTION, 0, NULL));
  send_message(root, evCommand, MAKEDWORD(win->id, tvnChanged), win);
  return true;
}

bool tableview_handle_activate(window_t *view) {
  tableview_state_t *s = view && view->proc == win_tableview ? (tableview_state_t *)view->userdata : NULL;
  return s && s->record_form && (send_message(view, tvEditRecord, 0, NULL), true);
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

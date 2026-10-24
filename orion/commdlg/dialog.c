#include <stdio.h>
#include <orion/ui.h>
#include <orion/user/user.h>
#include <orion/kernel/kernel.h>
#include <orion/commctl/commctl.h>

typedef struct {
  form_def_t const *def;
  database_t       *db;
  int               record_id;
  void             *record_buf;
  bool              is_new;
  const char       *fk_field;
  int               fk_value;
  winproc_t         hook;
  void             *synced;     // the record as last shown, to spot what the database changed
  struct { uint32_t ctrl_id; int table_id; } sources[8];  // polymorphic combos' current tables
} db_dlg_ctx_t;

static size_t db_field_size(const db_field_meta_t *f) {
  switch (f->type) {
    case DB_TYPE_INT:    return sizeof(int);
    case DB_TYPE_BOOL:   return sizeof(bool);
    case DB_TYPE_FLOAT:  return sizeof(float);
    case DB_TYPE_DOUBLE: return sizeof(double);
    default:             return (size_t)f->length;
  }
}

// Shows what the database changed since the record was last shown — totals a
// detail table recomputed — and keeps what the user is typing elsewhere.
static void db_dialog_resync(window_t *win, db_dlg_ctx_t *ctx) {
  const void *stored = ctx->record_id > 0 ? (const void *)send_db_message(ctx->db, dbFind,
    MAKEDWORD(ctx->def->db_table_id, 0), (void *)(intptr_t)ctx->record_id) : NULL;
  if (!stored || !ctx->synced) return;
  const db_field_meta_t *fields = (const db_field_meta_t *)ctx->def->db_fields;
  for (int i = 0; i < ctx->def->db_field_count; i++) {
    size_t off = fields[i].offset, n = db_field_size(&fields[i]);
    if (!memcmp((const char *)stored + off, (char *)ctx->synced + off, n)) continue;
    memcpy((char *)ctx->record_buf + off, (const char *)stored + off, n);
    memcpy((char *)ctx->synced + off, (const char *)stored + off, n);
    for (int b = 0; b < ctx->def->binding_count; b++)
      if (ctx->def->bindings[b].offset == off) dialog_push(win, ctx->record_buf, &ctx->def->bindings[b], 1);
  }
}

// Detail tables of the edited record follow it, as EODetailDataSource does:
// a TableView whose table refers to the dialog's table shows that record's rows.
static void db_dialog_bind_details(window_t *root, window_t *win, db_dlg_ctx_t *ctx, const db_schema_def_t *schema) {
  for (window_t *c = win ? win->children : NULL; c; c = c->next) {
    int table_id = 0;
    if (c->proc == win_tableview && send_message(c, tvGetTable, 0, &table_id)) {
      const db_table_schema_t *t = db_schema_table(schema, table_id);
      for (int i = 0; t && i < t->field_count; i++) {
        const char *rel = t->fields[i].relation_table, *dot = rel ? strchr(rel, '.') : NULL;
        if (!rel || rel[0] == '{' || strcmp(dot && strncmp(rel, ctx->db->name, (size_t)(dot - rel)) == 0 ? dot + 1 : rel, ctx->def->db_table)) continue;
        int f = db_schema_field_index(t, t->fields[i].name);
        if (f > 0) send_message(c, tvSetFilter, (uint32_t)f, (void *)(intptr_t)ctx->record_id);
        break;
      }
    }
    db_dialog_bind_details(root, c, ctx, schema);
  }
}

// A ComboBox bound to a polymorphic relation (relation="ib.{debit.subconto_kind.table}.id")
// lists the table the record's path names: the analytics of the chosen account.
static void db_dialog_sync_sources(window_t *win, db_dlg_ctx_t *ctx, bool keep_values) {
  const db_schema_def_t *schema = (const db_schema_def_t *)send_db_message(ctx->db, dbGetSchema, 0, NULL);
  const db_table_schema_t *t = db_schema_table(schema, ctx->def->db_table_id);
  if (!t) return;
  void *current = malloc(ctx->def->db_record_size);
  if (!current) return;
  memcpy(current, ctx->record_buf, ctx->def->db_record_size);
  dialog_pull(win, current, ctx->def->bindings, ctx->def->binding_count);
  for (int b = 0; b < ctx->def->binding_count; b++) {
    const ctrl_binding_t *binding = &ctx->def->bindings[b];
    int f = 0;
    while (f < t->meta_count && t->meta[f].offset != binding->offset) f++;
    const db_field_schema_t *fs = NULL;
    for (int i = 0; f < t->meta_count && i < t->field_count; i++) if (!strcmp(t->fields[i].name, t->meta[f].name)) fs = &t->fields[i];
    const char *rel = fs ? fs->relation_table : NULL, *open = rel ? strchr(rel, '{') : NULL, *close = open ? strchr(open, '}') : NULL;
    if (!close) continue;
    database_t *target_db = ctx->db;
    if (open > rel) {
      char name[64];
      snprintf(name, sizeof(name), "%.*s", (int)(open - rel - 1), rel);
      target_db = get_database_by_name(name);
    }
    char path[128], table[64] = "";
    snprintf(path, sizeof(path), "%.*s", (int)(close - open - 1), open + 1);
    db_schema_field_text(ctx->db, t, current, path, table, sizeof(table));
    const db_schema_def_t *target_schema = target_db ? (const db_schema_def_t *)send_db_message(target_db, dbGetSchema, 0, NULL) : NULL;
    const db_table_schema_t *target = table[0] ? db_schema_table_named(target_schema, table) : NULL;
    int slot = 0;
    while (slot < 7 && ctx->sources[slot].ctrl_id && ctx->sources[slot].ctrl_id != binding->ctrl_id) slot++;
    int table_id = target ? target->table_id : -1;
    if (ctx->sources[slot].ctrl_id == binding->ctrl_id && ctx->sources[slot].table_id == table_id) continue;
    ctx->sources[slot].ctrl_id = binding->ctrl_id;
    ctx->sources[slot].table_id = table_id;
    window_t *combo = get_window_item(win, binding->ctrl_id);
    if (target) send_message(combo, cbSetSource, (uint32_t)target->table_id, target_db);
    else send_message(combo, cbClear, 0, NULL);
    send_message(combo, cbSetCurrentValue, keep_values ? (uint32_t)*(const int *)((const char *)current + binding->offset) : 0, NULL);
    enable_window(combo, target != NULL);
  }
  free(current);
}

// Applies "field=value;field=value" to a record through the field metadata.
static void db_apply_defaults(const form_def_t *def, void *record, const char *defaults) {
  const db_field_meta_t *fields = (const db_field_meta_t *)def->db_fields;
  for (const char *p = defaults; p && *p; ) {
    const char *end = strchr(p, ';'), *eq = strchr(p, '=');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (eq && eq < p + len) {
      char name[64], value[256];
      snprintf(name, sizeof(name), "%.*s", (int)(eq - p), p);
      snprintf(value, sizeof(value), "%.*s", (int)(p + len - eq - 1), eq + 1);
      int i = 0;
      while (i < def->db_field_count && strcmp(fields[i].name, name)) i++;
      if (i < def->db_field_count) db_parse_field(&fields[i], record, value);
      else { fprintf(stderr, "[dialog] unknown default field form=%s field=%s\n", def->name ? def->name : "", name); fflush(stderr); }
    }
    p += len + (end ? 1 : 0);
  }
}

static result_t dialog_db_proc(window_t *win, uint32_t msg,
                               uint32_t wparam, void *lparam) {
  db_dlg_ctx_t *ctx = (db_dlg_ctx_t *)win->userdata;

  switch (msg) {
    case evCreate: {
      ctx = (db_dlg_ctx_t *)lparam;
      win->userdata = ctx;
      if (!ctx->def->db_table || !ctx->def->db_fields || !ctx->db) {
        end_dialog(win, 0);
        return true;
      }
      if (ctx->record_id > 0) {
        lresult_t result = send_db_message(ctx->db, dbFind,
          MAKEDWORD(ctx->def->db_table_id, 0),
          (void *)(intptr_t)ctx->record_id);
        void *fetched = (void *)result;
        if (fetched) {
          memcpy(ctx->record_buf, fetched, ctx->def->db_record_size);
          ctx->is_new = false;
        } else {
          ctx->is_new = true;
        }
      } else {
        ctx->is_new = true;
        if (ctx->fk_field && ctx->fk_value > 0 && ctx->def->db_fields) {
          const db_field_meta_t *fields = (const db_field_meta_t *)ctx->def->db_fields;
          for (int i = 0; i < ctx->def->db_field_count; i++) {
            if (strcmp(fields[i].name, ctx->fk_field) == 0) {
              int *fk_ptr = (int *)((char *)ctx->record_buf + fields[i].offset);
              *fk_ptr = ctx->fk_value;
              break;
            }
          }
        }
      }
      if (ctx->def->bindings && ctx->def->binding_count > 0) {
        dialog_push(win, ctx->record_buf,
                    ctx->def->bindings, ctx->def->binding_count);
      }
      if (ctx->synced) memcpy(ctx->synced, ctx->record_buf, ctx->def->db_record_size);
      db_dialog_sync_sources(win, ctx, true);
      if (ctx->record_id > 0)
        db_dialog_bind_details(win, win, ctx, (const db_schema_def_t *)send_db_message(ctx->db, dbGetSchema, 0, NULL));
      if (ctx->hook) ctx->hook(win, evCreate, 0, NULL);
      return true;
    }
    case evCommand: {
      if (!ctx) return false;
      uint16_t notif = HIWORD(wparam);
      if (notif == cbSelectionChange) db_dialog_sync_sources(win, ctx, false);
      if (notif == tvnChanged) {
        db_dialog_resync(win, ctx);
        return ctx->hook ? ctx->hook(win, msg, wparam, lparam) : true;
      }
      if (notif == edUpdate && ctx->hook) return ctx->hook(win, msg, wparam, lparam);
      if (notif == edUpdate) {
        if (ctx->def->bindings) {
          dialog_pull(win, ctx->record_buf,
                      ctx->def->bindings, ctx->def->binding_count);
        }
        if (ctx->is_new) {
          send_db_message(ctx->db, dbInsert,
            ctx->def->db_table_id, ctx->record_buf);
        } else {
          send_db_message(ctx->db, dbUpdate,
            MAKEDWORD(ctx->def->db_table_id, ctx->record_id),
            ctx->record_buf);
        }
        end_dialog(win, 1);
        return true;
      }
      if (notif != btnClicked) return ctx->hook ? ctx->hook(win, msg, wparam, lparam) : false;
      window_t *src = (window_t *)lparam;
      if (!src) return false;
      if (form_dispatch_target_action(win, ctx->def, src)) return true;
      if (ctx->def->ok_id && src->id == ctx->def->ok_id) {
        if (ctx->def->bindings) {
          dialog_pull(win, ctx->record_buf,
                      ctx->def->bindings, ctx->def->binding_count);
        }
        if (ctx->is_new) {
          send_db_message(ctx->db, dbInsert,
            ctx->def->db_table_id, ctx->record_buf);
        } else {
          send_db_message(ctx->db, dbUpdate,
            MAKEDWORD(ctx->def->db_table_id, ctx->record_id),
            ctx->record_buf);
        }
        end_dialog(win, 1);
        return true;
      }
      if (ctx->def->cancel_id && src->id == ctx->def->cancel_id) {
        end_dialog(win, 0);
        return true;
      }
      return ctx->hook ? ctx->hook(win, msg, wparam, lparam) : false;
    }
    default:
      return ctx && ctx->hook ? ctx->hook(win, msg, wparam, lparam) : false;
  }
}

bool form_dispatch_target_action(window_t *root, const form_def_t *def, window_t *control) {
  for (int i = 0; def && control && i < def->child_count; i++) {
    const form_ctrl_def_t *cd = &def->children[i];
    if (cd->id != control->id || !cd->target) continue;
    window_t *target = get_window_item(root, cd->target);
    if (!target) {
      fprintf(stderr, "[dialog] action target missing form=%s control=%u target=%u\n", def->id ? def->id : "", cd->id, cd->target);
      fflush(stderr);
      return true;
    }
    send_message(target, cd->target_message, 0, NULL);
    return true;
  }
  return false;
}

int db_dialog_record_id(window_t *win) {
  window_t *root = win ? get_root_window(win) : NULL;
  if (!root || root->proc != dialog_db_proc || !root->userdata) {
    fprintf(stderr, "[dialog] db_dialog_record_id: win=%u is not a database dialog\n",
            win ? win->id : 0);
    fflush(stderr);
    return 0;
  }
  return ((db_dlg_ctx_t *)root->userdata)->record_id;
}

void *db_dialog_record(window_t *win) {
  window_t *root = win ? get_root_window(win) : NULL;
  if (!root || root->proc != dialog_db_proc || !root->userdata) {
    fprintf(stderr, "[dialog] db_dialog_record: win=%u is not a database dialog\n", win ? win->id : 0);
    fflush(stderr);
    return NULL;
  }
  return ((db_dlg_ctx_t *)root->userdata)->record_buf;
}

uint32_t show_db_dialog_opts(form_def_t const *def, const char *title, window_t *parent,
                             int record_id, const db_dialog_opts_t *opts) {
  if (!def || !def->db_name || !def->db_table) return 0;
  database_t *db = get_database_by_name(def->db_name);
  if (!db || !def->db_record_size) {
    fprintf(stderr, "show_db_dialog: database '%s' not found or form '%s' has no record size\n",
            def->db_name, def->name ? def->name : "");
    fflush(stderr);
    return 0;
  }
  void *record_buf = calloc(1, def->db_record_size), *synced = calloc(1, def->db_record_size);
  if (!record_buf || !synced) { free(record_buf); free(synced); return 0; }
  db_dlg_ctx_t ctx = { .def = def, .db = db, .record_id = record_id, .record_buf = record_buf,
                       .is_new = (record_id == 0), .fk_field = opts ? opts->fk_field : NULL,
                       .fk_value = opts ? opts->fk_value : 0, .hook = opts ? opts->hook : NULL, .synced = synced };
  bool scoped = send_db_message(db, dbBeginChanges, 0, NULL) != 0;
  if (record_id == 0) {
    // A new record: the database's defaults, the caller's, then the foreign key.
    send_db_message(db, dbInitRecord, (uint32_t)def->db_table_id, record_buf);
    if (opts) db_apply_defaults(def, record_buf, opts->defaults);
    if (ctx.fk_field && ctx.fk_value > 0) {
      char fk[96];
      snprintf(fk, sizeof(fk), "%s=%d", ctx.fk_field, ctx.fk_value);
      db_apply_defaults(def, record_buf, fk);
    }
    // In an editing context the record exists while the dialog is open, so
    // detail tables can refer to it; Cancel reverts the insert.
    const db_field_meta_t *key = (const db_field_meta_t *)def->db_fields;
    void *inserted = scoped ? (void *)send_db_message(db, dbInsert, (uint32_t)def->db_table_id, record_buf) : NULL;
    if (inserted && key && key[0].type == DB_TYPE_INT) {
      ctx.record_id = *(const int *)((const char *)inserted + key[0].offset);
      ctx.is_new = false;
    }
    ctx.fk_field = NULL;  // already applied
  }
  uint32_t result = show_dialog_from_form_ex(def, title, parent,
                                             WINDOW_VSCROLL | WINDOW_DIALOG | WINDOW_NOTRAYBUTTON,
                                             dialog_db_proc, &ctx);
  if (scoped) send_db_message(db, result ? dbCommitChanges : dbRevertChanges, 0, NULL);
  if (opts && opts->saved_id) {
    const db_field_meta_t *key = (const db_field_meta_t *)def->db_fields;
    *opts->saved_id = !result ? 0 : ctx.record_id > 0 ? ctx.record_id
                    : key && key[0].type == DB_TYPE_INT ? *(const int *)((const char *)record_buf + key[0].offset) : 0;
  }
  free(record_buf);
  free(synced);
  return result;
}

uint32_t show_db_dialog_hook(form_def_t const *def, const char *title,
                             window_t *parent, int record_id, winproc_t hook) {
  db_dialog_opts_t opts = { .hook = hook };
  return show_db_dialog_opts(def, title, parent, record_id, &opts);
}

uint32_t show_db_dialog_ex(form_def_t const *def, const char *title,
                           window_t *parent, int record_id,
                           const char *fk_field, int fk_value) {
  db_dialog_opts_t opts = { .fk_field = fk_field, .fk_value = fk_value };
  return show_db_dialog_opts(def, title, parent, record_id, &opts);
}

uint32_t show_db_dialog(form_def_t const *def, const char *title,
                        window_t *parent, int record_id) {
  return show_db_dialog_ex(def, title, parent, record_id, NULL, 0);
}
#include <stdbool.h>
#include <orion/ui.h>

void ddx_push_check(window_t *dlg, const ctrl_binding_t *b, const void *state) {
  const char *base = (const char *)state;
  window_t *ctrl = get_window_item(dlg, b->ctrl_id);
  bool v;
  if (!ctrl) return;
  v = *(const bool *)(base + b->offset);
  send_message(ctrl, btnSetCheck, v ? btnStateChecked : btnStateUnchecked, NULL);
}

void ddx_pull_check(window_t *dlg, const ctrl_binding_t *b, void *state) {
  char *base = (char *)state;
  window_t *ctrl = get_window_item(dlg, b->ctrl_id);
  int v;
  if (!ctrl) return;
  v = (int)send_message(ctrl, btnGetCheck, 0, NULL);
  *(bool *)(base + b->offset) = (v == btnStateChecked);
}

int dialog_pull_command(window_t *win, void *state,
                        const ctrl_binding_t *b, int n,
                        uint16_t command) {
  if (!win || !state || !b) return 0;
  char *base = (char *)state;
  int applied = 0;
  for (int i = 0; i < n; i++) {
    if (command && b[i].command && b[i].command != command)
      continue;

    if (b[i].pull) {
      b[i].pull(win, &b[i], state);
      applied++;
      continue;
    }

    window_t *ctrl = get_window_item(win, b[i].ctrl_id);
    if (!ctrl) continue;
    switch (b[i].getter) {
      case 0:
        applied++;
        break;
      case cbGetCurrentSelection: {
        int v = -1;
        (void)send_message(ctrl, cbGetCurrentSelection, 0, &v);
        *(int *)(base + b[i].offset) = (v >= 0) ? v : (int)b[i].wparam;
        applied++;
        break;
      }
      case edGetText: {
        char  *dst = base + b[i].offset;
        size_t sz  = b[i].wparam;
        if (sz > 0)
          send_message(ctrl, edGetText, (uint32_t)sz, dst);
        applied++;
        break;
      }
      default:
        (void)send_message(ctrl, b[i].getter, (uint32_t)b[i].wparam,
                           base + b[i].offset);
        applied++;
        break;
    }
  }
  return applied;
}
#include <orion/user/user.h>

// Dialog Data Exchange (DDX) API entry points
void dialog_push(window_t *win, const void *state,
                 const ctrl_binding_t *b, int n) {
  if (!win || !state || !b) return;
  const char *base = (const char *)state;
  for (int i = 0; i < n; i++) {
    if (b[i].push) {
      b[i].push(win, &b[i], state);
      continue;
    }
    window_t *ctrl = get_window_item(win, b[i].ctrl_id);
    if (!ctrl) continue;
    switch (b[i].getter) {
      case cbGetCurrentSelection: {
        int v = *(const int *)(base + b[i].offset);
        if (v < 0) v = (int)b[i].wparam;
        send_message(ctrl, cbSetCurrentSelection, (uint32_t)v, NULL);
        break;
      }
      case edGetText: send_message(ctrl, edSetText, 0, (void *)(base + b[i].offset)); break;
      default: break;
    }
  }
}

void dialog_pull(window_t *win, void *state,
                 const ctrl_binding_t *b, int n) {
  (void)dialog_pull_command(win, state, b, n, 0);
}
// Moved from user/dialog.c to commdlg/dialog.c
#include <stdlib.h>
#include <string.h>
#include <orion/ui.h>

// ── Auto-height calculation helpers ──────────────────────────────────
// Returns true only if some control stretches vertically (blocking auto-height).
// orionc generates a flat array — parent relationships are encoded in .parent IDs.
// A WINDOW_FLEXSPACE inside a *horizontal* stack only fills horizontal space and
// does not block vertical auto-sizing, so we find the parent and check its flags.
static flags_t form_find_flags_by_id(const form_ctrl_def_t *children, int count,
                                      uint32_t id) {
  for (int i = 0; i < count; i++) {
    if (children[i].id == id) return children[i].flags;
  }
  return 0;
}

static bool form_has_flexspace(const form_def_t *def) {
  if (!def) return false;
  for (int i = 0; i < def->child_count; i++) {
    if (!(def->children[i].flags & WINDOW_FLEXSPACE)) continue;
    flags_t parent_flags = form_find_flags_by_id(def->children, def->child_count,
                                                  def->children[i].parent);
    if (!(parent_flags & WINDOW_STACK_HORIZONTAL))
      return true;
  }
  return false;
}

// Calculate the minimum height needed for a fixed-content form
// This is a simplified calculation - production version would measure actual controls
__attribute__((unused))
static int calculate_form_height(const form_def_t *def) {
  if (!def || !(def->flags & WINDOW_AUTO_LAYOUT)) return 0;
  
  // For now, use a simple heuristic based on child count and spacing
  // TODO: Actually measure each control's desired height
  int padding_v = def->padding.y + def->padding.h;
  int spacing = def->layout_spacing;
  
  // Estimate: each child row is ~19px (button height), separator is 1px
  int estimated_h = padding_v;
  for (int i = 0; i < def->child_count; i++) {
    if (i > 0) estimated_h += spacing;
    // Simple heuristic: most controls are 13-19px
    estimated_h += 19;
  }
  
  return estimated_h > 0 ? estimated_h : 84; // Fallback to reasonable default
}

// Shared modal message loop.  Runs until the dialog window is destroyed or the
// application exits.  Forces a full repaint after the dialog is gone.
static uint32_t run_dialog_loop(window_t *dlg, window_t *parent) {
  // Guard against the window being destroyed during evCreate
  // (e.g. end_dialog called from the window proc's create handler).
  if (!is_window(dlg)) return 0;
  uint32_t result = 0;
  ui_event_t event;
  window_t *modal_parent = parent ? get_root_window(parent) : NULL;
  window_t *prev_modal_overlay_parent = g_ui_runtime.modal_overlay_parent;
  dlg->userdata2 = &result;
  if (modal_parent) {
    g_ui_runtime.modal_overlay_parent = modal_parent;
    enable_window(modal_parent, false);
  }
  show_window(dlg, true);
  while (g_ui_runtime.running && is_window(dlg)) {
    while (get_message(&event)) {
      dispatch_message(&event);
    }
    repost_messages();
#ifdef AX_PLATFORM_IOS
    axWaitMessage(10);
#endif
  }
  // If the app is quitting, the modal loop exits without the dialog having
  // been closed via end_dialog(). Make sure we destroy it here so stack-owned
  // dialog state does not outlive the call site.
  if (is_window(dlg))
    destroy_window(dlg);
  if (modal_parent) {
    enable_window(modal_parent, true);
    set_focus(modal_parent);
    g_ui_runtime.modal_overlay_parent = prev_modal_overlay_parent;
  }
  // Force a clean stencil rebuild + full repaint to clear the area the dialog
  // occupied.  destroy_window's recursive child teardown re-queues
  // evRefreshStencil *after* any already-pending paint messages
  // (a dedup side-effect), so the repaint inside the loop above may paint
  // against a stale stencil, leaving a ghost.  Posting a fresh RefreshStencil
  // here, after all dialog windows are gone, guarantees correct ordering.
  if (g_ui_runtime.running) {
    post_message((window_t*)1, evRefreshStencil, 0, NULL);
    for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
      if (window_has_state(w, WINDOW_STATE_VISIBLE)) invalidate_window(w);
    }
    repost_messages();
  }
  return result;
}

uint32_t show_dialog_ex(char const *title,
                        int width,
                        int height,
                        window_t *parent,
                        flags_t flags,
                        winproc_t proc,
                        void *param)
{
  const char *dialog_title = title ? title : "";
  irect16_t dlg_frame = {0, 0, width, height};
  dlg_frame = center_window_rect(dlg_frame, parent);
  // Dialogs inherit their owner's hinstance so they belong to the same app.
  hinstance_t hinstance = parent ? get_root_window(parent)->hinstance : 0;
  window_t *dlg = create_window_proc(dialog_title, flags, &dlg_frame, NULL, proc, hinstance, param);
  return run_dialog_loop(dlg, parent);
}

uint32_t show_dialog(char const *title,
                     int width,
                     int height,
                     window_t *parent,
                     winproc_t proc,
                     void *param)
{
  return show_dialog_ex(title, width, height, parent,
                        WINDOW_VSCROLL | WINDOW_DIALOG | WINDOW_NOTRAYBUTTON,
                        proc, param);
}

// Show a modal dialog whose layout is described by a form_def_t.
// Children are instantiated (via create_window_from_form) before
// evCreate fires, so the window proc can find them already in
// place — analogous to WinAPI DialogBoxIndirectParam.
// title overrides def->name when non-NULL; like def->name it is source text,
// translated through the string catalogs when the form is a generated one
// (def->id set).  The dialog is centered on owner.
uint32_t show_dialog_from_form_ex(form_def_t const *def, char const *title,
                                  window_t *parent, flags_t flags,
                                  winproc_t proc, void *param)
{
  if (!def || !proc) return 0;

  // Merge dialog-specific flags into a local copy of the definition.
  form_def_t dlg_def = *def;
  dlg_def.flags |= flags;
  if (title) dlg_def.name = title;

  // For auto-height: create at 0,0, measure after children exist, then reposition
  bool need_auto_height = (dlg_def.height == 0 && (dlg_def.flags & WINDOW_AUTO_LAYOUT) && 
                           !form_has_flexspace(&dlg_def));

  // Use specified height or a temporary value for initial creation
  int initial_height = dlg_def.height > 0 ? dlg_def.height : 100;

  // Dialogs inherit their owner's hinstance so they belong to the same app.
  hinstance_t hinstance = parent ? get_root_window(parent)->hinstance : 0;
  
  // Create dialog at 0,0 initially
  irect16_t wr = {0, 0, dlg_def.width, initial_height};
  adjust_window_rect(&wr, dlg_def.flags);
  dlg_def.width = wr.w;
  dlg_def.height = wr.h;

  window_t *dlg = create_window_from_form(&dlg_def, 0, 0,
                                          NULL, proc, hinstance, param);
  if (!dlg) return 0;

  // If auto-height is needed, measure the actual size and reposition
  if (need_auto_height) {
    layout_measure_t measure = {0};
    irect16_t cr = get_client_rect(dlg);
    measure.avail_w = cr.w;
    measure.avail_h = 0;  // Unconstrained height
    send_message(dlg, evMeasure, 0, &measure);
    
    // Add chrome (title bar, borders) to get window rect
    irect16_t new_wr = {0, 0, measure.desired_w, measure.desired_h};
    adjust_window_rect(&new_wr, dlg->flags);
    
    // Update window size
    dlg->frame.w = new_wr.w;
    dlg->frame.h = new_wr.h;
    
    // Recalculate layout with new size
    window_layout_sync(dlg);
  }

  // Center the dialog (now with correct size)
  irect16_t centered = center_window_rect((irect16_t){0, 0, dlg->frame.w, dlg->frame.h}, parent);
  dlg->frame.x = centered.x;
  dlg->frame.y = centered.y;

  return run_dialog_loop(dlg, parent);
}

uint32_t show_dialog_from_form(form_def_t const *def, char const *title,
                               window_t *parent, winproc_t proc, void *param)
{
  return show_dialog_from_form_ex(def, title, parent,
                                  WINDOW_VSCROLL | WINDOW_DIALOG | WINDOW_NOTRAYBUTTON,
                                  proc, param);
}

// ── Generic DDX dialog proc ───────────────────────────────────────────────
// Used by show_ddx_dialog().  The lparam passed to evCreate is a pointer to a
// ddx_dlg_ctx_t that lives on show_ddx_dialog()'s stack for the lifetime of
// the modal loop (safe because show_dialog_from_form_ex blocks until close).

typedef struct {
  form_def_t const *def;   // form definition carrying bindings + ok/cancel IDs
  void             *state; // caller-supplied state for push/pull
} ddx_dlg_ctx_t;

static result_t dialog_ddx_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
  ddx_dlg_ctx_t *ctx = (ddx_dlg_ctx_t *)win->userdata;

  switch (msg) {
    case evCreate: {
      ctx = (ddx_dlg_ctx_t *)lparam;
      win->userdata = ctx;
      if (ctx->def->bindings && ctx->def->binding_count > 0)
        dialog_push(win, ctx->state,
                    ctx->def->bindings, ctx->def->binding_count);
      return true;
    }

    case evCommand: {
      if (!ctx) return false;
      uint16_t notif = HIWORD(wparam);

      // Pressing Enter (or Tab) in a single-line edit box fires edUpdate.
      // Treat this as equivalent to clicking the OK button.
      if (notif == edUpdate) {
        if (ctx->def->bindings)
          dialog_pull(win, ctx->state,
                      ctx->def->bindings, ctx->def->binding_count);
        end_dialog(win, 1);
        return true;
      }

      if (notif != btnClicked) return false;
      window_t *src = (window_t *)lparam;
      if (!src) return false;

      if (ctx->def->ok_id && src->id == ctx->def->ok_id) {
        if (ctx->def->bindings)
          dialog_pull(win, ctx->state,
                      ctx->def->bindings, ctx->def->binding_count);
        end_dialog(win, 1);
        return true;
      }
      if (ctx->def->cancel_id && src->id == ctx->def->cancel_id) {
        end_dialog(win, 0);
        return true;
      }
      return false;
    }

    default:
      return false;
  }
}

uint32_t show_ddx_dialog(form_def_t const *def, const char *title,
                         window_t *parent, void *state) {
  if (!def) return 0;
  ddx_dlg_ctx_t ctx = { .def = def, .state = state };
  return show_dialog_from_form_ex(def, title, parent,
                                  WINDOW_VSCROLL | WINDOW_DIALOG | WINDOW_NOTRAYBUTTON,
                                  dialog_ddx_proc, &ctx);
}
// win may be any window inside the dialog (e.g. a button); get_root_window
// walks up to the dialog window that owns the result pointer.
void end_dialog(window_t *win, uint32_t code) {
  window_t *root = get_root_window(win);
  if (root->userdata2) {
    *(uint32_t *)root->userdata2 = code;
  }
  destroy_window(root);
}

// ── Dialog Data Exchange (DDX) ────────────────────────────────────────────
// Push state → controls (populate on open); pull controls → state (read on OK).

void ddx_push_int(window_t *dlg, const ctrl_binding_t *b, const void *state) {
  const char *base = (const char *)state;
  int v = *(const int *)(base + b->offset);
  set_window_item_text(dlg, b->ctrl_id, "%d", v);
}

void ddx_pull_int(window_t *dlg, const ctrl_binding_t *b, void *state) {
  char *base = (char *)state;
  window_t *ctrl = get_window_item(dlg, b->ctrl_id);
  int v;
  if (!ctrl) return;
  v = atoi(ctrl->title);
  *(int *)(base + b->offset) = v;
}

void ddx_push_float(window_t *dlg, const ctrl_binding_t *b, const void *state) {
  const char *base = (const char *)state;
  float v = *(const float *)(base + b->offset);
  set_window_item_text(dlg, b->ctrl_id, "%.2f", v);
}

void ddx_pull_float(window_t *dlg, const ctrl_binding_t *b, void *state) {
  char *base = (char *)state;
  window_t *ctrl = get_window_item(dlg, b->ctrl_id);
  float v;
  if (!ctrl) return;
  v = strtof(ctrl->title, NULL);
  *(float *)(base + b->offset) = v;
}

void ddx_push_double(window_t *dlg, const ctrl_binding_t *b, const void *state) {
  double v = *(const double *)((const char *)state + b->offset), scale = 100.0;
  int decimals = 2;  // money shows cents; fractions keep only the digits they need
  while (decimals < 6 && v * scale != (double)(long long)(v * scale)) { decimals++; scale *= 10.0; }
  char text[64];
  set_window_item_text(dlg, b->ctrl_id, "%s", ui_format_number(v, decimals, text, sizeof(text)));
}

// Dates are stored as ISO "YYYY-MM-DD" and edited in the catalog's format.
void ddx_push_date(window_t *dlg, const ctrl_binding_t *b, const void *state) {
  char text[32];
  set_window_item_text(dlg, b->ctrl_id, "%s", ui_format_date((const char *)state + b->offset, text, sizeof(text)));
}

void ddx_pull_date(window_t *dlg, const ctrl_binding_t *b, void *state) {
  window_t *ctrl = get_window_item(dlg, b->ctrl_id);
  char iso[16];
  if (!ctrl) return;
  if (ui_parse_date(ctrl->title, iso, sizeof(iso)))
    snprintf((char *)state + b->offset, b->wparam ? b->wparam : 11, "%s", iso);
  else if (ctrl->title[0]) {
    fprintf(stderr, "[dialog] date rejected ctrl=%u text='%s'; keeping the previous value\n", b->ctrl_id, ctrl->title);
    fflush(stderr);
  }
}

// Accepts the catalog's number format ("1 234,50") as well as "1234.50".
void ddx_pull_double(window_t *dlg, const ctrl_binding_t *b, void *state) {
  window_t *ctrl = get_window_item(dlg, b->ctrl_id);
  if (ctrl) *(double *)((char *)state + b->offset) = ui_parse_number(ctrl->title);
}

void ddx_push_combo_value(window_t *dlg, const ctrl_binding_t *b, const void *state) {
  window_t *ctrl = get_window_item(dlg, b->ctrl_id);
  if (ctrl) send_message(ctrl, cbSetCurrentValue, (uint32_t)*(const int *)((const char *)state + b->offset), NULL);
}

void ddx_pull_combo_value(window_t *dlg, const ctrl_binding_t *b, void *state) {
  window_t *ctrl = get_window_item(dlg, b->ctrl_id);
  int v = ctrl ? (int)send_message(ctrl, cbGetCurrentValue, 0, NULL) : 0;
  *(int *)((char *)state + b->offset) = v == kComboBoxError ? 0 : v;
}

void ddx_push_u8(window_t *dlg, const ctrl_binding_t *b, const void *state) {
  const char *base = (const char *)state;
  uint8_t v = *(const uint8_t *)(base + b->offset);
  set_window_item_text(dlg, b->ctrl_id, "%u", (unsigned)v);
}

void ddx_pull_u8(window_t *dlg, const ctrl_binding_t *b, void *state) {
  char *base = (char *)state;
  window_t *ctrl = get_window_item(dlg, b->ctrl_id);
  int v;
  if (!ctrl) return;
  v = atoi(ctrl->title);
  *(uint8_t *)(base + b->offset) = (uint8_t)CLAMP(v, 0, 255);
}

void ddx_push_text(window_t *dlg, const ctrl_binding_t *b, const void *state) {
  const char *base = (const char *)state;
  window_t *ctrl = get_window_item(dlg, b->ctrl_id);
  if (!ctrl) return;
  send_message(ctrl, edSetText, 0, (void *)(base + b->offset));
}

void ddx_pull_text(window_t *dlg, const ctrl_binding_t *b, void *state) {
  char *base = (char *)state;
  window_t *ctrl = get_window_item(dlg, b->ctrl_id);
  if (!ctrl || b->wparam == 0) return;
  send_message(ctrl, edGetText, (uint32_t)b->wparam, base + b->offset);
}

void ddx_push_combo(window_t *dlg, const ctrl_binding_t *b, const void *state) {
  const char *base = (const char *)state;
  window_t *ctrl = get_window_item(dlg, b->ctrl_id);
  int v;
  if (!ctrl) return;
  v = *(const int *)(base + b->offset);
  if (v < 0) v = (int)b->wparam;
  send_message(ctrl, cbSetCurrentSelection, (uint32_t)v, NULL);
}

void ddx_pull_combo(window_t *dlg, const ctrl_binding_t *b, void *state) {
  char *base = (char *)state;
  window_t *ctrl = get_window_item(dlg, b->ctrl_id);
  if (!ctrl) return;
  *(int32_t *)(base + b->offset) = (int32_t)send_message(ctrl, cbGetCurrentSelection, 0, NULL);
}

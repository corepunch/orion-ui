// A page: an add-on's form and the Lua table that drives it. The host asks
// the page for data() and shows it, runs a button's action= as a method of
// the page with the typed values of the form's inputs, then asks again.

#include "script.h"
#include "script_internal.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <orion/commctl/commctl.h>
#include <orion/commctl/form_loader.h>
#include <orion/commdlg/msgbox.h>
#include <orion/user/i18n.h>
#include <orion/user/gdi.h>
#include <platform/platform.h>
#include <orion/user/display_group.h>

#define SCRIPT_MAX_DATABASES 8
#define SCRIPT_MAX_SPRITES 8

typedef struct {
  uint32_t control, timer;
  bitmap_t *bitmap;
  char path[1024];
  int columns, rows, count, frame, interval, revision;
  bool loop;
} page_sprite_t;

struct script_page_s {
  struct script_page_s *next;
  int handle;
  bool borrowed_library;
  winproc_t hook;
  char stem[1024], dir[1024], name[128];
  form_library_t *lib;
  const form_def_t *form;
  database_t *dbs[SCRIPT_MAX_DATABASES];
  int db_count;
  lua_State *L;
  script_runtime_t *runtime;
  int page_ref;
  uint32_t focus;
  window_t *win;
  bool modal, owns_window, closing;
  int result;
  char title[256];
  char message[1024];
  page_sprite_t sprites[SCRIPT_MAX_SPRITES];
};

static script_page_t *live_pages;
static int next_page_handle;

static void page_register(script_page_t *page) {
  script_runtime_page_retain(page->runtime);
  page->handle = ++next_page_handle; page->next = live_pages; live_pages = page;
}

static void page_log(const script_page_t *page, const char *what, const char *detail) {
  fprintf(stderr, "[script] %s: %s%s%s\n", page->name, what, detail && *detail ? ": " : "", detail ? detail : "");
  fflush(stderr);
}

static bool file_exists(const char *path) {
  FILE *f = fopen(path, "rb");
  if (f) fclose(f);
  return f != NULL;
}

static const char *control_attr(const script_page_t *page, uint32_t id, const char *attr) {
  return form_library_control_attr(page->lib, page->form, id, attr);
}

static bool class_is(const form_ctrl_def_t *d, const char *klass) { return d && !strcasecmp(d->class_name, klass); }
static bool is_text_input(const form_ctrl_def_t *d) {
  return class_is(d, "TextBox") || class_is(d, "TextEdit") || class_is(d, "MultiEdit");
}

// ── Values on the form ──────────────────────────────────────────────────────

// type="integer|number|date" on an input says how its text converts;
// decimals="N" how a number shows.
static void format_value(const script_page_t *page, uint32_t id, lua_State *L, int idx, char *buf, size_t sz) {
  const char *type = control_attr(page, id, "type"), *decimals = control_attr(page, id, "decimals");
  if (lua_isnil(L, idx)) { buf[0] = 0; return; }
  if (lua_type(L, idx) == LUA_TNUMBER) {
    if (type && !strcmp(type, "integer")) { snprintf(buf, sz, "%lld", (long long)llround(lua_tonumber(L, idx))); return; }
    if (lua_isinteger(L, idx) && !decimals && !(type && !strcmp(type, "number"))) { snprintf(buf, sz, "%lld", (long long)lua_tointeger(L, idx)); return; }
    ui_format_number(lua_tonumber(L, idx), decimals ? atoi(decimals) : 2, buf, sz);
    return;
  }
  const char *s = luaL_tolstring(L, idx, NULL);
  if (type && !strcmp(type, "date")) ui_format_date(s, buf, sz);
  else snprintf(buf, sz, "%s", s);
  lua_pop(L, 1);
}

static database_t *registered_database(const char *name);
static bool source_parts(const form_ctrl_def_t *d, char *db_name, size_t db_sz, char *table, size_t table_sz);

static void set_value(script_page_t *page, const form_ctrl_def_t *d, window_t *ctrl, lua_State *L, int idx) {
  if (class_is(d, "CheckBox")) { send_message(ctrl, btnSetCheck, lua_toboolean(L, idx), NULL); return; }
  if (class_is(d, "ComboBox") && d->lparam) {
    // A page action can create a record used by this selector. Refresh through
    // evSetDatabase to retain the control's declared filter and field mapping.
    char database[64], table[128];
    database_t *db = source_parts(d, database, sizeof(database), table, sizeof(table)) ? registered_database(database) : NULL;
    if (db) send_message(ctrl, evSetDatabase, 0, db);
    send_message(ctrl, cbSetCurrentValue, (uint32_t)(lua_isinteger(L, idx) ? lua_tointeger(L, idx) : 0), NULL);
    return;
  }
  if (class_is(d, "ComboBox")) {
    send_message(ctrl, cbSetCurrentSelection, (uint32_t)(lua_isinteger(L, idx) ? lua_tointeger(L, idx) - 1 : -1), NULL);
    return;
  }
  char text[1024];
  format_value(page, d->id, L, idx, text, sizeof(text));
  if (is_text_input(d)) send_message(ctrl, edSetText, 0, text);
  else set_window_title(ctrl, text);
}

static database_t *page_database(const script_page_t *page, const char *name) {
  for (int i = 0; i < page->db_count; i++)
    if (!strcmp(page->dbs[i]->name, name)) return page->dbs[i];
  return NULL;
}

static database_t *registered_database(const char *name) {
  database_t *db = NULL;
  for (int i = 0; (db = get_database_at(i)) != NULL; i++)
    if (!strcmp(db->name, name)) return db;
  return NULL;
}

// "db.table" of a control's source=, split; false without one.
static bool source_parts(const form_ctrl_def_t *d, char *db_name, size_t db_sz, char *table, size_t table_sz) {
  const char *dot = d && d->source ? strchr(d->source, '.') : NULL;
  if (!dot) return false;
  snprintf(db_name, db_sz, "%.*s", (int)(dot - d->source), d->source);
  snprintf(table, table_sz, "%s", dot + 1);
  return true;
}

static void clear_table(database_t *db, const db_table_schema_t *t) {
  int key = 0;
  for (int i = 0; i < t->field_count; i++) if (t->fields[i].primary_key) key = i;
  for (;;) {
    result_node_t *list = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(t->table_id, 0), NULL);
    if (!list) return;
    int count = 0;
    for (result_node_t *node = list; node; node = (result_node_t *)node->next) count++;
    int *ids = malloc((size_t)count * sizeof(*ids));
    if (!ids) { free_result_list(list); return; }
    int n = 0;
    for (result_node_t *node = list; node; node = (result_node_t *)node->next)
      ids[n++] = *(const int *)((const char *)*(void **)node->data + t->meta[key].offset);
    free_result_list(list);
    for (int i = 0; i < n; i++) send_db_message(db, dbDelete, (uint32_t)t->table_id, (void *)(intptr_t)ids[i]);
    free(ids);
  }
}

// lists = {name = rows}: a TableView over one of the add-on's own tables
// shows exactly these rows.
static void fill_list(script_page_t *page, const form_ctrl_def_t *d, window_t *ctrl, lua_State *L, int idx) {
  char db_name[64], table[128];
  if (!class_is(d, "TableView") || !source_parts(d, db_name, sizeof(db_name), table, sizeof(table)))
    luaL_error(L, "lists.%s: not a TableView with source=\"db.table\"", d->name);
  database_t *db = page_database(page, db_name);
  if (!db) luaL_error(L, "lists.%s: '%s' is not one of the add-on's own databases", d->name, db_name);
  const db_table_schema_t *t = db_schema_table_named(db->schema, table);
  if (!t) luaL_error(L, "lists.%s: no table '%s'", d->name, table);
  luaL_checktype(L, idx, LUA_TTABLE);
  clear_table(db, t);
  // The record buffer is a userdata, so a Lua error below cannot leak it.
  void *buf = lua_newuserdatauv(L, t->record_size, 0);
  char msg[256];
  for (lua_Integer i = 1, count = (lua_Integer)luaL_len(L, idx); i <= count; i++) {
    lua_geti(L, idx, i);
    memset(buf, 0, t->record_size);
    send_db_message(db, dbInitRecord, (uint32_t)t->table_id, buf);
    if (!script_read_record(L, -1, t, buf, msg, sizeof(msg))) luaL_error(L, "lists.%s, row %d: %s", d->name, (int)i, msg);
    lua_pop(L, 1);
    send_db_message(db, dbInsert, (uint32_t)t->table_id, buf);
  }
  lua_pop(L, 1);
  send_message(ctrl, tvRefresh, 0, NULL);
}

static void each_named(script_page_t *page, lua_State *L, int idx, const char *what,
                       void (*apply)(script_page_t *, const form_ctrl_def_t *, window_t *, lua_State *, int)) {
  lua_getfield(L, idx, what);
  if (lua_istable(L, -1)) {
    lua_pushnil(L);
    while (lua_next(L, -2)) {
      const char *name = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : NULL;
      uint32_t id = name ? form_library_control_id(page->lib, page->form, name) : 0;
      window_t *ctrl = id && page->win ? get_window_item(page->win, id) : NULL;
      if (!ctrl) luaL_error(L, "%s.%s: the form has no control named '%s'", what, name ? name : "?", name ? name : "?");
      apply(page, form_library_control(page->lib, page->form, id), ctrl, L, lua_gettop(L));
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
}

static void apply_hidden(script_page_t *page, const form_ctrl_def_t *d, window_t *ctrl, lua_State *L, int idx) {
  (void)page; (void)d;
  show_window(ctrl, !lua_toboolean(L, idx));
}
static void apply_disabled(script_page_t *page, const form_ctrl_def_t *d, window_t *ctrl, lua_State *L, int idx) {
  (void)page; (void)d;
  enable_window(ctrl, !lua_toboolean(L, idx));
}

// placeholders = { field = "Show debtors" }: the hint a TextBox shows while empty.
static void apply_placeholder(script_page_t *page, const form_ctrl_def_t *d, window_t *ctrl, lua_State *L, int idx) {
  (void)page;
  if (!class_is(d, "TextBox") && !class_is(d, "TextEdit")) { luaL_error(L, "placeholders.%s: not a TextBox", d->name); return; }
  send_message(ctrl, edSetPlaceholder, 0, (void *)luaL_checkstring(L, idx));
}

// Lua selects the clip; the host owns bitmap lifetimes and frame timers.
static void sprite_draw(script_page_t *page, page_sprite_t *s) {
  window_t *ctrl = page->win ? get_window_item(page->win, s->control) : NULL;
  if (!ctrl || !s->bitmap) return;
  isize16_t size = bitmap_size(s->bitmap);
  int w = size.w / s->columns, h = size.h / s->rows;
  irect16_t src = R((s->frame % s->columns) * w, (s->frame / s->columns) * h, w, h);
  sprite_button_t def = {.bm = s->bitmap, .up = src, .down = src};
  send_message(ctrl, spbSetSprites, 0, &def);
}

static void sprite_clear(script_page_t *page) {
  for (int i = 0; i < SCRIPT_MAX_SPRITES; i++) {
    page_sprite_t *s = &page->sprites[i];
    if (s->timer) axCancelTimer(s->timer);
    window_t *ctrl = page->win && s->control ? get_window_item(page->win, s->control) : NULL;
    if (ctrl) { sprite_button_t empty = {0}; send_message(ctrl, spbSetSprites, 0, &empty); }
    bitmap_free(s->bitmap);
    memset(s, 0, sizeof(*s));
  }
}

static int sprite_integer(lua_State *L, int idx, const char *key, int fallback, int min, int max) {
  lua_getfield(L, idx, key);
  lua_Integer value = lua_isnil(L, -1) ? fallback : luaL_checkinteger(L, -1);
  lua_pop(L, 1);
  if (value < min || value > max) luaL_error(L, "sprite %s out of range", key);
  return (int)value;
}

static void set_sprite(script_page_t *page, const form_ctrl_def_t *d, window_t *ctrl, lua_State *L, int idx) {
  (void)ctrl;
  if (!class_is(d, "SpriteButton")) { luaL_error(L, "sprites need a SpriteButton"); return; }
  luaL_checktype(L, idx, LUA_TTABLE);
  lua_getfield(L, idx, "path");
  const char *path = luaL_checkstring(L, -1);
  if (!*path || path[0] == '/' || strchr(path, '\\') || strchr(path, ':') || strstr(path, "..")) {
    luaL_error(L, "sprite path must be relative to the add-on"); return;
  }
  char full[1024];
  if (snprintf(full, sizeof(full), "%s/%s", page->dir, path) >= (int)sizeof(full)) {
    luaL_error(L, "sprite path too long"); return;
  }
  lua_pop(L, 1);
  int columns = sprite_integer(L, idx, "columns", 1, 1, 64);
  int rows = sprite_integer(L, idx, "rows", 1, 1, 64);
  int count = sprite_integer(L, idx, "count", columns * rows, 1, columns * rows);
  int interval = sprite_integer(L, idx, "interval", 125, 16, 60000);
  int revision = sprite_integer(L, idx, "revision", 0, 0, 2147483647);
  lua_getfield(L, idx, "loop"); bool loop = lua_toboolean(L, -1); lua_pop(L, 1);
  page_sprite_t *s = NULL;
  for (int i = 0; i < SCRIPT_MAX_SPRITES; i++)
    if (page->sprites[i].control == d->id) { s = &page->sprites[i]; break; }
  if (!s) for (int i = 0; i < SCRIPT_MAX_SPRITES; i++)
    if (!page->sprites[i].control) { s = &page->sprites[i]; break; }
  if (!s) { luaL_error(L, "too many animated controls"); return; }
  if (!strcmp(s->path, full) && s->columns == columns && s->rows == rows && s->count == count && s->interval == interval && s->loop == loop && s->revision == revision) return;
  bitmap_t *bitmap = bitmap_load(full);
  if (!bitmap) { luaL_error(L, "cannot load sprite %s", full); return; }
  isize16_t size = bitmap_size(bitmap);
  if (size.w <= 0 || size.h <= 0 || size.w % columns || size.h % rows) {
    bitmap_free(bitmap); luaL_error(L, "sprite dimensions do not match grid"); return;
  }
  if (s->timer) axCancelTimer(s->timer);
  bitmap_free(s->bitmap);
  *s = (page_sprite_t){.control=d->id, .bitmap=bitmap, .columns=columns, .rows=rows,
                       .count=count, .interval=interval, .loop=loop, .revision=revision};
  snprintf(s->path, sizeof(s->path), "%s", full);
  sprite_draw(page, s);
  if (count > 1) s->timer = axSetTimer(page->win, (uint32_t)interval, NULL, true);
}

// Runs inside script_call: (page) -> applies page:data().
static int render_data(lua_State *L) {
  script_page_t *page = (script_page_t *)lua_touserdata(L, lua_upvalueindex(1));
  lua_getfield(L, 1, "data");
  if (!lua_isfunction(L, -1)) return 0;
  lua_pushvalue(L, 1);
  lua_call(L, 1, 1);
  if (lua_isnil(L, -1)) return 0;
  luaL_checktype(L, -1, LUA_TTABLE);
  int data = lua_gettop(L);
  each_named(page, L, data, "values", set_value);
  each_named(page, L, data, "lists", fill_list);
  each_named(page, L, data, "hidden", apply_hidden);
  each_named(page, L, data, "disabled", apply_disabled);
  each_named(page, L, data, "placeholders", apply_placeholder);
  each_named(page, L, data, "sprites", set_sprite);
  lua_getfield(L, data, "title");
  if (lua_isstring(L, -1)) set_window_title(page->win, lua_tostring(L, -1));
  return 0;
}

// A control named "error" shows the message in place; otherwise a modal page
// shows a message box.
static window_t *error_label(const script_page_t *page) {
  uint32_t id = page->win ? form_library_control_id(page->lib, page->form, "error") : 0;
  return id ? get_window_item(page->win, id) : NULL;
}

static void show_message(script_page_t *page) {
  window_t *label = error_label(page);
  if (label) set_window_title(label, page->message);
  if (!page->message[0] || !page->win) return;
  if (!label && page->modal) {
    message_box(page->win, page->message, script_page_title(page), MB_OK);
  }
}

static void render(script_page_t *page) {
  if (!page->win) return;
  if (page->form->bindings && page->form->db_name) {
    database_t *db = registered_database(page->form->db_name);
    if (db && !strcmp(db->class_name, "db_display_group")) {
      result_node_t *rows = (void *)send_db_message(db, dbFetch, page->form->db_table_id, NULL);
      void *empty = rows ? NULL : calloc(1, page->form->db_record_size);
      dialog_push(page->win, rows ? *(void **)rows->data : empty, page->form->bindings, page->form->binding_count);
      free(empty); free_result_list(rows);
    }
  }
  if (!page->L) return;
  lua_State *L = page->L;
  lua_pushlightuserdata(L, page);
  lua_pushcclosure(L, render_data, 1);
  lua_rawgeti(L, LUA_REGISTRYINDEX, page->page_ref);
  char msg[1024];
  bool refused = false;
  const char *outer = ui_set_string_context(page->name);
  bool ok = script_call(L, 1, 0, msg, sizeof(msg), &refused);
  ui_set_string_context(outer);
  if (!ok) {
    if (refused) snprintf(page->message, sizeof(page->message), "%s", msg);
    else {
      page_log(page, "data() failed", msg);
      snprintf(page->message, sizeof(page->message), ui_tr("The add-on stopped with an error: %s"), strtok(msg, "\n"));
    }
    show_message(page);
  }
  if (page->owns_window && !page->form->height) {
    isize16_t size = script_page_preferred_size(page);
    if (size.w != page->win->frame.w || size.h != page->win->frame.h)
      resize_window(page->win, size.w, size.h);
  }
  window_layout_sync(page->win);
  if (page->focus) {
    window_t *ctrl = get_window_item(page->win, page->focus);
    page->focus = 0;
    if (ctrl) set_focus(ctrl);
  }
  invalidate_window(page->win);
}

// ── Inputs ──────────────────────────────────────────────────────────────────

static void push_input(script_page_t *page, const form_ctrl_def_t *d, window_t *ctrl) {
  lua_State *L = page->L;
  if (class_is(d, "CheckBox")) { lua_pushboolean(L, send_message(ctrl, btnGetCheck, 0, NULL) != 0); return; }
  if (class_is(d, "ComboBox") && d->lparam) {
    int value = (int)send_message(ctrl, cbGetCurrentValue, 0, NULL);
    if (value) lua_pushinteger(L, value); else lua_pushnil(L);
    return;
  }
  if (class_is(d, "ComboBox")) {
    int index = (int)send_message(ctrl, cbGetCurrentSelection, 0, NULL);
    if (index >= 0) lua_pushinteger(L, index + 1); else lua_pushnil(L);
    return;
  }
  if (class_is(d, "TableView")) {
    // The selected row, or nil.
    const void *record = (const void *)(intptr_t)send_message(ctrl, tvGetSelectedRecord, 0, NULL);
    char db_name[64], table[128];
    database_t *db = source_parts(d, db_name, sizeof(db_name), table, sizeof(table)) ? registered_database(db_name) : NULL;
    const db_table_schema_t *t = record && db && db->schema ? db_schema_table_named(db->schema, table) : NULL;
    if (t) script_push_record(L, t, record); else lua_pushnil(L);
    return;
  }
  char text[1024] = {0};
  send_message(ctrl, edGetText, sizeof(text), text);
  const char *type = control_attr(page, d->id, "type");
  const char *s = text;
  while (isspace((unsigned char)*s)) s++;
  if (type && (!strcmp(type, "integer") || !strcmp(type, "number"))) {
    if (!*s) { lua_pushnil(L); return; }
    double v = ui_parse_number(s);
    if (!strcmp(type, "integer")) lua_pushinteger(L, (lua_Integer)llround(v)); else lua_pushnumber(L, v);
    return;
  }
  if (type && !strcmp(type, "date")) {
    char iso[16];
    if (*s && ui_parse_date(s, iso, sizeof(iso))) lua_pushstring(L, iso); else lua_pushnil(L);
    return;
  }
  lua_pushstring(L, text);
}

// The typed values of every named input, as an action's argument.
static void push_inputs(script_page_t *page) {
  lua_State *L = page->L;
  lua_newtable(L);
  for (int i = 0; page->win && i < page->form->child_count; i++) {
    const form_ctrl_def_t *d = &page->form->children[i];
    if (!d->name || !*d->name) continue;
    if (!is_text_input(d) && !class_is(d, "CheckBox") && !class_is(d, "ComboBox") && !class_is(d, "TableView")) continue;
    window_t *ctrl = get_window_item(page->win, d->id);
    if (!ctrl) continue;
    push_input(page, d, ctrl);
    lua_setfield(L, -2, d->name);
  }
}

// ── Actions ─────────────────────────────────────────────────────────────────

static void finish(script_page_t *page) {
  if (page->closing && page->win && page->modal) end_dialog(page->win, (uint32_t)page->result);
  else if (page->closing && page->owns_window && page->win) destroy_window(page->win);
  else if (!page->closing) render(page);
  show_message(page);
}

// `control` names the button that ran the action (nil for a host request), so
// one action can serve several buttons, as Balloon.Show reports its label.
static bool perform(script_page_t *page, const char *action, const char *control) {
  if (!page || !page->L || !action || !*action) return false;
  lua_State *L = page->L;
  page->message[0] = 0;

  lua_rawgeti(L, LUA_REGISTRYINDEX, page->page_ref);
  lua_getfield(L, -1, action);
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2);
    page_log(page, "no such action", action);
    snprintf(page->message, sizeof(page->message), ui_tr("The add-on has no action '%s'."), action);
    show_message(page);
    return false;
  }
  lua_insert(L, -2);           // action, page
  push_inputs(page);           // action, page, values
  if (control && *control) lua_pushstring(L, control);
  else lua_pushnil(L);         // action, page, values, control
  char msg[1024];
  bool refused = false;
  const char *outer = ui_set_string_context(page->name);
  bool ok = script_call(L, 3, 2, msg, sizeof(msg), &refused);
  ui_set_string_context(outer);
  if (ok) {
    // An action may answer nil, "message" for a refusal it expected.
    if (lua_isnil(L, -2) && lua_isstring(L, -1)) {
      snprintf(page->message, sizeof(page->message), "%s", lua_tostring(L, -1));
      ok = false;
    }
    lua_pop(L, 2);
  } else if (refused) {
    snprintf(page->message, sizeof(page->message), "%s", msg);
  } else {
    page_log(page, action, msg);
    snprintf(page->message, sizeof(page->message), ui_tr("The add-on stopped with an error: %s"), strtok(msg, "\n"));
  }
  finish(page);
  return ok;
}

bool script_page_perform(script_page_t *page, const char *action) {
  return perform(page, action, NULL);
}

bool script_page_perform_from(script_page_t *page, const char *action, const char *control) {
  return perform(page, action, control);
}

// ── self.app: what the window offers the page ───────────────────────────────

static script_page_t *page_for_handle(lua_State *L, int handle) {
  lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
  lua_State *main = lua_tothread(L, -1); lua_pop(L, 1);
  for (script_page_t *page = live_pages; page; page = page->next)
    if (page->handle == handle && page->L == main) return page;
  luaL_error(L, "window handle %d is closed or belongs to another application", handle);
  return NULL;
}
static window_t *page_named_control(lua_State *L, int handle, const char *name) {
  window_t *control = script_page_control(page_for_handle(L, handle), name);
  if (!control) luaL_error(L, "unknown or closed window control '%s'", name);
  return control;
}
static void page_window_close(lua_State *L, int handle, int result) {
  script_page_t *page = page_for_handle(L, handle);
  page->closing = true; page->result = result;
}
static void page_window_focus(lua_State *L, int handle, const char *control) {
  script_page_t *page = page_for_handle(L, handle);
  page->focus = page_named_control(L, handle, control)->id;
}
static const char *page_window_text(lua_State *L, int handle, const char *control) {
  return page_named_control(L, handle, control)->title;
}
static void page_window_set_text(lua_State *L, int handle, const char *control, const char *text) {
  script_page_t *page = page_for_handle(L, handle);
  window_t *window = page_named_control(L, handle, control);
  const form_ctrl_def_t *def = form_library_control(page->lib, page->form, window->id);
  if (is_text_input(def)) send_message(window, edSetText, 0, (void *)text);
  else set_window_title(window, text);
}
static void page_window_show(lua_State *L, int handle, const char *control, bool visible) {
  show_window(page_named_control(L, handle, control), visible);
}
static void page_window_enable(lua_State *L, int handle, const char *control, bool enabled) {
  window_t *window = page_named_control(L, handle, control);
  enable_window(window, enabled);
}
static void page_window_refresh(lua_State *L, int handle) { (void)page_for_handle(L, handle); }
#include "build/generated/orion/script/window_lua.h"

static void push_app(script_page_t *page) {
  lua_State *L = page->L;
  lua_getglobal(L, "require"); lua_pushliteral(L, "orion.window"); lua_call(L, 1, 1);
  lua_getfield(L, -1, "new"); lua_pushinteger(L, page->handle); lua_call(L, 1, 1);
  lua_remove(L, -2);
}

// ── Window ──────────────────────────────────────────────────────────────────

static result_t script_page_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  script_page_t *page = (script_page_t *)win->userdata;
  if (page && msg != evCreate && msg != evDestroy && page->hook && page->hook(win, msg, wparam, lparam))
    return true;
  switch (msg) {
    case evCreate:
      page = (script_page_t *)lparam;
      win->userdata = page;
      page->win = win;
      render(page);
      if (page->hook) page->hook(win, msg, wparam, lparam);
      return true;
    case evCommand: {
      if (page && HIWORD(wparam) == RVN_SELCHANGE && lparam) {
        window_t *control = lparam;
        const form_ctrl_def_t *def = form_library_control(page->lib, page->form, control->id);
        char database[64], table[128];
        if (source_parts(def, database, sizeof(database), table, sizeof(table))) {
          database_t *db = registered_database(database);
          if (db && !strcmp(db->class_name, "db_display_group")) {
            const void *row = (void *)(intptr_t)send_message(control, tvGetSelectedRecord, 0, NULL);
            const db_table_schema_t *schema = db_schema_table_named(db->schema, table);
            int key = 0;
            for (int i = 0; row && schema && i < schema->field_count; i++) if (schema->fields[i].primary_key) key = *(int *)((const char *)row + schema->meta[i].offset);
            send_db_message(db, dbSelectRecord, (uint32_t)key, NULL);
            render(page);
          }
        }
      }
      // Enter in a text field presses the page's default button, as a dialog's does.
      if (page && HIWORD(wparam) == edUpdate && page->win) {
        window_t *button = find_default_button(page->win);
        const form_ctrl_def_t *d = button ? form_library_control(page->lib, page->form, button->id) : NULL;
        const char *action = d && !d->target ? control_attr(page, d->id, "action") : NULL;
        if (!action || strchr(action, '.')) return false;
        perform(page, action, d->name);
        return true;
      }
      // A TableView with action= runs it when a row is opened (Enter or a
      // double-click); the action reads the row as the list's value.
      if (page && HIWORD(wparam) == RVN_DBLCLK && lparam) {
        const form_ctrl_def_t *list = form_library_control(page->lib, page->form, ((window_t *)lparam)->id);
        const char *action = class_is(list, "TableView") ? control_attr(page, list->id, "action") : NULL;
        if (!action || strchr(action, '.')) return false;
        perform(page, action, list->name);
        return true;
      }
      if (!page || HIWORD(wparam) != btnClicked) return false;
      if (lparam && form_dispatch_target_action(win, page->form, (window_t *)lparam)) {
        render(page); return true;
      }
      uint32_t id = LOWORD(wparam);
      if (id && id == page->form->cancel_id) {
        page->closing = true;
        page->result = 0;
        finish(page);
        return true;
      }
      const form_ctrl_def_t *d = form_library_control(page->lib, page->form, id);
      const char *action = d && !d->target ? control_attr(page, id, "action") : NULL;
      const char *target = d ? control_attr(page, id, "target") : NULL;
      if (target && strchr(target, '.') && action && page->runtime) {
        script_runtime_action(page->runtime, target, action, control_attr(page, id, "argument"), page->message, sizeof(page->message));
        finish(page); return true;
      }
      if (!action || strchr(action, '.')) return false;
      perform(page, action, d->name);
      return true;
    }
    case evTimer:
      if (!page) return false;
      for (int i = 0; i < SCRIPT_MAX_SPRITES; i++) {
        page_sprite_t *s = &page->sprites[i];
        if (!s->timer || s->timer != wparam) continue;
        if (s->frame + 1 < s->count) s->frame++;
        else if (s->loop) s->frame = 0;
        else { axCancelTimer(s->timer); s->timer = 0; }
        sprite_draw(page, s);
        return true;
      }
      return false;
    case evDestroy:
      if (page && page->hook) page->hook(win, msg, wparam, lparam);
      if (page) { sprite_clear(page); page->win = NULL; }
      return true;
  }
  return false;
}

window_t *script_page_create(script_page_t *page, window_t *parent) {
  return script_page_create_hook(page, parent, NULL);
}

window_t *script_page_create_hook(script_page_t *page, window_t *parent, winproc_t hook) {
  if (!page) return NULL;
  page->hook = hook;
  page->modal = false;
  window_t *win = create_window_from_form(page->form, 0, 0, parent, script_page_proc, 0, page);
  if (win) {
    page->owns_window = true;
    if (!page->form->height) {
      isize16_t size = script_page_preferred_size(page);
      resize_window(win, size.w, size.h);
    }
    set_window_title(win, script_page_title(page));
  }
  return win;
}

void script_page_run(script_page_t *page, window_t *parent) {
  if (!page) return;
  page->modal = true;
  page->closing = false;
  show_dialog_from_form(page->form, script_page_title(page), parent, script_page_proc, page);
  page->win = NULL;
}

// ── Opening and closing ─────────────────────────────────────────────────────

static script_page_t *fail(script_page_t *page, char *error, size_t error_sz, const char *fmt, const char *detail) {
  char text[1024];
  snprintf(text, sizeof(text), fmt, detail ? detail : "");
  page_log(page, text, NULL);
  if (error && error_sz) snprintf(error, error_sz, "%s", text);
  script_page_close(page);
  return NULL;
}

static script_page_t *page_load_controller(script_page_t *page, const char *lua, char *error, size_t error_sz) {
  script_runtime_t *runtime = page->runtime;
  char chunk[160];
  snprintf(chunk, sizeof(chunk), "%s.lua", page->name);
  page->L = runtime ? script_runtime_state(runtime) : script_state_new(page->dir, chunk);
  if (!page->L) return fail(page, error, error_sz, ui_tr("The add-on script %s cannot run."), lua);
  lua_State *L = page->L;
  char msg[1024];
  bool refused = false, ran;
  const char *outer;
  if (runtime) {
    outer = ui_set_string_context(page->name);
    page->page_ref = script_runtime_controller(runtime, lua, msg, sizeof(msg));
    ui_set_string_context(outer);
    if (page->page_ref == LUA_NOREF) return fail(page, error, error_sz, "%s", msg);
    lua_rawgeti(L, LUA_REGISTRYINDEX, page->page_ref);
  } else {
    if (luaL_loadfilex(L, lua, "t") != LUA_OK) {
      snprintf(msg, sizeof(msg), "%s", lua_tostring(L, -1)); lua_pop(L, 1);
      return fail(page, error, error_sz, ui_tr("The add-on script does not compile: %s"), msg);
    }
    outer = ui_set_string_context(page->name);
    ran = script_call(L, 0, 1, msg, sizeof(msg), &refused);
    ui_set_string_context(outer);
    if (!ran) return fail(page, error, error_sz, ui_tr("The add-on script failed: %s"), strtok(msg, "\n"));
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return fail(page, error, error_sz, ui_tr("The add-on script %s must return its page table."), lua); }
  }
  push_app(page);
  lua_setfield(L, -2, "app");
  lua_getfield(L, -1, "form");
  const char *form_name = lua_tostring(L, -1);
  page->form = form_name ? form_library_form(page->lib, form_name) : page->form ? page->form : form_library_form_at(page->lib, 0);
  char missing[128];
  snprintf(missing, sizeof(missing), "%s", form_name ? form_name : "");
  lua_pop(L, 1);
  lua_getfield(L, -1, "title");
  snprintf(page->title, sizeof(page->title), "%s", lua_isstring(L, -1) ? lua_tostring(L, -1) : "");
  lua_pop(L, 1);
  if (runtime) lua_pop(L, 1);
  else page->page_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  if (!page->form) return fail(page, error, error_sz, ui_tr("The add-on has no form '%s'."), missing);
  if (!page->title[0]) snprintf(page->title, sizeof(page->title), "%s", page->form->name ? page->form->name : page->name);

  lua_rawgeti(L, LUA_REGISTRYINDEX, page->page_ref);
  lua_getfield(L, -1, "init");
  if (lua_isfunction(L, -1)) {
    lua_insert(L, -2);
    outer = ui_set_string_context(page->name);
    ran = script_call(L, 1, 0, msg, sizeof(msg), &refused);
    ui_set_string_context(outer);
    if (!ran)
      return fail(page, error, error_sz, refused ? "%s" : ui_tr("The add-on script failed: %s"), strtok(msg, "\n"));
  } else {
    lua_pop(L, 2);
  }
  return page;
}

script_page_t *script_page_open_in(script_runtime_t *runtime, const char *path, char *error, size_t error_sz) {
  if (error && error_sz) error[0] = 0;
  script_page_t *page = calloc(1, sizeof(*page));
  if (!page || !path || !*path) {
    fprintf(stderr, "[script] script_page_open: %s\n", page ? "no path" : "out of memory");
    fflush(stderr);
    free(page);
    return NULL;
  }
  page->page_ref = LUA_NOREF;
  page->runtime = runtime;
  page_register(page);
  snprintf(page->stem, sizeof(page->stem), "%s", path);
  size_t n = strlen(page->stem);
  if (n > 6 && !strcmp(page->stem + n - 6, ".orion")) page->stem[n - 6] = 0;
  else if (n > 4 && !strcmp(page->stem + n - 4, ".lua")) page->stem[n - 4] = 0;
  const char *slash = strrchr(page->stem, '/');
  snprintf(page->dir, sizeof(page->dir), "%.*s", slash ? (int)(slash - page->stem) : 1, slash ? page->stem : ".");
  snprintf(page->name, sizeof(page->name), "%.127s", slash ? slash + 1 : page->stem);

  char orion[1100], lua[1100], locale[1100];
  snprintf(orion, sizeof(orion), "%s.orion", page->stem);
  snprintf(lua, sizeof(lua), "%s.lua", page->stem);
  if (!file_exists(orion)) return fail(page, error, error_sz, ui_tr("The add-on form %s is missing."), orion);
  bool scripted = file_exists(lua);
  snprintf(locale, sizeof(locale), "%s/locale", page->dir);
  if (axPathExists(locale)) ui_add_scoped_string_catalog_dir(locale, page->name);

  page->lib = form_library_load(orion);
  if (!page->lib) return fail(page, error, error_sz, ui_tr("The add-on form %s cannot be read."), orion);
  for (int i = 0; i < form_library_schema_count(page->lib); i++) {
    const db_schema_def_t *schema = form_library_schema_at(page->lib, i);
    for (int k = 0; get_database_at(k); k++)
      if (!strcmp(get_database_at(k)->name, schema->name))
        return fail(page, error, error_sz, ui_tr("The add-on database '%s' clashes with one already open."), schema->name);
    if (page->db_count >= SCRIPT_MAX_DATABASES)
      return fail(page, error, error_sz, ui_tr("The add-on declares too many databases (%s)."), schema->name);
    database_t *db = create_database_with_schema(schema->name, schema->class_name ? schema->class_name : "db_schema_xml", schema, "");
    if (!db || !register_database(schema->name, db)) {
      if (db) destroy_database(db);
      return fail(page, error, error_sz, ui_tr("The add-on database '%s' cannot be created."), schema->name);
    }
    page->dbs[page->db_count++] = db;
  }

  if (!scripted) {
    page->form = form_library_form_at(page->lib, 0);
    if (!page->form) return fail(page, error, error_sz, "No declarative form in %s", orion);
    snprintf(page->title, sizeof(page->title), "%s", page->form->name);
    return page;
  }
  return page_load_controller(page, lua, error, error_sz);
}

script_page_t *script_page_open_library(script_runtime_t *runtime, form_library_t *library,
    const char *form, const char *controller, char *error, size_t size) {
  script_page_t *page = calloc(1, sizeof(*page));
  if (!page || !library) { free(page); return NULL; }
  page->runtime = runtime; page->borrowed_library = true; page->lib = library; page->page_ref = LUA_NOREF;
  page_register(page);
  page->form = form ? form_library_form(library, form) : form_library_form_at(library, 0);
  snprintf(page->name, sizeof(page->name), "%s", form ? form : "application");
  if (!page->form) return fail(page, error, size, "Unknown form: %s", form);
  snprintf(page->title, sizeof(page->title), "%s", page->form->name);
  if (controller && *controller) {
    if (!runtime) return fail(page, error, size, "Controller needs an application runtime: %s", controller);
    return page_load_controller(page, controller, error, size);
  }
  return page;
}

script_page_t *script_page_open(const char *path, char *error, size_t error_sz) {
  return script_page_open_in(NULL, path, error, error_sz);
}

void script_page_close(script_page_t *page) {
  if (!page) return;
  if (page->owns_window && page->win) destroy_window(page->win);
  sprite_clear(page);
  if (page->runtime) { script_runtime_release(page->runtime, page->page_ref); script_runtime_page_release(page->runtime); }
  else if (page->L) script_state_close(page->L);
  for (int i = 0; i < page->db_count; i++) {
    unregister_database(page->dbs[i]->name);
    destroy_database(page->dbs[i]);
  }
  if (!page->borrowed_library) form_library_free(page->lib);
  script_page_t **link = &live_pages;
  while (*link && *link != page) link = &(*link)->next;
  if (*link) *link = page->next;
  free(page);
}

const char *script_page_title(const script_page_t *page) { return page ? page->title : ""; }
isize16_t script_page_preferred_size(const script_page_t *page) {
  if (!page || !page->form) return (isize16_t){0};
  isize16_t size = {page->form->width, page->form->height};
  if (!size.h && page->win) {
    layout_measure_t measure = {.avail_w = size.w};
    layout_measure_window(page->win, &measure);
    size.h = measure.desired_h;
  }
  return size;
}
const char *script_page_message(const script_page_t *page) { return page ? page->message : ""; }

window_t *script_page_control(const script_page_t *page, const char *name) {
  uint32_t id = page && page->win ? form_library_control_id(page->lib, page->form, name) : 0;
  return id ? get_window_item(page->win, id) : NULL;
}

int script_page_handle(const script_page_t *page) { return page ? page->handle : 0; }
window_t *script_page_window(const script_page_t *page) { return page ? page->win : NULL; }

static void page_refresh_controls(window_t *window) {
  for (window_t *child = window ? window->children : NULL; child; child = child->next) {
    if (child->proc == win_tableview) send_message(child, tvRefresh, 0, NULL);
    page_refresh_controls(child);
  }
}
void script_page_update(script_page_t *page) { if (page) render(page); }
void script_page_refresh(script_page_t *page) {
  if (!page || !page->win) return;
  page_refresh_controls(page->win); render(page);
}

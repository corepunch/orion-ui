// ledger_addons.c — external processings, as 1C opens an .epf: a .orion
// form and a Lua script beside it, run from File → Open Add-on. Add-ons
// read and write the infobase through orion.model, so ledger_ib numbers and
// recalculates what they create; the "ledger" module adds what only Ledger
// knows: the chart's hierarchy, posted turnovers and posting. The module is
// declared in ledger_addons.cgen; its bindings are generated (tools/cgen.c).
//
//   local ledger = require("ledger")
//   ledger.configuration                      -- "ru-2026"
//   ledger.account("62")                      -- {id, code, name, analytics = "counterparties"} or nil
//   ledger.movements("62", {to = "2026-04-30"})  -- posted entries on 62 and its subaccounts, by date
//   ledger.turnover("62", {from, to, subconto})  -- {debit, credit, balance}
//   ledger.period()                           -- the report period: from, to
//   ledger.post(id), ledger.unpost(id)        -- true, or nil and a message

#include "ledger.h"
#include <orion/script/script.h>
#include <lauxlib.h>

static const db_account_t *addon_find_account(lua_State *L, int arg) {
  if (lua_type(L, arg) == LUA_TNUMBER)
    return (const db_account_t *)send_db_message(g_ledger->cfg, dbFind, MAKEDWORD(TABLE_ACCOUNTS, 0), (void *)(intptr_t)lua_tointeger(L, arg));
  return (const db_account_t *)send_db_message(g_ledger->cfg, dbFind, MAKEDWORD(TABLE_ACCOUNTS, ledger_field(g_ledger->cfg, TABLE_ACCOUNTS, "code")),
                                               (void *)luaL_checkstring(L, arg));
}

// An account by id or code; a missing one is a mistake in the script.
static const db_account_t *addon_account_arg(lua_State *L, int arg) {
  const db_account_t *a = addon_find_account(L, arg);
  if (!a) luaL_error(L, "no account %s in configuration %s", luaL_tolstring(L, arg, NULL), g_ledger->configuration);
  return a;
}

static const char *addon_code_of(int account_id) {
  const db_account_t *a = (const db_account_t *)send_db_message(g_ledger->cfg, dbFind, MAKEDWORD(TABLE_ACCOUNTS, 0), (void *)(intptr_t)account_id);
  return a ? a->code : "";
}

typedef struct { const char *from, *to; lua_Integer subconto; bool by_subconto; } addon_filter_t;

// {from, to, subconto}; the three values stay on the stack while the strings are used.
static addon_filter_t addon_filter_arg(lua_State *L, int filter) {
  addon_filter_t f = {0};
  if (!filter) return f;
  lua_getfield(L, filter, "from");     f.from = luaL_optstring(L, -1, NULL);
  lua_getfield(L, filter, "to");       f.to = luaL_optstring(L, -1, NULL);
  lua_getfield(L, filter, "subconto"); f.by_subconto = !lua_isnil(L, -1); f.subconto = luaL_optinteger(L, -1, 0);
  return f;
}

static bool addon_dated_within(const addon_filter_t *f, const char *date) {
  return !(f->from && *f->from && strcmp(date, f->from) < 0) && !(f->to && *f->to && strcmp(date, f->to) > 0);
}

static bool addon_side(const addon_filter_t *f, int account_id, int side_account, int side_subconto) {
  return ledger_account_within(g_ledger->cfg, side_account, account_id) && (!f->by_subconto || side_subconto == f->subconto);
}

static void addon_push_movement(lua_State *L, const db_entry_t *e, bool debit, int *n) {
  const db_document_t *doc = (const db_document_t *)send_db_message(g_ledger->ib, dbFind, MAKEDWORD(TABLE_DOCUMENTS, 0), (void *)(intptr_t)e->document_id);
  lua_createtable(L, 0, 9);
  lua_pushstring(L, e->date);                                               lua_setfield(L, -2, "date");
  lua_pushinteger(L, e->document_id);                                       lua_setfield(L, -2, "document");
  lua_pushstring(L, doc ? doc->number : "");                                lua_setfield(L, -2, "number");
  lua_pushstring(L, addon_code_of(debit ? e->debit_id : e->credit_id));     lua_setfield(L, -2, "account");
  lua_pushstring(L, addon_code_of(debit ? e->credit_id : e->debit_id));     lua_setfield(L, -2, "corr");
  lua_pushinteger(L, debit ? e->debit_subconto_id : e->credit_subconto_id); lua_setfield(L, -2, "subconto");
  lua_pushnumber(L, debit ? e->amount : 0);                                 lua_setfield(L, -2, "debit");
  lua_pushnumber(L, debit ? 0 : e->amount);                                 lua_setfield(L, -2, "credit");
  lua_pushstring(L, e->content);                                            lua_setfield(L, -2, "content");
  lua_rawseti(L, -2, ++*n);
}

static int ledger_addon_account(lua_State *L, int account) {
  const db_account_t *a = addon_find_account(L, account);
  if (!a) { lua_pushnil(L); return 1; }
  lua_createtable(L, 0, 4);
  lua_pushinteger(L, a->id);                                      lua_setfield(L, -2, "id");
  lua_pushstring(L, a->code);                                     lua_setfield(L, -2, "code");
  lua_pushstring(L, a->name);                                     lua_setfield(L, -2, "name");
  lua_pushstring(L, ledger_subconto_table(g_ledger->cfg, a->id)); lua_setfield(L, -2, "analytics");
  return 1;
}

static int ledger_addon_movements(lua_State *L, int account, int filter) {
  const db_account_t *a = addon_account_arg(L, account);
  addon_filter_t f = addon_filter_arg(L, filter);
  db_entry_t **entries = NULL;
  int count = ledger_posted_entries(g_ledger->ib, &entries), n = 0;
  lua_newtable(L);
  for (int i = 0; i < count; i++) {
    const db_entry_t *e = entries[i];
    if (!addon_dated_within(&f, e->date)) continue;
    if (addon_side(&f, a->id, e->debit_id, e->debit_subconto_id))   addon_push_movement(L, e, true, &n);
    if (addon_side(&f, a->id, e->credit_id, e->credit_subconto_id)) addon_push_movement(L, e, false, &n);
  }
  free(entries);
  return 1;
}

static int ledger_addon_turnover(lua_State *L, int account, int filter) {
  const db_account_t *a = addon_account_arg(L, account);
  addon_filter_t f = addon_filter_arg(L, filter);
  db_entry_t **entries = NULL;
  int count = ledger_posted_entries(g_ledger->ib, &entries);
  double dr = 0, cr = 0;
  for (int i = 0; i < count; i++) {
    const db_entry_t *e = entries[i];
    if (!addon_dated_within(&f, e->date)) continue;
    if (addon_side(&f, a->id, e->debit_id, e->debit_subconto_id))   dr += e->amount;
    if (addon_side(&f, a->id, e->credit_id, e->credit_subconto_id)) cr += e->amount;
  }
  free(entries);
  lua_createtable(L, 0, 3);
  lua_pushnumber(L, dr);      lua_setfield(L, -2, "debit");
  lua_pushnumber(L, cr);      lua_setfield(L, -2, "credit");
  lua_pushnumber(L, dr - cr); lua_setfield(L, -2, "balance");
  return 1;
}

static int ledger_addon_period(lua_State *L) {
  const db_report_setting_t *rs = ledger_report_settings(g_ledger->ib, g_ledger->cfg);
  lua_pushstring(L, rs ? rs->date_from : "");
  lua_pushstring(L, rs ? rs->date_to : "");
  return 2;
}

static const db_document_t *addon_document(int id) {
  return (const db_document_t *)send_db_message(g_ledger->ib, dbFind, MAKEDWORD(TABLE_DOCUMENTS, 0), (void *)(intptr_t)id);
}

static int ledger_addon_post(lua_State *L, int id) {
  if (addon_document(id) && ledger_post(g_ledger->ib, g_ledger->cfg, id)) { lua_pushboolean(L, 1); return 1; }
  lua_pushnil(L);
  lua_pushfstring(L, ui_tr("Document %d cannot be posted."), id);
  return 2;
}

static int ledger_addon_unpost(lua_State *L, int id) {
  if (addon_document(id)) { ledger_unpost(g_ledger->ib, g_ledger->cfg, id); lua_pushboolean(L, 1); return 1; }
  lua_pushnil(L);
  lua_pushfstring(L, ui_tr("There is no document %d."), id);
  return 2;
}

static int ledger_addon_open(lua_State *L, const char *table, int id) {
  lua_pushboolean(L, ledger_open_record(table, id));
  return 1;
}

static void ledger_addon_constants(lua_State *L) {
  lua_pushstring(L, g_ledger->configuration);
  lua_setfield(L, -2, "configuration");
}

#include "build/generated/apps/ledger/ledger_addons_lua.h"

void ledger_addons_init(void) { script_register_module("ledger", luaopen_ledger_addons); }

static script_page_t *assistant_page;

void ledger_close_assistant(void) {
  script_page_close(assistant_page);
  assistant_page = NULL;
}

void ledger_open_assistant(void) {
  window_t *mascot = assistant_page ? script_page_control(assistant_page, "mascot") : NULL;
  if (mascot) { set_focus(get_root_window(mascot)); return; }
  ledger_close_assistant();
  char path[LEDGER_PATH_MAX + 64], error[1024] = "";
  snprintf(path, sizeof(path), "%s/addons/assistant/assistant.orion", g_ledger->share_dir);
  script_page_t *page = script_page_open(path, error, sizeof(error));
  if (!page) {
    message_box(g_ledger->main_win, error, ui_tr("Assistant"), MB_OK);
    return;
  }
  // Keep the assistant available alongside the main application.
  window_t *win = script_page_create(page, NULL);
  if (!win) { script_page_close(page); return; }
  assistant_page = page;
  window_t *parent = g_ledger->main_win;
  if (parent) move_window(win, parent->frame.x + (parent->frame.w - win->frame.w) / 2,
                          parent->frame.y + (parent->frame.h - win->frame.h) / 2);
  show_window(win, true);
  set_focus(script_page_control(page, "command"));
}

void ledger_open_addon(void) {
  char path[LEDGER_PATH_MAX * 2], error[1024], filter[256] = "";
  snprintf(path, sizeof(path), "%s/addons/", g_ledger->share_dir);
  // "Description\0pattern\0\0", the description translated.
  int n = snprintf(filter, sizeof(filter) - 10, "%s", ui_tr("Ledger add-ons (*.orion)"));
  if (n < 0 || n >= (int)sizeof(filter) - 10) n = 0;
  memcpy(filter + n + 1, "*.orion", sizeof("*.orion"));
  openfilename_t ofn = { .lStructSize = sizeof(ofn), .hwndOwner = g_ledger->main_win, .lpstrFile = path, .nMaxFile = sizeof(path),
                         .lpstrFilter = filter, .Flags = OFN_FILEMUSTEXIST };
  if (!get_open_filename(&ofn)) return;
  script_page_t *page = script_page_open(path, error, sizeof(error));
  if (!page) {
    message_box(g_ledger->main_win, error, ui_tr("Open Add-on"), MB_OK);
    return;
  }
  script_page_run(page, g_ledger->main_win);
  script_page_close(page);
  ledger_build_reports(g_ledger->ib, g_ledger->cfg);  // what the add-on posted shows in the reports
  ledger_refresh_views();
}

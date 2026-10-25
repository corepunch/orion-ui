// The Lua state an add-on runs in: a sandbox without io, native loading or
// bytecode, whose require() reaches the add-on's folder, Orion's Lua modules
// and the modules a host registers; and the "orion" module scripts use for
// refusals, translation and locale formatting.

#include "script.h"
#include "script_internal.h"

#include <lualib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <orion/user/i18n.h>

#define SCRIPT_MAX_MODULES 32

static struct { const char *name; lua_CFunction open; } g_modules[SCRIPT_MAX_MODULES];
static int g_module_count;

bool script_register_module(const char *name, lua_CFunction open) {
  if (!name || !*name || !open) {
    fprintf(stderr, "[script] script_register_module: name and open function required\n");
    fflush(stderr);
    return false;
  }
  for (int i = 0; i < g_module_count; i++)
    if (!strcmp(g_modules[i].name, name)) { g_modules[i].open = open; return true; }
  if (g_module_count >= SCRIPT_MAX_MODULES) {
    fprintf(stderr, "[script] script_register_module: '%s' rejected, %d modules already registered\n", name, SCRIPT_MAX_MODULES);
    fflush(stderr);
    return false;
  }
  g_modules[g_module_count].name = name;
  g_modules[g_module_count].open = open;
  g_module_count++;
  return true;
}

// ── Refusals ────────────────────────────────────────────────────────────────

int script_refuse(lua_State *L, const char *message) {
  lua_createtable(L, 0, 1);
  lua_pushstring(L, message ? message : "");
  lua_setfield(L, -2, "message");
  luaL_setmetatable(L, SCRIPT_REFUSAL);
  return lua_error(L);
}

static int refusal_tostring(lua_State *L) {
  lua_getfield(L, 1, "message");
  return 1;
}

static bool is_refusal(lua_State *L, int idx) {
  idx = lua_absindex(L, idx);
  if (!lua_istable(L, idx) || !lua_getmetatable(L, idx)) return false;
  luaL_getmetatable(L, SCRIPT_REFUSAL);
  bool same = lua_rawequal(L, -1, -2);
  lua_pop(L, 2);
  return same;
}

// Keeps a refusal as it is; turns any other error into its traceback.
static int message_handler(lua_State *L) {
  if (is_refusal(L, 1)) return 1;
  const char *msg = lua_tostring(L, 1);
  if (!msg) msg = luaL_typename(L, 1);
  luaL_traceback(L, L, msg, 1);
  return 1;
}

bool script_call(lua_State *L, int nargs, int nresults, char *msg, size_t msg_sz, bool *refused) {
  int base = lua_gettop(L) - nargs;
  lua_pushcfunction(L, message_handler);
  lua_insert(L, base);
  int rc = lua_pcall(L, nargs, nresults, base);
  lua_remove(L, base);
  if (refused) *refused = false;
  if (rc == LUA_OK) return true;
  if (is_refusal(L, -1)) {
    if (refused) *refused = true;
    lua_getfield(L, -1, "message");
    snprintf(msg, msg_sz, "%s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
    lua_pop(L, 2);
  } else {
    snprintf(msg, msg_sz, "%s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "error object is not a string");
    lua_pop(L, 1);
  }
  return false;
}

// ── The "orion" module ──────────────────────────────────────────────────────

// orion.fail(message): stops the action and shows the message to the user.
static int l_fail(lua_State *L) { return script_refuse(L, luaL_checkstring(L, 1)); }

// orion.assert(value, message, ...): the value, or a refusal with the message
// (as Routes.assert in lua-objc: orion.assert(row:update{...}) passes on the
// message a model returned).
static int l_assert(lua_State *L) {
  if (lua_toboolean(L, 1)) return lua_gettop(L);
  return script_refuse(L, luaL_optstring(L, 2, ui_tr("The action was refused.")));
}

static int l_tr(lua_State *L) { lua_pushstring(L, ui_tr(luaL_checkstring(L, 1))); return 1; }

static int l_format_number(lua_State *L) {
  char buf[64];
  lua_pushstring(L, ui_format_number(luaL_checknumber(L, 1), (int)luaL_optinteger(L, 2, 2), buf, sizeof(buf)));
  return 1;
}

static int l_format_date(lua_State *L) {
  char buf[64];
  lua_pushstring(L, ui_format_date(luaL_checkstring(L, 1), buf, sizeof(buf)));
  return 1;
}

static int l_parse_number(lua_State *L) { lua_pushnumber(L, ui_parse_number(luaL_checkstring(L, 1))); return 1; }

static int l_parse_date(lua_State *L) {
  char iso[16];
  if (ui_parse_date(luaL_checkstring(L, 1), iso, sizeof(iso))) lua_pushstring(L, iso);
  else lua_pushnil(L);
  return 1;
}

static int l_today(lua_State *L) {
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  char iso[40];
  snprintf(iso, sizeof(iso), "%04d-%02d-%02d", tm ? tm->tm_year + 1900 : 1970, tm ? tm->tm_mon + 1 : 1, tm ? tm->tm_mday : 1);
  lua_pushstring(L, iso);
  return 1;
}

int script_open_orion(lua_State *L) {
  static const luaL_Reg fns[] = {
    {"fail", l_fail}, {"assert", l_assert}, {"tr", l_tr},
    {"formatNumber", l_format_number}, {"formatDate", l_format_date},
    {"parseNumber", l_parse_number}, {"parseDate", l_parse_date}, {"today", l_today},
    {NULL, NULL},
  };
  luaL_newlib(L, fns);
  return 1;
}

// ── Sandbox ─────────────────────────────────────────────────────────────────

static int l_print(lua_State *L) {
  const char *name = lua_tostring(L, lua_upvalueindex(1));
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  for (int i = 1; i <= lua_gettop(L); i++) {
    if (i > 1) luaL_addchar(&b, '\t');
    luaL_addstring(&b, luaL_tolstring(L, i, NULL));
    lua_pop(L, 1);
  }
  luaL_pushresult(&b);
  fprintf(stderr, "[script] %s: %s\n", name ? name : "?", lua_tostring(L, -1));
  fflush(stderr);
  return 0;
}

// Module names are dotted identifiers; "a.b" is a/b.lua under each root.
static int l_search(lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  for (const char *p = name; *p; p++)
    if (!(*p == '.' || *p == '_' || (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) || (p[0] == '.' && p[1] == '.'))
      return lua_pushfstring(L, "\n\tmodule name '%s' is not a dotted identifier", name), 1;
  char rel[256];
  snprintf(rel, sizeof(rel), "%s", name);
  for (char *p = rel; *p; p++) if (*p == '.') *p = '/';
  for (int i = 1; i <= 2; i++) {
    const char *root = lua_tostring(L, lua_upvalueindex(i));
    if (!root || !*root) continue;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.lua", root, rel);
    FILE *f = fopen(path, "rb");
    if (!f) continue;
    fclose(f);
    if (luaL_loadfilex(L, path, "t") != LUA_OK) return lua_error(L);
    lua_pushstring(L, path);
    return 2;
  }
  return lua_pushfstring(L, "\n\tno add-on or Orion module '%s'", name), 1;
}

static void open_lib(lua_State *L, const char *name, lua_CFunction open) {
  luaL_requiref(L, name, open, 1);
  lua_pop(L, 1);
}

static void keep_only(lua_State *L, const char *lib, const char *const *keep) {
  lua_getglobal(L, lib);
  lua_newtable(L);
  for (int i = 0; keep[i]; i++) {
    lua_getfield(L, -2, keep[i]);
    lua_setfield(L, -2, keep[i]);
  }
  lua_setglobal(L, lib);
  lua_pop(L, 1);
}

lua_State *script_state_new(const char *dir, const char *name) {
  lua_State *L = luaL_newstate();
  if (!L) {
    fprintf(stderr, "[script] %s: cannot create a Lua state\n", name ? name : "?");
    fflush(stderr);
    return NULL;
  }
  open_lib(L, "_G", luaopen_base);
  open_lib(L, LUA_LOADLIBNAME, luaopen_package);
  open_lib(L, LUA_COLIBNAME, luaopen_coroutine);
  open_lib(L, LUA_TABLIBNAME, luaopen_table);
  open_lib(L, LUA_STRLIBNAME, luaopen_string);
  open_lib(L, LUA_MATHLIBNAME, luaopen_math);
  open_lib(L, LUA_UTF8LIBNAME, luaopen_utf8);
  open_lib(L, LUA_OSLIBNAME, luaopen_os);

  // No files, native code or bytecode; os keeps only the clock and dates.
  static const char *const removed[] = { "dofile", "loadfile", "load", "collectgarbage", NULL };
  for (int i = 0; removed[i]; i++) { lua_pushnil(L); lua_setglobal(L, removed[i]); }
  static const char *const os_kept[] = { "time", "date", "clock", "difftime", NULL };
  keep_only(L, "os", os_kept);
  lua_getglobal(L, "string");
  lua_pushnil(L);
  lua_setfield(L, -2, "dump");
  lua_pop(L, 1);

  lua_pushstring(L, name ? name : "?");
  lua_pushcclosure(L, l_print, 1);
  lua_setglobal(L, "print");

  luaL_newmetatable(L, SCRIPT_REFUSAL);
  lua_pushcfunction(L, refusal_tostring);
  lua_setfield(L, -2, "__tostring");
  lua_pop(L, 1);

  lua_getglobal(L, LUA_LOADLIBNAME);
  lua_pushnil(L); lua_setfield(L, -2, "loadlib");
  lua_pushstring(L, ""); lua_setfield(L, -2, "path");
  lua_pushstring(L, ""); lua_setfield(L, -2, "cpath");
  lua_getfield(L, -1, "preload");
  lua_pushcfunction(L, script_open_orion); lua_setfield(L, -2, "orion");
  lua_pushcfunction(L, script_open_db);    lua_setfield(L, -2, "orion.db");
  for (int i = 0; i < g_module_count; i++) {
    lua_pushcfunction(L, g_modules[i].open);
    lua_setfield(L, -2, g_modules[i].name);
  }
  lua_pop(L, 1);
  // searchers = { preload, add-on folder and Orion's modules }
  lua_getfield(L, -1, "searchers");
  lua_createtable(L, 2, 0);
  lua_rawgeti(L, -2, 1);
  lua_rawseti(L, -2, 1);
  char lua_root[1024];
  snprintf(lua_root, sizeof(lua_root), "%s/../share/orion/lua", ui_get_exe_dir());
  lua_pushstring(L, dir ? dir : "");
  lua_pushstring(L, lua_root);
  lua_pushcclosure(L, l_search, 2);
  lua_rawseti(L, -2, 2);
  lua_setfield(L, -3, "searchers");
  lua_pop(L, 2);
  return L;
}

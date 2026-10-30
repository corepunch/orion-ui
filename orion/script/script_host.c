// The Lua state an add-on runs in: a sandbox without io, native loading or
// bytecode, whose require() reaches the add-on's folder, Orion's Lua modules
// and the modules a host registers; and the "orion" module scripts use for
// refusals, translation and locale formatting, declared in orion.cgen.

#include "script.h"
#include "script_internal.h"

#include <lualib.h>
#include "build/generated/orion/script/modules.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <orion/user/i18n.h>
#include <stdint.h>
#include <stdlib.h>

#define SCRIPT_MAX_MODULES 32

// ── Resource accounting ─────────────────────────────────────────────────────
// Every state carries its own accounting as the allocator's user data, so a
// state created elsewhere (lua_getallocf() names another allocator) has none.
#define SCRIPT_MEMORY_LIMIT  ((size_t)256 << 20)   // bytes a state may hold
#define SCRIPT_CPU_BUDGET    30.0                   // CPU seconds per outermost call
#define SCRIPT_HOOK_COUNT    10000                  // instructions between watchdog checks

typedef struct {
  size_t used, limit;
  double budget;
  clock_t deadline;
  int depth;
  bool warning;   // a multi-part warning is open
  char name[64];
} script_state_t;

static void *script_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
  script_state_t *state = ud;
  size_t old = ptr ? osize : 0;  // without a block, osize is the type being allocated
  if (nsize == 0) { free(ptr); state->used -= old; return NULL; }
  if (nsize > old && state->used + (nsize - old) > state->limit) return NULL;  // Lua raises "not enough memory"
  void *block = realloc(ptr, nsize);
  if (block) state->used = state->used - old + nsize;
  return block;
}
static script_state_t *state_of(lua_State *L) {
  void *ud = NULL;
  return lua_getallocf(L, &ud) == script_alloc ? ud : NULL;
}
static int script_panic(lua_State *L) {
  script_state_t *state = state_of(L);
  const char *msg = lua_tostring(L, -1);
  fprintf(stderr, "[script] %s: unprotected error: %s\n", state ? state->name : "?", msg ? msg : luaL_typename(L, -1));
  fflush(stderr);
  return 0;
}
// Lua 5.4 warnings (warn(), errors in __gc and __close) would otherwise vanish.
static void script_warn(void *ud, const char *message, int tocont) {
  script_state_t *state = ud;
  if (!state->warning && *message == '@') return;  // a control message such as "@on"
  if (!state->warning) fprintf(stderr, "[script] %s: warning: ", state->name);
  fputs(message, stderr);
  state->warning = tocont != 0;
  if (!tocont) { fputc('\n', stderr); fflush(stderr); }
}
static void script_watchdog(lua_State *L, lua_Debug *ar) {
  (void)ar;
  script_state_t *state = state_of(L);
  if (!state || clock() <= state->deadline) return;
  lua_sethook(L, NULL, 0, 0);
  char text[128];  // lua_pushfstring knows no precision, so format the budget here
  snprintf(text, sizeof(text), "the script ran for more than %.0f seconds of CPU time and was stopped", state->budget);
  luaL_error(L, "%s", text);
}
void script_state_limits(lua_State *L, size_t memory_bytes, double cpu_seconds) {
  script_state_t *state = state_of(L);
  if (!state) return;
  if (memory_bytes) state->limit = memory_bytes;
  if (cpu_seconds > 0) state->budget = cpu_seconds;
}
void script_state_close(lua_State *L) {
  if (!L) return;
  script_state_t *state = state_of(L);
  lua_close(L);
  free(state);
}

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
  // The outermost call arms the watchdog; calls nested through native code
  // (a model validating inside an action) share its deadline.
  script_state_t *state = state_of(L);
  if (state && state->depth++ == 0) {
    state->deadline = clock() + (clock_t)(state->budget * CLOCKS_PER_SEC);
    lua_sethook(L, script_watchdog, LUA_MASKCOUNT, SCRIPT_HOOK_COUNT);
  }
  int rc = lua_pcall(L, nargs, nresults, base);
  if (state && --state->depth == 0) lua_sethook(L, NULL, 0, 0);
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

// ── The "orion" module (orion.cgen) ─────────────────────────────────────────

static int script_fail(lua_State *L, const char *message) { return script_refuse(L, message); }

// Passes every argument on, as Routes.assert in lua-objc: orion.assert(row:update{...})
// passes on the message a model returned.
static int script_assert(lua_State *L, int value, const char *message) {
  if (lua_toboolean(L, value)) return lua_gettop(L);
  return script_refuse(L, message ? message : ui_tr("The action was refused."));
}

static int script_parse_date(lua_State *L, const char *text) {
  char iso[16];
  if (ui_parse_date(text, iso, sizeof(iso))) lua_pushstring(L, iso);
  else lua_pushnil(L);
  return 1;
}

static const char *script_today(char *iso, size_t size) {
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  snprintf(iso, size, "%04d-%02d-%02d", tm ? tm->tm_year + 1900 : 1970, tm ? tm->tm_mon + 1 : 1, tm ? tm->tm_mday : 1);
  return iso;
}

#include "build/generated/orion/script/orion_lua.h"

// ── Sandbox ─────────────────────────────────────────────────────────────────

static int l_print(lua_State *L) {
  const char *name = lua_tostring(L, lua_upvalueindex(1));
  int n = lua_gettop(L);  // the buffer may push a box above the arguments
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  for (int i = 1; i <= n; i++) {
    if (i > 1) luaL_addchar(&b, '\t');
    luaL_tolstring(L, i, NULL);
    luaL_addvalue(&b);
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
  script_state_t *state = calloc(1, sizeof(*state));
  if (state) {
    state->limit = SCRIPT_MEMORY_LIMIT; state->budget = SCRIPT_CPU_BUDGET;
    snprintf(state->name, sizeof(state->name), "%s", name ? name : "?");
  }
#if LUA_VERSION_NUM >= 505
  lua_State *L = state ? lua_newstate(script_alloc, state, (unsigned)time(NULL) ^ (unsigned)(uintptr_t)state) : NULL;
#else
  lua_State *L = state ? lua_newstate(script_alloc, state) : NULL;
#endif
  if (!L) {
    fprintf(stderr, "[script] %s: cannot create a Lua state\n", name ? name : "?");
    fflush(stderr);
    free(state);
    return NULL;
  }
  lua_atpanic(L, script_panic);
  lua_setwarnf(L, script_warn, state);
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
  script_preload_generated(L);
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
  // Modules return tables; a global assigned anywhere is a leak between
  // modules, so the sandbox refuses it. Reading an unknown global stays nil.
  static const char *const strict =
    "setmetatable(_G, {__newindex = function(_, name)"
    "  error(\"assignment to undeclared global '\" .. tostring(name) .. \"'\", 2) end})";
  if (luaL_loadstring(L, strict) != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
    fprintf(stderr, "[script] %s: cannot install strict globals: %s\n", state->name, lua_tostring(L, -1));
    fflush(stderr);
    lua_pop(L, 1);
  }
  return L;
}

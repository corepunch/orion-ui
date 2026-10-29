#include "runtime.h"
#include "script_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct script_runtime_s { lua_State *L; unsigned controllers, pages; };

// The wrapper receives _ENV from the package scope; no Lua load/loadfile API
// is exposed. Text-only loading and path validation remain native.
static int runtime_local_chunk(lua_State *L) {
  lua_pushvalue(L, lua_upvalueindex(1));
  lua_pushvalue(L, 1);
  lua_setupvalue(L, -2, 1);
  lua_call(L, 0, 1);
  return 1;
}
static int runtime_read(lua_State *L) {
  const char *name = luaL_checkstring(L, 1), *directory = lua_tostring(L, lua_upvalueindex(1));
  if (!*name || strstr(name, "..") || name[0] == '.') return luaL_error(L, "invalid local module name");
  char relative[256], path[1400];
  if (strlen(name) >= sizeof(relative)) return luaL_error(L, "local module name is too long");
  snprintf(relative, sizeof(relative), "%s", name);
  for (char *c = relative; *c; c++) {
    if (*c == '.') *c = '/';
    else if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_'))
      return luaL_error(L, "invalid local module name");
  }
  snprintf(path, sizeof(path), "%s/%s.lua", directory, relative);
  FILE *file = fopen(path, "rb");
  if (!file) { lua_pushnil(L); return 1; }
  fclose(file);
  if (luaL_loadfilex(L, path, "t") != LUA_OK) return lua_error(L);
  lua_pushcclosure(L, runtime_local_chunk, 1);
  return 1;
}
script_runtime_t *script_runtime_open(const char *directory, const char *name) {
  script_runtime_t *runtime = calloc(1, sizeof(*runtime));
  if (runtime) runtime->L = script_state_new(directory, name);
  if (!runtime || !runtime->L) { free(runtime); return NULL; }
  return runtime;
}
bool script_runtime_close(script_runtime_t *runtime) {
  if (!runtime) return true;
  if (runtime->controllers || runtime->pages) {
    fprintf(stderr, "[runtime] close rejected: controllers=%u pages=%u\n", runtime->controllers, runtime->pages); fflush(stderr);
    return false;
  }
  lua_close(runtime->L); free(runtime);
  return true;
}
lua_State *script_runtime_state(script_runtime_t *runtime) { return runtime ? runtime->L : NULL; }

int script_runtime_controller(script_runtime_t *runtime, const char *path, char *error, size_t size) {
  lua_State *L = runtime ? runtime->L : NULL;
  if (!L || !path) return LUA_NOREF;
  int base = lua_gettop(L);
  char directory[1200];
  const char *slash = strrchr(path, '/');
  snprintf(directory, sizeof(directory), "%.*s", slash ? (int)(slash - path) : 1, slash ? path : ".");
  bool refused = false;
  lua_getfield(L, LUA_REGISTRYINDEX, "orion.runtime.scopes");
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1); lua_newtable(L); lua_pushvalue(L, -1); lua_setfield(L, LUA_REGISTRYINDEX, "orion.runtime.scopes");
  }
  int scopes = lua_gettop(L);
  lua_getfield(L, scopes, directory);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    if (luaL_loadstring(L, "return require('orion.controller').scope") != LUA_OK || !script_call(L, 0, 1, error, size, &refused)) goto failed;
    lua_pushstring(L, directory); lua_pushcclosure(L, runtime_read, 1);
    lua_getglobal(L, "require"); lua_pushglobaltable(L);
    if (!script_call(L, 3, 1, error, size, &refused)) goto failed;
    lua_pushvalue(L, -1); lua_setfield(L, scopes, directory);
  }
  int environment = lua_gettop(L);
  if (luaL_loadfilex(L, path, "t") != LUA_OK) { snprintf(error, size, "%s", lua_tostring(L, -1)); goto failed; }
  lua_pushvalue(L, environment); lua_setupvalue(L, -2, 1);
  if (!script_call(L, 0, 1, error, size, &refused)) goto failed;
  if (!lua_istable(L, -1)) { snprintf(error, size, "controller must return an object: %s", path); goto failed; }
  int reference = luaL_ref(L, LUA_REGISTRYINDEX);
  lua_settop(L, base); runtime->controllers++;
  return reference;
failed:
  fprintf(stderr, "[runtime] controller %s: %s\n", path, error ? error : "load failed"); fflush(stderr);
  lua_settop(L, base); return LUA_NOREF;
}
bool script_runtime_invoke(script_runtime_t *runtime, int controller, const char *method, const char *argument,
                           char *error, size_t size) {
  if (!runtime || controller == LUA_NOREF) return false;
  lua_State *L = runtime->L;
  int base = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, controller); lua_getfield(L, -1, method);
  if (!lua_isfunction(L, -1)) { lua_settop(L, base); return false; }
  lua_insert(L, -2); lua_pushstring(L, argument ? argument : "");
  bool refused = false;
  bool ok = script_call(L, 2, 0, error, size, &refused);
  lua_settop(L, base);
  if (!ok) { fprintf(stderr, "[runtime] %s: %s\n", method, error); fflush(stderr); }
  return ok;
}
void script_runtime_release(script_runtime_t *runtime, int controller) {
  if (!runtime || controller == LUA_NOREF || controller == LUA_REFNIL) return;
  luaL_unref(runtime->L, LUA_REGISTRYINDEX, controller);
  if (runtime->controllers) runtime->controllers--;
}

void script_runtime_page_retain(script_runtime_t *runtime) { if (runtime) runtime->pages++; }
void script_runtime_page_release(script_runtime_t *runtime) { if (runtime && runtime->pages) runtime->pages--; }

bool script_runtime_action(script_runtime_t *runtime, const char *module, const char *method, const char *argument, char *error, size_t size) {
  if (!runtime) return false;
  lua_State *L = runtime->L; int base = lua_gettop(L); bool refused = false;
  lua_getglobal(L, "require"); lua_pushstring(L, module);
  bool ok = script_call(L, 1, 1, error, size, &refused);
  if (ok) {
    lua_getfield(L, -1, method); lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) { snprintf(error, size, "unknown action %s.%s", module, method); ok = false; }
    else { if (argument) lua_pushstring(L, argument); ok = script_call(L, argument ? 1 : 0, 0, error, size, &refused); }
  }
  lua_settop(L, base);
  if (!ok) { fprintf(stderr, "[runtime] action %s.%s: %s\n", module, method, error); fflush(stderr); }
  return ok;
}

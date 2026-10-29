#include "template.h"
#include "script_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEMPLATE_SOURCE "orion.template_source.context"

static const script_template_source_t *template_source(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, TEMPLATE_SOURCE);
  const script_template_source_t *source = lua_touserdata(L, -1);
  lua_pop(L, 1);
  if (!source) luaL_error(L, "no native template source is active");
  return source;
}

static int template_source_value(lua_State *L, int row, const char *name) {
  const script_template_source_t *source = template_source(L);
  char value[1024];
  if (source->resolve(source->userdata, row, name, value, sizeof(value))) lua_pushstring(L, value);
  else lua_pushnil(L);
  return 1;
}

typedef struct { lua_State *L; int count; } template_emitter_t;
static void template_emit(int row, void *target) {
  template_emitter_t *emitter = target;
  lua_pushinteger(emitter->L, row);
  lua_rawseti(emitter->L, -2, ++emitter->count);
}

static int template_source_rows(lua_State *L, const char *name, int parent) {
  const script_template_source_t *source = template_source(L);
  lua_newtable(L);
  template_emitter_t emitter = {L, 0};
  if (!source->rows(source->userdata, name, parent, template_emit, &emitter))
    return luaL_error(L, "template source cannot read block '%s'", name);
  return 1;
}

static int template_source_filter(lua_State *L, const char *name, const char *value) {
  const script_template_source_t *source = template_source(L);
  char formatted[1024];
  if (source->filter && source->filter(source->userdata, name, value, formatted, sizeof(formatted))) lua_pushstring(L, formatted);
  else lua_pushnil(L);
  return 1;
}
#include "build/generated/orion/script/template_source_lua.h"

bool script_template_render(const char *text, const char *format, const script_template_source_t *source,
                            char **output, char *error, size_t error_size) {
  char message[4096] = "";
  if (output) *output = NULL;
  if (error && error_size) error[0] = 0;
  if (!text || !source || !source->resolve || !source->rows || !output) {
    snprintf(message, sizeof(message), "text, output and native source callbacks are required");
  } else {
    lua_State *L = script_state_new(NULL, "template");
    if (!L) snprintf(message, sizeof(message), "cannot create the template Lua state");
    else {
      lua_pushlightuserdata(L, (void *)source); lua_setfield(L, LUA_REGISTRYINDEX, TEMPLATE_SOURCE);
      luaL_requiref(L, "orion.template_source", luaopen_orion_template_source, 0); lua_pop(L, 1);
      const char *entry =
        "local template = require('orion.plugins').use('template', 1) "
        "local native = require('orion.template_source') "
        "return function(text, format) return template.render(text, {}, {format=format, "
        "source={resolve=native.value, rows=native.rows, filter=native.filter}}) end";
      bool refused = false;
      bool ok = luaL_loadstring(L, entry) == LUA_OK;
      if (!ok) snprintf(message, sizeof(message), "%s", lua_tostring(L, -1));
      else ok = script_call(L, 0, 1, message, sizeof(message), &refused);
      if (ok) {
        lua_pushstring(L, text); lua_pushstring(L, format ? format : "html");
        ok = script_call(L, 2, 1, message, sizeof(message), &refused);
      }
      if (ok && lua_type(L, -1) == LUA_TSTRING) {
        size_t length;
        const char *rendered = lua_tolstring(L, -1, &length);
        *output = malloc(length + 1);
        if (*output) memcpy(*output, rendered, length + 1);
        else snprintf(message, sizeof(message), "cannot allocate template output");
      } else if (ok) snprintf(message, sizeof(message), "template plugin did not return text");
      lua_close(L);
    }
  }
  if (*message) {
    fprintf(stderr, "[template] %s\n", message); fflush(stderr);
    if (error && error_size) snprintf(error, error_size, "%s", message);
  }
  return output && *output;
}

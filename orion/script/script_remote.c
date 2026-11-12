#define _POSIX_C_SOURCE 200809L
#include "remote.h"
#include "remote_outbox.h"
#include "script_internal.h"
#include <orion/kernel/http.h>
#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/i18n.h>
#include <platform/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <pthread.h>
#endif

static char remote_origin[2048], remote_token[513];
static char remote_app[32], remote_company[33], remote_actor[101];
static bool configured;
extern bool is_valid_window_ptr(window_t *target, window_t *list);

bool script_remote_configure_from_environment(char *error, size_t size) {
  const char *url = getenv("ORION_SERVER_URL"), *token = getenv("ORION_SERVER_TOKEN");
  if (!url || !token || !*token || strlen(url) >= sizeof(remote_origin) || strlen(token) >= sizeof(remote_token)) goto invalid;
  const char *host = NULL;
  if (!strncmp(url, "https://", 8)) host = url + 8;
  else if (!strncmp(url, "http://127.0.0.1", 16) && (url[16] == 0 || url[16] == ':')) host = url + 7;
  if (!host || !*host) goto invalid;
  for (const unsigned char *p = (const unsigned char *)host; *p; p++)
    if (*p <= 32 || *p >= 127 || *p == '/' || *p == '\\' || *p == '@' || *p == '?' || *p == '#') goto invalid;
  for (const unsigned char *p = (const unsigned char *)token; *p; p++)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) goto invalid;
  if (configured && (strcmp(remote_origin, url) || strcmp(remote_token, token))) goto invalid;
  snprintf(remote_origin, sizeof(remote_origin), "%s", url); snprintf(remote_token, sizeof(remote_token), "%s", token);
  configured = true; return true;
invalid:
  if (error && size) snprintf(error, size, "%s", ui_tr("Remote server configuration is missing or invalid."));
  return false;
}
bool script_remote_manifest(const char *local, char *remote, size_t size) {
  if (!local || !remote || !size) return false;
  struct stat info;
  int written;
  if (!stat(local, &info) && S_ISDIR(info.st_mode)) written = snprintf(remote, size, "%s/remote/remote.orion", local);
  else {
    const char *slash = strrchr(local, '/');
    written = slash ? snprintf(remote, size, "%.*s/remote/remote.orion", (int)(slash - local), local) : snprintf(remote, size, "remote/remote.orion");
  }
  return written >= 0 && (size_t)written < size;
}

#ifndef _WIN32
static bool valid_identifier(const char *value, size_t maximum) {
  if (!value || !*value || strlen(value) > maximum) return false;
  for (const unsigned char *p = (const unsigned char *)value; *p; p++)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.' || *p == ':')) return false;
  return true;
}
static bool bind_status_identity(lua_State *L, const char *body, size_t length) {
  if (!body || memchr(body, 0, length)) return false;
  int base = lua_gettop(L); char error[1024]; bool refused; bool valid = false;
  lua_getglobal(L, "require"); lua_pushliteral(L, "orion.json");
  if (!script_call(L, 1, 1, error, sizeof(error), &refused)) goto done;
  lua_getfield(L, -1, "decode"); lua_pushlstring(L, body, length);
  if (!script_call(L, 1, 1, error, sizeof(error), &refused) || !lua_istable(L, -1)) goto done;
  lua_getfield(L, -1, "protocol"); bool protocol = lua_isinteger(L, -1) && lua_tointeger(L, -1) == 1; lua_pop(L, 1);
  lua_getfield(L, -1, "revision"); bool revision = lua_isinteger(L, -1) && lua_tointeger(L, -1) >= 0; lua_pop(L, 1);
  if (!protocol || !revision) goto done;
  lua_getfield(L, -1, "application"); lua_getfield(L, -2, "company_id"); lua_getfield(L, -3, "actor");
  const char *app = lua_tostring(L, -3), *company = lua_tostring(L, -2), *actor = lua_tostring(L, -1);
  if (!valid_identifier(app, sizeof(remote_app) - 1) || !valid_identifier(actor, sizeof(remote_actor) - 1) || !company || strlen(company) != 32) goto done;
  for (const char *p = company; *p; p++) if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) goto done;
  if (*remote_app && (strcmp(remote_app, app) || strcmp(remote_company, company) || strcmp(remote_actor, actor))) goto done;
  snprintf(remote_app, sizeof(remote_app), "%s", app); snprintf(remote_company, sizeof(remote_company), "%s", company);
  snprintf(remote_actor, sizeof(remote_actor), "%s", actor); valid = true;
done:
  lua_settop(L, base); return valid;
}
static bool valid_envelope(lua_State *L, const char *body) {
  if (!*remote_app || !body || strlen(body) > 1048576 || (*remote_token && strstr(body, remote_token))) return false;
  int base = lua_gettop(L); char error[1024]; bool refused, valid = false;
  lua_getglobal(L, "require"); lua_pushliteral(L, "orion.json");
  if (!script_call(L, 1, 1, error, sizeof(error), &refused)) goto done;
  lua_getfield(L, -1, "decode"); lua_pushstring(L, body);
  if (!script_call(L, 1, 1, error, sizeof(error), &refused) || !lua_istable(L, -1)) goto done;
  int object = lua_gettop(L), count = 0;
  lua_pushnil(L);
  while (lua_next(L, object)) {
    const char *key = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : NULL;
    if (!key || (strcmp(key, "request_id") && strcmp(key, "expected_revision") && strcmp(key, "command") && strcmp(key, "args"))) { lua_pop(L, 2); goto done; }
    count++; lua_pop(L, 1);
  }
  if (count != 4) goto done;
  lua_getfield(L, object, "request_id"); bool id = lua_type(L, -1) == LUA_TSTRING && valid_identifier(lua_tostring(L, -1), 100); lua_pop(L, 1);
  lua_getfield(L, object, "command"); bool command = lua_type(L, -1) == LUA_TSTRING && valid_identifier(lua_tostring(L, -1), 100); lua_pop(L, 1);
  lua_getfield(L, object, "expected_revision"); bool revision = lua_isinteger(L, -1) && lua_tointeger(L, -1) >= 0; lua_pop(L, 1);
  lua_getfield(L, object, "args"); bool args = lua_istable(L, -1); lua_pop(L, 1);
  valid = id && command && revision && args;
done:
  lua_settop(L, base); return valid;
}
typedef struct {
  lua_State *L;
  int page, callback;
  pthread_t thread;
  pthread_mutex_t mutex;
  bool started, cancelled;
  window_t *window;
  uint64_t serial;
  uint32_t id;
  void *ticket;
  char *body;
  char url[2100];
  const char *method;
  http_response_t *response;
} remote_request_t;
static bool_t request_cancelled(void *data) {
  remote_request_t *r = data;
  pthread_mutex_lock(&r->mutex); bool cancelled = r->cancelled; pthread_mutex_unlock(&r->mutex);
  return cancelled;
}
static void *request_worker(void *data) {
  remote_request_t *r = data;
  char *body = NULL; size_t length = 0; int status = 0;
  int code = axHttpRpc(r->method, r->url, remote_token, r->body, 30000, request_cancelled, r, &body, &length, &status);
  r->response->status = code == AX_HTTP_RPC_OK ? status : 0;
  r->response->body = body; r->response->body_len = length;
  if (code != AX_HTTP_RPC_OK) r->response->error = "The request outcome is uncertain; retry the same request.";
  http_external_complete(r->ticket, r->response);
  return NULL;
}
static result_t remote_window(window_t *window, uint32_t message, uint32_t wparam, void *lparam) {
  (void)wparam;
  remote_request_t *r = window->userdata;
  if (message == evCreate) { window->userdata = lparam; return true; }
  if (message == evHttpDone && r) {
    if (r->started) { pthread_join(r->thread, NULL); r->started = false; }
    http_response_t *response = lparam;
    lua_State *L = r->L; int page = r->page, callback = r->callback;
    uint64_t serial = window->lifetime_serial;
    if (response && response->status == 200 && !strcmp(r->method, "GET") && !bind_status_identity(L, response->body, response->body_len)) {
      response->status = 0; response->error = "Remote server identity is invalid or changed.";
    }
    r->callback = LUA_NOREF;
    lua_rawgeti(L, LUA_REGISTRYINDEX, callback); luaL_unref(L, LUA_REGISTRYINDEX, callback);
    lua_pushinteger(L, response ? response->status : 0);
    if (response && response->body) lua_pushlstring(L, response->body, response->body_len); else lua_pushnil(L);
    if (response && response->error) lua_pushstring(L, ui_tr(response->error)); else lua_pushnil(L);
    r->response = NULL; http_response_free(response);
    char error[1024]; bool refused;
    if (!script_call(L, 3, 0, error, sizeof(error), &refused)) { fprintf(stderr, "[remote] callback failed page=%d: %s\n", page, error); fflush(stderr); }
    script_page_remote_refresh(L, page);
    if (is_valid_window_ptr(window, g_ui_runtime.windows) && window->lifetime_serial == serial) destroy_window(window);
    return true;
  }
  if (message == evDestroy && r) {
    pthread_mutex_lock(&r->mutex); r->cancelled = true; pthread_mutex_unlock(&r->mutex);
    if (r->started) { pthread_join(r->thread, NULL); r->response = NULL; }
    else http_response_free(r->response);
    if (r->callback != LUA_NOREF) luaL_unref(r->L, LUA_REGISTRYINDEX, r->callback);
    pthread_mutex_destroy(&r->mutex); free(r->body); free(r); window->userdata = NULL;
    return true;
  }
  return false;
}
#endif
static int remote_outbox_failure(lua_State *L) {
  lua_pushnil(L); lua_pushstring(L, ui_tr("The pending request could not be stored or recovered.")); return 2;
}
static int script_remote_outbox_load(lua_State *L) {
#ifndef _WIN32
  if (!*remote_app) return remote_outbox_failure(L);
  char *body = NULL; int result = remote_outbox_load(remote_app, remote_origin, remote_company, remote_actor, &body);
  if (result == REMOTE_OUTBOX_EMPTY) { lua_pushnil(L); return 1; }
  if (result != REMOTE_OUTBOX_OK || !valid_envelope(L, body)) { free(body); return remote_outbox_failure(L); }
  lua_pushstring(L, body); free(body); return 1;
#else
  return remote_outbox_failure(L);
#endif
}
static int script_remote_outbox_save(lua_State *L, const char *body) {
#ifndef _WIN32
  if (valid_envelope(L, body) && remote_outbox_save(remote_app, remote_origin, remote_company, remote_actor, body) == REMOTE_OUTBOX_OK) { lua_pushboolean(L, true); return 1; }
#else
  (void)body;
#endif
  return remote_outbox_failure(L);
}
static int script_remote_outbox_clear(lua_State *L, const char *body) {
#ifndef _WIN32
  if (valid_envelope(L, body) && remote_outbox_clear(remote_app, remote_origin, remote_company, remote_actor, body) == REMOTE_OUTBOX_OK) { lua_pushboolean(L, true); return 1; }
#else
  (void)body;
#endif
  return remote_outbox_failure(L);
}
static int script_remote_new_id(lua_State *L) {
#ifndef _WIN32
  unsigned char bytes[16]; FILE *file = fopen("/dev/urandom", "rb");
  if (!file || fread(bytes, 1, sizeof(bytes), file) != sizeof(bytes)) { if (file) fclose(file); return luaL_error(L, "secure request ID unavailable"); }
  fclose(file); char id[33];
  for (int i = 0; i < 16; i++) snprintf(id + i * 2, 3, "%02x", bytes[i]);
  lua_pushstring(L, id); return 1;
#else
  return luaL_error(L, "secure request ID unavailable");
#endif
}
static int script_remote_request(lua_State *L, int handle, const char *method, const char *body, int callback) {
#ifdef _WIN32
  (void)handle; (void)method; (void)body; (void)callback;
  lua_pushnil(L); lua_pushstring(L, ui_tr("Remote connections are unavailable on this platform.")); return 2;
#else
  window_t *parent = script_page_remote_window(L, handle);
  if (!configured || !parent || (strcmp(method, "GET") && strcmp(method, "POST")) || strlen(body) > 1048576 || (!strcmp(method, "GET") && *body)) {
    lua_pushnil(L); lua_pushstring(L, ui_tr("Remote request is unavailable.")); return 2;
  }
  if (!strcmp(method, "POST")) {
    char *saved = NULL;
    int loaded = valid_envelope(L, body) ? remote_outbox_load(remote_app, remote_origin, remote_company, remote_actor, &saved) : REMOTE_OUTBOX_ERROR;
    bool matches = loaded == REMOTE_OUTBOX_OK && saved && !strcmp(saved, body);
    free(saved);
    if (!matches) return remote_outbox_failure(L);
  }
  remote_request_t *r = calloc(1, sizeof(*r));
  if (!r) { lua_pushnil(L); lua_pushstring(L, ui_tr("Remote request is unavailable.")); return 2; }
  r->callback = LUA_NOREF; r->L = L; r->page = handle; r->method = !strcmp(method, "GET") ? "GET" : "POST";
  r->body = !strcmp(method, "POST") ? strdup(body) : NULL;
  r->response = calloc(1, sizeof(*r->response));
  snprintf(r->url, sizeof(r->url), "%s/v1/%s", remote_origin, !strcmp(method, "GET") ? "status" : "command");
  if ((!strcmp(method, "POST") && !r->body) || !r->response || pthread_mutex_init(&r->mutex, NULL)) {
    free(r->body); free(r->response); free(r); lua_pushnil(L); lua_pushstring(L, ui_tr("Remote request is unavailable.")); return 2;
  }
  lua_pushvalue(L, callback); r->callback = luaL_ref(L, LUA_REGISTRYINDEX);
  irect16_t frame = {0, 0, 1, 1};
  window_t *window = create_window("", WINDOW_HIDDEN | WINDOW_NOTITLE, &frame, NULL, remote_window, 0, r);
  if (!window || !window_set_owner(window, parent)) {
    if (window) destroy_window(window); else { luaL_unref(L, LUA_REGISTRYINDEX, r->callback); pthread_mutex_destroy(&r->mutex); free(r->body); free(r->response); free(r); }
    lua_pushnil(L); lua_pushstring(L, ui_tr("Remote request is unavailable.")); return 2;
  }
  r->window = window; r->serial = window->lifetime_serial;
  r->ticket = http_external_begin(window, r->serial, &r->id);
  if (!r->ticket) { free(r->response); r->response = NULL; destroy_window(window); lua_pushnil(L); lua_pushstring(L, ui_tr("Remote request is unavailable.")); return 2; }
  if (pthread_create(&r->thread, NULL, request_worker, r)) {
    http_external_complete(r->ticket, NULL); free(r->response); r->response = NULL;
    destroy_window(window); lua_pushnil(L); lua_pushstring(L, ui_tr("Remote request is unavailable.")); return 2;
  }
  r->started = true;
  lua_pushinteger(L, r->id); return 1;
#endif
}
#include "build/generated/orion/script/remote_lua.h"

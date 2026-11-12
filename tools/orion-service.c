// One trusted, data-only business request. The gateway owns the staging directory.
#include <orion/script/application.h>
#include <orion/script/runtime.h>
#include <orion/script/script_internal.h>
#include <orion/user/database.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trusted_envelope(script_runtime_t *runtime, const char *source, const char *actor, const char *role,
                               char *error, size_t size) {
  lua_State *L = script_runtime_state(runtime); int base = lua_gettop(L); bool refused;
  char *result = NULL;
  lua_getglobal(L, "require"); lua_pushstring(L, "orion.json");
  if (!script_call(L, 1, 1, error, size, &refused)) goto done;
  int json = lua_gettop(L);
  lua_getfield(L, json, "decode"); lua_pushstring(L, source);
  if (!script_call(L, 1, 1, error, size, &refused)) goto done;
  if (!lua_istable(L, -1)) { snprintf(error, size, "request must be an object"); goto done; }
  lua_pushstring(L, actor); lua_setfield(L, -2, "actor");
  lua_pushstring(L, role); lua_setfield(L, -2, "role");
  lua_getfield(L, json, "encode"); lua_pushvalue(L, -2);
  if (!script_call(L, 1, 1, error, size, &refused)) goto done;
  result = strdup(luaL_checkstring(L, -1));
  if (!result) snprintf(error, size, "cannot allocate service envelope");
done:
  lua_settop(L, base); return result;
}

int main(int argc, char **argv) {
  if (argc == 2 && !strcmp(argv[1], "--version")) {
    printf("{\"host_api\":1,\"runtime_api\":%d}\n", ORION_RUNTIME_API); return 0;
  }
  const char *application = NULL, *data = NULL, *request = NULL, *actor = NULL, *role = NULL;
  for (int i = 1; i < argc; i++) {
    if (i + 1 < argc && !strcmp(argv[i], "--application")) application = argv[++i];
    else if (i + 1 < argc && !strcmp(argv[i], "--data")) data = argv[++i];
    else if (i + 1 < argc && !strcmp(argv[i], "--request")) request = argv[++i];
    else if (i + 1 < argc && !strcmp(argv[i], "--actor")) actor = argv[++i];
    else if (i + 1 < argc && !strcmp(argv[i], "--role")) role = argv[++i];
    else { fprintf(stderr, "[service] invalid argument\n"); return 2; }
  }
  if (!application || !data || !request || !actor || !*actor || strlen(actor) > 100 || !role ||
      (strcmp(role, "admin") && strcmp(role, "reception") && strcmp(role, "housekeeping") && strcmp(role, "auditor"))) {
    fprintf(stderr, "Usage: orion-service --application DIR --data STAGE --request JSON --actor ID --role ROLE\n"); return 2;
  }
  FILE *file = fopen(request, "rb");
  if (!file) { fprintf(stderr, "[service] cannot open request\n"); return 2; }
  char *source = malloc(1048578);
  if (!source) { fclose(file); return 1; }
  size_t length = fread(source, 1, 1048577, file);
  bool read_ok = !ferror(file) && length <= 1048576 && !memchr(source, 0, length);
  fclose(file); source[length] = 0;
  if (!read_ok) { fprintf(stderr, "[service] invalid request size or encoding\n"); free(source); return 2; }
  char error[2048];
  script_application_t *app = script_application_open_headless(application, data, error, sizeof(error));
  if (!app) { fprintf(stderr, "[service] %s\n", error); free(source); return 1; }
  uint32_t capabilities = DB_ACCESS_READ;
  if (strcmp(role, "auditor")) capabilities |= DB_ACCESS_WRITE;
  if (!strcmp(role, "admin")) capabilities |= DB_ACCESS_ADMIN;
  bool access = true;
  for (int i = 0; get_database_at(i); i++)
    if (!database_set_access(get_database_at(i), actor, capabilities, NULL, NULL, NULL)) access = false;
  if (!access) snprintf(error, sizeof(error), "cannot establish service session");
  char *envelope = access ? trusted_envelope(script_application_runtime(app), source, actor, role, error, sizeof(error)) : NULL;
  char *response = envelope ? script_runtime_request(script_application_runtime(app), "service", envelope, error, sizeof(error)) : NULL;
  free(envelope);
  free(source);
  // Domain handlers explicitly save. Never publish dirty state on failed requests.
  for (int i = 0; get_database_at(i); i++) database_discard_on_destroy(get_database_at(i));
  bool success = response != NULL;
  if (response) { puts(response); free(response); }
  else fprintf(stderr, "[service] %s\n", error);
  script_application_close(app);
  return success ? 0 : 1;
}

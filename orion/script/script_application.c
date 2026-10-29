#include "application.h"
#include <orion/commctl/commctl.h>
#include "model.h"
#include <orion/user/display_group.h>
#include "script_internal.h"
#include <orion/user/file.h>
#include <orion/commdlg/msgbox.h>
#include <platform/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define APP_CONTEXT "orion.application.context"
#define APP_MAX_PAGES 64
#define APP_MAX_DATABASES 16
struct script_application_s {
  struct script_application_s *next;
  form_library_t *library;
  script_runtime_t *runtime;
  database_t *databases[APP_MAX_DATABASES];
  script_page_t *pages[APP_MAX_PAGES];
  int database_count, page_count;
  int observers[APP_MAX_DATABASES], model;
  char directory[1200], data[1200], output[1600];
  bool quit_on_close;
};
static script_application_t *applications;

static void app_data_changed(database_t *database, void *userdata) {
  script_application_t *app = userdata;
  for (int i = 0; i < app->page_count; i++) {
    if (!strcmp(database->class_name, "db_display_group")) script_page_update(app->pages[i]);
    else script_page_refresh(app->pages[i]);
  }
}
static bool app_save_all(script_application_t *app) {
  for (int i = 0; i < app->database_count; i++)
    if (app->databases[i]->source_path[0] && !send_db_message(app->databases[i], dbSave, 0, NULL)) return false;
  return true;
}
static result_t app_window_hook(window_t *window, uint32_t msg, uint32_t wparam, void *lparam) {
  script_application_t *owner = NULL;
  for (script_application_t *app = applications; app && !owner; app = app->next)
    for (int p = 0; p < app->page_count; p++) if (script_page_window(app->pages[p]) == window) { owner = app; break; }
  if (!owner) return false;
  if (msg == evClose && !app_save_all(owner)) {
    message_box(window, ui_tr("The data could not be saved. Retry before closing."), ui_tr("Save"), MB_OK);
    return true;
  }
  if (msg == evDestroy && owner->quit_on_close && script_page_window(owner->pages[0]) == window) ui_request_quit();
  return false;
}
static script_page_t *app_open_form(script_application_t *app, const char *name, char *error, size_t size) {
  if (app->page_count >= APP_MAX_PAGES) { snprintf(error, size, "application window limit reached"); return NULL; }
  const form_def_t *form = form_library_form(app->library, name);
  if (!form) { snprintf(error, size, "unknown application form: %s", name ? name : ""); return NULL; }
  const char *module = form_library_form_attr(app->library, form, "controller");
  char path[1600] = "";
  if (module && *module) {
    if (strpbrk(module, "/\\") || strstr(module, "..")) { snprintf(error, size, "invalid controller module"); return NULL; }
    snprintf(path, sizeof(path), "%s/%s.lua", app->directory, module);
  }
  script_page_t *page = script_page_open_library(app->runtime, app->library, name, path, error, size);
  if (!page) return NULL;
  app->pages[app->page_count++] = page;
  if (!script_page_create_hook(page, NULL, app_window_hook)) { app->page_count--; script_page_close(page); return NULL; }
  return page;
}
static script_application_t *application_context(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, APP_CONTEXT);
  script_application_t *app = lua_touserdata(L, -1); lua_pop(L, 1);
  if (!app) luaL_error(L, "no application host is active");
  return app;
}
static bool application_api_save(lua_State *L, const char *database) {
  script_application_t *app = application_context(L);
  for (int i = 0; i < app->database_count; i++) {
    if (strcmp(app->databases[i]->name, database)) continue;
    if (send_db_message(app->databases[i], dbSave, 0, NULL)) return true;
    script_refuse(L, ui_tr("The data could not be saved. Retry before closing."));
  }
  luaL_error(L, "application does not own datasource '%s'", database);
  return false;
}
static const char *application_api_write(lua_State *L, const char *filename, const char *text) {
  script_application_t *app = application_context(L);
  if (!*filename || strpbrk(filename, "/\\:") || strstr(filename, "..")) { luaL_error(L, "output must be a document filename"); return NULL; }
  char temporary[1700];
  snprintf(app->output, sizeof(app->output), "%s/%s", app->data, filename);
  snprintf(temporary, sizeof(temporary), "%s.tmp", app->output);
  FILE *file = fopen(temporary, "wb");
  bool ok = false;
  if (file) { bool written = fwrite(text, 1, strlen(text), file) == strlen(text); ok = fclose(file) == 0 && written; }
  if (ok) ok = file_replace(temporary, app->output);
  if (!ok) { remove(temporary); script_refuse(L, ui_tr("The document could not be written.")); return NULL; }
  return app->output;
}
static int application_api_open(lua_State *L, const char *form) {
  char error[1024];
  script_page_t *page = app_open_form(application_context(L), form, error, sizeof(error));
  if (!page) { script_refuse(L, error); return 0; }
  return script_page_handle(page);
}
#include "build/generated/orion/script/application_lua.h"

script_application_t *script_application_open(const char *manifest, const char *data_directory, char *error, size_t size) {
  char fallback[1024];
  if (!error || !size) { error = fallback; size = sizeof(fallback); }
  error[0] = 0;
  char application_manifest[1600];
  struct stat info;
  if (manifest && stat(manifest, &info) == 0 && S_ISDIR(info.st_mode)) {
    int length = snprintf(application_manifest, sizeof(application_manifest), "%s/application.orion", manifest);
    if (length < 0 || (size_t)length >= sizeof(application_manifest)) {
      snprintf(error, size, "application directory is too long"); return NULL;
    }
    manifest = application_manifest;
  }
  script_application_t *app = calloc(1, sizeof(*app));
  if (!app) return NULL;
  app->model = LUA_NOREF;
  app->next = applications; applications = app;
  const char *slash = manifest ? strrchr(manifest, '/') : NULL;
  snprintf(app->directory, sizeof(app->directory), "%.*s", slash ? (int)(slash - manifest) : 1, slash ? manifest : ".");
  app->library = form_library_load(manifest);
  if (!app->library) { snprintf(error, size, "cannot load application manifest"); goto failed; }
  const char *name = form_library_application_attr(app->library, "name");
  const char *form = form_library_application_attr(app->library, "form");
  if (!name || !*name || strpbrk(name, "/\\.") || !form) { snprintf(error, size, "application requires name and form"); goto failed; }
  if (data_directory) snprintf(app->data, sizeof(app->data), "%s", data_directory);
  else snprintf(app->data, sizeof(app->data), "%s/%s", axSettingsDirectory(), name);
  axMkDir(app->data);
  char locale[1400]; snprintf(locale, sizeof(locale), "%s/locale", app->directory);
  ui_add_string_catalog_dir(locale);
  register_commctl_classes(); DB_CLASS(db_schema_xml); DB_CLASS(db_display_group);
  for (int i = 0; i < form_library_schema_count(app->library); i++) {
    const db_schema_def_t *schema = form_library_schema_at(app->library, i);
    for (int j = 0; get_database_at(j); j++) if (!strcmp(get_database_at(j)->name, schema->name)) {
      snprintf(error, size, "datasource '%s' already belongs to another application", schema->name); goto failed;
    }
    if (app->database_count >= APP_MAX_DATABASES) { snprintf(error, size, "too many application datasources"); goto failed; }
    char path[1600] = "";
    bool display_group = schema->class_name && !strcmp(schema->class_name, "db_display_group");
    if (display_group) snprintf(path, sizeof(path), "%s", schema->source_path);
    else if (schema->source_path && *schema->source_path) {
      if (strpbrk(schema->source_path, "/\\:") || strstr(schema->source_path, "..")) { snprintf(error, size, "invalid datasource filename"); goto failed; }
      snprintf(path, sizeof(path), "%s/%s", app->data, schema->source_path);
    }
    database_t *database = create_database_with_schema(schema->name, schema->class_name ? schema->class_name : "db_schema_xml", schema, path);
    if (!database || !register_database(schema->name, database)) { if (database) destroy_database(database); snprintf(error, size, "cannot create application datasource"); goto failed; }
    app->databases[app->database_count++] = database;
  }
  app->runtime = script_runtime_open(app->directory, name);
  if (!app->runtime) { snprintf(error, size, "cannot create application runtime"); goto failed; }
  lua_State *L = script_runtime_state(app->runtime);
  lua_pushlightuserdata(L, app); lua_setfield(L, LUA_REGISTRYINDEX, APP_CONTEXT);
  luaL_requiref(L, "orion.application", luaopen_orion_application, 0); lua_pop(L, 1);
  const char *model = form_library_application_attr(app->library, "model");
  if (model && *model) {
    char path[1600];
    if (strpbrk(model, "/\\") || strstr(model, "..")) { snprintf(error, size, "invalid application model"); goto failed; }
    snprintf(path, sizeof(path), "%s/%s.lua", app->directory, model);
    app->model = script_runtime_controller(app->runtime, path, error, size);
    if (app->model == LUA_NOREF) goto failed;
    for (int i = 0; i < app->database_count; i++)
      if (strcmp(app->databases[i]->class_name, "db_display_group") && !script_model_attach(app->databases[i], app->runtime, app->model)) goto failed;
  }
  for (int i = 0; i < app->database_count; i++) app->observers[i] = db_observe(app->databases[i], app_data_changed, app);
  if (!app_open_form(app, form, error, size)) goto failed;
  return app;
failed:
  fprintf(stderr, "[application] %s: %s\n", manifest ? manifest : "?", error); fflush(stderr);
  script_application_close(app); return NULL;
}
void script_application_close(script_application_t *app) {
  if (!app) return;
  for (int i = 0; i < app->database_count; i++) { db_unobserve(app->observers[i]); script_model_detach(app->databases[i]); }
  script_runtime_release(app->runtime, app->model);
  for (int i = app->page_count - 1; i >= 0; i--) script_page_close(app->pages[i]);
  script_runtime_close(app->runtime);
  for (int i = app->database_count - 1; i >= 0; i--) { unregister_database(app->databases[i]->name); destroy_database(app->databases[i]); }
  form_library_free(app->library);
  script_application_t **link = &applications;
  while (*link && *link != app) link = &(*link)->next;
  if (*link) *link = app->next;
  free(app);
}
script_page_t *script_application_main(script_application_t *app) { return app && app->page_count ? app->pages[0] : NULL; }
script_runtime_t *script_application_runtime(script_application_t *app) { return app ? app->runtime : NULL; }
void script_application_quit_on_close(script_application_t *app, bool quit) { if (app) app->quit_on_close = quit; }

#include "application.h"
#include <orion/commctl/commctl.h>
#include "model.h"
#include <orion/user/display_group.h>
#include "script_internal.h"
#include <orion/user/file.h>
#include <orion/user/page_host.h>
#include <orion/user/toolbar.h>
#include <orion/user/svg_icon_loader.h>
#include <orion/commdlg/commdlg.h>
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
  bool quit_on_close, refreshing;
  // The <ribbon>, when the application stands alone (a gem's shell owns the menu row).
  const form_ribbon_t *ribbon;
  window_t *chrome, *ribbon_win;
  accel_table_t *accel;
  uint32_t *command_states;
};
static script_application_t *applications;
static bool ribbon_allowed;
void script_application_allow_ribbon(bool allowed) { ribbon_allowed = allowed; }

static bool app_is_display_group(const database_t *database) { return !strcmp(database->class_name, "db_display_group"); }
// A page filling its lists as it renders, or a change made while pages
// refresh, needs no second refresh.
static void app_data_changed(database_t *database, void *userdata) {
  script_application_t *app = userdata;
  if (app->refreshing || script_page_filling_lists()) return;
  app->refreshing = true;
  for (int i = 0; i < app->page_count; i++) {
    if (app_is_display_group(database)) script_page_update(app->pages[i]);
    else script_page_refresh(app->pages[i]);
  }
  app->refreshing = false;
}
static bool app_save_all(script_application_t *app) {
  for (int i = 0; i < app->database_count; i++)
    if (app->databases[i]->source_path[0] && !send_db_message(app->databases[i], dbSave, 0, NULL)) return false;
  return true;
}
static void app_show_page_tab(script_application_t *app);
static result_t app_window_hook(window_t *window, uint32_t msg, uint32_t wparam, void *lparam) {
  if (msg == evPageChanged) {
    for (script_application_t *app = applications; app; app = app->next)
      if (app->page_count && script_page_window(app->pages[0]) == window) app_show_page_tab(app);
    return false;
  }
  if (msg != evClose && msg != evDestroy) return false;  // the page proc forwards every message
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
// A form and its controller="module", if any, as a page of the application.
static script_page_t *app_page(script_application_t *app, const char *name, char *error, size_t size) {
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
  if (page) app->pages[app->page_count++] = page;
  return page;
}
static void app_drop_pages(script_application_t *app, int from) {
  while (app->page_count > from) {
    script_page_t *page = app->pages[app->page_count - 1];
    window_t *window = script_page_window(page);
    // Hosted windows still use their controller during deactivation and destruction.
    if (window) destroy_window(window);
    app->page_count--;
    script_page_close(page);
  }
}
// A role="host" form docks its ActivityBar destinations, each a page with its
// own controller; the host's window owns their windows.
static bool app_host_pages(script_application_t *app, script_page_t *host, char *error, size_t size) {
  const form_def_t *form = script_page_form(host);
  if (!form || form->role != WINDOW_ROLE_HOST) return true;
  page_handler_t handlers[APP_MAX_PAGES];
  int first = app->page_count, count = form->page_binding_count;
  for (int i = 0; i < count; i++) {
    script_page_t *page = app_page(app, form->page_bindings[i].name, error, size);
    if (!page) { app_drop_pages(app, first); return false; }
    handlers[i] = (page_handler_t){ form->page_bindings[i].name, script_page_window_proc, page };
  }
  if (page_host_init(script_page_window(host), form, handlers, count)) return true;
  snprintf(error, size, "cannot build the pages of form %s", form->id);
  app_drop_pages(app, first);
  return false;
}
static window_t *app_host_window(script_application_t *app);
// A window the application opens over its main one: centred on it, or along
// its right edge with placement="right" (a palette such as an assistant).
static void app_place_window(script_application_t *app, script_page_t *page) {
  window_t *win = script_page_window(page), *host = app_host_window(app);
  const char *placement = form_library_form_attr(app->library, script_page_form(page), "placement");
  if (!win || !host || win == host) return;
  int x = host->frame.x + (host->frame.w - win->frame.w) / 2, y = host->frame.y + (host->frame.h - win->frame.h) / 3;
  if (placement && !strcmp(placement, "right")) { x = host->frame.x + host->frame.w - win->frame.w - 24; y = host->frame.y + 56; }
  else if (placement && strcmp(placement, "center")) {
    fprintf(stderr, "[application] form '%s': placement '%s' rejected (want center or right)\n", script_page_form(page)->id, placement);
    fflush(stderr);
  }
  move_window(win, x > host->frame.x ? x : host->frame.x, y > host->frame.y ? y : host->frame.y);
}
static script_page_t *app_open_form(script_application_t *app, const char *name, char *error, size_t size) {
  int first = app->page_count;
  // single="true": the form's open window comes forward instead of a second one.
  const form_def_t *def = form_library_form(app->library, name);
  const char *single = def ? form_library_form_attr(app->library, def, "single") : NULL;
  for (int i = 0; single && !strcmp(single, "true") && i < app->page_count; i++) {
    window_t *open = script_page_window(app->pages[i]);
    if (script_page_form(app->pages[i]) == def && open && is_window(open)) {
      show_window(open, true);
      script_page_refresh(app->pages[i]);
      return app->pages[i];
    }
  }
  script_page_t *page = app_page(app, name, error, size);
  if (!page) return NULL;
  if (!script_page_create_hook(page, NULL, app_window_hook) || !app_host_pages(app, page, error, size)) {
    app_drop_pages(app, first); return NULL;
  }
  if (first > 0) app_place_window(app, page);
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
// A page of the open host is selected, as a sidebar click selects it.
static script_page_t *app_hosted_page(script_application_t *app, const char *form) {
  script_page_t *page = script_application_page(app, form);
  window_t *win = script_page_window(page), *host = win ? win->parent : NULL;
  const form_def_t *def = host && host->userdata ? script_page_form(host->userdata) : NULL;
  for (int i = 0; def && def->role == WINDOW_ROLE_HOST && i < def->page_binding_count; i++)
    if (page_host_page(host, def->page_bindings[i].command) == win)
      return page_host_select(host, def->page_bindings[i].command) ? page : NULL;
  return NULL;
}
// ── Ribbon ──────────────────────────────────────────────────────────────────
// The ribbon runs a command on the selected page (or the page it names), so
// its buttons, Quick Access Toolbar and shortcuts reach the same Lua actions
// as the pages' own buttons; a page's tab shows while the page is selected.
static window_t *app_host_window(script_application_t *app) {
  window_t *win = app->page_count ? script_page_window(app->pages[0]) : NULL;
  const form_def_t *def = app->page_count ? script_page_form(app->pages[0]) : NULL;
  return win && def && def->role == WINDOW_ROLE_HOST ? win : NULL;
}
static script_page_t *app_selected_page(script_application_t *app) {
  window_t *host = app_host_window(app), *page = host ? window_active_page(host) : NULL;
  for (int i = 0; page && i < app->page_count; i++) if (script_page_window(app->pages[i]) == page) return app->pages[i];
  return NULL;
}
// The page a command acts on: the one it names, or the selected one.
static script_page_t *app_command_page(script_application_t *app, const form_command_t *command) {
  return command->page ? script_application_page(app, command->page) : app_selected_page(app);
}
static void app_update_commands(script_application_t *app) {
  if (!app->ribbon_win || !app->command_states) return;
  window_t *quick = app->chrome ? app_chrome_toolbar(app->chrome) : NULL;
  for (int i = 0; i < app->ribbon->command_count; i++) {
    const form_command_t *command = &app->ribbon->commands[i];
    bool enabled = command->target ? true : script_page_can_perform(app_command_page(app, command), command->action);
    uint32_t state = enabled ? 0 : TBSTATE_DISABLED;
    if (app->command_states[i] == state) continue;
    app->command_states[i] = state;
    uint32_t ident = app->ribbon->first_ident + (uint32_t)i;
    int current = (int)send_message(app->ribbon_win, rbGetState, ident, NULL);
    if (current >= 0) send_message(app->ribbon_win, rbSetState, ident, (void *)(uintptr_t)(((uint32_t)current & ~TBSTATE_DISABLED) | state));
    const application_toolbar_t *qat = app->ribbon->def.quick_access;
    for (int k = 0; quick && qat && k < qat->count; k++)
      if (qat->items[k].ident == (int)ident && toolbar_item_state(quick, (int)ident) >= 0) {
        send_message(quick, tbSetState, ident, (void *)(uintptr_t)(((uint32_t)toolbar_item_state(quick, (int)ident) & ~TBSTATE_DISABLED) | state));
        break;
      }
  }
}
static void app_page_rendered(script_page_t *page, void *userdata) { (void)page; app_update_commands(userdata); }
static void app_show_page_tab(script_application_t *app) {
  if (!app->ribbon_win) return;
  script_page_t *page = app_selected_page(app);
  const form_def_t *def = page ? script_page_form(page) : NULL;
  send_message(app->ribbon_win, rbSetContexts, form_ribbon_context(app->ribbon, def ? def->id : NULL), NULL);
  app_update_commands(app);
}
static script_page_t *app_hosted_page(script_application_t *app, const char *form);
static void app_run_command(script_application_t *app, uint32_t ident) {
  const form_command_t *command = form_ribbon_command(app->ribbon, ident);
  if (!command) {
    fprintf(stderr, "[application] ribbon command rejected ident=%u: no such command\n", (unsigned)ident);
    fflush(stderr);
    return;
  }
  window_t *host = app_host_window(app);
  if (command->target) {
    char message[1024] = "";
    if (!script_runtime_action(app->runtime, command->target, command->action, command->argument, message, sizeof(message)) && *message)
      message_box(host, message, script_page_title(app->pages[0]), MB_OK);
    return;
  }
  script_page_t *page = command->page ? app_hosted_page(app, command->page) : app_selected_page(app);
  if (!page) page = command->page ? script_application_page(app, command->page) : NULL;
  if (!page) {
    fprintf(stderr, "[application] ribbon command '%s' rejected: page '%s' is not open\n", command->action, command->page ? command->page : "(selected)");
    fflush(stderr);
    return;
  }
  if (!script_page_can_perform(page, command->action)) return;   // its button is greyed out too
  script_page_perform(page, command->action);
}
static script_application_t *app_of_chrome(window_t *win) {
  window_t *root = win ? get_root_window(win) : NULL;
  for (script_application_t *app = applications; app; app = app->next)
    if (app->chrome && (app->chrome == win || app->chrome == root || app->ribbon_win == win)) return app;
  return NULL;
}
static result_t app_ribbon_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  script_application_t *app = app_of_chrome(win);
  if (msg == evClose && app && app_host_window(app)) return send_message(app_host_window(app), evClose, 0, NULL);
  if (app && msg == evCommand && (HIWORD(wparam) == btnClicked || HIWORD(wparam) == kMenuBarNotificationItemClick ||
                                  HIWORD(wparam) == kAcceleratorNotification)) {
    app_run_command(app, LOWORD(wparam));
    return true;
  }
  return win_ribbon(win, msg, wparam, lparam);
}
static result_t app_quick_access_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)lparam;
  script_application_t *app = app_of_chrome(win);
  if (!app || msg != evCommand || HIWORD(wparam) != btnClicked) return false;
  app_run_command(app, LOWORD(wparam));
  return true;
}
static bool app_open_ribbon(script_application_t *app, const char *title, char *error, size_t size) {
  app->ribbon = form_library_ribbon(app->library);
  if (!app->ribbon || !ribbon_allowed) { app->ribbon = NULL; return true; }
  app->command_states = calloc((size_t)(app->ribbon->command_count ? app->ribbon->command_count : 1), sizeof(*app->command_states));
  app->chrome = app->command_states ? create_ribbon_chrome(title, app_ribbon_proc, &app->ribbon->def,
                                                           app->ribbon->def.quick_access ? app_quick_access_proc : NULL, 0) : NULL;
  if (!app->chrome) { snprintf(error, size, "cannot create the application ribbon"); return false; }
  app->ribbon_win = app_chrome_ribbon(app->chrome);
  if (app->ribbon->accelerator_count)
    app->accel = load_accelerators(app->ribbon->accelerators, app->ribbon->accelerator_count);
  if (app->accel) send_message(app->ribbon_win, kMenuBarMessageSetAccelerators, 0, app->accel);
  // Every menu goes to the operating system's menu bar where it has one; the ribbon keeps the rest.
  send_message(app->ribbon_win, kMenuBarMessageSetMenus, (uint32_t)app->ribbon->menu_count, (void *)app->ribbon->menus);
  return true;
}
// The host window is maximized into the workspace under the ribbon, as a
// compiled application's main window is, and tells it which page shows.
static void app_place_host(script_application_t *app) {
  window_t *host = app_host_window(app);
  if (!host || !app->chrome) return;
  maximize_window(host);
  for (int i = 0; i < app->page_count; i++) script_page_on_render(app->pages[i], app_page_rendered, app);
  // Windows a page opened while the application started follow the main window.
  for (int i = 1; i < app->page_count; i++) {
    window_t *win = script_page_window(app->pages[i]);
    if (win && !win->parent) app_place_window(app, app->pages[i]);
  }
  app_show_page_tab(app);
}
window_t *script_application_menu_window(script_application_t *app) { return app ? app->ribbon_win : NULL; }
accel_table_t *script_application_accelerators(script_application_t *app) { return app ? app->accel : NULL; }

// File → Exit: closes the main window as its close box does, saving first.
static bool application_api_quit(lua_State *L) {
  script_application_t *app = application_context(L);
  window_t *host = app->page_count ? script_page_window(app->pages[0]) : NULL;
  if (!host) return false;
  post_message(host, evClose, 0, NULL);
  return true;
}
static int application_api_open(lua_State *L, const char *form) {
  char error[1024];
  script_page_t *page = app_hosted_page(application_context(L), form);
  if (page) return script_page_handle(page);
  page = app_open_form(application_context(L), form, error, sizeof(error));
  if (!page) { script_refuse(L, error); return 0; }
  return script_page_handle(page);
}
#include "build/generated/orion/script/application_lua.h"

static script_application_t *app_open(const char *manifest, const char *data_directory, bool recovery, bool headless, char *error, size_t size) {
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
  if (!script_runtime_check_api(form_library_application_attr(app->library, "runtime-api"), error, size)) goto failed;
  if (recovery) {
    int persistent = 0;
    for (int i = 0; i < form_library_schema_count(app->library); i++) {
      const db_schema_def_t *schema = form_library_schema_at(app->library, i);
      if (schema->source_path && *schema->source_path && (!schema->class_name || strcmp(schema->class_name, "db_display_group"))) persistent++;
    }
    if (!data_directory || persistent != 1) { snprintf(error, size, "recovery requires an explicit data directory and exactly one persistent database"); goto failed; }
  }
  const char *name = form_library_application_attr(app->library, "name");
  const char *form = form_library_application_attr(app->library, "form");
  if (!name || !*name || strpbrk(name, "/\\.") || !form) { snprintf(error, size, "application requires name and form"); goto failed; }
  if (data_directory) snprintf(app->data, sizeof(app->data), "%s", data_directory);
  else snprintf(app->data, sizeof(app->data), "%s/%s", axSettingsDirectory(), name);
  axMkDir(app->data);
  char locale[1400]; snprintf(locale, sizeof(locale), "%s/locale", app->directory);
  ui_add_string_catalog_dir(locale);
  char icons[1400]; snprintf(icons, sizeof(icons), "%s/icons", app->directory);
  if (axPathExists(icons)) svg_add_icons_dir(0, icons);
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
    const char *class_name = schema->class_name ? schema->class_name : "db_schema_xml";
    database_t *database = recovery && !display_group && *path ?
      db_schema_xml_open_backup(schema->name, class_name, schema, path) :
      create_database_with_schema(schema->name, class_name, schema, path);
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
      if (!app_is_display_group(app->databases[i]) && !script_model_attach(app->databases[i], app->runtime, app->model)) goto failed;
  }
  for (int i = 0; i < app->database_count; i++) {
    if (!app_is_display_group(app->databases[i]) && !script_model_initialize(app->databases[i])) {
      snprintf(error, size, "%s", app->databases[i]->error); goto failed;
    }
  }
  for (int i = 0; i < app->database_count; i++) app->observers[i] = db_observe(app->databases[i], app_data_changed, app);
  if (!headless && !app_open_ribbon(app, name, error, size)) goto failed;
  if (!headless && !app_open_form(app, form, error, size)) goto failed;
  if (!headless) app_place_host(app);
  return app;
failed:
  fprintf(stderr, "[application] %s: %s\n", manifest ? manifest : "?", error); fflush(stderr);
  for (int i = 0; i < app->database_count; i++) database_discard_on_destroy(app->databases[i]);
  script_application_close(app); return NULL;
}
script_application_t *script_application_open(const char *manifest, const char *data_directory, char *error, size_t size) {
  return app_open(manifest, data_directory, false, false, error, size);
}
script_application_t *script_application_open_headless(const char *manifest, const char *data_directory, char *error, size_t size) {
  return app_open(manifest, data_directory, false, true, error, size);
}
script_application_t *script_application_open_recovery(const char *manifest, const char *data_directory, char *error, size_t size) {
  return app_open(manifest, data_directory, true, false, error, size);
}
void script_application_close(script_application_t *app) {
  if (!app) return;
  for (int i = 0; i < app->database_count; i++) { db_unobserve(app->observers[i]); script_model_detach(app->databases[i]); }
  script_runtime_release(app->runtime, app->model);
  app_drop_pages(app, 0);
  if (app->chrome && is_window(app->chrome)) destroy_window(app->chrome);
  free_accelerators(app->accel);
  free(app->command_states);
  script_runtime_close(app->runtime);
  for (int i = app->database_count - 1; i >= 0; i--) { unregister_database(app->databases[i]->name); destroy_database(app->databases[i]); }
  form_library_free(app->library);
  script_application_t **link = &applications;
  while (*link && *link != app) link = &(*link)->next;
  if (*link) *link = app->next;
  free(app);
}
script_page_t *script_application_main(script_application_t *app) { return app && app->page_count ? app->pages[0] : NULL; }
script_page_t *script_application_page(script_application_t *app, const char *form) {
  for (int i = app ? app->page_count - 1 : -1; form && i >= 0; i--) {
    const form_def_t *def = script_page_form(app->pages[i]);
    if (def && def->id && !strcmp(def->id, form)) return app->pages[i];
  }
  return NULL;
}
script_runtime_t *script_application_runtime(script_application_t *app) { return app ? app->runtime : NULL; }
void script_application_quit_on_close(script_application_t *app, bool quit) { if (app) app->quit_on_close = quit; }

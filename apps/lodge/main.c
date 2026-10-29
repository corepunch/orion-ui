// Lodge: the native host owns storage and the window; Lua owns hotel workflows.
#include <orion/ui.h>
#include <orion/gem.h>
#include <orion/script/script.h>
#include <orion/commdlg/msgbox.h>
#include <platform/platform.h>
#include <lauxlib.h>
#include <stdio.h>
#include <string.h>
#include "build/generated/apps/lodge/lodge.h"

static database_t *lodge_db;
static script_page_t *lodge_page;
static char lodge_data_dir[1024];

static int lodge_store_save(lua_State *L) {
  if (lodge_db && send_db_message(lodge_db, dbSave, 0, NULL)) { lua_pushboolean(L, true); return 1; }
  lua_pushnil(L); lua_pushstring(L, ui_tr("The hotel data could not be saved. Retry before closing."));
  return 2;
}

static int lodge_store_confirmation(lua_State *L, const char *html) {
  char path[1200];
  snprintf(path, sizeof(path), "%s/confirmation.html", lodge_data_dir);
  FILE *file = fopen(path, "wb");
  bool ok = false;
  if (file) { bool written = fwrite(html, 1, strlen(html), file) == strlen(html); ok = fclose(file) == 0 && written; }
  if (ok) { lua_pushstring(L, path); return 1; }
  fprintf(stderr, "[lodge] cannot write confirmation path=%s\n", path); fflush(stderr);
  lua_pushnil(L); lua_pushstring(L, ui_tr("The booking confirmation could not be written."));
  return 2;
}
#include "build/generated/apps/lodge/store_lua.h"

static result_t lodge_window(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (msg == evClose && lodge_db && !send_db_message(lodge_db, dbSave, 0, NULL)) {
    message_box(win, ui_tr("The hotel data could not be saved. Retry before closing."), ui_tr("Lodge"), MB_OK);
    return true;
  }
  if (msg == evDestroy) ui_request_quit();
  return false;
}

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  const char *data = NULL;
  for (int i = 1; i + 1 < argc; i++) {
    if (!strcmp(argv[i], "--data")) data = argv[++i];
    else if (!strcmp(argv[i], "--lang")) ui_set_language(argv[++i]);
  }
  register_commctl_classes();
  DB_CLASS(db_schema_xml);
  snprintf(lodge_data_dir, sizeof(lodge_data_dir), "%s", data ? data : axSettingsDirectory());
  if (!data) {
    size_t n = strlen(lodge_data_dir);
    snprintf(lodge_data_dir + n, sizeof(lodge_data_dir) - n, "/lodge");
  }
  axMkDir(lodge_data_dir);
  char path[1400], share[1200];
  snprintf(path, sizeof(path), "%s/hotel.xml", lodge_data_dir);
  lodge_db = create_database_with_schema("lodge", "db_schema_xml", &lodge_lodge_schema, path);
  if (!lodge_db || !register_database("lodge", lodge_db)) return false;
  script_register_module("lodge.store", luaopen_lodge_store);
  snprintf(share, sizeof(share), "%s/../share/lodge", ui_get_exe_dir());
  snprintf(path, sizeof(path), "%s/locale", share);
  ui_add_string_catalog_dir(path);
  snprintf(path, sizeof(path), "%s/reception", share);
  char error[1024];
  lodge_page = script_page_open(path, error, sizeof(error));
  if (!lodge_page) return false;
  return script_page_create_hook(lodge_page, NULL, lodge_window) != NULL;
}

void gem_shutdown(void) {
  script_page_close(lodge_page); lodge_page = NULL;
  if (lodge_db) { unregister_database("lodge"); destroy_database(lodge_db); lodge_db = NULL; }
}
GEM_DEFINE("Lodge", "0.1", gem_init, gem_shutdown, NULL)
GEM_STANDALONE_MAIN("Orion Lodge", UI_INIT_DESKTOP, 1240, 900, NULL, NULL)

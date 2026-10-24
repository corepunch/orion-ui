// Ledger — entry point. Opens the active configuration (cfg), its infobase
// (ib) and the catalog of installed configurations, then shows the main
// window declared in ledger.orion.
//
// Options:
//   --config CODE   open configuration CODE (default: last selected, ru-2003)
//   --data DIR      infobase directory (default: <settings>/ledger/CODE)
//   --lang CODE     UI language (default: last selected, the configuration's,
//                   then the system's)

#include "ledger.h"
#include <orion/gem.h>
#include <platform/platform.h>
#include <orion/user/svg_icon_loader.h>

static ledger_t g_ledger_state;

static bool find_share_dir(char *out, size_t sz) {
  const char *candidates[] = { "%s/../share/ledger", "%s/../../apps/ledger/share" };
  for (size_t i = 0; i < ARRAY_LEN(candidates); i++) {
    snprintf(out, sz, candidates[i], ui_get_exe_dir());
    if (axPathExists(out)) return true;
  }
  snprintf(out, sz, "apps/ledger/share");
  return axPathExists(out);
}

// A new infobase starts from the configuration's demo data, posted.
static database_t *open_infobase(void) {
  char path[LEDGER_PATH_MAX * 2], demo[LEDGER_PATH_MAX * 2];
  snprintf(path, sizeof(path), "%s/infobase.xml", g_ledger->data_dir);
  bool fresh = !axPathExists(path);
  snprintf(demo, sizeof(demo), "%s/demo.xml", g_ledger->config_dir);
  database_t *ib = create_database_with_schema("ib", "ledger_ib", &ledger_ib_schema, fresh ? demo : path);
  if (!ib) return NULL;
  register_database("ib", ib);
  if (fresh) {
    snprintf(ib->source_path, sizeof(ib->source_path), "%.*s", (int)sizeof(ib->source_path) - 1, path);
    if (!send_db_message(ib, dbFind, MAKEDWORD(TABLE_ORGANIZATIONS, 0), (void *)(intptr_t)1)) {
      db_organization_t org = { .id = 1 };
      snprintf(org.name, sizeof(org.name), "My Organization");
      send_db_message(ib, dbInsert, TABLE_ORGANIZATIONS, &org);
    }
    ledger_repost_all(ib, g_ledger->cfg);
    send_db_message(ib, dbSave, 0, NULL);
  } else {
    ledger_build_reports(ib, g_ledger->cfg);
    ib->dirty = false;  // totals are derived; nothing new to save
  }
  return ib;
}

// String catalogs stack: framework, Ledger's own, then the configuration's,
// which renames national terms ("Tax ID" -> "ИНН") and may do so in any language.
static void choose_ui_language(const char *requested, const char *share) {
  char lang[32] = {0}, path[LEDGER_PATH_MAX * 2];
  size_t loaded = 0;
  db_setting_t *settings = (db_setting_t *)send_db_message(g_ledger->cfg, dbFind, MAKEDWORD(TABLE_SETTINGS, 0), (void *)(intptr_t)1);
  if (requested) snprintf(lang, sizeof(lang), "%s", requested);
  else if (axSettingsLoad("ledger_language", lang, sizeof(lang) - 1, &loaded) && loaded) lang[loaded] = '\0';
  else if (settings && settings->language[0]) snprintf(lang, sizeof(lang), "%s", settings->language);
  else ui_system_language(lang, sizeof(lang));
  ui_set_language(lang);
  snprintf(path, sizeof(path), "%s/locale", share);
  ui_add_string_catalog_dir(path);
  snprintf(path, sizeof(path), "%s/locale", g_ledger->config_dir);
  ui_add_string_catalog_dir(path);
}

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  memset(&g_ledger_state, 0, sizeof(g_ledger_state));
  g_ledger = &g_ledger_state;
  g_ledger->hinstance = hinstance;

  const char *data_dir = NULL, *language = NULL;
  size_t loaded = 0;
  if (!axSettingsLoad("ledger_configuration", g_ledger->configuration, sizeof(g_ledger->configuration) - 1, &loaded) || !loaded)
    snprintf(g_ledger->configuration, sizeof(g_ledger->configuration), "%s", LEDGER_DEFAULT_CONFIGURATION);
  for (int i = 1; i + 1 < argc; i++) {
    if (!strcmp(argv[i], "--config")) snprintf(g_ledger->configuration, sizeof(g_ledger->configuration), "%s", argv[++i]);
    else if (!strcmp(argv[i], "--data")) data_dir = argv[++i];
    else if (!strcmp(argv[i], "--lang")) language = argv[++i];
  }

  char share[LEDGER_PATH_MAX], path[LEDGER_PATH_MAX * 3];
  if (!find_share_dir(share, sizeof(share))) {
    fprintf(stderr, "[ledger] share directory not found (run `make share`)\n");
    return false;
  }
  snprintf(g_ledger->config_dir, sizeof(g_ledger->config_dir), "%.900s/configurations/%s", share, g_ledger->configuration);
  if (data_dir) snprintf(g_ledger->data_dir, sizeof(g_ledger->data_dir), "%s", data_dir);
  else snprintf(g_ledger->data_dir, sizeof(g_ledger->data_dir), "%.900s/ledger/%s", axSettingsDirectory(), g_ledger->configuration);
  snprintf(path, sizeof(path), "%s/ledger", axSettingsDirectory());
  axMkDir(path);
  axMkDir(g_ledger->data_dir);

  snprintf(path, sizeof(path), "%s/icons", share);
  svg_add_icons_dir(hinstance, path);
  register_commctl_classes();
  DB_CLASS(db_schema_xml);
  DB_CLASS(ledger_ib);

  snprintf(path, sizeof(path), "%s/configurations/index.xml", share);
  g_ledger->catalog = create_database_with_schema("catalog", "db_schema_xml", &ledger_catalog_schema, path);
  snprintf(path, sizeof(path), "%s/configuration.xml", g_ledger->config_dir);
  if (!axPathExists(path)) {
    fprintf(stderr, "[ledger] configuration '%s' not found at %s\n", g_ledger->configuration, path);
    return false;
  }
  g_ledger->cfg = create_database_with_schema("cfg", "db_schema_xml", &ledger_cfg_schema, path);
  if (!g_ledger->catalog || !g_ledger->cfg) return false;
  register_database("catalog", g_ledger->catalog);
  register_database("cfg", g_ledger->cfg);
  choose_ui_language(language, share);
  if (!(g_ledger->ib = open_infobase())) return false;

  g_ledger->menubar_win = set_app_menu(ledger_menubar_proc, kLedgerMenus, kLedgerMenuCount,
                                       ledger_command, hinstance);
  g_ledger->accel = load_accelerators(ledger_default_accels, ledger_default_accel_count);
  if (g_ledger->menubar_win)
    send_message(g_ledger->menubar_win, kMenuBarMessageSetAccelerators, 0, g_ledger->accel);

  window_t *win = create_window_from_form(&ledger_main_window_form, 16, MENUBAR_HEIGHT + 16,
                                          NULL, ledger_main_proc, hinstance, NULL);
  if (!win) return false;
  show_window(win, true);
  return true;
}

void gem_shutdown(void) {
  if (!g_ledger) return;
  destroy_database(g_ledger->ib);  // saves when modified
  destroy_database(g_ledger->cfg);
  destroy_database(g_ledger->catalog);
  g_ledger = NULL;
}

GEM_DEFINE("Ledger", "1.0", gem_init, gem_shutdown, NULL)

GEM_STANDALONE_MAIN("Orion Ledger", UI_INIT_DESKTOP, LEDGER_SCREEN_W, LEDGER_SCREEN_H,
                    g_ledger->menubar_win, g_ledger->accel)

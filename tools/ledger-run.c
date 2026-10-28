// Headless host for trusted local Lua. Business operations live in share/automation.
#include "apps/ledger/ledger.h"
#include <platform/platform.h>

ledger_t *g_ledger;
#include "apps/ledger/ledger_ib.c"
#include "apps/ledger/ledger_script.c"
#include "apps/ledger/posting.c"
#include "apps/ledger/print.c"
#include "apps/ledger/home.c"

int main(int argc, char **argv) {
  ledger_t state = {0};
  g_ledger = &state;
  const char *configuration = "ru-2026", *data = NULL, *script = NULL, *share = "apps/ledger/share";
  bool write = false;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--write")) write = true;
    else if (i + 1 < argc && !strcmp(argv[i], "--config")) configuration = argv[++i];
    else if (i + 1 < argc && !strcmp(argv[i], "--data")) data = argv[++i];
    else if (i + 1 < argc && !strcmp(argv[i], "--script")) script = argv[++i];
    else if (i + 1 < argc && !strcmp(argv[i], "--share")) share = argv[++i];
    else { fprintf(stderr, "Usage: ledger-run --data DIR --script FILE [--config CODE] [--share DIR] [--write]\n"); return 2; }
  }
  if (!data || !script || strchr(configuration, '/') || strchr(configuration, '\\')) {
    fprintf(stderr, "[ledger-run] --data and --script are required; --config must be a code\n"); return 2;
  }
  if (snprintf(state.config_dir, sizeof(state.config_dir), "%s/configurations/%s", share, configuration) >= (int)sizeof(state.config_dir) ||
      snprintf(state.data_dir, sizeof(state.data_dir), "%s", data) >= (int)sizeof(state.data_dir)) return 2;
  char cfg_path[LEDGER_PATH_MAX * 2], ib_path[LEDGER_PATH_MAX * 2];
  snprintf(cfg_path, sizeof(cfg_path), "%s/configuration.xml", state.config_dir);
  snprintf(ib_path, sizeof(ib_path), "%s/infobase.xml", data);
  if (!axPathExists(cfg_path) || (write && !axMkDir(data) && !axPathExists(data))) {
    fprintf(stderr, "[ledger-run] configuration or data directory unavailable\n"); return 1;
  }
  DB_CLASS(db_schema_xml);
  DB_CLASS(ledger_ib);
  state.cfg = create_database_with_schema("cfg", "db_schema_xml", &ledger_cfg_schema, cfg_path);
  state.ib = create_database_with_schema("ib", "ledger_ib", &ledger_ib_schema, ib_path);
  if (!state.cfg || !state.ib) {
    fprintf(stderr, "[ledger-run] cannot open databases\n");
    destroy_database(state.ib); destroy_database(state.cfg); return 1;
  }
  register_database("cfg", state.cfg);
  register_database("ib", state.ib);
  char error[1024] = "cannot open script runtime";
  bool ok = ledger_script_open(state.config_dir, state.data_dir) && ledger_script_run(script, write, error, sizeof(error));
  if (!ok) fprintf(stderr, "[ledger-run] %s\n", error);
  if (ok && write && !send_db_message(state.ib, dbSave, 0, NULL)) {
    fprintf(stderr, "[ledger-run] cannot save infobase\n"); ok = false;
  }
  state.ib->dirty = false;  // save only explicitly above, never from a failing run
  state.cfg->dirty = false;
  ledger_script_close();
  destroy_database(state.ib);
  destroy_database(state.cfg);
  return ok ? 0 : 1;
}

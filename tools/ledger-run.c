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
  const char *configuration = "ru-2026", *data = NULL, *script = NULL, *request = NULL, *share = NULL;
  bool write = false;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--write")) write = true;
    else if (i + 1 < argc && !strcmp(argv[i], "--config")) configuration = argv[++i];
    else if (i + 1 < argc && !strcmp(argv[i], "--data")) data = argv[++i];
    else if (i + 1 < argc && !strcmp(argv[i], "--script")) script = argv[++i];
    else if (i + 1 < argc && !strcmp(argv[i], "--request")) request = argv[++i];
    else if (i + 1 < argc && !strcmp(argv[i], "--share")) share = argv[++i];
    else { fprintf(stderr, "Usage: ledger-run --data DIR --script FILE [--config CODE] [--share DIR] [--write]\n"); return 2; }
  }
  if (!data || (!!script == !!request) || strchr(configuration, '/') || strchr(configuration, '\\') ||
      (request && strcmp(configuration, "ru-2026") && strcmp(configuration, "de-2026"))) {
    fprintf(stderr, "[ledger-run] --data and exactly one of --script or --request are required; request mode supports ru-2026 and de-2026\n");
    if (request) puts("{\"ok\":false,\"error\":\"invalid invocation\"}"); return 2;
  }
  char default_share[LEDGER_PATH_MAX * 2];
  if (!share) {
    snprintf(default_share, sizeof(default_share), "%s/../share/ledger", ui_get_exe_dir());
    if (!axPathExists(default_share)) snprintf(default_share, sizeof(default_share), "%s/../../apps/ledger/share", ui_get_exe_dir());
    share = default_share;
  }
  if (snprintf(state.config_dir, sizeof(state.config_dir), "%s/configurations/%s", share, configuration) >= (int)sizeof(state.config_dir) ||
      snprintf(state.data_dir, sizeof(state.data_dir), "%s", data) >= (int)sizeof(state.data_dir)) return 2;
  char cfg_path[LEDGER_PATH_MAX * 2], ib_path[LEDGER_PATH_MAX * 2];
  snprintf(cfg_path, sizeof(cfg_path), "%s/configuration.xml", state.config_dir);
  snprintf(ib_path, sizeof(ib_path), "%s/infobase.xml", data);
  if (!axPathExists(cfg_path) || (write && !axMkDir(data) && !axPathExists(data))) {
    fprintf(stderr, "[ledger-run] configuration or data directory unavailable\n");
    if (request) puts("{\"ok\":false,\"error\":\"data unavailable\"}"); return 1;
  }
  DB_CLASS(db_schema_xml);
  DB_CLASS(ledger_ib);
  bool fresh = !axPathExists(ib_path);
  if (request && fresh && !write) {
    puts("{\"ok\":false,\"error\":\"new infobase requires write mode\"}"); return 0;
  }
  state.cfg = create_database_with_schema_readonly("cfg", "db_schema_xml", &ledger_cfg_schema, cfg_path);
  state.ib = create_database_with_schema("ib", "ledger_ib", &ledger_ib_schema, ib_path);
  if (!state.cfg || !state.ib) {
    fprintf(stderr, "[ledger-run] cannot open databases\n");
    if (request) puts("{\"ok\":false,\"error\":\"cannot open databases\"}");
    if (state.ib) database_discard_on_destroy(state.ib);
    destroy_database(state.ib); destroy_database(state.cfg); return 1;
  }
  register_database("cfg", state.cfg);
  register_database("ib", state.ib);
  if (request && fresh) {
    state.ib->dirty = true;
    if (!send_db_message(state.ib, dbSave, 0, NULL)) {
      fprintf(stderr, "[ledger-run] cannot initialize infobase\n");
      database_discard_on_destroy(state.ib); destroy_database(state.ib); destroy_database(state.cfg);
      return 1;
    }
  }
  char error[1024] = "cannot open script runtime";
  char *response = request ? calloc(1, 1024 * 1024) : NULL;
  char service[LEDGER_PATH_MAX * 2]; snprintf(service, sizeof(service), "%s/service.lua", share);
  bool ok = ledger_script_open(state.config_dir, state.data_dir) &&
            (request ? response && ledger_script_service(service, request, write, response, 1024 * 1024) :
                       ledger_script_run(script, write, error, sizeof(error)));
  if (!ok && !request) fprintf(stderr, "[ledger-run] %s\n", error);
  if (ok && write && state.ib->dirty && !send_db_message(state.ib, dbSave, 0, NULL)) {
    fprintf(stderr, "[ledger-run] cannot save infobase\n"); ok = false;
    if (response) response[0] = '\0';
  }
  bool delivered = request && response && response[0];
  if (request) puts(delivered ? response : "{\"ok\":false,\"error\":\"request failed\"}");
  free(response);
  state.ib->dirty = false;  // save only explicitly above, never from a failing run
  state.cfg->dirty = false;
  ledger_script_close();
  destroy_database(state.ib);
  destroy_database(state.cfg);
  return ok || delivered ? 0 : 1;
}

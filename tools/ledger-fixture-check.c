// Headless workload materialization and persistence/report benchmark.
#include "apps/ledger/ledger.h"
#include <platform/platform.h>
#include <math.h>
#include <time.h>

ledger_t *g_ledger;
#include "apps/ledger/ledger_ib.c"
#include "apps/ledger/ledger_script.c"
#include "apps/ledger/posting.c"
#include "apps/ledger/print.c"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "fixture check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static double elapsed(clock_t start) { return (double)(clock() - start) / CLOCKS_PER_SEC; }
static int rows(database_t *db, int table) {
  result_node_t *list = (void *)send_db_message(db, dbFetch, MAKEDWORD(table, 0), NULL);
  int n = count_result_list(list);
  free_result_list(list);
  return n;
}
static void reports(void) {
  ledger_build_reports(g_ledger->ib, g_ledger->cfg);
  double debit = 0, credit = 0, opening = 0, closing = 0;
  result_node_t *list = (void *)send_db_message(g_ledger->ib, dbFetch, MAKEDWORD(TABLE_BALANCES, 0), NULL);
  for (result_node_t *n = list; n; n = n->next) {
    db_balance_t *b = *(db_balance_t **)n->data;
    if (b->subaccount) continue;
    debit += b->debit_turnover; credit += b->credit_turnover;
    opening += b->opening_dr - b->opening_cr;
    closing += b->balance_dr - b->balance_cr;
  }
  free_result_list(list);
  CHECK(debit > 0 && fabs(debit - credit) < .005);
  CHECK(fabs(opening) < .005 && fabs(closing) < .005);
  CHECK(rows(g_ledger->ib, TABLE_VAT_BOOK) > 0);
  CHECK(rows(g_ledger->ib, TABLE_VAT_SUMMARY) > 0);
  CHECK(rows(g_ledger->ib, TABLE_JOURNAL_ROWS) > 0 && rows(g_ledger->ib, TABLE_CARD) > 0);
  list = (void *)send_db_message(g_ledger->ib, dbFetch, MAKEDWORD(TABLE_PAYROLL_ROWS, 0), NULL);
  CHECK(count_result_list(list) > 0);
  for (result_node_t *n = list; n; n = n->next) CHECK(fabs((*(db_payroll_row_t **)n->data)->closing) < .005);
  free_result_list(list);
}
int main(int argc, char **argv) {
  CHECK(argc == 4 || argc == 5);
  CHECK(!strcmp(argv[1], "ru-2026") || !strcmp(argv[1], "de-2026"));
  bool materialize = argc == 5 && !strcmp(argv[4], "--materialize");
  CHECK(argc == 4 || materialize);
  int expected = atoi(argv[3]);
  CHECK(expected >= 100);
  ledger_t state = {0}; g_ledger = &state;
  CHECK(snprintf(state.config_dir, sizeof(state.config_dir), "apps/ledger/share/configurations/%s", argv[1]) < (int)sizeof(state.config_dir));
  CHECK(snprintf(state.data_dir, sizeof(state.data_dir), "%s", argv[2]) < (int)sizeof(state.data_dir));
  char cfg_path[LEDGER_PATH_MAX * 2], ib_path[LEDGER_PATH_MAX * 2];
  snprintf(cfg_path, sizeof(cfg_path), "%s/configuration.xml", state.config_dir);
  snprintf(ib_path, sizeof(ib_path), "%s/infobase.xml", state.data_dir);
  CHECK(axPathExists(ib_path));
  DB_CLASS(db_schema_xml); DB_CLASS(ledger_ib);
  clock_t start = clock();
  state.cfg = create_database_with_schema("cfg", "db_schema_xml", &ledger_cfg_schema, cfg_path);
  state.ib = create_database_with_schema("ib", "ledger_ib", &ledger_ib_schema, ib_path);
  CHECK(state.cfg && state.ib);
  register_database("cfg", state.cfg); register_database("ib", state.ib);
  CHECK(ledger_script_open(state.config_dir, state.data_dir));
  CHECK(rows(state.ib, TABLE_DOCUMENTS) == expected);
  double load = elapsed(start);
  start = clock();
  result_node_t *docs = (void *)send_db_message(state.ib, dbFetch, MAKEDWORD(TABLE_DOCUMENTS, 0), NULL);
  int *counts = calloc((size_t)expected + 1, sizeof(int)); CHECK(counts);
  int payout_count = 0, closing_count = 0;
  // Generator IDs are in chronological order, accrual before payout before close.
  for (int id = 1; id <= expected; id++) {
    db_document_t *d = (void *)send_db_message(state.ib, dbFind, MAKEDWORD(TABLE_DOCUMENTS, 0), (void *)(intptr_t)id);
    CHECK(d && d->posted);
    const db_doc_type_t *type = (void *)send_db_message(state.cfg, dbFind, MAKEDWORD(TABLE_DOC_TYPES, 0), (void *)(intptr_t)d->doc_type_id);
    CHECK(type);
    if (!strcmp(type->code, "payout")) {
      payout_count++;
      if (materialize && !ledger_count_references(state.ib, TABLE_PAYROLL_LINES, "document_id", id))
        CHECK(ledger_fill_payroll(state.ib, state.cfg, id));
    }
    if (!strcmp(type->code, "closing")) closing_count++;
    if (materialize) { ledger_recalc_document(state.ib, state.cfg, id); CHECK(ledger_post(state.ib, state.cfg, id)); }
    CHECK(isfinite(d->total));
  }
  CHECK(payout_count == 12 && closing_count == 12);
  free_result_list(docs);
  double post = elapsed(start);
  result_node_t *entries = (void *)send_db_message(state.ib, dbFetch, MAKEDWORD(TABLE_ENTRIES, 0), NULL);
  int entry_count = count_result_list(entries);
  double amount = 0;
  for (result_node_t *n = entries; n; n = n->next) {
    db_entry_t *e = *(db_entry_t **)n->data;
    CHECK(e->document_id > 0 && e->document_id <= expected);
    CHECK(e->debit_id && e->credit_id && isfinite(e->amount) && e->amount >= 0);
    CHECK(send_db_message(state.cfg, dbFind, MAKEDWORD(TABLE_ACCOUNTS, 0), (void *)(intptr_t)e->debit_id));
    CHECK(send_db_message(state.cfg, dbFind, MAKEDWORD(TABLE_ACCOUNTS, 0), (void *)(intptr_t)e->credit_id));
    counts[e->document_id]++; amount += e->amount;
  }
  free_result_list(entries);
  for (int id = 1; id <= expected; id++) {
    if (!counts[id]) fprintf(stderr, "document without movements: %d\n", id);
    CHECK(counts[id] > 0);
  }
  free(counts);
  start = clock(); reports();
  db_report_setting_t *rs = ledger_report_settings(state.ib, state.cfg);
  snprintf(rs->date_from, sizeof(rs->date_from), "2026-06-01");
  snprintf(rs->date_to, sizeof(rs->date_to), "2026-06-30");
  reports();
  snprintf(rs->date_from, sizeof(rs->date_from), "2026-01-01");
  snprintf(rs->date_to, sizeof(rs->date_to), "2026-12-31");
  reports();
  double report = elapsed(start);
  // Search both ends and the middle through the same dbFind used by the UI.
  start = clock();
  for (int id = 1; id <= expected; id += expected / 100) {
    db_document_t *d = (void *)send_db_message(state.ib, dbFind, MAKEDWORD(TABLE_DOCUMENTS, 0), (void *)(intptr_t)id);
    CHECK(d);
    db_document_t *found = (void *)send_db_message(state.ib, dbFind,
      MAKEDWORD(TABLE_DOCUMENTS, ledger_field(state.ib, TABLE_DOCUMENTS, "number")), d->number);
    CHECK(found && found->id == id);
  }
  CHECK(!send_db_message(state.ib, dbFind, MAKEDWORD(TABLE_DOCUMENTS, ledger_field(state.ib, TABLE_DOCUMENTS, "number")), "SYN-MISSING"));
  double search = elapsed(start);
  start = clock(); CHECK(send_db_message(state.ib, dbSave, 0, NULL)); double save = elapsed(start);
  ledger_script_close(); destroy_database(state.ib);
  start = clock();
  state.ib = create_database_with_schema("ib", "ledger_ib", &ledger_ib_schema, ib_path); CHECK(state.ib);
  register_database("ib", state.ib);
  CHECK(rows(state.ib, TABLE_DOCUMENTS) == expected && rows(state.ib, TABLE_ENTRIES) == entry_count);
  CHECK(ledger_script_open(state.config_dir, state.data_dir));
  entries = (void *)send_db_message(state.ib, dbFetch, MAKEDWORD(TABLE_ENTRIES, 0), NULL);
  double reopened_amount = 0;
  for (result_node_t *n = entries; n; n = n->next) reopened_amount += (*(db_entry_t **)n->data)->amount;
  free_result_list(entries); CHECK(fabs(reopened_amount - amount) < .005);
  reports();
  double reopen = elapsed(start);
  printf("%s documents=%d entries=%d CPU_seconds load=%.3f post=%.3f reports=%.3f search=%.3f save=%.3f reopen_and_reports=%.3f\n",
    argv[1], expected, entry_count, load, post, report, search, save, reopen);
  state.ib->dirty = state.cfg->dirty = false;
  ledger_script_close(); destroy_database(state.ib); destroy_database(state.cfg);
  return 0;
}

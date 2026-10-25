// ledger_addons_test.c — external processings: the "ledger" Lua module over
// the ru-2026 demo, and the penalties add-on (share/addons/penalties) from
// calculation to the posted manual operation.

#include "test_framework.h"
#include "test_env.h"
#include "apps/ledger/ledger.h"
#include <orion/script/script.h>
#include <orion/script/script_internal.h>
#include <lauxlib.h>
#include <math.h>

#define SHARE "apps/ledger/share"
#define PENALTIES SHARE "/addons/penalties/penalties"

static ledger_t g_test_ledger;

static void open_configuration(const char *code) {
  char path[LEDGER_PATH_MAX * 2];
  if (g_ledger->ib) { unregister_database("ib"); destroy_database(g_ledger->ib); }
  if (g_ledger->cfg) { unregister_database("cfg"); destroy_database(g_ledger->cfg); }
  snprintf(g_ledger->configuration, sizeof(g_ledger->configuration), "%s", code);
  snprintf(g_ledger->config_dir, sizeof(g_ledger->config_dir), SHARE "/configurations/%s", code);
  snprintf(path, sizeof(path), "%s/configuration.xml", g_ledger->config_dir);
  g_ledger->cfg = create_database_with_schema("cfg", "db_schema_xml", &ledger_cfg_schema, path);
  register_database("cfg", g_ledger->cfg);
  ledger_script_close();
  ledger_script_open(g_ledger->config_dir, "build");
  snprintf(path, sizeof(path), "%s/demo.xml", g_ledger->config_dir);
  g_ledger->ib = create_database_with_schema("ib", "ledger_ib", &ledger_ib_schema, path);
  g_ledger->ib->source_path[0] = '\0';  // never saved
  register_database("ib", g_ledger->ib);
  ledger_repost_all(g_ledger->ib, g_ledger->cfg);
}

static void setup(void) {
  DB_CLASS(db_schema_xml);
  DB_CLASS(ledger_ib);
  memset(&g_test_ledger, 0, sizeof(g_test_ledger));
  g_ledger = &g_test_ledger;
  snprintf(g_ledger->share_dir, sizeof(g_ledger->share_dir), SHARE);
  ledger_addons_init();
  open_configuration("ru-2026");
}

static const db_account_t *account(const char *code) {
  return (const db_account_t *)send_db_message(g_ledger->cfg, dbFind,
    MAKEDWORD(TABLE_ACCOUNTS, ledger_field(g_ledger->cfg, TABLE_ACCOUNTS, "code")), (void *)code);
}

// The closing balance of the trial balance (debit positive).
static double balance(const char *code) {
  ledger_build_reports(g_ledger->ib, g_ledger->cfg);
  const db_balance_t *b = (const db_balance_t *)send_db_message(g_ledger->ib, dbFind,
    MAKEDWORD(TABLE_BALANCES, ledger_field(g_ledger->ib, TABLE_BALANCES, "account_id")), (void *)(intptr_t)account(code)->id);
  return b ? b->balance_dr - b->balance_cr : 0;
}

static int count_rows(const char *db_name, int table) {
  result_node_t *list = (result_node_t *)send_db_message(get_database_by_name(db_name), dbFetch, MAKEDWORD(table, 0), NULL);
  int n = count_result_list(list);
  free_result_list(list);
  return n;
}

// Credit turnover of an account and its subaccounts on posted documents.
static double ledger_turnover_of(const char *code) {
  db_entry_t **entries = NULL;
  int n = ledger_posted_entries(g_ledger->ib, &entries);
  double cr = 0;
  for (int i = 0; i < n; i++) if (ledger_account_within(g_ledger->cfg, entries[i]->credit_id, account(code)->id)) cr += entries[i]->amount;
  free(entries);
  return cr;
}

static const db_entry_t *last_entry(void) {
  result_node_t *list = (result_node_t *)send_db_message(g_ledger->ib, dbFetch, MAKEDWORD(TABLE_ENTRIES, 0), NULL);
  const db_entry_t *last = NULL;
  for (result_node_t *n = list; n; n = (result_node_t *)n->next) {
    const db_entry_t *e = *(db_entry_t **)n->data;
    if (!last || e->id > last->id) last = e;
  }
  free_result_list(list);
  return last;
}

// Runs a chunk in an add-on's sandbox; its error, if any, fails the test.
static bool run_lua(const char *chunk, char *msg, size_t msg_sz) {
  lua_State *L = script_state_new(SHARE "/addons", "spec");
  bool ok = L && luaL_loadstring(L, chunk) == LUA_OK && script_call(L, 0, 0, msg, msg_sz, NULL);
  if (L && !ok && !msg[0]) snprintf(msg, msg_sz, "%s", lua_tostring(L, -1));
  if (L) lua_close(L);
  return ok;
}

static void test_ledger_module(void) {
  TEST("ledger module: accounts, posted turnovers and movements agree with the trial balance");
  char chunk[2048], msg[2048] = "";
  double bank = balance("51"), receivables = balance("62");
  snprintf(chunk, sizeof(chunk),
    "local ledger = require('ledger')\n"
    "assert(ledger.configuration == 'ru-2026')\n"
    "local a = ledger.account('62')\n"
    "assert(a.code == '62' and a.analytics == 'counterparties', 'account 62 is analysed by counterparty')\n"
    "assert(ledger.account('62.01').id ~= a.id and ledger.account('nope') == nil)\n"
    "local from, to = ledger.period()\n"
    "assert(type(from) == 'string' and type(to) == 'string')\n"
    "local bank = ledger.turnover('51', {to = to})\n"
    "assert(math.abs(bank.balance - %.2f) < 0.005, 'bank balance ' .. bank.balance)\n"
    "local r = ledger.turnover('62', {to = to})\n"
    "assert(math.abs(r.balance - %.2f) < 0.005, 'receivables balance ' .. r.balance)\n"
    "local dr, cr, last = 0, 0, ''\n"
    "for _, m in ipairs(ledger.movements('62', {to = to})) do\n"
    "  assert(m.account:sub(1, 2) == '62' and m.date >= last, 'subaccounts roll up, in date order')\n"
    "  dr, cr, last = dr + m.debit, cr + m.credit, m.date\n"
    "end\n"
    "assert(math.abs(dr - r.debit) < 0.005 and math.abs(cr - r.credit) < 0.005)\n"
    "assert(#ledger.movements('62', {to = '2026-03-01'}) == 0, 'nothing on 62 before March')\n"
    "assert(not pcall(ledger.turnover, 'nope'), 'an unknown account is a mistake')\n",
    bank, receivables);
  bool ok = run_lua(chunk, msg, sizeof(msg));
  if (!ok) fprintf(stderr, "%s\n", msg);
  ASSERT(ok, "every expectation of the chunk holds");
  PASS();
}

static int test_document(int doc_type_id, const char *date, int counterparty_id, double total) {
  db_document_t blank = { .doc_type_id = doc_type_id, .counterparty_id = counterparty_id, .total = total };
  snprintf(blank.date, sizeof(blank.date), "%s", date);
  return ((db_document_t *)send_db_message(g_ledger->ib, dbInsert, TABLE_DOCUMENTS, &blank))->id;
}

static const db_document_t *test_document_at(int id) {
  return (const db_document_t *)send_db_message(g_ledger->ib, dbFind, MAKEDWORD(TABLE_DOCUMENTS, 0), (void *)(intptr_t)id);
}

static void set_text(script_page_t *page, const char *name, const char *text) {
  send_message(script_page_control(page, name), edSetText, 0, (void *)text);
}

// Ten monitors to Альфа on 16 March (341 600 with VAT), 100 000 paid on
// 25 March; paper and cartridges to Облачные сервисы on 20 March (29 280 in
// two lines, one debt); Бета-Снаб's shipment РТ-0002 is covered by its prepayment.
static int seed_debts(void) {
  int sale = test_document(2, "2026-03-16", 1, 0);
  db_doc_line_t line = { .document_id = sale, .item_id = 3, .vat_rate_id = 1, .quantity = 10, .price = 28000 };
  send_db_message(g_ledger->ib, dbInsert, TABLE_DOC_LINES, &line);
  ledger_post(g_ledger->ib, g_ledger->cfg, sale);
  int second = test_document(2, "2026-03-20", 5, 0);
  db_doc_line_t paper = { .document_id = second, .item_id = 1, .vat_rate_id = 1, .quantity = 20, .price = 550 };
  db_doc_line_t toner = { .document_id = second, .item_id = 2, .vat_rate_id = 1, .quantity = 2, .price = 6500 };
  send_db_message(g_ledger->ib, dbInsert, TABLE_DOC_LINES, &paper);
  send_db_message(g_ledger->ib, dbInsert, TABLE_DOC_LINES, &toner);
  ledger_post(g_ledger->ib, g_ledger->cfg, second);
  ledger_post(g_ledger->ib, g_ledger->cfg, test_document(7, "2026-03-25", 1, 100000));
  ledger_post(g_ledger->ib, g_ledger->cfg, 10);
  return sale;
}

static void test_penalties(void) {
  TEST("penalties add-on: overdue debts by payment term, posted as one manual operation");
  const db_document_t *sale = test_document_at(seed_debts());
  ASSERT(sale && sale->posted && fabs(sale->total - 341600) < 0.005, "the sale posts 341 600 with VAT");
  char sale_number[32], error[512], date[32];
  snprintf(sale_number, sizeof(sale_number), "%s", sale->number);

  script_page_t *page = script_page_open(PENALTIES, error, sizeof(error));
  if (!page) fprintf(stderr, "open: %s\n", error);
  ASSERT_NOT_NULL(page);
  ASSERT_STR_EQUAL(script_page_title(page), "Penalties on Overdue Receivables");
  ASSERT_NOT_NULL(script_page_create(page, NULL));
  ASSERT_EQUAL((int)send_message(script_page_control(page, "receivable"), cbGetCurrentValue, 0, NULL), account("62")->id);
  ASSERT_EQUAL((int)send_message(script_page_control(page, "debit"), cbGetCurrentValue, 0, NULL), account("76")->id);
  ASSERT_EQUAL((int)send_message(script_page_control(page, "credit"), cbGetCurrentValue, 0, NULL), account("91.01")->id);

  set_text(page, "date", "");
  ASSERT_FALSE(script_page_perform(page, "calculate"));
  ASSERT_STR_EQUAL(script_page_message(page), "Enter the date to calculate on.");

  set_text(page, "date", ui_format_date("2026-04-30", date, sizeof(date)));
  ASSERT_TRUE(script_page_perform(page, "calculate"));
  database_t *db = get_database_by_name("penalties");
  result_node_t *rows = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(0, 0), NULL);
  ASSERT_EQUAL(count_result_list(rows), 2);
  char text[6][128], second[6][128];
  const char *fields[] = { "counterparty.name", "number", "due", "days", "debt", "penalty" };
  for (size_t i = 0; i < ARRAY_LEN(fields); i++) {
    db_get_field_text(db, 0, *(void **)rows->data, fields[i], text[i], sizeof(text[i]));
    db_get_field_text(db, 0, *(void **)((result_node_t *)rows->next)->data, fields[i], second[i], sizeof(second[i]));
  }
  free_result_list(rows);
  ASSERT_STR_EQUAL(text[0], "ООО \"Торговый дом Альфа\"");
  ASSERT_STR_EQUAL(text[1], sale_number);
  ASSERT_STR_EQUAL(text[2], "2026-03-26");
  ASSERT_STR_EQUAL(text[3], "35");
  ASSERT_STR_EQUAL(text[4], "241600.00");
  ASSERT_STR_EQUAL(text[5], "8456.00");  // 241 600 × 0.1 % × 35 days
  ASSERT_STR_EQUAL(second[0], "ООО \"Облачные сервисы\"");
  ASSERT_STR_EQUAL(second[4], "29280.00");  // both lines of the document, one debt
  ASSERT_STR_EQUAL(second[3], "31");
  ASSERT_STR_EQUAL(second[5], "907.68");
  ASSERT(strstr(script_page_control(page, "summary")->title, "Overdue debts: 2.") != NULL, "the summary counts the debts");

  set_text(page, "term", "50");
  ASSERT_TRUE(script_page_perform(page, "calculate"));
  ASSERT_EQUAL(count_rows("penalties", 0), 0);
  ASSERT_FALSE(script_page_perform(page, "accrue"));
  ASSERT_STR_EQUAL(script_page_message(page), "Nothing is overdue on this date.");

  set_text(page, "term", "10");
  double income = ledger_turnover_of("91.01");
  ASSERT_TRUE(script_page_perform(page, "accrue"));
  script_page_close(page);
  ASSERT(fabs(ledger_turnover_of("91.01") - income - 9363.68) < 0.005, "91.01 is credited with the penalties");
  const db_entry_t *entry = last_entry();
  const db_document_t *op = test_document_at(entry->document_id);
  ASSERT(op && op->posted && !strcmp(op->date, "2026-04-30"), "the operation is posted on the calculation date");
  ASSERT_EQUAL(entry->debit_id, account("76")->id);
  ASSERT_EQUAL(entry->debit_subconto_id, 5);  // the last entry: Облачные сервисы
  ASSERT(fabs(entry->amount - 907.68) < 0.005, "each debt is one entry");
  ASSERT_EQUAL(entry->credit_id, account("91.01")->id);
  ASSERT(fabs(op->total - 9363.68) < 0.005, "the operation totals its entries");

  page = script_page_open(PENALTIES, NULL, 0);
  ASSERT_NOT_NULL(page);
  script_page_create(page, NULL);
  set_text(page, "date", date);
  ASSERT_FALSE(script_page_perform(page, "accrue"));
  ASSERT(strstr(script_page_message(page), "already posted") && strstr(script_page_message(page), op->number),
         "a second run on the same date names the operation");
  script_page_close(page);
  PASS();
}

static void test_german_accounts(void) {
  TEST("penalties add-on: de-2026 accrues interest on 1400 to 2650");
  open_configuration("de-2026");
  script_page_t *page = script_page_open(PENALTIES, NULL, 0);
  ASSERT_NOT_NULL(page);
  script_page_create(page, NULL);
  ASSERT_EQUAL((int)send_message(script_page_control(page, "receivable"), cbGetCurrentValue, 0, NULL), account("1400")->id);
  ASSERT_EQUAL((int)send_message(script_page_control(page, "credit"), cbGetCurrentValue, 0, NULL), account("2650")->id);
  script_page_close(page);
  PASS();
}

static void test_translations(void) {
  TEST("penalties add-on: its catalogs translate its own texts and leave the host's alone");
  ui_set_language("ru");
  script_page_t *page = script_page_open(PENALTIES, NULL, 0);
  ASSERT_NOT_NULL(page);
  ASSERT_STR_EQUAL(script_page_title(page), "Пени по просроченной дебиторской задолженности");
  script_page_create(page, NULL);
  ASSERT_STR_EQUAL(script_page_control(page, "calculate")->title, "Рассчитать");
  ASSERT(!strncmp(script_page_control(page, "summary")->title, "Просроченных долгов:", strlen("Просроченных долгов:")),
         "the script's orion.tr finds the add-on's catalog");
  ASSERT_STR_EQUAL(ui_tr("Close"), "Close");          // the add-on's form says Закрыть; the host does not
  ASSERT_STR_EQUAL(ui_tr("Enter the penalty rate."), "Enter the penalty rate.");
  script_page_close(page);
  page = script_page_open(PENALTIES, NULL, 0);        // a second opening loads nothing twice
  ASSERT_NOT_NULL(page);
  ASSERT_STR_EQUAL(script_page_title(page), "Пени по просроченной дебиторской задолженности");
  script_page_close(page);
  ui_set_language("en");
  PASS();
}

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  TEST_START("Ledger add-ons");
  test_env_init();
  setup();
  test_ledger_module();
  test_penalties();
  test_german_accounts();
  test_translations();
  unregister_database("ib");
  unregister_database("cfg");
  destroy_database(g_ledger->ib);
  destroy_database(g_ledger->cfg);
  ledger_script_close();
  test_env_shutdown();
  TEST_END();
}

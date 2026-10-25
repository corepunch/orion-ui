// ledger_test.c — schema-driven database, posting rules, payroll, fixed
// assets, currencies, month-end closing, reports and print forms, exercised
// on the ru-2003, de-2003, ru-2026 and de-2026 configurations and their demo infobases.

#include "test_framework.h"
#include "apps/ledger/ledger.h"
#include <math.h>

#define SHARE "apps/ledger/share/configurations"
#define TMP_DIR "build/test-ledger"

static ledger_t g_test_ledger;
static void ed_digest(const unsigned char *data, size_t len, char hex[65]);

static database_t *open_db(const char *name, const db_schema_def_t *schema, const char *path) {
  database_t *db = create_database_with_schema(name, strcmp(name, "ib") ? "db_schema_xml" : "ledger_ib", schema, path);
  if (db) register_database(name, db);
  return db;
}

static void setup(void) {
  DB_CLASS(db_schema_xml);
  DB_CLASS(ledger_ib);
  memset(&g_test_ledger, 0, sizeof(g_test_ledger));
  g_ledger = &g_test_ledger;
  snprintf(g_ledger->config_dir, sizeof(g_ledger->config_dir), SHARE "/ru-2003");
  snprintf(g_ledger->data_dir, sizeof(g_ledger->data_dir), TMP_DIR);
  axMkDir("build");
  axMkDir(TMP_DIR);
  g_ledger->cfg = open_db("cfg", &ledger_cfg_schema, SHARE "/ru-2003/configuration.xml");
  g_ledger->ib = open_db("ib", &ledger_ib_schema, SHARE "/ru-2003/demo.xml");
  snprintf(g_ledger->ib->source_path, sizeof(g_ledger->ib->source_path), TMP_DIR "/infobase.xml");
  ledger_script_open(g_ledger->config_dir, TMP_DIR);
  ledger_repost_all(g_ledger->ib, g_ledger->cfg);
}

static void *find_by(database_t *db, int table, const char *field, const char *value) {
  return (void *)send_db_message(db, dbFind, MAKEDWORD(table, ledger_field(db, table, field)), (void *)value);
}

static const db_balance_t *balance(const char *code) {
  db_account_t *a = find_by(g_ledger->cfg, TABLE_ACCOUNTS, "code", code);
  return a ? (const db_balance_t *)send_db_message(g_ledger->ib, dbFind,
               MAKEDWORD(TABLE_BALANCES, ledger_field(g_ledger->ib, TABLE_BALANCES, "account_id")),
               (void *)(intptr_t)a->id) : NULL;
}

static bool near(double a, double b) { return fabs(a - b) < 0.005; }

static void test_configuration_loads(void) {
  TEST("configuration: chart of accounts, rates and document types load from XML");
  result_node_t *accounts = (result_node_t *)send_db_message(g_ledger->cfg, dbFetch, MAKEDWORD(TABLE_ACCOUNTS, 0), NULL);
  int n = count_result_list(accounts);
  free_result_list(accounts);
  ASSERT(n > 50, "chart of accounts should be complete");
  db_account_t *goods = find_by(g_ledger->cfg, TABLE_ACCOUNTS, "code", "41.01");
  ASSERT_NOT_NULL(goods);
  ASSERT_STR_EQUAL(goods->name, "Товары на складах");
  db_vat_rate_t *vat = (db_vat_rate_t *)send_db_message(g_ledger->cfg, dbFind, MAKEDWORD(TABLE_VAT_RATES, 0), (void *)(intptr_t)1);
  ASSERT(vat && near(vat->rate, 20), "2003 VAT rate is 20%");
  PASS();
}

static void test_joins_cross_databases(void) {
  TEST("schema joins: ib.documents -> cfg.doc_types and nested item.unit paths");
  db_document_t *doc = find_by(g_ledger->ib, TABLE_DOCUMENTS, "number", "ПТ-0001");
  ASSERT_NOT_NULL(doc);
  char buf[256];
  ASSERT_TRUE(db_get_field_text(g_ledger->ib, TABLE_DOCUMENTS, doc, "doc_type.name", buf, sizeof(buf)));
  ASSERT_STR_EQUAL(buf, "Поступление товаров");
  ASSERT_TRUE(db_get_field_text(g_ledger->ib, TABLE_DOCUMENTS, doc, "doc_type.debit.code", buf, sizeof(buf)));
  ASSERT_STR_EQUAL(buf, "41.01");
  ASSERT_TRUE(db_get_field_text(g_ledger->ib, TABLE_DOCUMENTS, doc, "total", buf, sizeof(buf)));
  ASSERT_STR_EQUAL(buf, "51120.00");
  db_doc_line_t *line = (db_doc_line_t *)send_db_message(g_ledger->ib, dbFind, MAKEDWORD(TABLE_DOC_LINES, 0), (void *)(intptr_t)1);
  ASSERT_TRUE(db_get_field_text(g_ledger->ib, TABLE_DOC_LINES, line, "item.unit.code", buf, sizeof(buf)));
  ASSERT_STR_EQUAL(buf, "728");
  ASSERT_TRUE(db_get_field_text(g_ledger->ib, TABLE_DOC_LINES, line, "quantity", buf, sizeof(buf)));
  ASSERT_STR_EQUAL(buf, "100.000");
  PASS();
}

static void test_posting_rules(void) {
  TEST("posting: receipt lines write Dt 41.01 Kt 60.01 and Dt 19.03 Kt 60.01 with subconto");
  db_document_t *doc = find_by(g_ledger->ib, TABLE_DOCUMENTS, "number", "ПТ-0001");
  ASSERT(doc && doc->posted, "demo receipt is posted");
  ASSERT(near(doc->amount, 42600) && near(doc->vat, 8520) && near(doc->total, 51120), "receipt totals");
  result_node_t *entries = (result_node_t *)send_db_message(g_ledger->ib, dbFetch,
    MAKEDWORD(TABLE_ENTRIES, ledger_field(g_ledger->ib, TABLE_ENTRIES, "document_id")), (void *)(intptr_t)doc->id);
  ASSERT_EQUAL(count_result_list(entries), 6);  // goods and tax entry per line
  char dr[16], cr[16], item[64], party[64];
  db_entry_t *e = *(db_entry_t **)entries->data;
  db_get_field_text(g_ledger->ib, TABLE_ENTRIES, e, "debit.code", dr, sizeof(dr));
  db_get_field_text(g_ledger->ib, TABLE_ENTRIES, e, "credit.code", cr, sizeof(cr));
  db_get_field_text(g_ledger->ib, TABLE_ENTRIES, e, "debit_subconto.sku", item, sizeof(item));
  db_get_field_text(g_ledger->ib, TABLE_ENTRIES, e, "credit_subconto.tax_id", party, sizeof(party));
  bool ok = !strcmp(dr, "41.01") && !strcmp(cr, "60.01") && near(e->amount, 9500) && near(e->quantity, 100) &&
            !strcmp(item, "00001") && !strcmp(party, "7705123456");
  free_result_list(entries);
  ASSERT(ok, "net amount entry carries the item on the debit side and the supplier on the credit side");
  PASS();
}

static double entry_sum(const char *debit, const char *credit) {
  db_account_t *dr = find_by(g_ledger->cfg, TABLE_ACCOUNTS, "code", debit), *cr = find_by(g_ledger->cfg, TABLE_ACCOUNTS, "code", credit);
  double sum = 0;
  result_node_t *entries = (result_node_t *)send_db_message(g_ledger->ib, dbFetch, MAKEDWORD(TABLE_ENTRIES, 0), NULL);
  for (result_node_t *n = entries; n; n = n->next) {
    db_entry_t *e = *(db_entry_t **)n->data;
    if (dr && cr && e->debit_id == dr->id && e->credit_id == cr->id) sum += e->amount;
  }
  free_result_list(entries);
  return sum;
}

static void test_cost_of_sales(void) {
  TEST("sale: goods leave 41.01 for 90.02 at average cost, item and quantity on the credit side");
  ASSERT(near(entry_sum("90.02", "41.01"), 2 * 6200 + 150), "two monitors at 6 200 and a keyboard from opening stock");
  const db_balance_t *goods = balance("41.01");
  ASSERT(goods && near(goods->balance_dr, 1500 + 42600 + 15750 - 12550), "stock after the sale");
  PASS();
}

static void test_payroll(void) {
  TEST("payroll: accrual with income tax withheld and unified social tax; payout of net pay; fill");
  db_document_t *accrual = find_by(g_ledger->ib, TABLE_DOCUMENTS, "number", "НЗ-0001");
  ASSERT(accrual && near(accrual->total, 36000), "gross payroll");
  db_payroll_line_t *line = (db_payroll_line_t *)send_db_message(g_ledger->ib, dbFind, MAKEDWORD(TABLE_PAYROLL_LINES, 0), (void *)(intptr_t)1);
  ASSERT(line && near(line->withheld, (15000 - 400) * 0.13) && near(line->contributions, 15000 * 0.356) && near(line->net, 13102),
         "13 % after the 400 allowance; 35.6 % on top");
  ASSERT(near(balance("68.01")->balance_cr, 4524) && near(balance("69")->balance_cr, 12816), "taxes owed");
  ASSERT(near(balance("70")->balance_cr, 0) && near(balance("70")->debit_turnover, 36000), "net pay paid out from the till");
  char buf[128];
  result_node_t *rows = (result_node_t *)send_db_message(g_ledger->ib, dbFetch, MAKEDWORD(TABLE_PAYROLL_ROWS, 0), NULL);
  ASSERT(count_result_list(rows) == 3, "payroll statement per employee");
  db_get_field_text(g_ledger->ib, TABLE_PAYROLL_ROWS, *(void **)rows->data, "employee.position", buf, sizeof(buf));
  free_result_list(rows);
  ASSERT_STR_EQUAL(buf, "Генеральный директор");
  db_document_t blank = { .doc_type_id = 10 };
  snprintf(blank.date, sizeof(blank.date), "2003-04-30");
  db_document_t *april = (db_document_t *)send_db_message(g_ledger->ib, dbInsert, TABLE_DOCUMENTS, &blank);
  int id = april->id;
  ledger_fill_payroll(g_ledger->ib, g_ledger->cfg, id);
  ASSERT(ledger_count_references(g_ledger->ib, TABLE_PAYROLL_LINES, "document_id", id) == 3 && near(april->total, 36000), "filled with salaries");
  send_db_message(g_ledger->ib, dbDelete, TABLE_DOCUMENTS, (void *)(intptr_t)id);
  ASSERT_EQUAL(ledger_count_references(g_ledger->ib, TABLE_PAYROLL_LINES, "document_id", id), 0);
  PASS();
}

static void test_polymorphic_analytics(void) {
  TEST("analytics: each side names the catalog its account is analysed by");
  db_document_t *payout = find_by(g_ledger->ib, TABLE_DOCUMENTS, "number", "ВЗ-0001");
  result_node_t *entries = (result_node_t *)send_db_message(g_ledger->ib, dbFetch,
    MAKEDWORD(TABLE_ENTRIES, ledger_field(g_ledger->ib, TABLE_ENTRIES, "document_id")), (void *)(intptr_t)payout->id);
  char dr[128], cr[128];
  db_entry_t *e = *(db_entry_t **)entries->data;
  db_get_field_text(g_ledger->ib, TABLE_ENTRIES, e, "debit_subconto.name", dr, sizeof(dr));
  db_get_field_text(g_ledger->ib, TABLE_ENTRIES, e, "credit_subconto.name", cr, sizeof(cr));
  free_result_list(entries);
  ASSERT_STR_EQUAL(dr, "Петров Петр Петрович");
  ASSERT_STR_EQUAL(cr, "");
  ASSERT_STR_EQUAL(ledger_subconto_table(g_ledger->cfg, ((db_account_t *)find_by(g_ledger->cfg, TABLE_ACCOUNTS, "code", "02"))->id), "assets");
  PASS();
}

static void test_fixed_assets(void) {
  TEST("fixed assets: put into service, straight-line depreciation from the next month");
  db_asset_t *printer = (db_asset_t *)send_db_message(g_ledger->ib, dbFind, MAKEDWORD(TABLE_ASSETS, 0), (void *)(intptr_t)2);
  ASSERT_STR_EQUAL(printer->commissioned, "2003-03-14");
  ASSERT(near(balance("01")->balance_dr, 48000) && near(balance("02")->balance_cr, 3000), "computer depreciated for March only");
  result_node_t *rows = (result_node_t *)send_db_message(g_ledger->ib, dbFetch, MAKEDWORD(TABLE_ASSET_ROWS, 0), NULL);
  db_asset_row_t *computer = *(db_asset_row_t **)rows->data, *second = *(db_asset_row_t **)((result_node_t *)rows->next)->data;
  bool ok = near(computer->depreciation, 3000) && near(computer->residual, 33000) && near(second->monthly, 500) && near(second->depreciation, 0);
  free_result_list(rows);
  ASSERT(ok, "asset register");
  db_document_t blank = { .doc_type_id = 14 };
  snprintf(blank.date, sizeof(blank.date), "2003-04-30");
  db_document_t *april = (db_document_t *)send_db_message(g_ledger->ib, dbInsert, TABLE_DOCUMENTS, &blank);
  int id = april->id;
  ASSERT_TRUE(ledger_post(g_ledger->ib, g_ledger->cfg, id));
  ledger_build_reports(g_ledger->ib, g_ledger->cfg);
  ASSERT(near(balance("02")->balance_cr, 3000 + 1000 + 500), "April charges both assets");
  send_db_message(g_ledger->ib, dbDelete, TABLE_DOCUMENTS, (void *)(intptr_t)id);
  ledger_build_reports(g_ledger->ib, g_ledger->cfg);
  PASS();
}

static void test_currency(void) {
  TEST("currencies: documents post at the day's rate; closing revalues currency accounts");
  db_document_t *import = find_by(g_ledger->ib, TABLE_DOCUMENTS, "number", "ПИ-0001");
  ASSERT(import && near(import->rate, 31.5) && near(import->total, 500), "rate on the document date");
  ASSERT(near(entry_sum("41.01", "60.21"), 15750), "500 USD at 31.50");
  const db_balance_t *account = balance("52");
  ASSERT(account && near(account->balance_dr, 500 * 31.38), "500 USD left, restated at the month-end rate");
  ASSERT(near(entry_sum("91.02", "52"), 165) && near(entry_sum("60.21", "91.01"), 25), "exchange differences");
  ASSERT(near(balance("60.21")->balance_dr, 0) && near(balance("60.21")->balance_cr, 0), "settled supplier");
  PASS();
}

static void test_month_closing(void) {
  TEST("closing: expenses close to 90, the month's result to 99");
  ASSERT(near(balance("26")->balance_dr, 0) && near(balance("44")->balance_dr, 0), "26 and 44 closed");
  ASSERT(near(balance("90")->balance_dr, 0) && near(balance("90")->balance_cr, 0), "90 nets to zero");
  ASSERT(near(entry_sum("99", "90.09"), 48486) && near(entry_sum("99", "91.09"), 140), "loss from sales and other expenses");
  PASS();
}

static void test_cash_book(void) {
  TEST("cash book: day opening, cash orders with payee, day totals; KO-4 report form");
  result_node_t *rows = (result_node_t *)send_db_message(g_ledger->ib, dbFetch, MAKEDWORD(TABLE_CASH_BOOK, 0), NULL);
  int count = count_result_list(rows);
  db_cash_book_t *last = NULL;
  double receipts = 0;  // what the totals row adds up: the orders, not the day rows
  for (result_node_t *n = rows; n; n = n->next) {
    last = *(db_cash_book_t **)n->data;
    if (!last->summary) receipts += last->receipt;
  }
  bool ok = count == 12 && last && last->summary && near(last->balance, 18656) && near(last->receipt, 31476) && near(last->expense, 31476);
  free_result_list(rows);
  ASSERT(ok, "three days with movements");
  ASSERT(near(receipts, 2000 + 16656 + 31476), "orders alone sum to the receipts");
  char report[1024];
  ASSERT_EQUAL(ledger_print_reports(report, sizeof(report)), 1);
  FILE *f = fopen(TMP_DIR "/print/ko4.html", "rb");
  ASSERT_NOT_NULL(f);
  static char html[65536];
  size_t n = fread(html, 1, sizeof(html) - 1, f);
  fclose(f);
  html[n] = '\0';
  ASSERT(strstr(html, "КАССОВАЯ КНИГА") && strstr(html, "Сидорова Анна Викторовна") && !strstr(html, "{{"), "filled cash book");
  PASS();
}

static void test_unpost_and_trial_balance(void) {
  TEST("trial balance: subaccounts roll up; opening balances; unposting removes entries");
  const db_balance_t *bank = balance("51"), *goods = balance("41"), *vat = balance("68"), *aux = balance("000");
  ASSERT(bank && near(bank->balance_dr, 100000 + 60000 - 51120 - 31476), "bank balance");
  ASSERT(aux && near(aux->balance_dr, 0) && near(aux->balance_cr, 0), "opening entries balance through 000");
  ASSERT(goods && near(goods->debit_turnover, 1500 + 42600 + 15750), "41 includes 41.01");
  ASSERT(vat && near(vat->debit_turnover, 8520) && near(vat->credit_turnover, 2776 + 4524), "68.02 and 68.01 roll into 68");
  // The totals row counts top-level accounts only: debit and credit agree.
  double dr = 0, cr = 0, open_dr = 0, open_cr = 0, close_dr = 0, close_cr = 0;
  result_node_t *rows = (result_node_t *)send_db_message(g_ledger->ib, dbFetch, MAKEDWORD(TABLE_BALANCES, 0), NULL);
  for (result_node_t *n = rows; n; n = n->next) {
    const db_balance_t *b = *(db_balance_t **)n->data;
    if (b->subaccount) continue;
    dr += b->debit_turnover; cr += b->credit_turnover; open_dr += b->opening_dr; open_cr += b->opening_cr;
    close_dr += b->balance_dr; close_cr += b->balance_cr;
  }
  free_result_list(rows);
  ASSERT(near(dr, cr) && near(open_dr, open_cr) && near(close_dr, close_cr), "trial balance totals agree");
  ASSERT(balance("41.01") && balance("41.01")->subaccount && !goods->subaccount, "subaccount rows are marked");
  db_document_t *sale = find_by(g_ledger->ib, TABLE_DOCUMENTS, "number", "РТ-0002");
  ASSERT(sale && !sale->posted, "second sale is a draft");
  ASSERT_TRUE(ledger_post(g_ledger->ib, g_ledger->cfg, sale->id));
  ledger_build_reports(g_ledger->ib, g_ledger->cfg);
  ASSERT(near(balance("90.01")->credit_turnover, 16656 + sale->total), "revenue includes the new sale");
  ledger_unpost(g_ledger->ib, g_ledger->cfg, sale->id);
  ledger_build_reports(g_ledger->ib, g_ledger->cfg);
  ASSERT(near(balance("90.01")->credit_turnover, 16656), "unpost restores revenue");
  PASS();
}

static void set_period(const char *from, const char *to, const char *account) {
  db_report_setting_t *rs = ledger_report_settings(g_ledger->ib, g_ledger->cfg);
  db_account_t *a = find_by(g_ledger->cfg, TABLE_ACCOUNTS, "code", account);
  snprintf(rs->date_from, sizeof(rs->date_from), "%s", from);
  snprintf(rs->date_to, sizeof(rs->date_to), "%s", to);
  rs->account_id = a ? a->id : 0;
  ledger_build_reports(g_ledger->ib, g_ledger->cfg);
}

static void test_period_reports(void) {
  TEST("reports: period opening/turnover, account card, analysis by counterparty, VAT books");
  set_period("2003-03-05", "2003-03-31", "51");
  const db_balance_t *bank = balance("51");
  ASSERT(bank && near(bank->opening_dr, 100000 + 60000 - 51120) && near(bank->debit_turnover, 0), "bank opens with earlier movements");
  result_node_t *card = (result_node_t *)send_db_message(g_ledger->ib, dbFetch, MAKEDWORD(TABLE_CARD, 0), NULL);
  ASSERT_EQUAL(count_result_list(card), 1);  // cash drawn for the payroll
  free_result_list(card);
  set_period("", "", "60");
  card = (result_node_t *)send_db_message(g_ledger->ib, dbFetch, MAKEDWORD(TABLE_CARD, 0), NULL);
  double last = -1; int rows = count_result_list(card);
  for (result_node_t *n = card; n; n = n->next) last = (*(db_card_t **)n->data)->balance;
  free_result_list(card);
  ASSERT(rows == 12 && near(last, -14400), "supplier card: receipts, payments, the unpaid printer");
  result_node_t *rows_ = (result_node_t *)send_db_message(g_ledger->ib, dbFetch, MAKEDWORD(TABLE_ANALYSIS_ROWS, 0), NULL);
  db_analysis_row_t *alfa = rows_ ? *(db_analysis_row_t **)rows_->data : NULL;
  char name[128] = "";
  if (alfa) db_get_field_text(g_ledger->ib, TABLE_ANALYSIS_ROWS, alfa, "subconto.name", name, sizeof(name));
  bool ok = count_result_list(rows_) == 2 && alfa && near(alfa->credit_turnover, 51120 + 14400) && near(alfa->debit_turnover, 51120) &&
            !strcmp(name, "ООО \"Торговый дом Альфа\"");
  free_result_list(rows_);
  ASSERT(ok, "analysis of 60 lists the suppliers");
  int sales = ledger_count_references(g_ledger->ib, TABLE_VAT_BOOK, "book", 1);
  int purchases = ledger_count_references(g_ledger->ib, TABLE_VAT_BOOK, "book", 2);
  ASSERT(sales == 1 && purchases == 2, "posted sale, goods and equipment receipts are in the books");
  PASS();
}

static void test_manual_operation(void) {
  TEST("manual operation: typed entries count while posted and stay when unposted");
  db_document_t *op = find_by(g_ledger->ib, TABLE_DOCUMENTS, "number", "ВО-0001");
  ASSERT(op && op->posted && near(op->total, 342160), "opening balances total their entries");
  ledger_unpost(g_ledger->ib, g_ledger->cfg, op->id);
  ASSERT_EQUAL(ledger_count_references(g_ledger->ib, TABLE_ENTRIES, "document_id", op->id), 8);
  ledger_build_reports(g_ledger->ib, g_ledger->cfg);
  ASSERT(near(balance("51")->balance_cr, 51120 + 31476 - 60000), "unposted operation leaves the reports");
  ledger_post(g_ledger->ib, g_ledger->cfg, op->id);
  ledger_build_reports(g_ledger->ib, g_ledger->cfg);
  ASSERT(near(balance("51")->balance_dr, 100000 + 60000 - 51120 - 31476), "posted again");
  PASS();
}

static void test_transient_tables(void) {
  TEST("db_schema_xml: transient report tables do not dirty the database or reach the file");
  send_db_message(g_ledger->ib, dbSave, 0, NULL);
  ledger_build_reports(g_ledger->ib, g_ledger->cfg);
  ASSERT_FALSE(send_db_message(g_ledger->ib, dbGetDirty, 0, NULL));
  FILE *f = fopen(TMP_DIR "/infobase.xml", "rb");
  ASSERT_NOT_NULL(f);
  static char xml[1 << 18];
  size_t n = fread(xml, 1, sizeof(xml) - 1, f);
  fclose(f);
  xml[n] = '\0';
  ASSERT(!strstr(xml, "<balances") && strstr(xml, "<entries"), "only stored tables are written");
  PASS();
}

static void test_numbering_and_delete(void) {
  TEST("documents: numbering by configuration prefix, cascading delete");
  db_document_t blank = { .doc_type_id = 2 };
  snprintf(blank.date, sizeof(blank.date), "2003-03-20");
  db_document_t *doc = (db_document_t *)send_db_message(g_ledger->ib, dbInsert, TABLE_DOCUMENTS, &blank);
  ASSERT_NOT_NULL(doc);
  int id = doc->id;
  ledger_number_document(g_ledger->ib, g_ledger->cfg, id);
  ASSERT_STR_EQUAL(doc->number, "РТ-0003");
  db_doc_line_t line = { .document_id = id, .item_id = 2, .quantity = 2 };
  ASSERT_NOT_NULL((void *)send_db_message(g_ledger->ib, dbInsert, TABLE_DOC_LINES, &line));
  ledger_recalc_document(g_ledger->ib, g_ledger->cfg, id);
  ASSERT(near(doc->amount, 2900) && near(doc->vat, 580), "line takes the item's price and tax rate");
  ledger_post(g_ledger->ib, g_ledger->cfg, id);
  send_db_message(g_ledger->ib, dbDelete, TABLE_DOCUMENTS, (void *)(intptr_t)id);
  ASSERT_EQUAL(ledger_count_references(g_ledger->ib, TABLE_DOC_LINES, "document_id", id), 0);
  ASSERT_EQUAL(ledger_count_references(g_ledger->ib, TABLE_ENTRIES, "document_id", id), 0);
  PASS();
}

static void test_record_lifecycle(void) {
  TEST("datasource: defaults on insert, numbering and recalculation by ledger_ib, delete rules, change scopes");
  database_t *ib = g_ledger->ib;
  db_document_t blank;
  ASSERT_TRUE(send_db_message(ib, dbInitRecord, TABLE_DOCUMENTS, &blank));
  ASSERT(strlen(blank.date) == 10 && !blank.number[0], "date defaults to today");
  ASSERT_TRUE(send_db_message(ib, dbBeginChanges, 0, NULL));
  blank.doc_type_id = 1;  // receipt
  snprintf(blank.date, sizeof(blank.date), "2003-03-25");
  db_document_t *doc = (db_document_t *)send_db_message(ib, dbInsert, TABLE_DOCUMENTS, &blank);
  ASSERT(doc && !strcmp(doc->number, "ПТ-0002"), "numbered when inserted");
  int id = doc->id;
  db_doc_line_t line;
  send_db_message(ib, dbInitRecord, TABLE_DOC_LINES, &line);
  ASSERT(near(line.quantity, 1), "quantity defaults to one");
  line.document_id = id; line.item_id = 1;
  send_db_message(ib, dbInsert, TABLE_DOC_LINES, &line);
  ASSERT(near(doc->amount, 95) && near(doc->vat, 19), "a new line recalculates its document");
  ASSERT_TRUE(send_db_message(ib, dbRevertChanges, 0, NULL));
  ASSERT(!send_db_message(ib, dbFind, MAKEDWORD(TABLE_DOCUMENTS, 0), (void *)(intptr_t)id), "revert removes the document");
  ASSERT_EQUAL(ledger_count_references(ib, TABLE_DOC_LINES, "document_id", id), 0);
  db_document_t *receipt = find_by(ib, TABLE_DOCUMENTS, "number", "ПТ-0001");
  ASSERT(receipt && near(receipt->total, 51120), "surviving records keep their address and contents");
  int referrers = (int)send_db_message(ib, dbCountReferrers, TABLE_COUNTERPARTIES, (void *)(intptr_t)1);
  ASSERT(referrers >= 3, "documents refer to the supplier");
  ASSERT_FALSE(send_db_message(ib, dbDelete, TABLE_COUNTERPARTIES, (void *)(intptr_t)1));  // on-delete="deny"
  ASSERT_NOT_NULL((void *)send_db_message(ib, dbFind, MAKEDWORD(TABLE_COUNTERPARTIES, 0), (void *)(intptr_t)1));
  int receipt_id = receipt->id;  // a deleted record comes back at a new address
  send_db_message(ib, dbBeginChanges, 0, NULL);
  ASSERT_TRUE(send_db_message(ib, dbDelete, TABLE_DOCUMENTS, (void *)(intptr_t)receipt_id));  // cascade
  ASSERT_EQUAL(ledger_count_references(ib, TABLE_DOC_LINES, "document_id", receipt_id), 0);
  ASSERT_EQUAL(ledger_count_references(ib, TABLE_ENTRIES, "document_id", receipt_id), 0);
  send_db_message(ib, dbRevertChanges, 0, NULL);
  ASSERT_EQUAL(ledger_count_references(ib, TABLE_DOC_LINES, "document_id", receipt_id), 3);
  ASSERT_EQUAL(ledger_count_references(ib, TABLE_ENTRIES, "document_id", receipt_id), 6);
  PASS();
}

static void test_save_and_reload(void) {
  TEST("db_schema_xml: save and reload keep records, doubles and ids");
  ASSERT_TRUE(send_db_message(g_ledger->ib, dbSave, 0, NULL));
  database_t *copy = create_database_with_schema("ib_copy", "db_schema_xml", &ledger_ib_schema, TMP_DIR "/infobase.xml");
  ASSERT_NOT_NULL(copy);
  db_document_t *doc = (db_document_t *)send_db_message(copy, dbFind,
    MAKEDWORD(TABLE_DOCUMENTS, ledger_field(copy, TABLE_DOCUMENTS, "number")), (void *)"ПТ-0001");
  bool ok = doc && near(doc->total, 51120) && doc->posted;
  db_document_t fresh = { .doc_type_id = 1 };
  db_document_t *added = (db_document_t *)send_db_message(copy, dbInsert, TABLE_DOCUMENTS, &fresh);
  ok = ok && added && added->id > 7;
  destroy_database(copy);
  ASSERT(ok, "reloaded infobase matches");
  PASS();
}

static void test_print_forms(void) {
  TEST("print forms: configuration templates are filled from the document");
  db_document_t *sale = find_by(g_ledger->ib, TABLE_DOCUMENTS, "number", "РТ-0001");
  char report[2048];
  ASSERT_EQUAL(ledger_print_document(sale->id, report, sizeof(report)), 3);
  FILE *f = fopen(TMP_DIR "/print/РТ-0001-schet-faktura.html", "rb");
  ASSERT_NOT_NULL(f);
  static char html[65536];
  size_t n = fread(html, 1, sizeof(html) - 1, f);
  fclose(f);
  html[n] = '\0';
  ASSERT(strstr(html, "СЧЕТ-ФАКТУРА № РТ-0001 от 10.03.2003"), "header with number and date");
  ASSERT(strstr(html, "Монитор 17&quot; Samsung SyncMaster 753s"), "escaped line item");
  ASSERT(strstr(html, "7701234567/770101001"), "seller tax id and KPP");
  ASSERT(!strstr(html, "{{"), "no unfilled tags");
  f = fopen(TMP_DIR "/print/РТ-0001-torg12.html", "rb");
  ASSERT_NOT_NULL(f);
  n = fread(html, 1, sizeof(html) - 1, f);
  fclose(f);
  html[n] = '\0';
  ASSERT(strstr(html, "Шестнадцать тысяч шестьсот пятьдесят шесть рублей 00 копеек"), "amount in words, in Russian whatever the UI language");
  db_document_t *payroll = find_by(g_ledger->ib, TABLE_DOCUMENTS, "number", "НЗ-0001");
  ASSERT_EQUAL(ledger_print_document(payroll->id, report, sizeof(report)), 1);
  f = fopen(TMP_DIR "/print/НЗ-0001-t49.html", "rb");
  ASSERT_NOT_NULL(f);
  n = fread(html, 1, sizeof(html) - 1, f);
  fclose(f);
  html[n] = '\0';
  ASSERT(strstr(html, "Кузнецова Елена Николаевна") && strstr(html, "Тридцать шесть тысяч рублей 00 копеек"), "T-49 lists employees");
  db_document_t *cash_in = find_by(g_ledger->ib, TABLE_DOCUMENTS, "number", "ПКО-0001");
  ASSERT_EQUAL(ledger_print_document(cash_in->id, report, sizeof(report)), 1);
  f = fopen(TMP_DIR "/print/ПКО-0001-ko1.html", "rb");
  ASSERT_NOT_NULL(f);
  n = fread(html, 1, sizeof(html) - 1, f);
  fclose(f);
  html[n] = '\0';
  ASSERT(strstr(html, "Принято от <span class=\"line\" style=\"width:75%\">ИП Иванова Ивана Ивановича</span>"), "the payer in the genitive");
  PASS();
}

static void test_generated_bindings(void) {
  TEST("orionc: numeric fields bind through typed DDX, combos by value");
  ASSERT(ledger_document_form.db_record_size == sizeof(db_document_t), "record size");
  bool amount = false, doc_type = false;
  for (int i = 0; i < ledger_document_form.binding_count; i++) {
    const ctrl_binding_t *b = &ledger_document_form.bindings[i];
    if (b->ctrl_id == ID_DOCUMENT_AMOUNT) amount = b->push == ddx_push_double && b->pull == ddx_pull_double;
    if (b->ctrl_id == ID_DOCUMENT_DOC_TYPE) doc_type = b->pull == ddx_pull_combo_value;
  }
  ASSERT(amount && doc_type, "typed bindings");
  PASS();
}

static void test_russian_catalogs(void) {
  TEST("localization: Russian catalog is complete; the configuration renames national terms");
  xmlDocPtr doc = xmlReadFile("apps/ledger/share/locale/ru.xml", NULL, XML_PARSE_NONET);
  ASSERT_NOT_NULL(doc);
  int empty = 0, total = 0;
  for (xmlNodePtr n = xmlDocGetRootElement(doc)->children; n; n = n->next) {
    if (n->type != XML_ELEMENT_NODE) continue;
    xmlChar *text = xmlNodeGetContent(n);
    total++; empty += !text || !text[0];
    xmlFree(text);
  }
  xmlFreeDoc(doc);
  ASSERT(total > 100 && empty == 0, "every Ledger string has a Russian translation");
  ui_set_language("ru");
  ui_add_string_catalog_dir("apps/ledger/share/locale");
  ASSERT_STR_EQUAL(ui_tr("Tax ID:"), "Налоговый номер:");
  ui_add_string_catalog_dir(SHARE "/ru-2003/locale");
  ASSERT_STR_EQUAL(ui_tr("Tax ID:"), "ИНН:");
  ASSERT_STR_EQUAL(ui_tr("Trial Balance"), "Оборотно-сальдовая ведомость");
  char buf[64];
  db_document_t *doc_rec = find_by(g_ledger->ib, TABLE_DOCUMENTS, "number", "ПТ-0001");
  ASSERT_TRUE(db_get_field_text(g_ledger->ib, TABLE_DOCUMENTS, doc_rec, "total", buf, sizeof(buf)));
  ASSERT_STR_EQUAL(buf, "51 120,00");
  ASSERT_TRUE(db_get_field_text(g_ledger->ib, TABLE_DOCUMENTS, doc_rec, "date", buf, sizeof(buf)));
  ASSERT_STR_EQUAL(buf, "03.03.2003");
  ui_set_language("en");
  PASS();
}

static void test_german_configuration(void) {
  TEST("de-2003: SKR 03 postings, 16 % USt and German formats from the same platform");
  database_t *cfg = g_ledger->cfg, *ib = g_ledger->ib;
  g_ledger->cfg = open_db("cfg", &ledger_cfg_schema, SHARE "/de-2003/configuration.xml");
  g_ledger->ib = open_db("ib", &ledger_ib_schema, SHARE "/de-2003/demo.xml");
  snprintf(g_ledger->ib->source_path, sizeof(g_ledger->ib->source_path), TMP_DIR "/infobase-de.xml");
  ledger_repost_all(g_ledger->ib, g_ledger->cfg);
  const db_balance_t *bank = balance("1200"), *ust = balance("1775"), *receivables = balance("1400");
  bool ok = bank && near(bank->balance_dr, 25000 - 2003.32 - 1633.54 - 1450.54) && ust && near(ust->balance_cr, 96.48) &&
            receivables && near(receivables->balance_dr, 0);
  ok = ok && near(balance("1741")->balance_cr, (2500 - 603 + 2200 - 603) * 0.18) && near(balance("1740")->balance_cr, 0);
  ok = ok && near(balance("4830")->balance_dr, 41.67) && near(balance("0490")->balance_dr, 1500 - 166.68 - 41.67);  // AfA from the month of purchase
  ok = ok && near(balance("1210")->balance_dr, 2000 * 0.917) && near(balance("2150")->balance_dr, 1852 - 1834);
  ui_set_language("de");
  ui_add_string_catalog_dir("apps/ledger/share/locale");
  ui_add_string_catalog_dir(SHARE "/de-2003/locale");
  char buf[64];
  ok = ok && !strcmp(ui_tr("Tax ID:"), "Steuernummer:") && !strcmp(ui_tr("Tax"), "USt") &&
       !strcmp(ui_tr("Trial Balance"), "Summen- und Saldenliste");
  ok = ok && !strcmp(ui_format_number(2003.32, 2, buf, sizeof(buf)), "2.003,32");
  ui_set_language("en");
  destroy_database(g_ledger->ib);
  destroy_database(g_ledger->cfg);
  g_ledger->cfg = cfg; g_ledger->ib = ib;
  register_database("cfg", cfg);
  register_database("ib", ib);
  ASSERT(ok, "German books balance and speak German");
  PASS();
}

// Opens another configuration and its demo infobase in place of the test's.
static void switch_configuration(const char *code, const char *tmp_ib) {
  snprintf(g_ledger->config_dir, sizeof(g_ledger->config_dir), SHARE "/%s", code);
  char path[512];
  snprintf(path, sizeof(path), SHARE "/%s/configuration.xml", code);
  g_ledger->cfg = open_db("cfg", &ledger_cfg_schema, path);
  snprintf(path, sizeof(path), SHARE "/%s/demo.xml", code);
  g_ledger->ib = open_db("ib", &ledger_ib_schema, path);
  snprintf(g_ledger->ib->source_path, sizeof(g_ledger->ib->source_path), TMP_DIR "/%s", tmp_ib);
  ledger_script_open(g_ledger->config_dir, TMP_DIR);
  ledger_repost_all(g_ledger->ib, g_ledger->cfg);
}

static void restore_configuration(database_t *cfg, database_t *ib) {
  destroy_database(g_ledger->ib);
  destroy_database(g_ledger->cfg);
  g_ledger->cfg = cfg; g_ledger->ib = ib;
  register_database("cfg", cfg);
  register_database("ib", ib);
  snprintf(g_ledger->config_dir, sizeof(g_ledger->config_dir), SHARE "/ru-2003");
  ledger_script_open(g_ledger->config_dir, TMP_DIR);
}

static const db_payroll_line_t *payroll_line(int document_id, int employee_id) {
  const db_payroll_line_t *found = NULL;
  result_node_t *lines = (result_node_t *)send_db_message(g_ledger->ib, dbFetch,
    MAKEDWORD(TABLE_PAYROLL_LINES, ledger_field(g_ledger->ib, TABLE_PAYROLL_LINES, "document_id")), (void *)(intptr_t)document_id);
  for (result_node_t *n = lines; n; n = n->next) {
    const db_payroll_line_t *l = *(db_payroll_line_t **)n->data;
    if (l->employee_id == employee_id) found = l;
  }
  free_result_list(lines);
  return found;
}

// Posts an accrual of amount for employee on date and returns its line.
static const db_payroll_line_t *accrue(int doc_type_id, const char *date, int employee_id, double amount, int *doc_id) {
  db_document_t blank = { .doc_type_id = doc_type_id };
  snprintf(blank.date, sizeof(blank.date), "%s", date);
  db_document_t *doc = (db_document_t *)send_db_message(g_ledger->ib, dbInsert, TABLE_DOCUMENTS, &blank);
  *doc_id = doc->id;
  db_payroll_line_t line = { .document_id = doc->id, .employee_id = employee_id, .amount = amount };
  send_db_message(g_ledger->ib, dbInsert, TABLE_PAYROLL_LINES, &line);
  ledger_post(g_ledger->ib, g_ledger->cfg, doc->id);
  return payroll_line(doc->id, employee_id);
}

static const db_vat_summary_t *vat_summary(int book, double rate) {
  const db_vat_summary_t *found = NULL;
  result_node_t *rows = (result_node_t *)send_db_message(g_ledger->ib, dbFetch, MAKEDWORD(TABLE_VAT_SUMMARY, 0), NULL);
  for (result_node_t *n = rows; n; n = n->next) {
    const db_vat_summary_t *s = *(db_vat_summary_t **)n->data;
    const db_vat_rate_t *r = (const db_vat_rate_t *)send_db_message(g_ledger->cfg, dbFind, MAKEDWORD(TABLE_VAT_RATES, 0), (void *)(intptr_t)s->vat_rate_id);
    if (s->book == book && (r ? near(r->rate, rate) : rate < 0)) found = s;
  }
  free_result_list(rows);
  return found;
}

static bool file_contains(const char *path, const char *a, const char *b) {
  static char text[1 << 16];
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  size_t n = fread(text, 1, sizeof(text) - 1, f);
  fclose(f);
  text[n] = '\0';
  return strstr(text, a) && (!b || strstr(text, b)) && !strstr(text, "{{");
}

static void test_ru_2026(void) {
  TEST("ru-2026: VAT 22 % and 10 % books, progressive income tax, contribution cap, 2026 forms");
  database_t *cfg = g_ledger->cfg, *ib = g_ledger->ib;
  switch_configuration("ru-2026", "infobase-ru-2026.xml");
  db_document_t *receipt = find_by(g_ledger->ib, TABLE_DOCUMENTS, "number", "ПТ-0001");
  bool ok = receipt && near(receipt->amount, 176000) && near(receipt->vat, 37880) && near(receipt->total, 213880);
  ok = ok && near(balance("19.04")->balance_dr, 13200);  // input tax on services waits for its deduction document
  ok = ok && near(balance("68.02")->credit_turnover, 13038) && near(balance("68.02")->debit_turnover, 37880);
  const db_vat_summary_t *purchases22 = vat_summary(2, 22), *sales10 = vat_summary(1, 10), *purchases_norate = vat_summary(2, -1);
  ok = ok && purchases22 && near(purchases22->vat, 37180 + 13200) && purchases22->documents == 2;
  ok = ok && sales10 && near(sales10->amount, 3000) && near(sales10->vat, 300);
  ok = ok && purchases_norate && near(purchases_norate->vat, 13200);  // the equipment receipt has no lines
  // Payroll: 13 % after the child deduction, rounded to roubles; 30 % and 0.2 % on top.
  const db_payroll_line_t *sidorova = payroll_line(14, 2), *petrov = payroll_line(14, 1);
  ok = ok && sidorova && near(sidorova->withheld, 15418) && near(sidorova->contributions, 36240) && petrov && near(petrov->net, 130500);
  ok = ok && near(balance("68.01")->balance_cr, 46072) && near(balance("69.09")->balance_cr, 108000) && near(balance("69.11")->balance_cr, 720);
  ok = ok && near(balance("70")->balance_cr, 0) && near(balance("02.01")->balance_cr, 15000);
  ASSERT(ok, "March 2026 of the demo company balances");
  // A high earner in April: the 15 % bracket on the year's income so far and the 15.1 % rate above the cap.
  int id = 0;
  const db_payroll_line_t *rich = accrue(11, "2026-04-30", 1, 3000000, &id);
  ASSERT(rich && near(rich->withheld, 405000), "cumulative scale: 312 000 + 15 % of the part above 2.4 million, less March's tax");
  ASSERT(rich && near(rich->contributions, 874521 + 6000), "30 % up to the 2 979 000 base, 15.1 % above, 0.2 % injury insurance");
  send_db_message(g_ledger->ib, dbDelete, TABLE_DOCUMENTS, (void *)(intptr_t)id);
  ledger_build_reports(g_ledger->ib, g_ledger->cfg);
  // Print forms of 2026: Decree 1137 invoice, UPD, books, VAT summary, trial balance, journal.
  db_document_t *sale = find_by(g_ledger->ib, TABLE_DOCUMENTS, "number", "РТ-0001");
  char report[4096];
  ASSERT_EQUAL(ledger_print_document(sale->id, report, sizeof(report)), 3);
  ASSERT(file_contains(TMP_DIR "/print/РТ-0001-schet-faktura.html", "СЧЕТ-ФАКТУРА № РТ-0001 от 10.03.2026", "Российский рубль, 643"), "invoice per Decree 1137");
  ASSERT(file_contains(TMP_DIR "/print/РТ-0001-upd.html", "Статус:", "ИП Иванов Иван Иванович"), "UPD");
  ASSERT_EQUAL(ledger_print_reports(report, sizeof(report)), 6);
  ASSERT(file_contains(TMP_DIR "/print/kniga-prodazh.html", "КНИГА ПРОДАЖ", "ИП Иванов Иван Иванович"), "sales book");
  ASSERT(file_contains(TMP_DIR "/print/kniga-pokupok.html", "КНИГА ПОКУПОК", "Торговый дом Альфа"), "purchase book");
  ASSERT(file_contains(TMP_DIR "/print/osv.html", "ОБОРОТНО-САЛЬДОВАЯ ВЕДОМОСТЬ", "Расчетные счета"), "trial balance form");
  ASSERT(file_contains(TMP_DIR "/print/journal.html", "ЖУРНАЛ ОПЕРАЦИЙ", "Реализация товаров РТ-0001"), "journal form");
  ASSERT(ledger_count_references(g_ledger->cfg, TABLE_REGULATIONS, "area", 0) == 0, "regulations filter by text, not by number");
  result_node_t *rules = (result_node_t *)send_db_message(g_ledger->cfg, dbFetch, MAKEDWORD(TABLE_REGULATIONS, 0), NULL);
  int nrules = count_result_list(rules);
  free_result_list(rules);
  ASSERT(nrules >= 20, "legal basis listed");
  restore_configuration(cfg, ib);
  PASS();
}

static void test_de_2026(void) {
  TEST("de-2026: automatic accounts per rate, 2026 social insurance caps, § 32a tariff, effective dating, DATEV export");
  database_t *cfg = g_ledger->cfg, *ib = g_ledger->ib;
  switch_configuration("de-2026", "infobase-de-2026.xml");
  bool ok = near(balance("1776")->balance_cr, 99.35) && near(balance("1771")->balance_cr, 8.38);
  ok = ok && near(balance("1576")->balance_dr, 197.60 + 228) && near(balance("1571")->balance_dr, 20.93);
  ok = ok && near(balance("8400")->balance_cr, 522.90) && near(balance("8300")->balance_cr, 119.70);
  ok = ok && near(balance("3400")->balance_dr, 1040) && near(balance("3300")->balance_dr, 299) && near(balance("4210")->balance_dr, 1200);
  ASSERT(ok, "7 % lines post to 3300/1571 and 8300/1771, 19 % lines to 3400/1576 and 8400/1776; a service invoice keeps its expense account");
  const db_payroll_line_t *anna = payroll_line(8, 1), *thomas = payroll_line(8, 2);
  ok = anna && near(anna->withheld, 528.75 + 180.91) && near(anna->contributions, 532.50) && near(anna->net, 1790.34);
  ok = ok && thomas && near(thomas->withheld, 465.30 + 121.58) && near(thomas->net, 1613.12);
  ok = ok && near(balance("1741")->balance_cr, 302.49) && near(balance("1742")->balance_cr, 1995.15) && near(balance("1740")->balance_cr, 0);
  ASSERT(ok, "21.15 % employee insurance, Lohnsteuer on twelve monthly bases less Vorsorgepauschale and Pauschbeträge, employer 21.3 %");
  int id = 0;
  const db_payroll_line_t *rich = accrue(9, "2026-04-30", 1, 9000, &id);
  ASSERT(rich && near(rich->withheld, 508.59 + 104.63 + 785.85 + 109.85 + 2173.91 + 56.89),
         "insurance capped at 5 812,50 / 8 450; 42 % zone; Solidaritätszuschlag from rules.lua in the Milderungszone");
  ASSERT(rich && near(rich->contributions, 508.59 + 104.63 + 785.85 + 109.85 + 12.68), "employer shares and Insolvenzgeldumlage capped too");
  send_db_message(g_ledger->ib, dbDelete, TABLE_DOCUMENTS, (void *)(intptr_t)id);
  const db_payroll_line_t *last_year = accrue(9, "2025-06-30", 1, 2500, &id);
  ASSERT(last_year && near(last_year->withheld, 523.75 + 188.25), "a 2025 document applies the 2025 rows: Zusatzbeitrag 2.5 %, tariff 2025");
  send_db_message(g_ledger->ib, dbDelete, TABLE_DOCUMENTS, (void *)(intptr_t)id);
  ledger_build_reports(g_ledger->ib, g_ledger->cfg);
  ui_set_language("de");
  ui_add_string_catalog_dir("apps/ledger/share/locale");
  db_document_t *sale = find_by(g_ledger->ib, TABLE_DOCUMENTS, "number", "AR-0001");
  char report[4096];
  ASSERT_EQUAL(ledger_print_document(sale->id, report, sizeof(report)), 1);
  ASSERT(file_contains(TMP_DIR "/print/AR-0001-rechnung.html", "Rechnung AR-0001", "DE123456789"), "Rechnung per § 14 UStG");
  ASSERT_EQUAL(ledger_print_reports(report, sizeof(report)), 5);
  ASSERT(file_contains(TMP_DIR "/print/ustva.html", "<td class=\"c\">81</td><td class=\"c\">1</td><td class=\"r\">522,90</td><td class=\"r b\">99,35</td>",
                       "<td class=\"c\">86</td><td class=\"c\">1</td><td class=\"r\">119,70</td><td class=\"r b\">8,38</td>"), "UStVA figures per rate under their Kennzahl");
  ASSERT(file_contains(TMP_DIR "/print/datev-buchungsstapel.csv", "\"EXTF\";700;21;\"Buchungsstapel\"", "498,00;\"S\";\"EUR\";;;;1400;8400;;1003;\"AR-0001\""), "DATEV Buchungsstapel rows");
  ASSERT(file_contains(TMP_DIR "/print/susa.html", "Summen- und Saldenliste", "Umsatzsteuer 19 %"), "SuSa");
  ui_set_language("en");
  restore_configuration(cfg, ib);
  PASS();
}

// rules.lua of a configuration posts document types, computes taxes and
// closes the month; plugins subscribe to postings.
static void test_scripted_rules(void) {
  TEST("rules.lua: scripted document posting at average cost, scripted payroll tax, plugin event, errors stay contained");
  database_t *cfg = g_ledger->cfg, *ib = g_ledger->ib;
  switch_configuration("ru-2026", "infobase-ru-2026-script.xml");
  ASSERT(ledger_script_has("post_writeoff") && ledger_script_has("contributions_sme") && !ledger_script_has("no_such_rule"), "rules.lua loaded");
  // Inventory write-off: Dt 94 Kt 41.01 per line at the average cost on the date (ФСБУ 5/2019).
  db_doc_type_t *writeoff = find_by(g_ledger->cfg, TABLE_DOC_TYPES, "code", "writeoff");
  ASSERT(writeoff && !strcmp(writeoff->script, "post_writeoff"), "document type posts by script");
  db_document_t blank = { .doc_type_id = writeoff->id };
  snprintf(blank.date, sizeof(blank.date), "2026-03-25");
  db_document_t *doc = (db_document_t *)send_db_message(g_ledger->ib, dbInsert, TABLE_DOCUMENTS, &blank);
  db_doc_line_t line = { .document_id = doc->id, .item_id = 1, .quantity = 10, .price = 0 };  // paper bought at 450
  send_db_message(g_ledger->ib, dbInsert, TABLE_DOC_LINES, &line);
  ASSERT_TRUE(ledger_post(g_ledger->ib, g_ledger->cfg, doc->id));
  ledger_build_reports(g_ledger->ib, g_ledger->cfg);
  ASSERT(near(entry_sum("94", "41.01"), 4500) && near(doc->total, 4500), "ten sheets of paper written off at 450 each");
  // A scripted tax row: the SME reduced tariff, 30 % up to 1.5 МРОТ and 15 % above it.
  db_payroll_tax_t *tax = find_by(g_ledger->cfg, TABLE_PAYROLL_TAXES, "code", "contributions");
  snprintf(tax->script, sizeof(tax->script), "contributions_sme");
  send_db_message(g_ledger->cfg, dbUpdate, TABLE_PAYROLL_TAXES, tax);
  int id = 0;
  const db_payroll_line_t *petrov = accrue(11, "2026-04-30", 1, 150000, &id);
  ASSERT(petrov && near(petrov->contributions, 40639.50 * 0.30 + (150000 - 40639.50) * 0.15 + 300), "scripted tariff plus the built-in injury row");
  send_db_message(g_ledger->ib, dbDelete, TABLE_DOCUMENTS, (void *)(intptr_t)id);
  tax->script[0] = '\0';
  send_db_message(g_ledger->cfg, dbUpdate, TABLE_PAYROLL_TAXES, tax);
  // A tax naming a missing function is skipped with a diagnostic; the others still apply.
  db_payroll_tax_t *injury = find_by(g_ledger->cfg, TABLE_PAYROLL_TAXES, "code", "injury");
  snprintf(injury->script, sizeof(injury->script), "no_such_rule");
  send_db_message(g_ledger->cfg, dbUpdate, TABLE_PAYROLL_TAXES, injury);
  petrov = accrue(11, "2026-05-31", 1, 150000, &id);
  ASSERT(petrov && near(petrov->contributions, 45000), "the broken row contributes nothing, the unified tariff still posts");
  send_db_message(g_ledger->ib, dbDelete, TABLE_DOCUMENTS, (void *)(intptr_t)id);
  injury->script[0] = '\0';
  send_db_message(g_ledger->cfg, dbUpdate, TABLE_PAYROLL_TAXES, injury);
  // The cash-limit plugin ran on every posting (it logs; posting is unaffected).
  db_document_t *cash = find_by(g_ledger->ib, TABLE_DOCUMENTS, "number", "ПКО-0001");
  ASSERT(cash && cash->posted, "plugins do not block postings");
  restore_configuration(cfg, ib);
  PASS();
}

// Every installed configuration loads and refers only to accounts, templates
// and scales it defines: the check a compliance refresh must keep passing.
static void test_configuration_integrity(void) {
  TEST("configurations: every installed configuration resolves its accounts, templates and tax scales");
  database_t *catalog = create_database_with_schema("catalog_test", "db_schema_xml", &ledger_catalog_schema, SHARE "/index.xml");
  ASSERT_NOT_NULL(catalog);
  result_node_t *configs = (result_node_t *)send_db_message(catalog, dbFetch, MAKEDWORD(TABLE_CONFIGURATIONS, 0), NULL);
  int checked = 0, problems = 0;
  for (result_node_t *n = configs; n; n = n->next) {
    const db_configuration_t *c = *(db_configuration_t **)n->data;
    char path[512];
    snprintf(path, sizeof(path), SHARE "/%s/configuration.xml", c->code);
    database_t *cfg = create_database_with_schema("cfg_check", "db_schema_xml", &ledger_cfg_schema, path);
    if (!cfg) { problems++; fprintf(stderr, "  %s: configuration.xml missing\n", c->code); continue; }
    const int account_fields[][2] = { { TABLE_DOC_TYPES, 0 }, { TABLE_PAYROLL_TAXES, 0 }, { TABLE_CLOSING_RULES, 0 }, { TABLE_BALANCE_RULES, 0 }, { TABLE_VAT_RATES, 0 }, { TABLE_SETTINGS, 0 } };
    for (size_t t = 0; t < ARRAY_LEN(account_fields); t++) {
      const db_table_schema_t *table = db_schema_table(cfg->schema, account_fields[t][0]);
      result_node_t *rows = (result_node_t *)send_db_message(cfg, dbFetch, MAKEDWORD(table->table_id, 0), NULL);
      for (result_node_t *r = rows; r; r = r->next) {
        for (int j = 0; j < table->join_count; j++) {
          if (strcmp(table->joins[j].foreign_table, "accounts")) continue;
          int f = db_schema_field_index(table, table->joins[j].local_field);
          int id = *(const int *)((const char *)*(void **)r->data + table->meta[f].offset);
          if (id && !send_db_message(cfg, dbFind, MAKEDWORD(TABLE_ACCOUNTS, 0), (void *)(intptr_t)id)) {
            problems++; fprintf(stderr, "  %s: %s.%s = %d names no account\n", c->code, table->name, table->joins[j].local_field, id);
          }
        }
      }
      free_result_list(rows);
    }
    result_node_t *forms = (result_node_t *)send_db_message(cfg, dbFetch, MAKEDWORD(TABLE_PRINT_FORMS, 0), NULL);
    for (result_node_t *r = forms; r; r = r->next) {
      const db_print_form_t *form = *(db_print_form_t **)r->data;
      snprintf(path, sizeof(path), SHARE "/%s/print/%s", c->code, form->template);
      if (!axPathExists(path)) { problems++; fprintf(stderr, "  %s: template %s missing\n", c->code, form->template); }
      if (form->report[0] && !db_schema_table_named(&ledger_ib_schema, form->report)) { problems++; fprintf(stderr, "  %s: report table %s unknown\n", c->code, form->report); }
    }
    free_result_list(forms);
    snprintf(path, sizeof(path), SHARE "/%s", c->code);
    ledger_script_open(path, NULL);
    result_node_t *types = (result_node_t *)send_db_message(cfg, dbFetch, MAKEDWORD(TABLE_DOC_TYPES, 0), NULL);
    for (result_node_t *r = types; r; r = r->next) {
      const db_doc_type_t *type = *(db_doc_type_t **)r->data;
      if (type->script[0] && !ledger_script_has(type->script)) { problems++; fprintf(stderr, "  %s: doc type %s names no rules.lua function %s\n", c->code, type->code, type->script); }
    }
    free_result_list(types);
    result_node_t *taxes = (result_node_t *)send_db_message(cfg, dbFetch, MAKEDWORD(TABLE_PAYROLL_TAXES, 0), NULL);
    for (result_node_t *r = taxes; r; r = r->next) {
      const db_payroll_tax_t *tax = *(db_payroll_tax_t **)r->data;
      if (tax->script[0] && !ledger_script_has(tax->script)) { problems++; fprintf(stderr, "  %s: tax %s names no rules.lua function %s\n", c->code, tax->code, tax->script); }
      bool scaled = tax->scale[0] != '\0';
      int zones = ledger_count_references(cfg, TABLE_TAX_SCALES, "tax_id", tax->id);
      if (scaled != (zones > 0)) { problems++; fprintf(stderr, "  %s: tax %s scale='%s' has %d zones\n", c->code, tax->code, tax->scale, zones); }
      if (tax->valid_to[0] && strcmp(tax->valid_from, tax->valid_to) > 0) { problems++; fprintf(stderr, "  %s: tax %s valid_from after valid_to\n", c->code, tax->code); }
    }
    free_result_list(taxes);
    destroy_database(cfg);
    checked++;
  }
  free_result_list(configs);
  destroy_database(catalog);
  ledger_script_open(SHARE "/ru-2003", TMP_DIR);
  ASSERT(checked >= 4 && problems == 0, "all configurations consistent");
  PASS();
}

static void test_import_enterprisedata(void) {
  TEST("1C import: EnterpriseData catalogs and opening balances, matched records, no double import");
  ledger_import_t r;
  ASSERT_FALSE(ledger_import_enterprisedata(g_ledger->ib, g_ledger->cfg, SHARE "/ru-2003/configuration.xml", &r));
  ASSERT(r.error[0], "a file in another format is refused");
  ledger_build_reports(g_ledger->ib, g_ledger->cfg);
  double bank = balance("51")->balance_dr;
  ASSERT_TRUE(ledger_import_enterprisedata(g_ledger->ib, g_ledger->cfg, "apps/ledger/tests/enterprisedata_sample.xml", &r));
  ASSERT(r.counterparties == 2 && r.items == 2 && r.employees == 1 && r.assets == 1 && r.currencies == 0, "new catalog objects added");
  ASSERT(r.matched == 2 && r.ignored == 1, "Alfa (by tax ID) and EUR matched; other documents ignored");
  ASSERT(r.entries == 6 && near(r.debit, 66108) && near(r.credit, 16108), "opening entries of every balance row");
  ASSERT_STR_EQUAL(r.skipped, "ОстаткиОсновныхСредств");  // no balance rule in the configuration
  ASSERT_NULL(find_by(g_ledger->ib, TABLE_CURRENCIES, "code", "RUB"));  // the national currency is no catalog entry
  db_counterparty_t *gamma = find_by(g_ledger->ib, TABLE_COUNTERPARTIES, "tax_id", "7714000001");
  ASSERT(gamma && strstr(gamma->address, "Арбат") && !strcmp(gamma->phone, "(095) 241-00-01"), "contact information read");
  db_item_t *toner = find_by(g_ledger->ib, TABLE_ITEMS, "sku", "00077");
  ASSERT(toner && toner->unit_id == 1 && toner->vat_rate_id == 1, "unit by OKEI code, VAT rate by value");
  db_item_t *paper = find_by(g_ledger->ib, TABLE_ITEMS, "name", "Бумага SvetoCopy A4");
  ASSERT(paper && !paper->sku[0] && paper->unit_id == 1 && paper->vat_rate_id && paper->vat_rate_id != toner->vat_rate_id,
         "an item created from a balance row takes the row's unit and VAT rate, not its group's code");
  db_employee_t *smirnova = find_by(g_ledger->ib, TABLE_EMPLOYEES, "name", "Смирнова Ольга Петровна");
  ASSERT(smirnova && smirnova->account_id, "employee named from ФИО, with the usual expense account");
  db_document_t *opening = find_by(g_ledger->ib, TABLE_DOCUMENTS, "comment", ui_tr("Opening balances imported from 1C"));
  ASSERT(opening && opening->posted && !strcmp(opening->date, "2003-02-28"), "one posted opening operation on the 1C date");
  db_counterparty_t *delta = find_by(g_ledger->ib, TABLE_COUNTERPARTIES, "tax_id", "7714000002");
  ASSERT_NOT_NULL(delta);  // created from the reference in a balance row
  bool delta_row = false, alfa_row = false, toner_row = false, paper_row = false, eur_row = false;
  db_counterparty_t *alfa = find_by(g_ledger->ib, TABLE_COUNTERPARTIES, "tax_id", "7705123456");
  result_node_t *entries = (result_node_t *)send_db_message(g_ledger->ib, dbFetch,
    MAKEDWORD(TABLE_ENTRIES, ledger_field(g_ledger->ib, TABLE_ENTRIES, "document_id")), (void *)(intptr_t)opening->id);
  for (result_node_t *n = entries; n; n = n->next) {
    const db_entry_t *e = *(db_entry_t **)n->data;
    delta_row |= e->debit_subconto_id == delta->id && near(e->amount, 12000);
    alfa_row |= e->credit_subconto_id == alfa->id && near(e->amount, 16108);
    toner_row |= e->debit_subconto_id == toner->id && near(e->quantity, 10) && near(e->amount, 950);
    paper_row |= e->debit_subconto_id == paper->id && near(e->quantity, 2) && e->amount == 0;
    eur_row |= e->currency_id != 0 && near(e->currency_amount, 100) && near(e->amount, 3158);
  }
  free_result_list(entries);
  ASSERT(delta_row && alfa_row && toner_row && paper_row && eur_row, "side, analytics, quantity and currency amount on the entries");
  ledger_build_reports(g_ledger->ib, g_ledger->cfg);
  ASSERT(near(balance("51")->balance_dr, bank + 50000), "the opening balance reaches the trial balance");
  ASSERT_TRUE(ledger_import_enterprisedata(g_ledger->ib, g_ledger->cfg, "apps/ledger/tests/enterprisedata_sample.xml", &r));
  ASSERT(r.counterparties + r.items + r.employees + r.assets == 0 && r.entries == 0 && r.balances_present, "a second import adds nothing");
  PASS();
}

// Inject failures at the database boundary, leaving the production API unchanged.
static dbproc_t import_db_proc;
static int import_fault, import_writes, import_entry_writes;
static lresult_t import_fault_proc(database_t *db, uint32_t msg, uint32_t wp, void *lp) {
  if (msg == dbInsert || msg == dbUpdate || msg == dbDelete) import_writes++;
  if (import_fault == 1 && msg == dbBeginChanges) return 0;
  if (import_fault == 2 && msg == dbInsert && wp == TABLE_COUNTERPARTIES) return 0;
  if (import_fault == 3 && msg == dbInsert && wp == TABLE_ENTRIES && ++import_entry_writes == 2) return 0;
  if (import_fault == 4 && msg == dbUpdate && wp == TABLE_DOCUMENTS && ((db_document_t *)lp)->posted) return 0;
  if (import_fault == 5 && msg == dbCommitChanges) return 0;
  if (import_fault == 6 && msg == dbInsert && wp == TABLE_DOCUMENTS) return 0;
  return import_db_proc(db, msg, wp, lp);
}

static char *import_snapshot(database_t *db) {
  db->dirty = true;
  if (!send_db_message(db, dbSave, 0, NULL)) return NULL;
  FILE *f = fopen(db->source_path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  rewind(f);
  char *text = calloc((size_t)size + 1, 1);
  if (text && fread(text, 1, (size_t)size, f) != (size_t)size) { free(text); text = NULL; }
  fclose(f);
  return text;
}

static void test_import_integrity(void) {
  TEST("1C import: distinct same-date exchanges, persisted identity and transaction failures");
  const char *path = TMP_DIR "/distinct.xml";
  FILE *f = fopen(path, "wb");
  ASSERT_NOT_NULL(f);
  fputs("<Message><Header><Format>EnterpriseData</Format></Header><Body>"
        "<Справочник.Контрагенты><Наименование>Atomic Vendor</Наименование><ИНН>9999999999</ИНН></Справочник.Контрагенты>"
        "<Документ.ОстаткиБезналичныхДенежныхСредств><Дата>2003-02-28T00:00:00</Дата>"
        "<ДенежныеСредства><Строка><Сумма>725</Сумма></Строка></ДенежныеСредства>"
        "</Документ.ОстаткиБезналичныхДенежныхСредств>"
        "<Документ.ОстаткиРасчетовСПоставщикамиДолги><Дата>2003-02-28T00:00:00</Дата>"
        "<Расчеты><Строка><Контрагент><Наименование>Atomic Vendor</Наименование><ИНН>9999999999</ИНН></Контрагент>"
        "<Сумма>250</Сумма></Строка></Расчеты></Документ.ОстаткиРасчетовСПоставщикамиДолги>"
        "</Body></Message>", f);
  fclose(f);
  database_t *ib = g_ledger->ib;
  char *before = import_snapshot(ib);
  ASSERT_NOT_NULL(before);
  ledger_import_t r;
  import_db_proc = ib->proc;
  for (int fault = 1; fault <= 6; fault++) {
    import_fault = fault; import_writes = import_entry_writes = 0;
    ib->proc = import_fault_proc;
    bool ok = ledger_import_enterprisedata(ib, g_ledger->cfg, path, &r);
    ib->proc = import_db_proc;
    ASSERT_FALSE(ok);
    ASSERT(r.error[0] && !r.entries && !r.counterparties && !r.debit && !r.credit, "failed import reports no committed records");
    ASSERT(fault != 1 || !import_writes, "failed begin performs no writes");
    ASSERT_FALSE(ib->dirty);
    char *after = import_snapshot(ib);
    ASSERT_NOT_NULL(after);
    ASSERT_STR_EQUAL(before, after);
    free(after);
  }
  free(before);
  ledger_build_reports(ib, g_ledger->cfg);
  double bank = balance("51")->balance_dr, suppliers = balance("60.01")->balance_cr;
  ASSERT_TRUE(ledger_import_enterprisedata(ib, g_ledger->cfg, path, &r));
  ASSERT(r.entries == 2 && r.counterparties == 1 && !r.balances_present && near(r.debit, 725) && near(r.credit, 250),
         "a different exchange of the same date imports both debit and credit");
  ledger_build_reports(ib, g_ledger->cfg);
  ASSERT(near(balance("51")->balance_dr, bank + 725) && near(balance("60.01")->balance_cr, suppliers + 250),
         "existing balances survive and new postings reach their accounts");
  db_document_t *opening = NULL;
  result_node_t *docs = (result_node_t *)send_db_message(ib, dbFetch, MAKEDWORD(TABLE_DOCUMENTS, 0), NULL);
  for (result_node_t *n = docs; n; n = n->next) {
    db_document_t *d = *(db_document_t **)n->data;
    if (d->import_source[0] && near(d->total, 975)) opening = d;
  }
  free_result_list(docs);
  ASSERT_NOT_NULL(opening);
  ASSERT_TRUE(opening->posted);
  db_account_t *bank_account = find_by(g_ledger->cfg, TABLE_ACCOUNTS, "code", "51");
  db_account_t *supplier_account = find_by(g_ledger->cfg, TABLE_ACCOUNTS, "code", "60.01");
  db_account_t *offset_account = find_by(g_ledger->cfg, TABLE_ACCOUNTS, "code", "000");
  db_counterparty_t *vendor = find_by(ib, TABLE_COUNTERPARTIES, "tax_id", "9999999999");
  ASSERT(bank_account && supplier_account && offset_account && vendor, "posting references exist");
  result_node_t *entries = (result_node_t *)send_db_message(ib, dbFetch,
    MAKEDWORD(TABLE_ENTRIES, ledger_field(ib, TABLE_ENTRIES, "document_id")), (void *)(intptr_t)opening->id);
  ASSERT_EQUAL(count_result_list(entries), 2);
  for (result_node_t *n = entries; n; n = n->next) {
    const db_entry_t *e = *(db_entry_t **)n->data;
    ASSERT_STR_EQUAL(e->date, "2003-02-28");
    ASSERT((near(e->amount, 725) && e->debit_id == bank_account->id && e->credit_id == offset_account->id) ||
           (near(e->amount, 250) && e->debit_id == offset_account->id && e->credit_id == supplier_account->id && e->credit_subconto_id == vendor->id),
           "balanced bank and supplier postings use the opening account and correct analytics");
  }
  free_result_list(entries);
  snprintf(opening->comment, sizeof(opening->comment), "Edited comment in another language");
  ASSERT_TRUE(send_db_message(ib, dbUpdate, TABLE_DOCUMENTS, opening));
  ASSERT_TRUE(send_db_message(ib, dbSave, 0, NULL));
  ASSERT_TRUE(send_db_message(ib, dbLoad, 0, NULL));
  ASSERT(rename(path, TMP_DIR "/renamed.xml") == 0, "rename exchange");
  before = import_snapshot(ib);
  ASSERT_NOT_NULL(before);
  ASSERT_TRUE(ledger_import_enterprisedata(ib, g_ledger->cfg, TMP_DIR "/renamed.xml", &r));
  ASSERT(!r.entries && !r.counterparties && r.balances_present, "identity survives save/reload, renamed file and edited comment");
  char *after = import_snapshot(ib);
  ASSERT_NOT_NULL(after);
  ASSERT_STR_EQUAL(before, after);
  free(before); free(after);
  // Legacy imports lack a digest: refuse ambiguity rather than skip or double it.
  docs = (result_node_t *)send_db_message(ib, dbFetch, MAKEDWORD(TABLE_DOCUMENTS, 0), NULL);
  opening = NULL;
  for (result_node_t *n = docs; n; n = n->next) {
    db_document_t *d = *(db_document_t **)n->data;
    if (d->import_source[0] && near(d->total, 975)) opening = d;
  }
  free_result_list(docs);
  ASSERT_NOT_NULL(opening);
  ASSERT_TRUE(send_db_message(ib, dbBeginChanges, 0, NULL));
  opening->import_source[0] = 0;
  snprintf(opening->comment, sizeof(opening->comment), "%s", ui_tr("Opening balances imported from 1C"));
  ASSERT_TRUE(send_db_message(ib, dbUpdate, TABLE_DOCUMENTS, opening));
  ASSERT_FALSE(ledger_import_enterprisedata(ib, g_ledger->cfg, TMP_DIR "/renamed.xml", &r));
  ASSERT(r.error[0] && !r.entries, "legacy imports require explicit reconciliation");
  ASSERT_TRUE(send_db_message(ib, dbRevertChanges, 0, NULL));
  remove(TMP_DIR "/renamed.xml");
  PASS();
}

static void test_import_digest(void) {
  TEST("1C exchange SHA-256: standard vectors including padding and multiple blocks");
  char digest[65], text[1001];
  ed_digest((const unsigned char *)"", 0, digest);
  ASSERT_STR_EQUAL(digest, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  ed_digest((const unsigned char *)"abc", 3, digest);
  ASSERT_STR_EQUAL(digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  memset(text, 'a', 1000);
  ed_digest((const unsigned char *)text, 1000, digest);
  ASSERT_STR_EQUAL(digest, "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
  ed_digest((const unsigned char *)text, 56, digest);
  ASSERT_STR_EQUAL(digest, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
  PASS();
}

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  TEST_START("Ledger");
  setup();
  test_configuration_loads();
  test_joins_cross_databases();
  test_posting_rules();
  test_cost_of_sales();
  test_payroll();
  test_polymorphic_analytics();
  test_fixed_assets();
  test_currency();
  test_month_closing();
  test_cash_book();
  test_unpost_and_trial_balance();
  test_period_reports();
  test_manual_operation();
  test_transient_tables();
  test_numbering_and_delete();
  test_record_lifecycle();
  test_save_and_reload();
  test_print_forms();
  test_generated_bindings();
  test_russian_catalogs();
  test_import_enterprisedata();
  test_import_digest();
  test_import_integrity();
  test_german_configuration();
  test_ru_2026();
  test_de_2026();
  test_scripted_rules();
  test_configuration_integrity();
  destroy_database(g_ledger->ib);
  destroy_database(g_ledger->cfg);
  ledger_script_close();
  TEST_END();
}

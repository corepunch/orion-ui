// ledger_test.c — schema-driven database, posting rules and print forms,
// exercised on the ru-2003 configuration and its demo infobase.

#include "test_framework.h"
#include "apps/ledger/ledger.h"
#include <math.h>

#define SHARE "apps/ledger/share/configurations"
#define TMP_DIR "build/test-ledger"

static ledger_t g_test_ledger;

static database_t *open_db(const char *name, const db_schema_def_t *schema, const char *path) {
  database_t *db = create_database_with_schema(name, "db_schema_xml", schema, path);
  if (db) register_database(name, db);
  return db;
}

static void setup(void) {
  DB_CLASS(db_schema_xml);
  memset(&g_test_ledger, 0, sizeof(g_test_ledger));
  g_ledger = &g_test_ledger;
  snprintf(g_ledger->config_dir, sizeof(g_ledger->config_dir), SHARE "/ru-2003");
  snprintf(g_ledger->data_dir, sizeof(g_ledger->data_dir), TMP_DIR);
  axMkDir("build");
  axMkDir(TMP_DIR);
  g_ledger->cfg = open_db("cfg", &ledger_cfg_schema, SHARE "/ru-2003/configuration.xml");
  g_ledger->ib = open_db("ib", &ledger_ib_schema, SHARE "/ru-2003/demo.xml");
  snprintf(g_ledger->ib->source_path, sizeof(g_ledger->ib->source_path), TMP_DIR "/infobase.xml");
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
  db_get_field_text(g_ledger->ib, TABLE_ENTRIES, e, "item.sku", item, sizeof(item));
  db_get_field_text(g_ledger->ib, TABLE_ENTRIES, e, "counterparty.tax_id", party, sizeof(party));
  bool ok = !strcmp(dr, "41.01") && !strcmp(cr, "60.01") && near(e->amount, 9500) && near(e->quantity, 100) &&
            !strcmp(item, "00001") && !strcmp(party, "7705123456");
  free_result_list(entries);
  ASSERT(ok, "net amount entry carries item, quantity and supplier");
  PASS();
}

static void test_unpost_and_trial_balance(void) {
  TEST("trial balance: subaccounts roll up; opening balances; unposting removes entries");
  const db_balance_t *bank = balance("51"), *goods = balance("41"), *vat = balance("68"), *aux = balance("000");
  ASSERT(bank && near(bank->balance_dr, 100000 + 60000 - 51120), "bank balance");
  ASSERT(aux && near(aux->balance_dr, 0) && near(aux->balance_cr, 0), "opening entries balance through 000");
  ASSERT(goods && near(goods->debit_turnover, 42600), "41 includes 41.01");
  ASSERT(vat && near(vat->debit_turnover, 8520) && near(vat->credit_turnover, 2776), "VAT 68.02 rolls into 68");
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
  ASSERT_EQUAL(count_result_list(card), 0);  // no bank movement in the period
  free_result_list(card);
  set_period("", "", "60");
  card = (result_node_t *)send_db_message(g_ledger->ib, dbFetch, MAKEDWORD(TABLE_CARD, 0), NULL);
  double last = -1; int rows = count_result_list(card);
  for (result_node_t *n = card; n; n = n->next) last = (*(db_card_t **)n->data)->balance;
  free_result_list(card);
  ASSERT(rows == 7 && near(last, 0), "supplier card: six receipt entries and the payment settle to zero");
  result_node_t *rows_ = (result_node_t *)send_db_message(g_ledger->ib, dbFetch, MAKEDWORD(TABLE_ANALYSIS_ROWS, 0), NULL);
  db_analysis_row_t *alfa = rows_ ? *(db_analysis_row_t **)rows_->data : NULL;
  bool ok = count_result_list(rows_) == 1 && alfa && near(alfa->credit_turnover, 51120) && near(alfa->debit_turnover, 51120);
  free_result_list(rows_);
  ASSERT(ok, "analysis of 60 lists the one supplier");
  int sales = ledger_count_references(g_ledger->ib, TABLE_VAT_BOOK, "book", 1);
  int purchases = ledger_count_references(g_ledger->ib, TABLE_VAT_BOOK, "book", 2);
  ASSERT(sales == 1 && purchases == 1, "posted sale and receipt are in the books");
  PASS();
}

static void test_manual_operation(void) {
  TEST("manual operation: typed entries count while posted and stay when unposted");
  db_document_t *op = find_by(g_ledger->ib, TABLE_DOCUMENTS, "number", "ВО-0001");
  ASSERT(op && op->posted && near(op->total, 200000), "opening balances total their entries");
  ledger_unpost(g_ledger->ib, g_ledger->cfg, op->id);
  ASSERT_EQUAL(ledger_count_references(g_ledger->ib, TABLE_ENTRIES, "document_id", op->id), 3);
  ledger_build_reports(g_ledger->ib, g_ledger->cfg);
  ASSERT(near(balance("51")->balance_dr, 60000 - 51120), "unposted operation leaves the reports");
  ledger_post(g_ledger->ib, g_ledger->cfg, op->id);
  ledger_build_reports(g_ledger->ib, g_ledger->cfg);
  ASSERT(near(balance("51")->balance_dr, 100000 + 60000 - 51120), "posted again");
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
  ledger_delete_document(g_ledger->ib, id);
  ASSERT_EQUAL(ledger_count_references(g_ledger->ib, TABLE_DOC_LINES, "document_id", id), 0);
  ASSERT_EQUAL(ledger_count_references(g_ledger->ib, TABLE_ENTRIES, "document_id", id), 0);
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
  bool ok = bank && near(bank->balance_dr, 25000 - 2003.32) && ust && near(ust->balance_cr, 96.48) &&
            receivables && near(receivables->balance_dr, 0);
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

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  TEST_START("Ledger");
  setup();
  test_configuration_loads();
  test_joins_cross_databases();
  test_posting_rules();
  test_unpost_and_trial_balance();
  test_period_reports();
  test_manual_operation();
  test_transient_tables();
  test_numbering_and_delete();
  test_save_and_reload();
  test_print_forms();
  test_generated_bindings();
  test_russian_catalogs();
  test_german_configuration();
  destroy_database(g_ledger->ib);
  destroy_database(g_ledger->cfg);
  TEST_END();
}

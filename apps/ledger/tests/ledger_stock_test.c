// Native stock operations share the same transaction and sale posting that
// the document UI and trusted Ledger automation use.
#include "test_framework.h"
#include "apps/ledger/ledger.h"
#include "apps/ledger/tests/test_databases.h"

#define STOCK_CFG "apps/ledger/share/configurations/ru-2026"
static ledger_t stock_ledger;

static void stock_setup(void) {
  DB_CLASS(db_schema_xml);
  DB_CLASS(ledger_ib);
  g_ledger = &stock_ledger;
  stock_ledger.cfg = ledger_test_writable_seed("cfg", "db_schema_xml", &ledger_cfg_schema, STOCK_CFG "/configuration.xml");
  stock_ledger.ib = ledger_test_infobase(STOCK_CFG "/demo.xml");
  register_database("cfg", stock_ledger.cfg);
  register_database("ib", stock_ledger.ib);
  ledger_script_open(STOCK_CFG, "build");
}

static int stock_trade(database_t *ib, int type, int warehouse, int item, double quantity, const char *date) {
  db_document_t doc = { .doc_type_id = type, .warehouse_id = warehouse, .counterparty_id = 1 };
  snprintf(doc.date, sizeof(doc.date), "%s", date);
  const db_document_t *inserted = (const void *)send_db_message(ib, dbInsert, TABLE_DOCUMENTS, &doc);
  if (!inserted) return 0;
  int id = inserted->id;
  db_doc_line_t line = { .document_id = id, .item_id = item, .quantity = quantity, .price = 10.0 };
  return send_db_message(ib, dbInsert, TABLE_DOC_LINES, &line) ? id : 0;
}

static bool stock_equals(database_t *ib, database_t *cfg, int item, int warehouse, int64_t on_hand, int64_t reserved) {
  ledger_stock_t value;
  return ledger_stock_balance(ib, cfg, item, warehouse, &value) && value.on_hand == on_hand &&
    value.reserved == reserved && value.available == on_hand - reserved;
}

static int stock_rows(database_t *ib, int table) {
  result_node_t *of = (void *)send_db_message(ib, dbFetch, table, NULL);
  int count = count_result_list(of);
  free_result_list(of);
  return count;
}

static void test_stock_lifecycle(void) {
  TEST("named warehouse: exact balances, competing orders, transfer/reversal, fulfillment, reopen");
  database_t *ib = stock_ledger.ib, *cfg = stock_ledger.cfg;
  db_item_t item = { .name = "Test inventory item" };
  db_warehouse_t first = { .name = "First" }, second = { .name = "Second" };
  const db_item_t *added_item = (void *)send_db_message(ib, dbInsert, TABLE_ITEMS, &item);
  const db_warehouse_t *added_first = (void *)send_db_message(ib, dbInsert, TABLE_WAREHOUSES, &first);
  const db_warehouse_t *added_second = (void *)send_db_message(ib, dbInsert, TABLE_WAREHOUSES, &second);
  ASSERT(added_item && added_first && added_second, "fixture catalog rows added");
  int sku = added_item->id, a = added_first->id, b = added_second->id;
  int receipt = stock_trade(ib, 1, a, sku, 10, "2027-01-03");
  ASSERT(receipt > 0 && ledger_post(ib, cfg, receipt), "named receipt posts");
  ASSERT_TRUE(stock_equals(ib, cfg, sku, a, 10000, 0));
  ASSERT_TRUE(stock_equals(ib, cfg, sku, b, 0, 0));
  ASSERT_TRUE(stock_equals(ib, cfg, sku, 0, 0, 0));

  int hold = ledger_stock_reserve(ib, cfg, "marketplace/order-1/item-1", sku, a, 6000);
  ASSERT(hold > 0, "marketplace order holds six");
  ASSERT_EQUAL(hold, ledger_stock_reserve(ib, cfg, "marketplace/order-1/item-1", sku, a, 6000));
  ASSERT_FALSE(ledger_stock_reserve(ib, cfg, "marketplace/order-1/item-1", sku, a, 2000));
  ASSERT_FALSE(ledger_stock_reserve(ib, cfg, "workshop/work-2/item-1", sku, a, 5000));
  ASSERT_FALSE(ledger_stock_transfer(ib, cfg, "transfer-overavailable", sku, a, b, 5000));
  ASSERT_TRUE(stock_equals(ib, cfg, sku, a, 10000, 6000));
  ASSERT_FALSE(ledger_reverse_posting(ib, cfg, receipt, "2027-01-04", "Held stock cannot be reversed"));

  int financial_entries = stock_rows(ib, TABLE_ENTRIES);
  int transfer = ledger_stock_transfer(ib, cfg, "warehouse/transfer-1", sku, a, b, 3000);
  ASSERT(transfer > 0, "moving unreserved goods succeeds");
  ASSERT_EQUAL(transfer, ledger_stock_transfer(ib, cfg, "warehouse/transfer-1", sku, a, b, 3000));
  ASSERT_TRUE(stock_equals(ib, cfg, sku, a, 7000, 6000));
  ASSERT_TRUE(stock_equals(ib, cfg, sku, b, 3000, 0));
  ASSERT_EQUAL(stock_rows(ib, TABLE_ENTRIES), financial_entries);
  int inverse = ledger_stock_reverse_transfer(ib, cfg, "warehouse/transfer-1/reverse", transfer);
  ASSERT(inverse > 0, "transfer reversal restores warehouse counts");
  ASSERT_FALSE(ledger_stock_reverse_transfer(ib, cfg, "warehouse/transfer-1/other", transfer));
  ASSERT_TRUE(stock_equals(ib, cfg, sku, a, 10000, 6000));
  ASSERT_TRUE(stock_equals(ib, cfg, sku, b, 0, 0));

  int release = ledger_stock_release(ib, cfg, "marketplace/order-1/cancel", hold);
  ASSERT(release > 0, "cancellation releases held goods");
  ASSERT_EQUAL(ledger_stock_release(ib, cfg, "marketplace/order-1/cancel", hold), release);
  ASSERT_FALSE(ledger_stock_release(ib, cfg, "marketplace/order-1/cancel-again", hold));
  int work = ledger_stock_reserve(ib, cfg, "workshop/work-2/item-1", sku, a, 5000);
  ASSERT(work > 0, "other flow can reserve just-released stock");

  int sale = stock_trade(ib, 2, a, sku, 5, "2027-01-06");
  ASSERT(sale > 0, "sale draft exists");
  int wrong = stock_trade(ib, 2, b, sku, 5, "2027-01-05");
  ASSERT(wrong > 0, "wrong warehouse draft exists");
  ASSERT_FALSE(ledger_post(ib, cfg, wrong));
  ASSERT_FALSE(ledger_stock_fulfill(ib, cfg, "workshop/work-2/finish", work, wrong));
  ASSERT_TRUE(stock_equals(ib, cfg, sku, a, 10000, 5000));
  ASSERT_TRUE(stock_equals(ib, cfg, sku, b, 0, 0));
  int fulfilled = ledger_stock_fulfill(ib, cfg, "workshop/work-2/finish", work, sale);
  ASSERT(fulfilled > 0, "fulfillment posts matched sale and consumes its hold");
  ASSERT_EQUAL(ledger_stock_fulfill(ib, cfg, "workshop/work-2/finish", work, sale), fulfilled);
  ASSERT_FALSE(ledger_stock_release(ib, cfg, "workshop/work-2/release", work));
  ASSERT_TRUE(stock_equals(ib, cfg, sku, a, 5000, 0));
  ASSERT(stock_rows(ib, TABLE_ENTRIES) > financial_entries, "sale uses existing accounting postings");

  int undo_sale = ledger_reverse_posting(ib, cfg, sale, "2027-01-07", "Returned to warehouse");
  ASSERT(undo_sale > 0, "accounting reversal restores sold goods at the original warehouse");
  ASSERT_TRUE(stock_equals(ib, cfg, sku, a, 10000, 0));
  ASSERT_FALSE(send_db_message(ib, dbDelete, TABLE_STOCK_OPERATIONS, (void *)(intptr_t)hold));
  ASSERT_TRUE(send_db_message(ib, dbSave, 0, NULL));
  ASSERT_TRUE(send_db_message(ib, dbLoad, 0, NULL));
  ASSERT_TRUE(stock_equals(ib, cfg, sku, a, 10000, 0));
  ASSERT_TRUE(stock_equals(ib, cfg, sku, b, 0, 0));
  ASSERT_EQUAL(ledger_stock_fulfill(ib, cfg, "workshop/work-2/finish", work, sale), fulfilled);
  PASS();
}

static void test_received_transfer_can_sell(void) {
  TEST("transferred goods can be sold from the receiving warehouse with pooled cost");
  database_t *ib = stock_ledger.ib, *cfg = stock_ledger.cfg;
  db_item_t item = { .name = "Transfer sale fixture" };
  db_warehouse_t first = { .name = "Receiving" }, second = { .name = "Sales" };
  const db_item_t *i = (void *)send_db_message(ib, dbInsert, TABLE_ITEMS, &item);
  const db_warehouse_t *w1 = (void *)send_db_message(ib, dbInsert, TABLE_WAREHOUSES, &first);
  const db_warehouse_t *w2 = (void *)send_db_message(ib, dbInsert, TABLE_WAREHOUSES, &second);
  ASSERT(i && w1 && w2, "transfer fixture rows created");
  int sku = i->id, a = w1->id, b = w2->id;
  int receipt = stock_trade(ib, 1, a, sku, 8, "2027-02-01");
  ASSERT(receipt > 0 && ledger_post(ib, cfg, receipt), "purchase posted at the source warehouse");
  ASSERT(ledger_stock_transfer(ib, cfg, "transfer-sale/1", sku, a, b, 3000) > 0,
         "three units allocated to the sales warehouse");
  int sale = stock_trade(ib, 2, b, sku, 2.5, "2027-02-02");
  ASSERT(sale > 0 && ledger_post(ib, cfg, sale), "sale uses two and a half transferred units");
  ASSERT_TRUE(stock_equals(ib, cfg, sku, a, 5000, 0));
  ASSERT_TRUE(stock_equals(ib, cfg, sku, b, 500, 0));
  bool cost_entry = false;
  result_node_t *entries = (void *)send_db_message(ib, dbFetch,
    MAKEDWORD(TABLE_ENTRIES, ledger_field(ib, TABLE_ENTRIES, "document_id")), (void *)(intptr_t)sale);
  const db_doc_type_t *type = (void *)send_db_message(cfg, dbFind, TABLE_DOC_TYPES, (void *)(intptr_t)2);
  for (result_node_t *n = entries; n; n = n->next) {
    const db_entry_t *e = *(db_entry_t **)n->data;
    if (e->credit_id == type->cost_credit_id && e->amount_minor == 2500 && e->quantity_milli == 2500) cost_entry = true;
  }
  free_result_list(entries);
  ASSERT_TRUE(cost_entry);
  PASS();
}

static void test_imported_warehouse_quantities(void) {
  TEST("imported quantity uses explicit debit and credit measures and excludes inactive rows");
  database_t *ib = stock_ledger.ib, *cfg = stock_ledger.cfg;
  db_item_t item = { .name = "Imported quantity fixture" };
  db_warehouse_t wh = { .name = "Imported location" };
  const db_item_t *i = (void *)send_db_message(ib, dbInsert, TABLE_ITEMS, &item);
  const db_warehouse_t *w = (void *)send_db_message(ib, dbInsert, TABLE_WAREHOUSES, &wh);
  ASSERT(i && w, "side-quantity fixture rows created");
  const db_doc_type_t *type = (void *)send_db_message(cfg, dbFind, TABLE_DOC_TYPES, (void *)(intptr_t)2);
  ASSERT_NOT_NULL(type);
  db_document_t doc = { .doc_type_id = 9, .warehouse_id = w->id, .date = "2027-03-01",
    .posted = true, .import_source = "1cd:stock-quantity-test" };
  const db_document_t *d = (void *)db_schema_xml(ib, dbInsert, TABLE_DOCUMENTS, &doc);
  ASSERT_NOT_NULL(d);
  db_entry_t entry = { .document_id = d->id, .debit_id = type->cost_credit_id, .credit_id = type->cost_credit_id,
    .debit_subconto_id = i->id, .credit_subconto_id = i->id,
    .quantity = 99, .debit_quantity_set = true, .credit_quantity_set = true,
    .debit_quantity_milli = 1300, .credit_quantity_milli = 800,
    .source_period = "2027-03-01T00:00:00", .date = "2027-03-01", .amount = 2 };
  ASSERT_TRUE(db_schema_xml(ib, dbInsert, TABLE_ENTRIES, &entry));
  entry.source_inactive = true;
  entry.debit_quantity_milli = 100000;
  ASSERT_TRUE(db_schema_xml(ib, dbInsert, TABLE_ENTRIES, &entry));
  ASSERT_TRUE(stock_equals(ib, cfg, i->id, w->id, 500, 0));
  PASS();
}

static void test_manual_side_rejects_use_of_held_stock(void) {
  TEST("manual operation with no warehouse header cannot spend held stock by side analytics");
  database_t *ib = stock_ledger.ib, *cfg = stock_ledger.cfg;
  db_item_t item = { .name = "Manual named-stock fixture" };
  db_warehouse_t wh = { .name = "Manual movement location" };
  const db_item_t *i = (void *)send_db_message(ib, dbInsert, TABLE_ITEMS, &item);
  const db_warehouse_t *w = (void *)send_db_message(ib, dbInsert, TABLE_WAREHOUSES, &wh);
  ASSERT(i && w, "manual movement catalog rows exist");
  int sku = i->id, warehouse = w->id;
  int receipt = stock_trade(ib, 1, warehouse, sku, 5, "2027-04-01");
  ASSERT(receipt > 0 && ledger_post(ib, cfg, receipt), "five units received");
  ASSERT(ledger_stock_reserve(ib, cfg, "manual-guard/hold", sku, warehouse, 5000) > 0, "all five held");
  const db_doc_type_t *sale = (void *)send_db_message(cfg, dbFind, TABLE_DOC_TYPES, (void *)(intptr_t)2);
  ASSERT_NOT_NULL(sale);
  db_document_t draft = { .doc_type_id = 9, .date = "2027-04-02" };
  const db_document_t *d = (void *)send_db_message(ib, dbInsert, TABLE_DOCUMENTS, &draft);
  ASSERT_NOT_NULL(d);
  int id = d->id;
  db_entry_t entry = { .document_id = id, .debit_id = sale->cost_debit_id, .credit_id = sale->cost_credit_id,
    .credit_subconto_id = sku, .credit_subconto2_id = warehouse, .quantity = 1, .amount = 10, .date = "2027-04-02" };
  ASSERT_TRUE(send_db_message(ib, dbInsert, TABLE_ENTRIES, &entry));
  ASSERT_FALSE(ledger_post(ib, cfg, id));
  ASSERT_TRUE(stock_equals(ib, cfg, sku, warehouse, 5000, 5000));
  PASS();
}

static void test_sell_imported_stock(void) {
  TEST("1CD goods stock supports first exact native sale, with asymmetric source quantities");
  database_t *ib = stock_ledger.ib, *cfg = stock_ledger.cfg;
  ledger_stage_import_t report = {0};
  ASSERT(ledger_import_stage(ib, cfg, "apps/ledger/tests/fixtures/onecd/stage_stock_cutover.xml", &report), report.error);
  const db_item_t *item = (void *)send_db_message(ib, dbFind,
    MAKEDWORD(TABLE_ITEMS, ledger_field(ib, TABLE_ITEMS, "sku")), "CUTOVER");
  const db_warehouse_t *warehouse = (void *)send_db_message(ib, dbFind,
    MAKEDWORD(TABLE_WAREHOUSES, ledger_field(ib, TABLE_WAREHOUSES, "name")), "Migration cutover warehouse");
  ASSERT(item && warehouse, "1CD catalogs applied");
  int sku = item->id, wh = warehouse->id;
  ASSERT_TRUE(stock_equals(ib, cfg, sku, wh, 8000, 0));
  int hold = ledger_stock_reserve(ib, cfg, "cutover/hold", sku, wh, 2500);
  ASSERT(hold > 0, "reserve imported units");
  int sale = stock_trade(ib, 2, wh, sku, 2.5, "2027-05-03");
  ASSERT(sale > 0, "native draft exists");
  int done = ledger_stock_fulfill(ib, cfg, "cutover/ship", hold, sale);
  ASSERT(done > 0, "native exact fulfillment posts on imported valuation");
  ASSERT_TRUE(stock_equals(ib, cfg, sku, wh, 5500, 0));
  const db_doc_type_t *type = (void *)send_db_message(cfg, dbFind, TABLE_DOC_TYPES, (void *)(intptr_t)2);
  result_node_t *entries = (void *)send_db_message(ib, dbFetch,
    MAKEDWORD(TABLE_ENTRIES, ledger_field(ib, TABLE_ENTRIES, "document_id")), (void *)(intptr_t)sale);
  int cogs = 0;
  for (const result_node_t *n = entries; n; n = n->next) {
    const db_entry_t *e = *(db_entry_t **)n->data;
    if (e->credit_id == type->cost_credit_id && e->exact_money &&
        e->amount_minor == 3125 && e->quantity_milli == 2500) cogs++;
  }
  free_result_list(entries);
  ASSERT_EQUAL(cogs, 1);  // 100.00 / 8 × 2.5, not the debit-preferred quantity.
  ASSERT_TRUE(send_db_message(ib, dbSave, 0, NULL));
  ASSERT_TRUE(send_db_message(ib, dbLoad, 0, NULL));
  ASSERT_EQUAL(ledger_stock_fulfill(ib, cfg, "cutover/ship", hold, sale), done);
  ASSERT_TRUE(stock_equals(ib, cfg, sku, wh, 5500, 0));
  ASSERT(ledger_reverse_posting(ib, cfg, sale, "2027-05-04", "Cutover return") > 0, "reversal uses the same exact history");
  ASSERT_TRUE(stock_equals(ib, cfg, sku, wh, 8000, 0));
  const db_item_t *uncosted = (void *)send_db_message(ib, dbFind,
    MAKEDWORD(TABLE_ITEMS, ledger_field(ib, TABLE_ITEMS, "sku")), "UNVERIFIABLE");
  ASSERT_NOT_NULL(uncosted);
  int uncosted_id = uncosted->id;
  int uncosted_hold = ledger_stock_reserve(ib, cfg, "cutover/uncosted/hold", uncosted_id, wh, 500);
  ASSERT(uncosted_hold > 0, "quantity is known even though cost does not fit cents");
  int uncosted_sale = stock_trade(ib, 2, wh, uncosted_id, 0.5, "2027-05-05");
  ASSERT(uncosted_sale > 0, "uncosted draft exists");
  ASSERT_FALSE(ledger_stock_fulfill(ib, cfg, "cutover/uncosted/ship", uncosted_hold, uncosted_sale));
  ASSERT_TRUE(stock_equals(ib, cfg, uncosted_id, wh, 1000, 500));
  PASS();
}

int main(void) {
  TEST_START("Ledger warehouse stock");
  stock_setup();
  test_stock_lifecycle();
  test_received_transfer_can_sell();
  test_imported_warehouse_quantities();
  test_manual_side_rejects_use_of_held_stock();
  test_sell_imported_stock();
  ledger_script_close();
  TEST_END();
}

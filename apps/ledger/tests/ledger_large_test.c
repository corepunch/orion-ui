// Large, committed synthetic samples: native infobases and EnterpriseData.
#include "test_framework.h"
#include "apps/ledger/ledger.h"
#include <math.h>

static ledger_t large_state;
static int large_count(database_t *db, int table) {
  result_node_t *rows = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(table, 0), NULL);
  int count = count_result_list(rows);
  free_result_list(rows);
  return count;
}

static bool large_near(double a, double b) { return fabs(a - b) < 0.005; }

static bool large_balanced(database_t *ib, database_t *cfg, double expected_turnover) {
  ledger_build_reports(ib, cfg);
  double dr = 0, cr = 0, closing_dr = 0, closing_cr = 0;
  result_node_t *rows = (result_node_t *)send_db_message(ib, dbFetch, MAKEDWORD(TABLE_BALANCES, 0), NULL);
  for (result_node_t *n = rows; n; n = n->next) {
    const db_balance_t *b = *(db_balance_t **)n->data;
    if (b->subaccount) continue;
    dr += b->debit_turnover; cr += b->credit_turnover;
    closing_dr += b->balance_dr; closing_cr += b->balance_cr;
  }
  free_result_list(rows);
  return large_near(dr, expected_turnover) && large_near(cr, expected_turnover) && large_near(closing_dr, closing_cr);
}

static void test_large_country(const char *country) {
  TEST(country);
  char config[256], native[256], exchange[256], saved[256];
  snprintf(config, sizeof(config), "apps/ledger/share/configurations/%s-2003/configuration.xml", country);
  snprintf(native, sizeof(native), "apps/ledger/share/samples/%s-2003-large.xml", country);
  snprintf(exchange, sizeof(exchange), "apps/ledger/share/samples/%s-2003-enterprisedata-large.xml", country);
  snprintf(saved, sizeof(saved), "build/test-ledger-large-%s.xml", country);
  database_t *cfg = create_database_with_schema("cfg", "db_schema_xml", &ledger_cfg_schema, config);
  ASSERT_NOT_NULL(cfg);
  register_database("cfg", cfg);
  database_t *ib = create_database_with_schema("ib", "ledger_ib", &ledger_ib_schema, native);
  ASSERT_NOT_NULL(ib);
  register_database("ib", ib);
  large_state.cfg = cfg; large_state.ib = ib;
  snprintf(large_state.config_dir, sizeof(large_state.config_dir), "apps/ledger/share/configurations/%s-2003", country);
  ASSERT_TRUE(ledger_script_open(large_state.config_dir, NULL));
  ASSERT_EQUAL(large_count(ib, TABLE_COUNTERPARTIES), 500);
  ASSERT_EQUAL(large_count(ib, TABLE_ITEMS), 2000);
  ASSERT_EQUAL(large_count(ib, TABLE_DOCUMENTS), 1000);
  ASSERT_EQUAL(large_count(ib, TABLE_ENTRIES), 10000);
  ledger_repost_all(ib, cfg);
  ASSERT_EQUAL(large_count(ib, TABLE_ENTRIES), 10000);
  ASSERT(large_balanced(ib, cfg, 1000000), "native sample keeps balanced postings after recalculation");
  snprintf(ib->source_path, sizeof(ib->source_path), "%s", saved);
  ib->dirty = true;
  ASSERT_TRUE(send_db_message(ib, dbSave, 0, NULL));
  ASSERT_TRUE(send_db_message(ib, dbLoad, 0, NULL));
  ASSERT_EQUAL(large_count(ib, TABLE_DOCUMENTS), 1000);
  ASSERT_EQUAL(large_count(ib, TABLE_ENTRIES), 10000);
  ASSERT(large_balanced(ib, cfg, 1000000), "native sample survives save/reload");
  destroy_database(ib);
  ib = create_database_with_schema("ib", "ledger_ib", &ledger_ib_schema, "");
  ASSERT_NOT_NULL(ib);
  register_database("ib", ib); large_state.ib = ib;
  ledger_import_t first, repeat;
  ASSERT_TRUE(ledger_import_enterprisedata(ib, cfg, exchange, &first));
  ASSERT(first.items == 2000 && first.counterparties == 500 && first.entries == 10000 && first.currencies == 1,
         "large exchange imports all catalogs, USD and opening rows");
  ASSERT(large_near(first.debit, 1184325) && large_near(first.credit, 343737.5), "large exchange national amounts match generated totals");
  ASSERT(large_balanced(ib, cfg, first.debit + first.credit), "large imported trial balance agrees");
  result_node_t *entries = (result_node_t *)send_db_message(ib, dbFetch, MAKEDWORD(TABLE_ENTRIES, 0), NULL);
  int currency_rows = 0, goods_rows = 0;
  for (result_node_t *n = entries; n; n = n->next) {
    const db_entry_t *e = *(db_entry_t **)n->data;
    if (e->currency_id) {
      currency_rows++;
      ASSERT(large_near(e->amount, e->currency_amount * 2), "USD rows retain national and foreign amounts");
    }
    if (e->quantity) {
      goods_rows++;
      ASSERT(e->debit_subconto_id > 0 && e->debit_subconto_id <= 2000 && large_near(e->quantity, 10), "goods analytics and quantities survive import");
    }
  }
  free_result_list(entries);
  ASSERT_EQUAL(currency_rows, 250);
  ASSERT_EQUAL(goods_rows, 2500);
  snprintf(ib->source_path, sizeof(ib->source_path), "%s", saved);
  ASSERT_TRUE(send_db_message(ib, dbSave, 0, NULL));
  ASSERT_TRUE(send_db_message(ib, dbLoad, 0, NULL));
  ASSERT_TRUE(ledger_import_enterprisedata(ib, cfg, exchange, &repeat));
  ASSERT(!repeat.entries && !repeat.items && !repeat.counterparties && repeat.balances_present, "large exchange remains idempotent after reload");
  ASSERT_EQUAL(large_count(ib, TABLE_ENTRIES), 10000);
  ASSERT(large_balanced(ib, cfg, first.debit + first.credit), "repeat import preserves report totals");
  ledger_script_close();
  destroy_database(ib); destroy_database(cfg);
  large_state.ib = large_state.cfg = NULL;
  remove(saved);
  PASS();
}

int main(void) {
  TEST_START("Ledger large samples");
  DB_CLASS(db_schema_xml); DB_CLASS(ledger_ib);
  g_ledger = &large_state;
  test_large_country("ru");
  test_large_country("de");
  TEST_END();
}

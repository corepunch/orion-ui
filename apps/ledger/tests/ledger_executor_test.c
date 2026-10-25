#include "test_framework.h"
#include "apps/ledger/ledger.h"
#include <platform/platform.h>
#include <math.h>

static ledger_t executor_test;
static int exec_fault, exec_inserts;
static dbproc_t executor_original_proc;
static lresult_t executor_fault_proc(database_t *db, uint32_t msg, uint32_t wp, void *lp) {
  if (exec_fault == 1 && msg == dbInsert && LOWORD(wp) == TABLE_PAYROLL_LINES && ++exec_inserts == 2) return 0;
  if (exec_fault == 2 && msg == dbCommitChanges) return 0;
  return executor_original_proc(db, msg, wp, lp);
}
static int payroll_count(int id) { return ledger_count_references(g_ledger->ib, TABLE_PAYROLL_LINES, "document_id", id); }
static db_document_t *executor_document(int type, const char *number) {
  db_document_t doc = {.doc_type_id=type};
  snprintf(doc.date,sizeof(doc.date),"2003-04-30");
  if (number) snprintf(doc.number,sizeof(doc.number),"%s",number);
  return (db_document_t *)send_db_message(g_ledger->ib,dbInsert,TABLE_DOCUMENTS,&doc);
}
static void setup_executor(void) {
  DB_CLASS(db_schema_xml);
  DB_CLASS(ledger_ib);
  g_ledger = &executor_test;
  snprintf(g_ledger->config_dir,sizeof(g_ledger->config_dir),"apps/ledger/share/configurations/ru-2003");
  g_ledger->cfg = create_database_with_schema("cfg","db_schema_xml",&ledger_cfg_schema,"apps/ledger/share/configurations/ru-2003/configuration.xml");
  g_ledger->ib = create_database_with_schema("ib","ledger_ib",&ledger_ib_schema,"apps/ledger/share/configurations/ru-2003/demo.xml");
  register_database("cfg",g_ledger->cfg); register_database("ib",g_ledger->ib);
  ledger_script_open(g_ledger->config_dir,NULL);
  ledger_repost_all(g_ledger->ib,g_ledger->cfg);
  send_db_message(g_ledger->ib,dbBeginChanges,0,NULL);
}
static void test_executor_workflows(void) {
  TEST("standard Lua policies preserve shared-prefix numbering and payroll accrual/payout behavior");
  db_document_t *cash = executor_document(3,NULL);
  ASSERT_NOT_NULL(cash);
  ASSERT_STR_EQUAL(cash->number,"ПКО-0003");
  db_document_t *other = executor_document(17,NULL);
  ASSERT_NOT_NULL(other);
  ASSERT_STR_EQUAL(other->number,"ПКО-0004");
  ASSERT_TRUE(ledger_number_document(g_ledger->ib,g_ledger->cfg,cash->id));
  ASSERT_STR_EQUAL(cash->number,"ПКО-0003");
  db_document_t *accrual = executor_document(10,"EXEC-ACCRUAL");
  ASSERT_NOT_NULL(accrual);
  ASSERT_TRUE(ledger_fill_payroll(g_ledger->ib,g_ledger->cfg,accrual->id));
  ASSERT_EQUAL(payroll_count(accrual->id),3);
  ASSERT(fabs(accrual->total-36000)<0.001,"filled salaries");
  char error[1024];
  bool ok = ledger_script_run("apps/ledger/tests/executor_readonly.lua",false,error,sizeof(error));
  if (!ok) fprintf(stderr,"%s\n",error);
  ASSERT_TRUE(ok);
  ok = ledger_script_run("apps/ledger/tests/executor_failure.lua",true,error,sizeof(error));
  ASSERT_FALSE(ok);
  ASSERT(strstr(error,"abort after an executor commit"),"outer script error propagated");
  ASSERT_EQUAL(payroll_count(accrual->id),3);
  ASSERT(fabs(accrual->total-36000)<0.001,"outer rollback restores the executor changes");
  ok = ledger_script_run("apps/ledger/tests/executor_public.lua",true,error,sizeof(error));
  if (!ok) fprintf(stderr,"%s\n",error);
  ASSERT_TRUE(ok);
  ASSERT_TRUE(ledger_post(g_ledger->ib,g_ledger->cfg,accrual->id));
  db_document_t *payout = executor_document(11,"EXEC-PAYOUT");
  ASSERT_NOT_NULL(payout);
  ASSERT_TRUE(ledger_fill_payroll(g_ledger->ib,g_ledger->cfg,payout->id));
  ASSERT_EQUAL(payroll_count(payout->id),3);
  ASSERT(fabs(payout->total-31476)<0.001,"fill the posted employee liabilities, net of withholding");
  ASSERT_TRUE(ledger_fill_payroll(g_ledger->ib,g_ledger->cfg,payout->id));
  ASSERT_EQUAL(payroll_count(payout->id),3);
  ASSERT(fabs(payout->total-31476)<0.001,"repeated fill replaces lines without double-counting");
  PASS();
}
static void test_executor_failure_rollback(void) {
  TEST("executor contracts reject invalid plans and roll back failed replacements/commits");
  db_document_t *doc = executor_document(10,"EXEC-FAILURE");
  ASSERT_NOT_NULL(doc);
  ASSERT_TRUE(ledger_fill_payroll(g_ledger->ib,g_ledger->cfg,doc->id));
  ASSERT_EQUAL(payroll_count(doc->id),3);
  ASSERT_TRUE(ledger_script_open("apps/ledger/tests/fixtures/executor",NULL));
  const char *modes[] = {"bad-reference","bad-number","missing-field","bad-field","bad-array","write-in-policy"};
  for (size_t i=0;i<ARRAY_LEN(modes);i++) {
    snprintf(doc->comment,sizeof(doc->comment),"%s",modes[i]);
    ASSERT_FALSE(ledger_fill_payroll(g_ledger->ib,g_ledger->cfg,doc->id));
    ASSERT_EQUAL(payroll_count(doc->id),3);
    ASSERT(fabs(doc->total-36000)<0.001,"invalid plan leaves totals unchanged");
  }
  ASSERT_FALSE(ledger_number_document(g_ledger->ib,g_ledger->cfg,doc->id));
  ASSERT_STR_EQUAL(doc->number,"EXEC-FAILURE");
  result_node_t *before = (result_node_t *)send_db_message(g_ledger->ib,dbFetch,MAKEDWORD(TABLE_DOCUMENTS,0),NULL);
  int count = count_result_list(before);
  free_result_list(before);
  ASSERT_NULL(executor_document(10,NULL));
  result_node_t *after = (result_node_t *)send_db_message(g_ledger->ib,dbFetch,MAKEDWORD(TABLE_DOCUMENTS,0),NULL);
  int after_count = count_result_list(after);
  free_result_list(after);
  ASSERT_EQUAL(after_count,count);  // failed numbering must not leave an inserted document

  snprintf(doc->comment,sizeof(doc->comment),"valid");
  executor_original_proc=g_ledger->ib->proc;
  for (int fault=1;fault<=2;fault++) {
    exec_fault=fault; exec_inserts=0; g_ledger->ib->proc=executor_fault_proc;
    bool ok=ledger_fill_payroll(g_ledger->ib,g_ledger->cfg,doc->id);
    g_ledger->ib->proc=executor_original_proc;
    ASSERT_FALSE(ok);
    ASSERT_EQUAL(payroll_count(doc->id),3);
    ASSERT(fabs(doc->total-36000)<0.001,"failed apply restores lines and totals");
  }
  ASSERT_TRUE(ledger_fill_payroll(g_ledger->ib,g_ledger->cfg,doc->id));
  ASSERT_EQUAL(payroll_count(doc->id),2);
  ASSERT(fabs(doc->total-300)<0.001,"runtime remains usable after rollback");
  PASS();
}
int main(void) {
  TEST_START("Ledger script executor");
  setup_executor();
  test_executor_workflows();
  test_executor_failure_rollback();
  send_db_message(g_ledger->ib,dbRevertChanges,0,NULL);
  g_ledger->ib->dirty=false; g_ledger->cfg->dirty=false;
  ledger_script_close(); destroy_database(g_ledger->ib); destroy_database(g_ledger->cfg);
  TEST_END();
}

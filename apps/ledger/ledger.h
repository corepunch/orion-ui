#ifndef __LEDGER_H__
#define __LEDGER_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <orion/ui.h>
#include <orion/commctl/commctl.h>
#include <orion/commctl/columnview.h>
#include <orion/commctl/menubar.h>
#include <orion/user/accel.h>
#include <orion/user/page_host.h>

// Schemas, forms, menus and command IDs generated from ledger.orion.
#include "build/generated/apps/ledger/ledger.h"

#define LEDGER_SCREEN_W   1280
#define LEDGER_SCREEN_H   800
#define LEDGER_PATH_MAX   1024
#define LEDGER_DEFAULT_CONFIGURATION "ru-2026"

typedef struct {
  hinstance_t    hinstance;
  database_t    *cfg;      // active configuration (read-only)
  database_t    *ib;       // infobase: the organization's data
  database_t    *catalog;  // installed configurations
  window_t      *main_win;
  window_t      *menubar_win;
  accel_table_t *accel;
  char configuration[32];               // active configuration code
  char config_dir[LEDGER_PATH_MAX];     // share/configurations/<code>
  char data_dir[LEDGER_PATH_MAX];       // settings/ledger/<code>
  char share_dir[LEDGER_PATH_MAX];      // share/ledger: configurations, add-ons
} ledger_t;

extern ledger_t *g_ledger;

// posting.c — accounting rules over the cfg and ib databases.
// A posting in progress: the document, its type and its currency rate. Scripts
// and plugins (ledger_script.c) add entries through the same functions.
typedef struct {
  database_t *ib, *cfg;
  db_document_t *doc;
  const db_doc_type_t *type;
  double rate;  // national units per document currency unit
} ledger_post_t;
// What a posting is about; each side of an entry takes the object its account is analysed by.
typedef struct { int counterparty, item, employee, asset; } ledger_subjects_t;
typedef struct { double dr, cr, dr_qty, cr_qty, dr_cur, cr_cur; } ledger_turnover_t;
typedef struct { const db_payroll_tax_t *tax; double amount; } ledger_tax_amount_t;
void ledger_add_entry(const ledger_post_t *p, int debit, int credit, double amount, const ledger_subjects_t *s,
                      double quantity, int currency_id, double currency_amount, const char *content);
// Turnovers of account (with subaccounts) before the document being posted plus
// its own entries so far; subconto and currency filter when >= 0.
ledger_turnover_t ledger_turnover(const ledger_post_t *p, int account_id, int subconto_id, int currency_id);
double ledger_rate_on(database_t *ib, int currency_id, const char *date);
int  ledger_field(database_t *db, int table_id, const char *field);
// The account or one of its subaccounts (41.01 is within 41).
bool ledger_account_within(database_t *cfg, int account_id, int parent_id);
// Entries of posted documents in date order, as reports count them. The
// caller frees the array; the entries stay the infobase's.
int  ledger_posted_entries(database_t *ib, db_entry_t ***out);
// The infobase table an account is analysed by ("counterparties"), or "".
const char *ledger_subconto_table(database_t *cfg, int account_id);
void ledger_recalc_document(database_t *ib, database_t *cfg, int doc_id);
bool ledger_post(database_t *ib, database_t *cfg, int doc_id);
void ledger_unpost(database_t *ib, database_t *cfg, int doc_id);
bool ledger_number_document(database_t *ib, database_t *cfg, int doc_id);
void ledger_repost_all(database_t *ib, database_t *cfg);
// Fills a payroll document: salaries for an accrual, what is owed for a payout.
bool ledger_fill_payroll(database_t *ib, database_t *cfg, int doc_id);
// Rebuilds the transient report tables for the period in report settings.
void ledger_build_reports(database_t *ib, database_t *cfg);
db_report_setting_t *ledger_report_settings(database_t *ib, database_t *cfg);
int  ledger_count_references(database_t *ib, int table_id, const char *field, int id);

// ledger_script.c — the configuration's rules.lua and plugins/*.lua: document
// types, payroll taxes and closing steps the configuration scripts (like a 1C
// configuration's modules), and event subscriptions. Every call is a no-op
// when no script defines the function.
bool ledger_script_open(const char *config_dir, const char *data_dir);
void ledger_script_close(void);
bool ledger_script_in_executor(void);
bool ledger_execute_document(database_t *ib, database_t *cfg, const char *operation, int document_id);
// Execute trusted local Lua; the entire run rolls back on error.
bool ledger_script_run(const char *path, bool writable, char *error, size_t error_sz);
bool ledger_script_has(const char *function);
bool ledger_script_post(const ledger_post_t *p, const char *function);
bool ledger_script_closing(const ledger_post_t *p, const db_closing_rule_t *rule);
bool ledger_script_tax(const ledger_post_t *p, const db_payroll_tax_t *tax, const db_payroll_line_t *line, double ytd, int accruals,
                       double withheld, const ledger_tax_amount_t *so_far, int so_far_count, double *amount);
void ledger_script_event(const char *event, const ledger_post_t *p);

// ledger_ib.c — the infobase datasource class (db_schema_xml with Ledger's rules).
lresult_t ledger_ib(database_t *db, uint32_t msg, uint32_t wparam, void *lparam);

// print.c — fills the configuration's HTML print-form templates.
int  ledger_print_document(int doc_id, char *report, size_t report_sz);
// Writes the configuration's report forms (cash book) for the report period.
int  ledger_print_reports(char *report, size_t report_sz);

// import_1c.c — 1C:Enterprise 8 exchange files (EnterpriseData XML): catalogs
// and opening balances, in one editing context.
typedef struct {
  int counterparties, items, employees, assets, currencies;  // added
  int matched;            // objects already in the infobase
  int entries;            // opening entries written
  int ignored;            // objects of other kinds in the file
  double debit, credit;   // the opening entries' totals
  bool balances_present;  // the file's balances were imported before
  char skipped[256];      // balance documents the configuration has no rule for
  char error[256];
} ledger_import_t;
bool ledger_import_enterprisedata(database_t *ib, database_t *cfg, const char *path, ledger_import_t *out);

// ledger_addons.c — external processings: an add-on's .orion form driven by
// its Lua script, with the "ledger" module for accounts, turnovers and posting.
void ledger_addons_init(void);
void ledger_open_addon(void);

// controller.c — commands and views.
void     ledger_command(uint16_t id);
void     ledger_refresh_views(void);
void     ledger_update_status(void);
result_t ledger_main_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t ledger_menubar_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

#endif // __LEDGER_H__

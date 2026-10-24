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

#define LEDGER_SCREEN_W   1024
#define LEDGER_SCREEN_H   680
#define LEDGER_PATH_MAX   1024
#define LEDGER_DEFAULT_CONFIGURATION "ru-2003"

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
} ledger_t;

extern ledger_t *g_ledger;

// posting.c — accounting rules over the cfg and ib databases.
int  ledger_field(database_t *db, int table_id, const char *field);
void ledger_recalc_document(database_t *ib, database_t *cfg, int doc_id);
bool ledger_post(database_t *ib, database_t *cfg, int doc_id);
void ledger_unpost(database_t *ib, database_t *cfg, int doc_id);
void ledger_delete_document(database_t *ib, int doc_id);
void ledger_number_document(database_t *ib, database_t *cfg, int doc_id);
void ledger_repost_all(database_t *ib, database_t *cfg);
// Rebuilds the transient report tables for the period in report settings.
void ledger_build_reports(database_t *ib, database_t *cfg);
db_report_setting_t *ledger_report_settings(database_t *ib, database_t *cfg);
int  ledger_count_references(database_t *ib, int table_id, const char *field, int id);

// print.c — fills the configuration's HTML print-form templates.
int  ledger_print_document(int doc_id, char *report, size_t report_sz);

// controller.c — commands and views.
void     ledger_command(uint16_t id);
void     ledger_refresh_views(void);
void     ledger_update_status(void);
result_t ledger_main_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t ledger_menubar_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

#endif // __LEDGER_H__

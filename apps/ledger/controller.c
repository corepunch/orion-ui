// Controller: command routing, document editing and view refresh.
// Screens, menus and bindings are declared in ledger.orion; this file holds
// only behavior the declarations cannot express.

#include "ledger.h"
#include <orion/commdlg/msgbox.h>
#include <orion/commdlg/filepicker.h>
#include <time.h>

ledger_t *g_ledger = NULL;

static window_t *view(uint32_t id) { return g_ledger->main_win ? get_window_item(g_ledger->main_win, id) : NULL; }

static void *selected(uint32_t table_view) {
  window_t *tv = view(table_view);
  return tv ? (void *)send_message(tv, tvGetSelectedRecord, 0, NULL) : NULL;
}

static void refresh_tree(window_t *win) {
  for (window_t *c = win ? win->children : NULL; c; c = c->next) {
    if (c->proc == win_tableview) send_message(c, tvRefresh, 0, NULL);
    refresh_tree(c);
  }
}

void ledger_refresh_views(void) {
  refresh_tree(g_ledger->main_win);
  ledger_update_status();
}

void ledger_update_status(void) {
  if (!g_ledger->main_win) return;
  char org[160] = "", cfg[128] = "", text[512];
  result_node_t *o = (result_node_t *)send_db_message(g_ledger->ib, dbFetch, MAKEDWORD(TABLE_ORGANIZATIONS, 0), NULL);
  result_node_t *s = (result_node_t *)send_db_message(g_ledger->cfg, dbFetch, MAKEDWORD(TABLE_SETTINGS, 0), NULL);
  if (o) snprintf(org, sizeof(org), "%s", (*(db_organization_t **)o->data)->name);
  if (s) snprintf(cfg, sizeof(cfg), "%s", (*(db_setting_t **)s->data)->name);
  free_result_list(o); free_result_list(s);
  char from[16] = "", to[16] = "", period[128];
  const db_report_setting_t *rs = ledger_report_settings(g_ledger->ib, g_ledger->cfg);
  ui_format_date(rs ? rs->date_from : "", from, sizeof(from));
  ui_format_date(rs ? rs->date_to : "", to, sizeof(to));
  if (!from[0] && !to[0]) snprintf(period, sizeof(period), "%s", ui_tr("Report period: all dates"));
  else snprintf(period, sizeof(period), ui_tr("Report period: %s – %s"), from[0] ? from : "…", to[0] ? to : "…");
  bool dirty = send_db_message(g_ledger->ib, dbGetDirty, 0, NULL);
  snprintf(text, sizeof(text), "%s   |   %s   |   %s%s%s", org, cfg, period, dirty ? "   |   " : "", dirty ? ui_tr("modified") : "");
  send_message(g_ledger->main_win, evStatusBar, 0, text);
  // The title row names the page, as Finder names the folder, over the company being kept.
  window_t *page = window_active_page(g_ledger->main_win);
  set_window_title(g_ledger->main_win, page && page->title[0] ? page->title : ui_tr("Ledger"));
  set_window_subtitle(g_ledger->main_win, org);
  send_message(g_ledger->main_win, tbEnableItem, ID_VIEW_BACK, (void *)(intptr_t)page_host_can_go(g_ledger->main_win, -1));
  send_message(g_ledger->main_win, tbEnableItem, ID_VIEW_FORWARD, (void *)(intptr_t)page_host_can_go(g_ledger->main_win, 1));
}


// ── Document dialogs ───────────────────────────────────────────────────────
// Lines, entries and assets are declared in ledger.orion: each detail
// TableView follows the edited document, its buttons insert, edit and delete
// through the row's form, and the ledger_ib datasource recalculates the
// document, whose totals the dialog then shows.

static db_document_t *find_document(int doc_id) {
  return (db_document_t *)send_db_message(g_ledger->ib, dbFind, MAKEDWORD(TABLE_DOCUMENTS, 0), (void *)(intptr_t)doc_id);
}

static db_doc_type_t *document_type(const db_document_t *doc) {
  return doc ? (db_doc_type_t *)send_db_message(g_ledger->cfg, dbFind, MAKEDWORD(TABLE_DOC_TYPES, 0),
                                               (void *)(intptr_t)doc->doc_type_id) : NULL;
}

// Fill takes the type and date the dialog shows, which may not be saved yet.
static result_t payroll_hook(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (msg != evCommand || HIWORD(wparam) != btnClicked || !lparam || ((window_t *)lparam)->id != ID_PAYROLL_FILL) return false;
  int doc_id = db_dialog_record_id(win);
  db_document_t *doc = find_document(doc_id), *edited = db_dialog_record(win);
  if (!doc || !edited) return true;
  dialog_pull(win, edited, ledger_payroll_form.bindings, ledger_payroll_form.binding_count);
  doc->doc_type_id = edited->doc_type_id;
  snprintf(doc->date, sizeof(doc->date), "%s", edited->date);
  ledger_fill_payroll(g_ledger->ib, g_ledger->cfg, doc_id);
  window_t *lines = get_window_item(win, ID_PAYROLL_LINES);
  send_message(lines, tvRefresh, 0, NULL);
  send_message(win, evCommand, MAKEDWORD(ID_PAYROLL_LINES, tvnChanged), lines);
  return true;
}

// Each form of document type has its dialog.
static const struct { const char *form; const form_def_t *def; winproc_t hook; const char *title, *new_title; } kDocumentForms[] = {
  { "document",  &ledger_document_form,       NULL,         "Document",         "New Document" },
  { "operation", &ledger_operation_form,      NULL,         "Manual Operation", "New Manual Operation" },
  { "payroll",   &ledger_payroll_form,        payroll_hook, "Payroll",          "New Payroll Document" },
  { "assets",    &ledger_asset_document_form, NULL,         "Put into Service", "Put Assets into Service" },
  { "closing",   &ledger_closing_form,        NULL,         "Close Month",      "Close Month" },
};

static size_t document_form(const db_doc_type_t *type) {
  for (size_t i = 0; type && i < ARRAY_LEN(kDocumentForms); i++) if (!strcmp(type->form, kDocumentForms[i].form)) return i;
  return 0;
}

// OK in a document dialog saves and posts it.
static void after_save(int doc_id) {
  if (doc_id) ledger_post(g_ledger->ib, g_ledger->cfg, doc_id);
  ledger_build_reports(g_ledger->ib, g_ledger->cfg);
  ledger_refresh_views();
}

// The dialog is captioned like the document it edits: "Sale RT-0001 dated 10.03.2003".
static void edit_document(int doc_id) {
  const db_document_t *doc = find_document(doc_id);
  const db_doc_type_t *type = document_type(doc);
  size_t f = document_form(type);
  char date[16] = "", title[256];
  if (doc && type) {
    ui_format_date(doc->date, date, sizeof(date));
    snprintf(title, sizeof(title), ui_tr("%s %s dated %s"), type->name, doc->number, date);
  } else snprintf(title, sizeof(title), "%s", ui_tr(kDocumentForms[f].title));
  db_dialog_opts_t opts = { .hook = kDocumentForms[f].hook };
  if (show_db_dialog_opts(kDocumentForms[f].def, title, g_ledger->main_win, doc_id, &opts)) after_save(doc_id);
}

// A named record, for trusted Lua add-ons. Opening never creates a record.
bool ledger_open_record(const char *table, int id) {
  if (!g_ledger || !g_ledger->main_win || id <= 0) return false;
  if (!strcmp(table, "documents")) {
    if (!find_document(id)) return false;
    edit_document(id);
    return true;
  }
  const form_def_t *form = NULL;
  int table_id = 0;
  if (!strcmp(table, "counterparties")) { form = &ledger_counterparty_form; table_id = TABLE_COUNTERPARTIES; }
  else if (!strcmp(table, "items")) { form = &ledger_item_form; table_id = TABLE_ITEMS; }
  else return false;
  if (!send_db_message(g_ledger->ib, dbFind, MAKEDWORD(table_id, 0), (void *)(intptr_t)id)) return false;
  if (show_db_dialog(form, NULL, g_ledger->main_win, id)) ledger_refresh_views();
  return true;
}

// New documents start as the selected document's type, or the first type of
// the wanted form in the configuration.
static int first_doc_type(const char *form) {
  int id = 0;
  result_node_t *types = (result_node_t *)send_db_message(g_ledger->cfg, dbFetch, MAKEDWORD(TABLE_DOC_TYPES, 0), NULL);
  for (result_node_t *n = types; n && !id; n = n->next) {
    db_doc_type_t *t = *(db_doc_type_t **)n->data;
    if (!strcmp(t->form, form)) id = t->id;
  }
  free_result_list(types);
  return id;
}

// Month-end closing is dated the last day of the current month.
static void month_end(char *date, size_t sz) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  int y = t->tm_year + 1900, m = t->tm_mon + 1;
  snprintf(date, sz, "%04d-%02d-%02d", y, m, days[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)));
}

// The document is created by its dialog, inside the dialog's editing context:
// Cancel leaves no trace.
static void new_document(const char *form) {
  db_document_t *last = selected(ID_JOURNAL_PAGE_DOCUMENTS);
  db_doc_type_t *last_type = document_type(last);
  int type_id = last_type && !strcmp(last_type->form, form) ? last->doc_type_id : first_doc_type(form);
  if (!type_id) {
    message_box(g_ledger->main_win, ui_tr("The configuration defines no document type of this kind."), ui_tr("New Document"), MB_OK);
    return;
  }
  char defaults[64], date[16] = "";
  if (!strcmp(form, "closing")) month_end(date, sizeof(date));
  snprintf(defaults, sizeof(defaults), "doc_type_id=%d%s%s", type_id, date[0] ? ";date=" : "", date);
  size_t f = document_form(document_type(&(db_document_t){ .doc_type_id = type_id }));
  int doc_id = 0;
  db_dialog_opts_t opts = { .hook = kDocumentForms[f].hook, .defaults = defaults, .saved_id = &doc_id };
  if (show_db_dialog_opts(kDocumentForms[f].def, kDocumentForms[f].new_title, g_ledger->main_win, 0, &opts)) after_save(doc_id);
}

// The account card of the account chosen on the current page: the selected
// account, or the debit account of the selected entry.
static void show_account_card(void) {
  window_t *page = window_active_page(g_ledger->main_win);
  db_entry_t *entry = page && get_window_item(page, ID_ENTRIES_PAGE_ENTRIES) ? selected(ID_ENTRIES_PAGE_ENTRIES) : NULL;
  db_account_t *account = entry ? NULL : selected(ID_ACCOUNTS_PAGE_ACCOUNTS);
  int account_id = entry ? entry->debit_id : account ? account->id : 0;
  db_report_setting_t *rs = ledger_report_settings(g_ledger->ib, g_ledger->cfg);
  if (!account_id || !rs) return;
  rs->account_id = account_id;
  send_db_message(g_ledger->ib, dbUpdate, TABLE_REPORT_SETTINGS, rs);
  after_save(0);
  page_host_select(g_ledger->main_win, ID_VIEW_REPORTS);
  send_message(view(ID_REPORTS_PAGE_REPORT_TABS), tcSetSelection, 1, NULL);  // the card tab
}

// Catalog commands act on the page's TableView, which owns the row form.
static void catalog_action(uint32_t table_view, uint32_t action) {
  if (send_message(view(table_view), action, 0, NULL)) ledger_refresh_views();
}

static void select_configuration(void) {
  db_choice_t choice = {0};
  result_node_t *all = (result_node_t *)send_db_message(g_ledger->catalog, dbFetch, MAKEDWORD(TABLE_CONFIGURATIONS, 0), NULL);
  for (result_node_t *n = all; n; n = n->next) {
    db_configuration_t *c = *(db_configuration_t **)n->data;
    if (!strcmp(c->code, g_ledger->configuration)) choice.configuration_id = c->id;
  }
  free_result_list(all);
  if (!show_ddx_dialog(&ledger_select_configuration_form, NULL, g_ledger->main_win, &choice)) return;
  db_configuration_t *c = (db_configuration_t *)send_db_message(g_ledger->catalog, dbFind,
    MAKEDWORD(TABLE_CONFIGURATIONS, 0), (void *)(intptr_t)choice.configuration_id);
  if (!c || !strcmp(c->code, g_ledger->configuration)) return;
  axSettingsSave("ledger_configuration", c->code, strlen(c->code) + 1);
  message_box(g_ledger->main_win, ui_tr("The selected configuration opens with its own infobase\n"
              "the next time Ledger starts."), ui_tr("Select Configuration"), MB_OK);
}

static void select_language(void) {
  db_choice_t choice = {0};
  result_node_t *all = (result_node_t *)send_db_message(g_ledger->catalog, dbFetch, MAKEDWORD(TABLE_LANGUAGES, 0), NULL);
  for (result_node_t *n = all; n; n = n->next) {
    db_language_t *l = *(db_language_t **)n->data;
    if (!strcmp(l->code, ui_get_language())) choice.language_id = l->id;
  }
  free_result_list(all);
  if (!show_ddx_dialog(&ledger_select_language_form, NULL, g_ledger->main_win, &choice)) return;
  db_language_t *l = (db_language_t *)send_db_message(g_ledger->catalog, dbFind,
    MAKEDWORD(TABLE_LANGUAGES, 0), (void *)(intptr_t)choice.language_id);
  if (!l || !strcmp(l->code, ui_get_language())) return;
  axSettingsSave("ledger_language", l->code, strlen(l->code) + 1);
  message_box(g_ledger->main_win, ui_tr("The new language applies the next time Ledger starts."),
              ui_tr("Language"), MB_OK);
}

// Catalogs and opening balances from a 1C:Enterprise 8 exchange file, with a
// summary of what was added, found, and left out.
static void import_from_1c(void) {
  char path[LEDGER_PATH_MAX] = "", money_dr[32], money_cr[32], text[1024];
  openfilename_t ofn = { .lStructSize = sizeof(ofn), .hwndOwner = g_ledger->main_win, .lpstrFile = path, .nMaxFile = sizeof(path),
                         .lpstrFilter = "1C EnterpriseData (*.xml)\0*.xml\0All Files\0*.*\0", .Flags = OFN_FILEMUSTEXIST };
  if (!get_open_filename(&ofn)) return;
  ledger_import_t r;
  if (!ledger_import_enterprisedata(g_ledger->ib, g_ledger->cfg, path, &r)) {
    message_box(g_ledger->main_win, r.error, ui_tr("Import from 1C"), MB_OK);
    return;
  }
  after_save(0);
  ledger_update_status();
  ui_format_number(r.debit, 2, money_dr, sizeof(money_dr));
  ui_format_number(r.credit, 2, money_cr, sizeof(money_cr));
  int n = snprintf(text, sizeof(text), ui_tr("Counterparties: %d\nItems: %d\nEmployees: %d\nFixed assets: %d\nCurrencies: %d\n"
                                             "Already in the infobase: %d\nOpening entries: %d (debit %s, credit %s)"),
                   r.counterparties, r.items, r.employees, r.assets, r.currencies, r.matched, r.entries, money_dr, money_cr);
  if (r.balances_present && n > 0 && n < (int)sizeof(text))
    n += snprintf(text + n, sizeof(text) - (size_t)n, "\n\n%s", ui_tr("The balances in this exchange file were imported before and were not entered again."));
  if (r.skipped[0] && n > 0 && n < (int)sizeof(text))
    snprintf(text + n, sizeof(text) - (size_t)n, ui_tr("\n\nBalances the configuration has no account for, left out: %s"), r.skipped);
  message_box(g_ledger->main_win, text, ui_tr("Import from 1C"), MB_OK);
}

void ledger_command(uint16_t id) {
  if (!g_ledger) return;
  window_t *parent = g_ledger->main_win;
  db_document_t *doc = selected(ID_JOURNAL_PAGE_DOCUMENTS);
  char report[2048];
  switch (id) {
    case ID_FILE_SAVE:
      send_db_message(g_ledger->ib, dbSave, 0, NULL);
      ledger_update_status();
      break;
    case ID_FILE_ORGANIZATION:
      if (show_db_dialog(&ledger_organization_form, NULL, parent, 1)) ledger_update_status();
      break;
    case ID_FILE_CONFIGURATION: select_configuration(); break;
    case ID_FILE_LANGUAGE:      select_language();      break;
    case ID_FILE_IMPORT_1C:     import_from_1c();       break;
    case ID_FILE_ADDON:         ledger_open_addon();    break;
    case ID_FILE_ASSISTANT:     ledger_open_assistant(); break;
    case ID_FILE_QUIT:          ui_request_quit();      break;

    case ID_DOC_NEW:           new_document("document");  break;
    case ID_DOC_NEW_OPERATION: new_document("operation"); break;
    case ID_DOC_NEW_PAYROLL:   new_document("payroll");   break;
    case ID_DOC_NEW_ASSETS:    new_document("assets");    break;
    case ID_DOC_CLOSE_MONTH:   new_document("closing");   break;
    case ID_DOC_OPEN: if (doc) edit_document(doc->id); break;
    case ID_DOC_OPEN_ENTRY: {
      db_entry_t *entry = selected(ID_ENTRIES_PAGE_ENTRIES);
      if (entry) edit_document(entry->document_id);
      break;
    }
    case ID_DOC_POST:
      if (doc && ledger_post(g_ledger->ib, g_ledger->cfg, doc->id)) after_save(0);
      break;
    case ID_DOC_UNPOST:
      if (doc) { ledger_unpost(g_ledger->ib, g_ledger->cfg, doc->id); after_save(0); }
      break;
    case ID_DOC_PRINT:
      if (!doc) break;
      if (ledger_print_document(doc->id, report, sizeof(report)))
        message_box(parent, report, ui_tr("Print Forms Written"), MB_OK);
      else
        message_box(parent, ui_tr("The configuration defines no print forms for this document type."), ui_tr("Print"), MB_OK);
      break;
    case ID_DOC_DELETE:  // lines and entries go with it (on-delete="cascade")
      if (doc && message_box(parent, ui_tr("Delete the selected document and its entries?"), ui_tr("Delete Document"), MB_YESNO) == IDYES) {
        send_db_message(g_ledger->ib, dbDelete, TABLE_DOCUMENTS, (void *)(intptr_t)doc->id);
        after_save(0);
      }
      break;

    case ID_CATALOG_NEW_COUNTERPARTY:    catalog_action(ID_COUNTERPARTIES_PAGE_COUNTERPARTIES, tvInsertRecord); break;
    case ID_CATALOG_EDIT_COUNTERPARTY:   catalog_action(ID_COUNTERPARTIES_PAGE_COUNTERPARTIES, tvEditRecord);   break;
    case ID_CATALOG_DELETE_COUNTERPARTY: catalog_action(ID_COUNTERPARTIES_PAGE_COUNTERPARTIES, tvDeleteRecord); break;
    case ID_CATALOG_NEW_ITEM:            catalog_action(ID_ITEMS_PAGE_ITEMS, tvInsertRecord);                   break;
    case ID_CATALOG_EDIT_ITEM:           catalog_action(ID_ITEMS_PAGE_ITEMS, tvEditRecord);                     break;
    case ID_CATALOG_DELETE_ITEM:         catalog_action(ID_ITEMS_PAGE_ITEMS, tvDeleteRecord);                   break;
    case ID_CATALOG_NEW_EMPLOYEE:        catalog_action(ID_EMPLOYEES_PAGE_EMPLOYEES, tvInsertRecord);           break;
    case ID_CATALOG_EDIT_EMPLOYEE:       catalog_action(ID_EMPLOYEES_PAGE_EMPLOYEES, tvEditRecord);             break;
    case ID_CATALOG_DELETE_EMPLOYEE:     catalog_action(ID_EMPLOYEES_PAGE_EMPLOYEES, tvDeleteRecord);           break;
    case ID_CATALOG_NEW_ASSET:           catalog_action(ID_ASSETS_PAGE_ASSETS, tvInsertRecord);                 break;
    case ID_CATALOG_EDIT_ASSET:          catalog_action(ID_ASSETS_PAGE_ASSETS, tvEditRecord);                   break;
    case ID_CATALOG_DELETE_ASSET:        catalog_action(ID_ASSETS_PAGE_ASSETS, tvDeleteRecord);                 break;
    case ID_CATALOG_NEW_CURRENCY:        catalog_action(ID_CURRENCIES_PAGE_CURRENCIES, tvInsertRecord);         break;
    case ID_CATALOG_EDIT_CURRENCY:       catalog_action(ID_CURRENCIES_PAGE_CURRENCIES, tvEditRecord);           break;
    case ID_CATALOG_NEW_RATE:            catalog_action(ID_CURRENCIES_PAGE_RATES, tvInsertRecord);              break;
    case ID_CATALOG_DELETE_RATE:         catalog_action(ID_CURRENCIES_PAGE_RATES, tvDeleteRecord);              break;

    case ID_VIEW_JOURNAL: case ID_VIEW_COUNTERPARTIES: case ID_VIEW_ITEMS: case ID_VIEW_ACCOUNTS:
    case ID_VIEW_EMPLOYEES: case ID_VIEW_ASSETS: case ID_VIEW_CURRENCIES:
    case ID_VIEW_ENTRIES: case ID_VIEW_REPORTS: case ID_VIEW_CONFIG:
      page_host_select(parent, id);
      break;
    case ID_VIEW_BACK:    if (page_host_can_go(parent, -1)) page_host_go(parent, -1); break;
    case ID_VIEW_FORWARD: if (page_host_can_go(parent, 1)) page_host_go(parent, 1);   break;
    case ID_VIEW_SIDEBAR: page_host_set_sidebar_collapsed(parent, !page_host_sidebar_collapsed(parent)); break;

    case ID_REPORT_SETTINGS:
      if (show_db_dialog(&ledger_report_settings_form, NULL, parent, 1)) after_save(0);
      break;
    case ID_REPORT_RECALC:
      ledger_repost_all(g_ledger->ib, g_ledger->cfg);
      ledger_refresh_views();
      break;
    case ID_REPORT_CARD: show_account_card(); break;
    case ID_REPORT_PRINT:
      if (ledger_print_reports(report, sizeof(report)))
        message_box(parent, report, ui_tr("Print Forms Written"), MB_OK);
      else
        message_box(parent, ui_tr("The configuration defines no report forms."), ui_tr("Print"), MB_OK);
      break;
    case ID_HELP_ABOUT:
      message_box(parent, ui_tr("Ledger 1.0\n\nDouble-entry bookkeeping on Orion.\n"
                  "National rules, accounts and legal forms come from\n"
                  "the active configuration."), ui_tr("About Ledger"), MB_OK);
      break;
  }
}

// ── Windows ─────────────────────────────────────────────────────────────────

result_t ledger_main_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      g_ledger->main_win = win;
      if (!page_host_init(win, &ledger_main_window_form, NULL, 0)) return false;
      ledger_update_status();
      return true;
    case evCommand: {
      uint16_t code = HIWORD(wparam);
      window_t *src = (window_t *)lparam;
      // Catalog rows open their record form themselves; a document opens the
      // dialog of its type's form.
      if (code == RVN_DBLCLK && src) {
        if (src->id == ID_JOURNAL_PAGE_DOCUMENTS) ledger_command(ID_DOC_OPEN);
        return true;
      }
      if (code == tvnChanged) { ledger_refresh_views(); return true; }
      if (code == btnClicked || code == kMenuBarNotificationItemClick || code == kAcceleratorNotification || code == 0) {
        ledger_command(LOWORD(wparam));
        return true;
      }
      return false;
    }
    case evPageChanged:
      if (g_ledger->main_win == win) ledger_update_status();
      return true;
    case evClose:
      ui_request_quit();
      return true;
    case evDestroy:
      if (g_ledger && g_ledger->main_win == win) g_ledger->main_win = NULL;
      return false;
    default:
      return false;
  }
}

result_t ledger_menubar_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (msg == evCommand && (HIWORD(wparam) == kMenuBarNotificationItemClick ||
                           HIWORD(wparam) == kAcceleratorNotification)) {
    ledger_command(LOWORD(wparam));
    return true;
  }
  return win_menubar(win, msg, wparam, lparam);
}

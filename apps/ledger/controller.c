// Controller: command routing, document editing and view refresh.
// Screens, menus and bindings are declared in ledger.orion; this file holds
// only behavior the declarations cannot express.

#include "ledger.h"
#include <orion/commdlg/msgbox.h>
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
  snprintf(period, sizeof(period), ui_tr("Report period: %s – %s"), from[0] ? from : "…", to[0] ? to : "…");
  bool dirty = send_db_message(g_ledger->ib, dbGetDirty, 0, NULL);
  snprintf(text, sizeof(text), "%s   |   %s   |   %s%s%s", org, cfg, period, dirty ? "   |   " : "", dirty ? ui_tr("modified") : "");
  send_message(g_ledger->main_win, evStatusBar, 0, text);
}

static void after_change(int doc_id, bool post) {
  if (doc_id) {
    ledger_recalc_document(g_ledger->ib, g_ledger->cfg, doc_id);
    ledger_number_document(g_ledger->ib, g_ledger->cfg, doc_id);
    db_document_t *doc = (db_document_t *)send_db_message(g_ledger->ib, dbFind,
      MAKEDWORD(TABLE_DOCUMENTS, 0), (void *)(intptr_t)doc_id);
    if (doc && (post || doc->posted)) ledger_post(g_ledger->ib, g_ledger->cfg, doc_id);
  }
  ledger_build_reports(g_ledger->ib, g_ledger->cfg);
  ledger_refresh_views();
}

// ── Document dialog: header via bindings, lines managed here ───────────────

static db_document_t *find_document(int doc_id) {
  return (db_document_t *)send_db_message(g_ledger->ib, dbFind, MAKEDWORD(TABLE_DOCUMENTS, 0), (void *)(intptr_t)doc_id);
}

static db_doc_type_t *document_type(const db_document_t *doc) {
  return doc ? (db_doc_type_t *)send_db_message(g_ledger->cfg, dbFind, MAKEDWORD(TABLE_DOC_TYPES, 0),
                                               (void *)(intptr_t)doc->doc_type_id) : NULL;
}

static void show_amount(window_t *dlg, uint32_t id, double value) {
  char text[64];
  set_window_item_text(dlg, id, "%s", ui_format_number(value, 2, text, sizeof(text)));
}

static void show_document_totals(window_t *dlg, int doc_id) {
  db_document_t *doc = find_document(doc_id);
  if (!doc) return;
  show_amount(dlg, ID_DOCUMENT_AMOUNT, doc->amount);
  show_amount(dlg, ID_DOCUMENT_VAT, doc->vat);
  show_amount(dlg, ID_DOCUMENT_TOTAL, doc->total);
}

static result_t document_hook(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  int doc_id = db_dialog_record_id(win);
  window_t *lines = get_window_item(win, ID_DOCUMENT_LINES);
  if (msg == evCreate) {
    send_message(lines, tvSetFilter, (uint32_t)ledger_field(g_ledger->ib, TABLE_DOC_LINES, "document_id"), (void *)(intptr_t)doc_id);
    return true;
  }
  if (msg != evCommand) return false;
  uint16_t code = HIWORD(wparam);
  if (code == edUpdate) return true;  // Enter edits a field; it does not close the document
  db_doc_line_t *line = (db_doc_line_t *)send_message(lines, tvGetSelectedRecord, 0, NULL);
  uint32_t id = code == RVN_DBLCLK ? ID_DOCUMENT_EDIT_LINE : (code == btnClicked && lparam ? ((window_t *)lparam)->id : 0);
  switch (id) {
    case ID_DOCUMENT_ADD_LINE:
      show_db_dialog_ex(&ledger_doc_line_form, "Add Line", win, 0, "document_id", doc_id);
      break;
    case ID_DOCUMENT_EDIT_LINE:
      if (!line) return true;
      show_db_dialog(&ledger_doc_line_form, "Edit Line", win, line->id);
      break;
    case ID_DOCUMENT_DELETE_LINE:
      if (!line) return true;
      send_db_message(g_ledger->ib, dbDelete, TABLE_DOC_LINES, (void *)(intptr_t)line->id);
      break;
    default:
      return false;
  }
  ledger_recalc_document(g_ledger->ib, g_ledger->cfg, doc_id);
  send_message(lines, tvRefresh, 0, NULL);
  show_document_totals(win, doc_id);
  return true;
}

// ── Entry dialog: the analytics boxes follow the chosen accounts ───────────

// Lists the catalog the account is analysed by; empty and disabled without one.
static void source_subconto(window_t *dlg, uint32_t account_id, uint32_t subconto_id, int value) {
  window_t *account = get_window_item(dlg, account_id), *subconto = get_window_item(dlg, subconto_id);
  int account_value = (int)send_message(account, cbGetCurrentValue, 0, NULL);
  const char *table = account_value == kComboBoxError ? "" : ledger_subconto_table(g_ledger->cfg, account_value);
  const db_table_schema_t *schema = table[0] ? db_schema_table_named(g_ledger->ib->schema, table) : NULL;
  if (schema) send_message(subconto, cbSetSource, (uint32_t)schema->table_id, g_ledger->ib);
  else send_message(subconto, cbClear, 0, NULL);
  send_message(subconto, cbSetCurrentValue, (uint32_t)value, NULL);
  enable_window(subconto, schema != NULL);
}

static result_t entry_hook(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)lparam;
  if (msg == evCreate) {
    const db_entry_t *e = db_dialog_record(win);
    source_subconto(win, ID_ENTRY_DEBIT, ID_ENTRY_DEBIT_SUBCONTO, e ? e->debit_subconto_id : 0);
    source_subconto(win, ID_ENTRY_CREDIT, ID_ENTRY_CREDIT_SUBCONTO, e ? e->credit_subconto_id : 0);
    return true;
  }
  if (msg != evCommand || HIWORD(wparam) != cbSelectionChange) return false;
  if (LOWORD(wparam) == ID_ENTRY_DEBIT)  source_subconto(win, ID_ENTRY_DEBIT, ID_ENTRY_DEBIT_SUBCONTO, 0);
  if (LOWORD(wparam) == ID_ENTRY_CREDIT) source_subconto(win, ID_ENTRY_CREDIT, ID_ENTRY_CREDIT_SUBCONTO, 0);
  return true;
}

// ── Manual operation dialog: the accountant's entries ──────────────────────

static result_t operation_hook(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  int doc_id = db_dialog_record_id(win);
  window_t *entries = get_window_item(win, ID_OPERATION_ENTRIES);
  if (msg == evCreate) {
    send_message(entries, tvSetFilter, (uint32_t)ledger_field(g_ledger->ib, TABLE_ENTRIES, "document_id"), (void *)(intptr_t)doc_id);
    return true;
  }
  if (msg != evCommand) return false;
  uint16_t code = HIWORD(wparam);
  if (code == edUpdate) return true;
  db_entry_t *entry = (db_entry_t *)send_message(entries, tvGetSelectedRecord, 0, NULL);
  db_dialog_opts_t add_opts = { "document_id", doc_id, entry_hook }, edit_opts = { .hook = entry_hook };
  uint32_t id = code == RVN_DBLCLK ? ID_OPERATION_EDIT_ENTRY : (code == btnClicked && lparam ? ((window_t *)lparam)->id : 0);
  switch (id) {
    case ID_OPERATION_ADD_ENTRY:    show_db_dialog_opts(&ledger_entry_form, "Add Entry", win, 0, &add_opts); break;
    case ID_OPERATION_EDIT_ENTRY:   if (entry) show_db_dialog_opts(&ledger_entry_form, "Entry", win, entry->id, &edit_opts); break;
    case ID_OPERATION_DELETE_ENTRY: if (entry) send_db_message(g_ledger->ib, dbDelete, TABLE_ENTRIES, (void *)(intptr_t)entry->id); break;
    default: return false;
  }
  ledger_recalc_document(g_ledger->ib, g_ledger->cfg, doc_id);
  send_message(entries, tvRefresh, 0, NULL);
  db_document_t *doc = find_document(doc_id);
  if (doc) show_amount(win, ID_OPERATION_TOTAL, doc->total);
  return true;
}

// ── Payroll dialog: employees' lines ───────────────────────────────────────

static result_t payroll_hook(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  int doc_id = db_dialog_record_id(win);
  window_t *lines = get_window_item(win, ID_PAYROLL_LINES);
  if (msg == evCreate) {
    send_message(lines, tvSetFilter, (uint32_t)ledger_field(g_ledger->ib, TABLE_PAYROLL_LINES, "document_id"), (void *)(intptr_t)doc_id);
    return true;
  }
  if (msg != evCommand) return false;
  uint16_t code = HIWORD(wparam);
  if (code == edUpdate) return true;
  db_payroll_line_t *line = (db_payroll_line_t *)send_message(lines, tvGetSelectedRecord, 0, NULL);
  uint32_t id = code == RVN_DBLCLK ? ID_PAYROLL_EDIT_LINE : (code == btnClicked && lparam ? ((window_t *)lparam)->id : 0);
  switch (id) {
    case ID_PAYROLL_FILL: {
      // The dialog's header may name another type or date than the stored record.
      db_document_t *doc = find_document(doc_id), *edited = db_dialog_record(win);
      if (doc && edited) {
        dialog_pull(win, edited, ledger_payroll_form.bindings, ledger_payroll_form.binding_count);
        doc->doc_type_id = edited->doc_type_id;
        snprintf(doc->date, sizeof(doc->date), "%s", edited->date);
      }
      ledger_fill_payroll(g_ledger->ib, g_ledger->cfg, doc_id);
      break;
    }
    case ID_PAYROLL_ADD_LINE:    show_db_dialog_ex(&ledger_payroll_line_form, "Add Line", win, 0, "document_id", doc_id); break;
    case ID_PAYROLL_EDIT_LINE:   if (line) show_db_dialog(&ledger_payroll_line_form, "Edit Line", win, line->id); break;
    case ID_PAYROLL_DELETE_LINE: if (line) send_db_message(g_ledger->ib, dbDelete, TABLE_PAYROLL_LINES, (void *)(intptr_t)line->id); break;
    default: return false;
  }
  ledger_recalc_document(g_ledger->ib, g_ledger->cfg, doc_id);
  send_message(lines, tvRefresh, 0, NULL);
  db_document_t *doc = find_document(doc_id);
  if (doc) show_amount(win, ID_PAYROLL_TOTAL, doc->total);
  return true;
}

// ── Assets put into service ────────────────────────────────────────────────

static result_t asset_document_hook(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  int doc_id = db_dialog_record_id(win);
  window_t *lines = get_window_item(win, ID_ASSET_DOCUMENT_LINES);
  if (msg == evCreate) {
    send_message(lines, tvSetFilter, (uint32_t)ledger_field(g_ledger->ib, TABLE_ASSET_LINES, "document_id"), (void *)(intptr_t)doc_id);
    return true;
  }
  if (msg != evCommand) return false;
  uint16_t code = HIWORD(wparam);
  if (code == edUpdate) return true;
  db_asset_line_t *line = (db_asset_line_t *)send_message(lines, tvGetSelectedRecord, 0, NULL);
  uint32_t id = code == btnClicked && lparam ? ((window_t *)lparam)->id : 0;
  switch (id) {
    case ID_ASSET_DOCUMENT_ADD_LINE:    show_db_dialog_ex(&ledger_asset_line_form, "Add Asset", win, 0, "document_id", doc_id); break;
    case ID_ASSET_DOCUMENT_DELETE_LINE: if (line) send_db_message(g_ledger->ib, dbDelete, TABLE_ASSET_LINES, (void *)(intptr_t)line->id); break;
    default: return false;
  }
  ledger_recalc_document(g_ledger->ib, g_ledger->cfg, doc_id);
  send_message(lines, tvRefresh, 0, NULL);
  return true;
}

// Month-end closing shows the entries its last posting made.
static result_t closing_hook(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)lparam;
  if (msg == evCreate) {
    send_message(get_window_item(win, ID_CLOSING_ENTRIES), tvSetFilter,
                 (uint32_t)ledger_field(g_ledger->ib, TABLE_ENTRIES, "document_id"), (void *)(intptr_t)db_dialog_record_id(win));
    return true;
  }
  return msg == evCommand && HIWORD(wparam) == edUpdate;
}

static void today(char *out, size_t sz) {
  time_t now = time(NULL);
  strftime(out, sz, "%Y-%m-%d", localtime(&now));
}

// Each form of document type has its dialog.
static const struct { const char *form; const form_def_t *def; winproc_t hook; const char *title, *new_title; } kDocumentForms[] = {
  { "document",  &ledger_document_form,       document_hook,       "Document",         "New Document" },
  { "operation", &ledger_operation_form,      operation_hook,      "Manual Operation", "New Manual Operation" },
  { "payroll",   &ledger_payroll_form,        payroll_hook,        "Payroll",          "New Payroll Document" },
  { "assets",    &ledger_asset_document_form, asset_document_hook, "Put into Service", "Put Assets into Service" },
  { "closing",   &ledger_closing_form,        closing_hook,        "Close Month",      "Close Month" },
};

static void edit_document(int doc_id, bool is_new) {
  db_doc_type_t *type = document_type(find_document(doc_id));
  size_t f = 0;
  for (size_t i = 0; type && i < ARRAY_LEN(kDocumentForms); i++) if (!strcmp(type->form, kDocumentForms[i].form)) f = i;
  db_dialog_opts_t opts = { .hook = kDocumentForms[f].hook };
  uint32_t ok = show_db_dialog_opts(kDocumentForms[f].def, is_new ? kDocumentForms[f].new_title : kDocumentForms[f].title,
                                    g_ledger->main_win, doc_id, &opts);
  if (!ok && is_new) {
    ledger_delete_document(g_ledger->ib, doc_id);
    ledger_refresh_views();
    return;
  }
  after_change(doc_id, ok != 0);
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
static void month_end(char *date) {
  int y, m;
  if (sscanf(date, "%d-%d", &y, &m) != 2) return;
  static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  int d = days[(m - 1) % 12] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
  snprintf(date + 8, 3, "%02d", d);
}

static void new_document(const char *form) {
  db_document_t *last = selected(ID_JOURNAL_PAGE_DOCUMENTS);
  db_doc_type_t *last_type = document_type(last);
  db_document_t blank = { .doc_type_id = last_type && !strcmp(last_type->form, form) ? last->doc_type_id : first_doc_type(form) };
  if (!blank.doc_type_id) {
    message_box(g_ledger->main_win, ui_tr("The configuration defines no document type of this kind."), ui_tr("New Document"), MB_OK);
    return;
  }
  today(blank.date, sizeof(blank.date));
  if (!strcmp(form, "closing")) month_end(blank.date);
  db_document_t *doc = (db_document_t *)send_db_message(g_ledger->ib, dbInsert, TABLE_DOCUMENTS, &blank);
  if (doc) edit_document(doc->id, true);
}

// confirm and in_use are whole sentences so each translates on its own.
static void delete_catalog_record(uint32_t table_view, int table_id, const char *confirm,
                                  const char *in_use, int ref_table, const char *ref_field) {
  int *record = selected(table_view);  // every record starts with its integer key
  if (!record) return;
  char text[256];
  int refs = ledger_count_references(g_ledger->ib, ref_table, ref_field, *record);
  if (refs) {
    snprintf(text, sizeof(text), ui_tr(in_use), refs);
    message_box(g_ledger->main_win, text, ui_tr("Delete"), MB_OK);
    return;
  }
  if (message_box(g_ledger->main_win, ui_tr(confirm), ui_tr("Delete"), MB_YESNO) != IDYES) return;
  send_db_message(g_ledger->ib, dbDelete, (uint32_t)table_id, (void *)(intptr_t)*record);
  ledger_refresh_views();
}

static void edit_catalog_record(uint32_t table_view, const form_def_t *form, const char *title) {
  int *record = selected(table_view);
  if (record && show_db_dialog(form, title, g_ledger->main_win, *record)) ledger_refresh_views();
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
    case ID_FILE_QUIT:          ui_request_quit();      break;

    case ID_DOC_NEW:           new_document("document");  break;
    case ID_DOC_NEW_OPERATION: new_document("operation"); break;
    case ID_DOC_NEW_PAYROLL:   new_document("payroll");   break;
    case ID_DOC_NEW_ASSETS:    new_document("assets");    break;
    case ID_DOC_CLOSE_MONTH:   new_document("closing");   break;
    case ID_DOC_OPEN: if (doc) edit_document(doc->id, false); break;
    case ID_DOC_POST:
      if (doc && ledger_post(g_ledger->ib, g_ledger->cfg, doc->id)) after_change(0, false);
      break;
    case ID_DOC_UNPOST:
      if (doc) { ledger_unpost(g_ledger->ib, g_ledger->cfg, doc->id); after_change(0, false); }
      break;
    case ID_DOC_PRINT:
      if (!doc) break;
      if (ledger_print_document(doc->id, report, sizeof(report)))
        message_box(parent, report, ui_tr("Print Forms Written"), MB_OK);
      else
        message_box(parent, ui_tr("The configuration defines no print forms for this document type."), ui_tr("Print"), MB_OK);
      break;
    case ID_DOC_DELETE:
      if (doc && message_box(parent, ui_tr("Delete the selected document and its entries?"), ui_tr("Delete Document"), MB_YESNO) == IDYES) {
        ledger_delete_document(g_ledger->ib, doc->id);
        after_change(0, false);
      }
      break;

    case ID_CATALOG_NEW_COUNTERPARTY:
      if (show_db_dialog(&ledger_counterparty_form, "New Counterparty", parent, 0)) ledger_refresh_views();
      break;
    case ID_CATALOG_EDIT_COUNTERPARTY:
      edit_catalog_record(ID_COUNTERPARTIES_PAGE_COUNTERPARTIES, &ledger_counterparty_form, "Counterparty");
      break;
    case ID_CATALOG_DELETE_COUNTERPARTY:
      delete_catalog_record(ID_COUNTERPARTIES_PAGE_COUNTERPARTIES, TABLE_COUNTERPARTIES,
                            "Delete the selected counterparty?",
                            "This counterparty is used by %d document(s) and cannot be deleted.",
                            TABLE_DOCUMENTS, "counterparty_id");
      break;
    case ID_CATALOG_NEW_ITEM:
      if (show_db_dialog(&ledger_item_form, "New Item", parent, 0)) ledger_refresh_views();
      break;
    case ID_CATALOG_EDIT_ITEM:
      edit_catalog_record(ID_ITEMS_PAGE_ITEMS, &ledger_item_form, "Item");
      break;
    case ID_CATALOG_DELETE_ITEM:
      delete_catalog_record(ID_ITEMS_PAGE_ITEMS, TABLE_ITEMS, "Delete the selected item?",
                            "This item is used by %d document line(s) and cannot be deleted.",
                            TABLE_DOC_LINES, "item_id");
      break;

    case ID_CATALOG_NEW_EMPLOYEE:
      if (show_db_dialog(&ledger_employee_form, "New Employee", parent, 0)) ledger_refresh_views();
      break;
    case ID_CATALOG_EDIT_EMPLOYEE:
      edit_catalog_record(ID_EMPLOYEES_PAGE_EMPLOYEES, &ledger_employee_form, "Employee");
      break;
    case ID_CATALOG_DELETE_EMPLOYEE:
      delete_catalog_record(ID_EMPLOYEES_PAGE_EMPLOYEES, TABLE_EMPLOYEES, "Delete the selected employee?",
                            "This employee is used by %d payroll line(s) and cannot be deleted.",
                            TABLE_PAYROLL_LINES, "employee_id");
      break;
    case ID_CATALOG_NEW_ASSET:
      if (show_db_dialog(&ledger_asset_form, "New Fixed Asset", parent, 0)) ledger_refresh_views();
      break;
    case ID_CATALOG_EDIT_ASSET:
      edit_catalog_record(ID_ASSETS_PAGE_ASSETS, &ledger_asset_form, "Fixed Asset");
      break;
    case ID_CATALOG_DELETE_ASSET:
      delete_catalog_record(ID_ASSETS_PAGE_ASSETS, TABLE_ASSETS, "Delete the selected fixed asset?",
                            "This fixed asset is used by %d document line(s) and cannot be deleted.",
                            TABLE_ASSET_LINES, "asset_id");
      break;
    case ID_CATALOG_NEW_CURRENCY:
      if (show_db_dialog(&ledger_currency_form, "New Currency", parent, 0)) ledger_refresh_views();
      break;
    case ID_CATALOG_EDIT_CURRENCY:
      edit_catalog_record(ID_CURRENCIES_PAGE_CURRENCIES, &ledger_currency_form, "Currency");
      break;
    case ID_CATALOG_NEW_RATE: {
      db_currency_t *currency = selected(ID_CURRENCIES_PAGE_CURRENCIES);
      if (show_db_dialog_ex(&ledger_rate_form, "New Exchange Rate", parent, 0, "currency_id", currency ? currency->id : 0))
        ledger_refresh_views();
      break;
    }
    case ID_CATALOG_DELETE_RATE: {
      db_rate_t *rate = selected(ID_CURRENCIES_PAGE_RATES);
      if (rate && message_box(parent, ui_tr("Delete the selected exchange rate?"), ui_tr("Delete"), MB_YESNO) == IDYES) {
        send_db_message(g_ledger->ib, dbDelete, TABLE_RATES, (void *)(intptr_t)rate->id);
        ledger_refresh_views();
      }
      break;
    }

    case ID_VIEW_JOURNAL: case ID_VIEW_COUNTERPARTIES: case ID_VIEW_ITEMS: case ID_VIEW_ACCOUNTS:
    case ID_VIEW_EMPLOYEES: case ID_VIEW_ASSETS: case ID_VIEW_CURRENCIES:
    case ID_VIEW_ENTRIES: case ID_VIEW_REPORTS: case ID_VIEW_CONFIG:
      page_host_select(parent, id);
      break;

    case ID_REPORT_SETTINGS:
      if (show_db_dialog(&ledger_report_settings_form, NULL, parent, 1)) after_change(0, false);
      break;
    case ID_REPORT_RECALC:
      ledger_repost_all(g_ledger->ib, g_ledger->cfg);
      ledger_refresh_views();
      break;
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
      if (code == RVN_DBLCLK && src) {
        if (src->id == ID_JOURNAL_PAGE_DOCUMENTS)             ledger_command(ID_DOC_OPEN);
        if (src->id == ID_COUNTERPARTIES_PAGE_COUNTERPARTIES) ledger_command(ID_CATALOG_EDIT_COUNTERPARTY);
        if (src->id == ID_ITEMS_PAGE_ITEMS)                   ledger_command(ID_CATALOG_EDIT_ITEM);
        if (src->id == ID_EMPLOYEES_PAGE_EMPLOYEES)           ledger_command(ID_CATALOG_EDIT_EMPLOYEE);
        if (src->id == ID_ASSETS_PAGE_ASSETS)                 ledger_command(ID_CATALOG_EDIT_ASSET);
        if (src->id == ID_CURRENCIES_PAGE_CURRENCIES)         ledger_command(ID_CATALOG_EDIT_CURRENCY);
        return true;
      }
      // The posted checkbox in the journal posts or cancels posting.
      if (code == RVN_ITEMCHECK && src && src->id == ID_JOURNAL_PAGE_DOCUMENTS) {
        db_document_t *doc = (db_document_t *)send_message(src, tvGetRecord, LOWORD(wparam), NULL);
        if (doc && doc->posted) ledger_unpost(g_ledger->ib, g_ledger->cfg, doc->id);
        else if (doc) ledger_post(g_ledger->ib, g_ledger->cfg, doc->id);
        after_change(0, false);
        return true;
      }
      if (code == btnClicked || code == kMenuBarNotificationItemClick || code == kAcceleratorNotification || code == 0) {
        ledger_command(LOWORD(wparam));
        return true;
      }
      return false;
    }
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

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
  uint32_t id = code == RVN_DBLCLK ? ID_OPERATION_EDIT_ENTRY : (code == btnClicked && lparam ? ((window_t *)lparam)->id : 0);
  switch (id) {
    case ID_OPERATION_ADD_ENTRY:    show_db_dialog_ex(&ledger_entry_form, "Add Entry", win, 0, "document_id", doc_id); break;
    case ID_OPERATION_EDIT_ENTRY:   if (entry) show_db_dialog(&ledger_entry_form, "Entry", win, entry->id);           break;
    case ID_OPERATION_DELETE_ENTRY: if (entry) send_db_message(g_ledger->ib, dbDelete, TABLE_ENTRIES, (void *)(intptr_t)entry->id); break;
    default: return false;
  }
  ledger_recalc_document(g_ledger->ib, g_ledger->cfg, doc_id);
  send_message(entries, tvRefresh, 0, NULL);
  db_document_t *doc = find_document(doc_id);
  if (doc) show_amount(win, ID_OPERATION_TOTAL, doc->total);
  return true;
}

static void today(char *out, size_t sz) {
  time_t now = time(NULL);
  strftime(out, sz, "%Y-%m-%d", localtime(&now));
}

static void edit_document(int doc_id, bool is_new) {
  db_doc_type_t *type = document_type(find_document(doc_id));
  uint32_t ok = type && type->manual
    ? show_db_dialog_hook(&ledger_operation_form, is_new ? "New Manual Operation" : "Manual Operation",
                          g_ledger->main_win, doc_id, operation_hook)
    : show_db_dialog_hook(&ledger_document_form, is_new ? "New Document" : "Document",
                          g_ledger->main_win, doc_id, document_hook);
  if (!ok && is_new) {
    ledger_delete_document(g_ledger->ib, doc_id);
    ledger_refresh_views();
    return;
  }
  after_change(doc_id, ok != 0);
}

// New documents start as the selected document's type, or the first type of
// the wanted kind in the configuration.
static int first_doc_type(bool manual) {
  int id = 0;
  result_node_t *types = (result_node_t *)send_db_message(g_ledger->cfg, dbFetch, MAKEDWORD(TABLE_DOC_TYPES, 0), NULL);
  for (result_node_t *n = types; n && !id; n = n->next) {
    db_doc_type_t *t = *(db_doc_type_t **)n->data;
    if (t->manual == manual) id = t->id;
  }
  free_result_list(types);
  return id;
}

static void new_document(bool manual) {
  db_document_t *last = selected(ID_JOURNAL_PAGE_DOCUMENTS);
  db_doc_type_t *last_type = document_type(last);
  db_document_t blank = { .doc_type_id = last_type && last_type->manual == manual ? last->doc_type_id : first_doc_type(manual) };
  today(blank.date, sizeof(blank.date));
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

    case ID_DOC_NEW:           new_document(false); break;
    case ID_DOC_NEW_OPERATION: new_document(true);  break;
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

    case ID_VIEW_JOURNAL: case ID_VIEW_COUNTERPARTIES: case ID_VIEW_ITEMS: case ID_VIEW_ACCOUNTS:
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

// Controller: command routing, document editing and view refresh.
// Screens, menus and bindings are declared in ledger.orion; this file holds
// only behavior the declarations cannot express.

#include "ledger.h"
#include <orion/commdlg/msgbox.h>
#include <orion/commdlg/filepicker.h>
#include <time.h>
#include <orion/script/script_internal.h>

ledger_t *g_ledger = NULL;

static window_t *view(uint32_t id) { return g_ledger->main_win ? get_window_item(g_ledger->main_win, id) : NULL; }

static void *selected(uint32_t table_view) {
  window_t *tv = view(table_view);
  return tv ? (void *)send_message(tv, tvGetSelectedRecord, 0, NULL) : NULL;
}

static void update_home(void);

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
  if (!g_ledger->main_win || !g_ledger->runtime) return;
  char error[2048];
  script_runtime_invoke(g_ledger->runtime, g_ledger->controller, "status", NULL, error, sizeof(error));  // the runtime logs faults
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
  database_t *database = db_dialog_database(win);
  db_document_t *doc = database ? (db_document_t *)send_db_message(database, dbFind, TABLE_DOCUMENTS, (void *)(intptr_t)doc_id) : NULL;
  db_document_t *edited = db_dialog_record(win);
  if (!doc || !edited) return true;
  dialog_pull(win, edited, ledger_payroll_form.bindings, ledger_payroll_form.binding_count);
  doc->doc_type_id = edited->doc_type_id;
  snprintf(doc->date, sizeof(doc->date), "%s", edited->date);
  snprintf(doc->shipper, sizeof(doc->shipper), "%s", edited->shipper);
  snprintf(doc->consignee, sizeof(doc->consignee), "%s", edited->consignee);
  snprintf(doc->supply_date, sizeof(doc->supply_date), "%s", edited->supply_date);
  snprintf(doc->tax_date, sizeof(doc->tax_date), "%s", edited->tax_date);
  snprintf(doc->due_date, sizeof(doc->due_date), "%s", edited->due_date);
  snprintf(doc->supply_kind, sizeof(doc->supply_kind), "%s", edited->supply_kind);
  snprintf(doc->tax_scheme, sizeof(doc->tax_scheme), "%s", edited->tax_scheme);
  snprintf(doc->shipment_reference, sizeof(doc->shipment_reference), "%s", edited->shipment_reference);
  snprintf(doc->payment_reference, sizeof(doc->payment_reference), "%s", edited->payment_reference);
  snprintf(doc->advance_invoice_refs, sizeof(doc->advance_invoice_refs), "%s", edited->advance_invoice_refs);
  snprintf(doc->contract_reference, sizeof(doc->contract_reference), "%s", edited->contract_reference);
  snprintf(doc->discount_terms, sizeof(doc->discount_terms), "%s", edited->discount_terms);
  snprintf(doc->legal_note, sizeof(doc->legal_note), "%s", edited->legal_note);
  ledger_fill_payroll(database, g_ledger->cfg, doc_id);
  window_t *lines = get_window_item(win, ID_PAYROLL_LINES);
  send_message(lines, tvRefresh, 0, NULL);
  send_message(win, evCommand, MAKEDWORD(ID_PAYROLL_LINES, tvnChanged), lines);
  return true;
}

// The caption names the document as 1C does: "Sale RT-0001 dated 10.03.2003".
static bool document_caption(const db_document_t *doc, char *out, size_t out_sz) {
  const db_doc_type_t *type = document_type(doc);
  char date[16] = "";
  if (!type || !doc->number[0]) return false;
  ui_format_date(doc->date, date, sizeof(date));
  snprintf(out, out_sz, ui_tr("%s %s dated %s"), type->name, doc->number, date);
  return true;
}

// The goods form previews the entries posting will write. They are written in
// the form's own editing context from what the form shows, so the books change
// only when the document is saved.
static void preview_entries(window_t *win) {
  database_t *database = db_dialog_database(win);
  int doc_id = db_dialog_record_id(win);
  db_document_t *doc = database ? (db_document_t *)send_db_message(database, dbFind, TABLE_DOCUMENTS, (void *)(intptr_t)doc_id) : NULL;
  db_document_t *edited = db_dialog_record(win);
  if (!doc || !edited) return;
  dialog_pull(win, edited, ledger_document_form.bindings, ledger_document_form.binding_count);
  doc->doc_type_id = edited->doc_type_id;
  doc->counterparty_id = edited->counterparty_id;
  doc->currency_id = edited->currency_id;
  doc->rate = edited->rate;
  snprintf(doc->date, sizeof(doc->date), "%s", edited->date);
  snprintf(doc->shipper, sizeof(doc->shipper), "%s", edited->shipper);
  snprintf(doc->consignee, sizeof(doc->consignee), "%s", edited->consignee);
  snprintf(doc->supply_date, sizeof(doc->supply_date), "%s", edited->supply_date);
  snprintf(doc->tax_date, sizeof(doc->tax_date), "%s", edited->tax_date);
  snprintf(doc->due_date, sizeof(doc->due_date), "%s", edited->due_date);
  snprintf(doc->supply_kind, sizeof(doc->supply_kind), "%s", edited->supply_kind);
  snprintf(doc->tax_scheme, sizeof(doc->tax_scheme), "%s", edited->tax_scheme);
  snprintf(doc->shipment_reference, sizeof(doc->shipment_reference), "%s", edited->shipment_reference);
  snprintf(doc->payment_reference, sizeof(doc->payment_reference), "%s", edited->payment_reference);
  snprintf(doc->advance_invoice_refs, sizeof(doc->advance_invoice_refs), "%s", edited->advance_invoice_refs);
  snprintf(doc->contract_reference, sizeof(doc->contract_reference), "%s", edited->contract_reference);
  snprintf(doc->discount_terms, sizeof(doc->discount_terms), "%s", edited->discount_terms);
  snprintf(doc->legal_note, sizeof(doc->legal_note), "%s", edited->legal_note);
  ledger_preview_entries(database, g_ledger->cfg, doc_id);
  send_message(get_window_item(win, ID_DOCUMENT_POSTINGS), tvRefresh, 0, NULL);
}

// The heading names the document; the badge says whether the books hold it.
// Saving keeps that state.
static void show_document_state(window_t *win) {
  const db_document_t *doc = db_dialog_record(win);
  window_t *badge = get_window_item(win, ID_DOCUMENT_STATE), *caption = get_window_item(win, ID_DOCUMENT_CAPTION);
  char title[256];
  if (!doc || !badge || !caption) return;
  bool named = document_caption(doc, title, sizeof(title));
  set_window_title(caption, named ? title : ui_tr("New Document"));
  if (named) set_window_title(win, title);
  set_window_title(badge, doc->posted ? ui_tr("Posted") : ui_tr("Not posted"));
  send_message(badge, bdSetColor, doc->posted ? brTextSuccess : brTextWarning, NULL);
  send_message(badge, btnSetIconName, 0, doc->posted ? "ledger-doc-posted" : "ledger-doc-draft");
  // Settlement comes from the books, so a sale shows whether its customer has paid.
  window_t *payment = get_window_item(win, ID_DOCUMENT_PAYMENT);
  double due = 0, paid = 0;
  bool settles = payment && ledger_document_settlement(g_ledger->ib, g_ledger->cfg, db_dialog_record_id(win), &due, &paid);
  if (payment) show_window(payment, settles);
  if (settles) {
    set_window_title(payment, paid >= due ? ui_tr("Paid") : paid > 0 ? ui_tr("Partly paid") : ui_tr("Not paid"));
    send_message(payment, bdSetColor, paid >= due ? brTextInfo : paid > 0 ? brTextWarning : brTextSecondary, NULL);
  }
  window_layout_sync(caption->parent);
}

// Post and Close, and Post, save the document as posted; Save keeps its state.
static void save_document(window_t *win, bool post, bool close) {
  db_document_t *edited = db_dialog_record(win);
  if (!edited) return;
  if (post) edited->posted = true;
  db_form_save(win, close);
}

typedef struct { int document_type; } print_picker_t;

static result_t print_picker_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      const print_picker_t *picker = lparam;
      send_message(get_window_item(win, ID_SELECT_PRINT_FORM_FORMS), tvSetFilter,
        (uint32_t)ledger_field(g_ledger->cfg, TABLE_PRINT_FORMS, "doc_type_id"), (void *)(intptr_t)picker->document_type);
      return true;
    }
    case evCommand:
      if (LOWORD(wparam) == ID_SELECT_PRINT_FORM_PRINT && HIWORD(wparam) == btnClicked) {
        const db_print_form_t *form = (const db_print_form_t *)send_message(get_window_item(win, ID_SELECT_PRINT_FORM_FORMS), tvGetSelectedRecord, 0, NULL);
        if (form) end_dialog(win, (uint32_t)form->id);
        return true;
      }
      if (LOWORD(wparam) == ID_SELECT_PRINT_FORM_CANCEL && HIWORD(wparam) == btnClicked) { end_dialog(win, 0); return true; }
      return false;
    case evClose: end_dialog(win, 0); return true;
    case evPaint: case evDestroy: return false;
    default: return false;
  }
}

static void print_document(window_t *parent, int doc_id) {
  const db_document_t *doc = find_document(doc_id);
  if (!doc) return;
  result_node_t *forms = (result_node_t *)send_db_message(g_ledger->cfg, dbFetch,
    MAKEDWORD(TABLE_PRINT_FORMS, ledger_field(g_ledger->cfg, TABLE_PRINT_FORMS, "doc_type_id")), (void *)(intptr_t)doc->doc_type_id);
  int count = count_result_list(forms), selected = forms ? (*(db_print_form_t **)forms->data)->id : 0;
  free_result_list(forms);
  if (!count) { message_box(parent, ui_tr("The configuration defines no print forms for this document type."), ui_tr("Print"), MB_OK); return; }
  if (count > 1) {
    print_picker_t picker = {doc->doc_type_id};
    selected = (int)show_dialog_from_form(&ledger_select_print_form_form, NULL, parent, print_picker_proc, &picker);
  }
  if (!selected) return;
  char report[2048];
  int written = ledger_print_form(doc_id, selected, report, sizeof(report));
  message_box(parent, report[0] ? report : ui_tr("The print form could not be completed. No incomplete file was published."),
    written ? ui_tr("Print Forms Written") : ui_tr("Print"), MB_OK);
}

static result_t document_hook(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      preview_entries(win);
      db_edit_context_mark_clean(db_dialog_database(win));  // opening is not an edit
      show_document_state(win);
      return true;
    case evCommand: {
      uint16_t code = HIWORD(wparam);
      window_t *src = (window_t *)lparam;
      if ((code == tvnChanged && LOWORD(wparam) == ID_DOCUMENT_LINES) || code == cbSelectionChange) {
        preview_entries(win);
        return true;
      }
      if (code == edUpdate) return true;  // Enter in a field does not close the document
      if (code != btnClicked || !src) return false;
      switch (src->id) {
        case ID_DOCUMENT_POST_CLOSE: save_document(win, true, true);   return true;
        case ID_DOCUMENT_POST:       save_document(win, true, false);  return true;
        case ID_DOCUMENT_SAVE:       save_document(win, false, false); return true;
        case ID_DOCUMENT_ENTRIES_TOGGLE: {  // Dr/Cr: the entries pane, as 1C's button shows them
          window_t *pane = get_window_item(win, ID_DOCUMENT_POSTINGS_PANE);
          if (!pane) return true;
          show_window(pane, !window_is_visible(pane));
          window_layout_sync(pane->parent);
          return true;
        }
        case ID_DOCUMENT_PRINT: {
          int doc_id = db_dialog_record_id(win);
          save_document(win, false, false);
          print_document(win, doc_id);
          return true;
        }
      }
      return false;
    }
    default:
      return false;
  }
}

// Each form of document type has its dialog.
static const struct { const char *form; const form_def_t *def; winproc_t hook; const char *title, *new_title; } kDocumentForms[] = {
  { "document",  &ledger_document_form,       document_hook, "Document",        "New Document" },
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
  if (doc_id && !ledger_post(g_ledger->ib, g_ledger->cfg, doc_id))
    message_box(g_ledger->main_win, ui_tr("Posting failed. Previous accounting entries were restored. Check the configuration and document."), ui_tr("Posting Failed"), MB_OK);
  ledger_build_reports(g_ledger->ib, g_ledger->cfg);
  ledger_refresh_views();
  ledger_assistant_books_changed();
}

// The goods form also saves drafts: a document saved unposted leaves the books.
static void document_saved(int doc_id, void *userdata) {
  (void)userdata;
  const db_document_t *doc = find_document(doc_id);
  const db_doc_type_t *type = document_type(doc);
  if (doc && type && !strcmp(type->form, "document") && !doc->posted) {
    ledger_unpost(g_ledger->ib, g_ledger->cfg, doc_id);
    after_save(0);
    return;
  }
  after_save(doc_id);
}

static void edit_document(int doc_id) {
  const db_document_t *doc = find_document(doc_id);
  size_t f = document_form(document_type(doc));
  char title[256];
  if (!doc || !document_caption(doc, title, sizeof(title))) snprintf(title, sizeof(title), "%s", ui_tr(kDocumentForms[f].title));
  db_dialog_opts_t opts = { .hook = kDocumentForms[f].hook };
  db_form_open(kDocumentForms[f].def, title, view(ID_MAIN_WINDOW_DOCUMENTS_CLIENT), doc_id, &opts, document_saved, NULL);
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

// The document is created by its dialog, inside the dialog's editing context:
// Cancel leaves no trace. date (ISO) may be empty.
static void new_document_of(int type_id, const char *date) {
  char defaults[64];
  snprintf(defaults, sizeof(defaults), "doc_type_id=%d%s%s", type_id, date && date[0] ? ";date=" : "", date ? date : "");
  size_t f = document_form(document_type(&(db_document_t){ .doc_type_id = type_id }));
  db_dialog_opts_t opts = { .hook = kDocumentForms[f].hook, .defaults = defaults };
  db_form_open(kDocumentForms[f].def, ui_tr(kDocumentForms[f].new_title), view(ID_MAIN_WINDOW_DOCUMENTS_CLIENT), 0, &opts, document_saved, NULL);
}

static void open_account_card(int account_id) {
  db_report_setting_t *rs = ledger_report_settings(g_ledger->ib, g_ledger->cfg);
  if (!account_id || !rs) return;
  rs->account_id = account_id;
  send_db_message(g_ledger->ib, dbUpdate, TABLE_REPORT_SETTINGS, rs);
  after_save(0);
  page_host_select(g_ledger->main_win, ID_VIEW_REPORTS);
  send_message(view(ID_REPORTS_PAGE_REPORT_TABS), tcSetSelection, 1, NULL);  // the card tab
}

// The account card of the account chosen on the current page: the selected
// account, or the debit account of the selected entry.
static void show_account_card(void) {
  window_t *page = window_active_page(g_ledger->main_win);
  db_entry_t *entry = page && get_window_item(page, ID_ENTRIES_PAGE_ENTRIES) ? selected(ID_ENTRIES_PAGE_ENTRIES) : NULL;
  db_account_t *account = entry ? NULL : selected(ID_ACCOUNTS_PAGE_ACCOUNTS);
  open_account_card(entry ? entry->debit_id : account ? account->id : 0);
}

// ── Home ────────────────────────────────────────────────────────────────────

// Posts every draft after saying which, as 1C's group reposting does.
static void post_drafts(void) {
  char list[1024] = "", line[160], date[16], total[48], text[1400];
  int count = 0;
  result_node_t *docs = (result_node_t *)send_db_message(g_ledger->ib, dbFetch, MAKEDWORD(TABLE_DOCUMENTS, 0), NULL);
  for (result_node_t *n = docs; n; n = n->next) {
    const db_document_t *d = *(db_document_t **)n->data;
    if (d->posted) continue;
    ui_format_date(d->date, date, sizeof(date));
    snprintf(line, sizeof(line), "%s %s, %s\n", d->number, date, ui_format_number(d->total, 2, total, sizeof(total)));
    if (++count <= 12) { size_t used = strlen(list); snprintf(list + used, sizeof(list) - used, "%s", line); }
  }
  if (!count) { free_result_list(docs); return; }
  snprintf(text, sizeof(text), ui_tr("Post the documents that are not posted (%d)?\n\n%s"), count, list);
  if (message_box(g_ledger->main_win, text, ui_tr("Post Documents"), MB_YESNO) == IDYES) {
    int failed = 0;
    for (result_node_t *n = docs; n; n = n->next) {
      const db_document_t *d = *(db_document_t **)n->data;
      if (!d->posted && !ledger_post(g_ledger->ib, g_ledger->cfg, d->id)) failed++;
    }
    after_save(0);
    if (failed) {
      snprintf(text, sizeof(text), ui_tr("Documents that could not be posted: %d. Open them to see why."), failed);
      message_box(g_ledger->main_win, text, ui_tr("Post Documents"), MB_OK);
    }
  }
  free_result_list(docs);
}

void ledger_home_perform(int row_id) {
  const db_home_row_t *row = (const db_home_row_t *)send_db_message(g_ledger->ib, dbFind, MAKEDWORD(TABLE_HOME_ROWS, 0), (void *)(intptr_t)row_id);
  if (!row) {
    fprintf(stderr, "[ledger] home: no row %d to settle\n", row_id);
    fflush(stderr);
    return;
  }
  if      (!strcmp(row->command, "post"))  post_drafts();
  else if (!strcmp(row->command, "close")) { int type = first_doc_type("closing"); if (type) new_document_of(type, row->date); }
  else if (!strcmp(row->command, "open") && row->document_id) edit_document(row->document_id);
  else if (!strcmp(row->command, "new") && row->doc_type_id)  new_document_of(row->doc_type_id, "");
  else if (!strcmp(row->command, "card") && row->account_id)  open_account_card(row->account_id);
  else if (!strcmp(row->command, "repost"))                    ledger_command(ID_REPORT_RECALC);
}

// A row of the home page was clicked: do what it offers.
static void home_activate(window_t *table) {
  if (table->id == ID_HOME_PAGE_RECENT) {
    db_recent_document_t *recent = (db_recent_document_t *)send_message(table, tvGetSelectedRecord, 0, NULL);
    if (recent) edit_document(recent->document_id);
    return;
  }
  db_home_row_t *row = (db_home_row_t *)send_message(table, tvGetSelectedRecord, 0, NULL);
  if (row) ledger_home_perform(row->id);
}

static bool is_home_table(uint32_t id) {
  return id == ID_HOME_PAGE_ATTENTION || id == ID_HOME_PAGE_MONEY || id == ID_HOME_PAGE_SETTLEMENTS ||
         id == ID_HOME_PAGE_CREATE || id == ID_HOME_PAGE_RECENT;
}

// The figures the tables cannot show: the money total, the count of tasks.
static void update_home(void) {
  window_t *total = view(ID_HOME_PAGE_MONEY_TOTAL), *count = view(ID_HOME_PAGE_ATTENTION_COUNT), *none = view(ID_HOME_PAGE_ATTENTION_NONE);
  if (!total) return;
  double money = 0;
  int tasks = 0;
  result_node_t *rows = (result_node_t *)send_db_message(g_ledger->ib, dbFetch, MAKEDWORD(TABLE_HOME_ROWS, 0), NULL);
  for (result_node_t *n = rows; n; n = n->next) {
    const db_home_row_t *r = *(db_home_row_t **)n->data;
    if (r->card == 2) money += r->amount;
    if (r->card == 1) tasks++;
  }
  free_result_list(rows);
  char text[64];
  set_window_title(total, ui_format_number(money, 2, text, sizeof(text)));
  snprintf(text, sizeof(text), "%d", tasks);
  set_window_title(count, tasks ? text : "");
  set_window_title(none, tasks ? "" : ui_tr("The books are in order: nothing is waiting."));
  window_layout_sync(total->parent);
  window_layout_sync(count->parent);
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

static bool ledger_ui_dirty(lua_State *L) { return send_db_message(g_ledger->ib, dbGetDirty, 0, NULL); }
static const char *ledger_ui_asof(lua_State *L) { return page_host_selection(g_ledger->main_win) == ID_VIEW_HOME ? ledger_home_asof() : ""; }
static void ledger_ui_status(lua_State *L, const char *text, const char *subtitle) {
  send_message(g_ledger->main_win, evStatusBar, 0, (void *)text);
  update_home();
  window_t *page = window_active_page(g_ledger->main_win);
  set_window_title(g_ledger->main_win, page && page->title[0] ? page->title : ui_tr("Ledger"));
  set_window_subtitle(g_ledger->main_win, subtitle);
  send_message(g_ledger->main_win, tbEnableItem, ID_VIEW_BACK, (void *)(intptr_t)page_host_can_go(g_ledger->main_win, -1));
  send_message(g_ledger->main_win, tbEnableItem, ID_VIEW_FORWARD, (void *)(intptr_t)page_host_can_go(g_ledger->main_win, 1));
}
// Native capabilities retain window/storage ownership. Workflow lives in controller.lua.
static int ledger_ui_selected(lua_State *L, const char *name) {
  uint32_t id = !strcmp(name, "entries") ? ID_ENTRIES_PAGE_ENTRIES : !strcmp(name, "documents") ? ID_JOURNAL_PAGE_DOCUMENTS : 0;
  if (!id) return luaL_error(L, "unknown selection '%s'", name);
  const int *record = selected(id);
  return record ? *record : 0;
}
static void ledger_ui_new_document(lua_State *L, int type, const char *date) { new_document_of(type, date); }
static void ledger_ui_edit_document(lua_State *L, int id) { if (id) edit_document(id); }
static void ledger_ui_print_document(lua_State *L, int id) { if (id) print_document(g_ledger->main_win, id); }
static void ledger_ui_changed(lua_State *L, int id) { after_save(id); }
static void ledger_ui_unpost(lua_State *L, int id) { ledger_unpost(g_ledger->ib, g_ledger->cfg, id); }
static bool ledger_ui_repost(lua_State *L) { return ledger_repost_all(g_ledger->ib, g_ledger->cfg); }
static bool ledger_ui_confirm(lua_State *L, const char *text, const char *title) { return message_box(g_ledger->main_win, text, title, MB_YESNO) == IDYES; }
static void ledger_ui_message(lua_State *L, const char *text, const char *title) { message_box(g_ledger->main_win, text, title, MB_OK); }
static void ledger_ui_save(lua_State *L) {
  window_t *document = mdi_active_document(view(ID_MAIN_WINDOW_DOCUMENTS_CLIENT));
  if (document) db_form_save(document, false);
  else { send_db_message(g_ledger->ib, dbSave, 0, NULL); ledger_update_status(); }
}
static void ledger_ui_form(lua_State *L, const char *name) {
  const form_def_t *form = !strcmp(name, "organization") ? &ledger_organization_form
                         : !strcmp(name, "report_settings") ? &ledger_report_settings_form : NULL;
  if (!form) { luaL_error(L, "unknown form '%s'", name); return; }
  if (show_db_dialog(form, NULL, g_ledger->main_win, 1)) after_save(0);
}
static void ledger_ui_catalog(lua_State *L, const char *name, const char *verb) {
  static const struct { const char *name; int id; } tables[] = {
    {"counterparty", ID_COUNTERPARTIES_PAGE_COUNTERPARTIES}, {"item", ID_ITEMS_PAGE_ITEMS},
    {"employee", ID_EMPLOYEES_PAGE_EMPLOYEES}, {"asset", ID_ASSETS_PAGE_ASSETS},
    {"currency", ID_CURRENCIES_PAGE_CURRENCIES}, {"rate", ID_CURRENCIES_PAGE_RATES}
  };
  for (size_t i = 0; i < ARRAY_LEN(tables); i++) if (!strcmp(name, tables[i].name)) {
    catalog_action(tables[i].id, !strcmp(verb, "new") ? tvInsertRecord : !strcmp(verb, "edit") ? tvEditRecord : tvDeleteRecord); return;
  }
  luaL_error(L, "unknown catalog '%s'", name);
}
static void ledger_ui_page(lua_State *L, const char *name) {
  if (!strcmp(name, "back") || !strcmp(name, "forward")) {
    int direction = !strcmp(name, "back") ? -1 : 1;
    if (page_host_can_go(g_ledger->main_win, direction)) page_host_go(g_ledger->main_win, direction);
  } else if (!strcmp(name, "journal_entries")) {
    page_host_select(g_ledger->main_win, ID_VIEW_JOURNAL);
    send_message(view(ID_JOURNAL_PAGE_DETAILS), tcSetSelection, 1, NULL);
  } else if (!strcmp(name, "sidebar")) page_host_set_sidebar_collapsed(g_ledger->main_win, !page_host_sidebar_collapsed(g_ledger->main_win));
  else {
    for (int i = 0; i < ledger_action_meta_count; i++) if (!strncmp(ledger_action_meta[i].name, "view.", 5) && !strcmp(ledger_action_meta[i].name + 5, name)) {
      page_host_select(g_ledger->main_win, ledger_action_meta[i].id); return;
    }
    luaL_error(L, "unknown page '%s'", name);
  }
}
static void ledger_ui_window(lua_State *L, const char *operation) {
  window_t *client = view(ID_MAIN_WINDOW_DOCUMENTS_CLIENT), *document = mdi_active_document(client);
  if (!strcmp(operation, "next_document")) mdi_cycle_document(client, 1);
  else if (!strcmp(operation, "previous_document")) mdi_cycle_document(client, -1);
  else if (!strcmp(operation, "cascade")) mdi_arrange_documents(client, false);
  else if (!strcmp(operation, "tile")) mdi_arrange_documents(client, true);
  else if (!strcmp(operation, "close_document") && document) send_message(document, evClose, 0, NULL);
  else if (!strcmp(operation, "quit")) send_message(g_ledger->main_win, evClose, 0, NULL);
  else if (!strcmp(operation, "post_close")) {
    db_document_t *edited = document ? db_dialog_record(document) : NULL;
    const db_doc_type_t *type = document_type(edited);
    window_t *button = type && !strcmp(type->form, "document") ? get_window_item(document, ID_DOCUMENT_POST_CLOSE) : NULL;
    if (button) send_message(document, evCommand, MAKEDWORD(button->id, btnClicked), button);
  } else if (strcmp(operation, "close_document")) luaL_error(L, "unknown window operation '%s'", operation);
}
static void ledger_ui_configuration(lua_State *L) { select_configuration(); }
static void ledger_ui_language(lua_State *L) { select_language(); }
static void ledger_ui_import(lua_State *L) { import_from_1c(); }
static void ledger_ui_addon(lua_State *L) { ledger_open_addon(); }
static void ledger_ui_assistant(lua_State *L) { ledger_open_assistant(); }
static void ledger_ui_account_card(lua_State *L) { show_account_card(); }
static void ledger_ui_print_reports(lua_State *L) {
  char report[2048]; bool ok = ledger_print_reports(report, sizeof(report));
  message_box(g_ledger->main_win, report[0] ? report : ui_tr("The configuration defines no report forms."), ui_tr(ok ? "Print Forms Written" : "Print"), MB_OK);
}
#include "build/generated/apps/ledger/controller_lua.h"

bool ledger_ui_open(const char *directory) {
  if (g_ledger->runtime) return true;
  script_register_module("ledger.ui", luaopen_ledger_ui);
  g_ledger->runtime = script_runtime_open(directory, "Ledger");
  if (!g_ledger->runtime) return false;
  char path[LEDGER_PATH_MAX * 2], error[2048];
  snprintf(path, sizeof(path), "%s/controller.lua", directory);
  g_ledger->controller = script_runtime_controller(g_ledger->runtime, path, error, sizeof(error));
  if (g_ledger->controller == LUA_NOREF) { fprintf(stderr, "[ledger] %s\n", error); ledger_ui_close(); return false; }
  return true;
}
void ledger_ui_close(void) {
  if (!g_ledger || !g_ledger->runtime) return;
  script_runtime_release(g_ledger->runtime, g_ledger->controller);
  script_runtime_close(g_ledger->runtime); g_ledger->runtime = NULL;
}
void ledger_command(uint16_t id) {
  if (!g_ledger) return;
  if (!g_ledger->runtime && !ledger_ui_open(g_ledger->share_dir)) return;
  const char *name = id == ID_HOME_PAGE_ALL_DOCUMENTS ? "view.journal" : NULL;
  for (int i = 0; !name && i < ledger_action_meta_count; i++) if (ledger_action_meta[i].id == id) name = ledger_action_meta[i].name;
  if (!name) return;
  char error[2048];  // the runtime logs script faults; refusals are answers for the user
  if (!script_runtime_invoke(g_ledger->runtime, g_ledger->controller, "command", name, error, sizeof(error)))
    message_box(g_ledger->main_win, error, ui_tr("Ledger"), MB_OK);
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
        else if (is_home_table(src->id)) home_activate(src);
        return true;
      }
      if (code == tvnChanged) { ledger_refresh_views(); return true; }
      // An operation, payroll or closing has no goods lines: show what it wrote
      // instead of an empty Lines tab.
      if (code == RVN_SELCHANGE && src && src->id == ID_JOURNAL_PAGE_DOCUMENTS) {
        const db_doc_type_t *type = document_type(selected(ID_JOURNAL_PAGE_DOCUMENTS));
        if (type && strcmp(type->form, "document")) send_message(view(ID_JOURNAL_PAGE_DETAILS), tcSetSelection, 1, NULL);
        return false;
      }
      if (code == LEDGER_HOME_SETTLE) { ledger_home_perform(LOWORD(wparam)); return true; }
      if (code == btnClicked || code == kMenuBarNotificationItemClick || code == kAcceleratorNotification || code == 0) {
        ledger_command(LOWORD(wparam));
        return true;
      }
      return false;
    }
    case evPageChanged:
      if (g_ledger->main_win == win) ledger_update_status();
      return true;
    case evMove:
    case evResize:
    case evDisplayChange:
      ledger_position_assistant();
      return false;
    case evClose:
      if (!mdi_close_all(view(ID_MAIN_WINDOW_DOCUMENTS_CLIENT))) return true;
      ui_request_quit();
      return true;
    case evDestroy:
      if (g_ledger && g_ledger->main_win == win) {
        ledger_close_assistant();
        g_ledger->main_win = NULL;
      }
      return false;
    default:
      return false;
  }
}

result_t ledger_menubar_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (msg == evClose && g_ledger && is_window(g_ledger->main_win))
    return send_message(g_ledger->main_win, evClose, 0, NULL);
  if (msg == evCommand && (HIWORD(wparam) == kMenuBarNotificationItemClick ||
                           HIWORD(wparam) == kAcceleratorNotification)) {
    ledger_command(LOWORD(wparam));
    return true;
  }
  return win_menubar(win, msg, wparam, lparam);
}

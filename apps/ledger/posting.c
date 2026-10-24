// Posting: turns documents into double-entry records using the posting rules
// of the active configuration (cfg.doc_types), and rebuilds the totals
// register (ib.balances). Pure data logic over the two databases.

#include "ledger.h"
#include <math.h>

static double round2(double v) { return v < 0 ? -floor(-v * 100.0 + 0.5) / 100.0 : floor(v * 100.0 + 0.5) / 100.0; }

// Table ids are numbered per database, so fields resolve through its schema.
int ledger_field(database_t *db, int table_id, const char *field) {
  int index = db_schema_field_index(db_schema_table(db->schema, table_id), field);
  if (index < 0) fprintf(stderr, "[ledger] unknown field db=%s table=%d field=%s\n", db->name, table_id, field);
  return index;
}

static result_node_t *fetch(database_t *db, int table_id, const char *field, int value) {
  int index = field ? ledger_field(db, table_id, field) : 0;
  return (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(table_id, index < 0 ? 0 : index),
                                          (void *)(intptr_t)value);
}

#define EACH_RECORD(type, var, list) \
  for (result_node_t *var##_node = (list); var##_node; var##_node = var##_node->next) \
    for (type *var = *(type **)var##_node->data; var; var = NULL)

static void *find(database_t *db, int table_id, int id) {
  return id ? (void *)send_db_message(db, dbFind, MAKEDWORD(table_id, 0), (void *)(intptr_t)id) : NULL;
}

static void delete_entries(database_t *ib, int doc_id) {
  result_node_t *entries = fetch(ib, TABLE_ENTRIES, "document_id", doc_id);
  EACH_RECORD(db_entry_t, e, entries) send_db_message(ib, dbDelete, TABLE_ENTRIES, (void *)(intptr_t)e->id);
  free_result_list(entries);
}

// Line amount = quantity x price; tax on top at the line's rate. Documents
// without lines keep their entered total.
void ledger_recalc_document(database_t *ib, database_t *cfg, int doc_id) {
  db_document_t *doc = find(ib, TABLE_DOCUMENTS, doc_id);
  if (!doc) return;
  result_node_t *lines = fetch(ib, TABLE_DOC_LINES, "document_id", doc_id);
  double amount = 0, vat = 0;
  int count = 0;
  EACH_RECORD(db_doc_line_t, line, lines) {
    db_item_t *item = find(ib, TABLE_ITEMS, line->item_id);
    if (item && line->price == 0) line->price = item->price;
    if (item && !line->vat_rate_id) line->vat_rate_id = item->vat_rate_id;
    db_vat_rate_t *rate = find(cfg, TABLE_VAT_RATES, line->vat_rate_id);
    line->amount = round2(line->quantity * line->price);
    line->vat = round2(line->amount * (rate ? rate->rate : 0) / 100.0);
    line->total = line->amount + line->vat;
    send_db_message(ib, dbUpdate, TABLE_DOC_LINES, line);
    amount += line->amount; vat += line->vat; count++;
  }
  free_result_list(lines);
  if (count) { doc->amount = amount; doc->vat = vat; doc->total = amount + vat; }
  else       { doc->amount = doc->total - doc->vat; }
  send_db_message(ib, dbUpdate, TABLE_DOCUMENTS, doc);
}

static void add_entry(database_t *ib, const db_document_t *doc, const db_doc_type_t *type,
                      int debit, int credit, double amount, const char *what) {
  if (!debit || !credit || amount == 0) return;
  db_entry_t e = { .document_id = doc->id, .debit_id = debit, .credit_id = credit, .amount = amount };
  snprintf(e.date, sizeof(e.date), "%s", doc->date);
  snprintf(e.content, sizeof(e.content), "%s %s%s", type->name, doc->number, what);
  send_db_message(ib, dbInsert, TABLE_ENTRIES, &e);
}

bool ledger_post(database_t *ib, database_t *cfg, int doc_id) {
  db_document_t *doc = find(ib, TABLE_DOCUMENTS, doc_id);
  db_doc_type_t *type = doc ? find(cfg, TABLE_DOC_TYPES, doc->doc_type_id) : NULL;
  if (!type) {
    fprintf(stderr, "[ledger] post rejected: document=%d has no document type in configuration\n", doc_id);
    return false;
  }
  delete_entries(ib, doc_id);
  double base = strcmp(type->base, "net") ? doc->total : doc->amount;
  add_entry(ib, doc, type, type->debit_id, type->credit_id, base, "");
  add_entry(ib, doc, type, type->vat_debit_id, type->vat_credit_id, doc->vat, ", tax");
  doc->posted = true;
  send_db_message(ib, dbUpdate, TABLE_DOCUMENTS, doc);
  return true;
}

void ledger_unpost(database_t *ib, int doc_id) {
  db_document_t *doc = find(ib, TABLE_DOCUMENTS, doc_id);
  if (!doc) return;
  delete_entries(ib, doc_id);
  doc->posted = false;
  send_db_message(ib, dbUpdate, TABLE_DOCUMENTS, doc);
}

void ledger_delete_document(database_t *ib, int doc_id) {
  delete_entries(ib, doc_id);
  result_node_t *lines = fetch(ib, TABLE_DOC_LINES, "document_id", doc_id);
  EACH_RECORD(db_doc_line_t, line, lines) send_db_message(ib, dbDelete, TABLE_DOC_LINES, (void *)(intptr_t)line->id);
  free_result_list(lines);
  send_db_message(ib, dbDelete, TABLE_DOCUMENTS, (void *)(intptr_t)doc_id);
}

// Numbers follow the configuration's prefix per document type: ПТ-0001, ...
void ledger_number_document(database_t *ib, database_t *cfg, int doc_id) {
  db_document_t *doc = find(ib, TABLE_DOCUMENTS, doc_id);
  db_doc_type_t *type = doc ? find(cfg, TABLE_DOC_TYPES, doc->doc_type_id) : NULL;
  if (!type || doc->number[0]) return;
  int last = 0;
  size_t n = strlen(type->prefix);
  result_node_t *docs = fetch(ib, TABLE_DOCUMENTS, "doc_type_id", doc->doc_type_id);
  EACH_RECORD(db_document_t, d, docs)
    if (!strncmp(d->number, type->prefix, n) && atoi(d->number + n) > last) last = atoi(d->number + n);
  free_result_list(docs);
  snprintf(doc->number, sizeof(doc->number), "%s%04d", type->prefix, last + 1);
  send_db_message(ib, dbUpdate, TABLE_DOCUMENTS, doc);
}

void ledger_repost_all(database_t *ib, database_t *cfg) {
  result_node_t *docs = fetch(ib, TABLE_DOCUMENTS, NULL, 0);
  EACH_RECORD(db_document_t, d, docs) {
    ledger_recalc_document(ib, cfg, d->id);
    if (d->posted) ledger_post(ib, cfg, d->id);
    else ledger_unpost(ib, d->id);
  }
  free_result_list(docs);
  ledger_recalc_balances(ib, cfg);
}

// 41.01 rolls up into 41.
static bool account_within(database_t *cfg, int account_id, const db_account_t *parent) {
  const db_account_t *a = find(cfg, TABLE_ACCOUNTS, account_id);
  size_t n = strlen(parent->code);
  return a && !strncmp(a->code, parent->code, n) && (a->code[n] == '\0' || a->code[n] == '.');
}

// One row per account with activity, in chart-of-accounts order.
void ledger_recalc_balances(database_t *ib, database_t *cfg) {
  result_node_t *old = fetch(ib, TABLE_BALANCES, NULL, 0);
  EACH_RECORD(db_balance_t, b, old) send_db_message(ib, dbDelete, TABLE_BALANCES, (void *)(intptr_t)b->id);
  free_result_list(old);
  result_node_t *accounts = fetch(cfg, TABLE_ACCOUNTS, NULL, 0);
  result_node_t *entries = fetch(ib, TABLE_ENTRIES, NULL, 0);
  EACH_RECORD(db_account_t, a, accounts) {
    db_balance_t row = { .account_id = a->id };
    EACH_RECORD(db_entry_t, e, entries) {
      if (account_within(cfg, e->debit_id, a))  row.debit_turnover += e->amount;
      if (account_within(cfg, e->credit_id, a)) row.credit_turnover += e->amount;
    }
    if (row.debit_turnover == 0 && row.credit_turnover == 0) continue;
    double saldo = round2(row.debit_turnover - row.credit_turnover);
    row.balance_dr = saldo > 0 ? saldo : 0;
    row.balance_cr = saldo < 0 ? -saldo : 0;
    send_db_message(ib, dbInsert, TABLE_BALANCES, &row);
  }
  free_result_list(entries);
  free_result_list(accounts);
}

int ledger_count_references(database_t *ib, int table_id, const char *field, int id) {
  result_node_t *rows = fetch(ib, table_id, field, id);
  int n = count_result_list(rows);
  free_result_list(rows);
  return n;
}

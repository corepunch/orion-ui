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

static void post_manual(database_t *ib, db_document_t *doc);

// Line amount = quantity x price; tax on top at the line's rate. Documents
// without lines keep their entered total.
void ledger_recalc_document(database_t *ib, database_t *cfg, int doc_id) {
  db_document_t *doc = find(ib, TABLE_DOCUMENTS, doc_id);
  if (!doc) return;
  db_doc_type_t *type = find(cfg, TABLE_DOC_TYPES, doc->doc_type_id);
  if (type && type->manual) { post_manual(ib, doc); send_db_message(ib, dbUpdate, TABLE_DOCUMENTS, doc); return; }
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

// The entry carries the analytics (subconto) that either account keeps.
static void add_entry(database_t *ib, database_t *cfg, const db_document_t *doc, const db_doc_type_t *type,
                      int debit, int credit, double amount, int item_id, double quantity) {
  if (!debit || !credit || amount == 0) return;
  const db_account_t *dr = find(cfg, TABLE_ACCOUNTS, debit), *cr = find(cfg, TABLE_ACCOUNTS, credit);
  bool by_counterparty = (dr && !strcmp(dr->subconto, "counterparty")) || (cr && !strcmp(cr->subconto, "counterparty"));
  bool by_item = (dr && !strcmp(dr->subconto, "item")) || (cr && !strcmp(cr->subconto, "item"));
  db_entry_t e = { .document_id = doc->id, .debit_id = debit, .credit_id = credit, .amount = amount,
                   .counterparty_id = by_counterparty ? doc->counterparty_id : 0,
                   .item_id = by_item ? item_id : 0, .quantity = by_item ? quantity : 0 };
  snprintf(e.date, sizeof(e.date), "%s", doc->date);
  snprintf(e.content, sizeof(e.content), "%s %s", type->name, doc->number);
  send_db_message(ib, dbInsert, TABLE_ENTRIES, &e);
}

// A manual operation's entries are the accountant's; posting only dates them
// and totals the operation.
static void post_manual(database_t *ib, db_document_t *doc) {
  double total = 0;
  result_node_t *entries = fetch(ib, TABLE_ENTRIES, "document_id", doc->id);
  EACH_RECORD(db_entry_t, e, entries) {
    snprintf(e->date, sizeof(e->date), "%s", doc->date);
    if (!e->content[0]) snprintf(e->content, sizeof(e->content), "%.*s", (int)sizeof(e->content) - 1, doc->comment);
    send_db_message(ib, dbUpdate, TABLE_ENTRIES, e);
    total += e->amount;
  }
  free_result_list(entries);
  doc->amount = doc->total = total;
  doc->vat = 0;
}

bool ledger_post(database_t *ib, database_t *cfg, int doc_id) {
  db_document_t *doc = find(ib, TABLE_DOCUMENTS, doc_id);
  db_doc_type_t *type = doc ? find(cfg, TABLE_DOC_TYPES, doc->doc_type_id) : NULL;
  if (!type) {
    fprintf(stderr, "[ledger] post rejected: document=%d has no document type in configuration\n", doc_id);
    return false;
  }
  if (type->manual) {
    post_manual(ib, doc);
  } else {
    delete_entries(ib, doc_id);
    bool net = !strcmp(type->base, "net");
    result_node_t *lines = fetch(ib, TABLE_DOC_LINES, "document_id", doc_id);
    EACH_RECORD(db_doc_line_t, line, lines) {  // one pair of entries per line, as goods move per item
      add_entry(ib, cfg, doc, type, type->debit_id, type->credit_id, net ? line->amount : line->total, line->item_id, line->quantity);
      add_entry(ib, cfg, doc, type, type->vat_debit_id, type->vat_credit_id, line->vat, line->item_id, line->quantity);
    }
    if (!lines) {
      add_entry(ib, cfg, doc, type, type->debit_id, type->credit_id, net ? doc->amount : doc->total, 0, 0);
      add_entry(ib, cfg, doc, type, type->vat_debit_id, type->vat_credit_id, doc->vat, 0, 0);
    }
    free_result_list(lines);
  }
  doc->posted = true;
  send_db_message(ib, dbUpdate, TABLE_DOCUMENTS, doc);
  return true;
}

// Unposting a manual operation keeps its entries; reports skip them.
void ledger_unpost(database_t *ib, database_t *cfg, int doc_id) {
  db_document_t *doc = find(ib, TABLE_DOCUMENTS, doc_id);
  if (!doc) return;
  db_doc_type_t *type = find(cfg, TABLE_DOC_TYPES, doc->doc_type_id);
  if (!type || !type->manual) delete_entries(ib, doc_id);
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
    else ledger_unpost(ib, cfg, d->id);
  }
  free_result_list(docs);
  ledger_build_reports(ib, cfg);
}

// 41.01 rolls up into 41.
static bool account_within(database_t *cfg, int account_id, const db_account_t *parent) {
  const db_account_t *a = find(cfg, TABLE_ACCOUNTS, account_id);
  size_t n = strlen(parent->code);
  return a && !strncmp(a->code, parent->code, n) && (a->code[n] == '\0' || a->code[n] == '.');
}

// ── Reports ────────────────────────────────────────────────────────────────

db_report_setting_t *ledger_report_settings(database_t *ib, database_t *cfg) {
  db_report_setting_t *rs = find(ib, TABLE_REPORT_SETTINGS, 1);
  if (rs) return rs;
  db_report_setting_t fresh = { .id = 1 };
  result_node_t *accounts = fetch(cfg, TABLE_ACCOUNTS, NULL, 0);
  EACH_RECORD(db_account_t, a, accounts)  // analyse the first settlement account by default
    if (!fresh.account_id && !strcmp(a->subconto, "counterparty")) fresh.account_id = a->id;
  free_result_list(accounts);
  return (db_report_setting_t *)send_db_message(ib, dbInsert, TABLE_REPORT_SETTINGS, &fresh);
}

static void clear_table(database_t *ib, int table_id) {
  result_node_t *rows = fetch(ib, table_id, NULL, 0);
  EACH_RECORD(int, key, rows) send_db_message(ib, dbDelete, (uint32_t)table_id, (void *)(intptr_t)*key);
  free_result_list(rows);
}

// Reports count entries of posted documents only.
static bool entry_active(database_t *ib, const db_entry_t *e) {
  const db_document_t *doc = find(ib, TABLE_DOCUMENTS, e->document_id);
  return doc && doc->posted;
}

typedef enum { BEFORE, WITHIN, AFTER } period_pos_t;

static period_pos_t in_period(const db_report_setting_t *rs, const char *date) {
  if (rs->date_from[0] && strcmp(date, rs->date_from) < 0) return BEFORE;
  if (rs->date_to[0] && strcmp(date, rs->date_to) > 0) return AFTER;
  return WITHIN;
}

typedef struct { double open_dr, open_cr, turn_dr, turn_cr; } totals_t;

static void add_totals(totals_t *t, period_pos_t pos, double dr, double cr) {
  if (pos == BEFORE) { t->open_dr += dr; t->open_cr += cr; }
  if (pos == WITHIN) { t->turn_dr += dr; t->turn_cr += cr; }
}

// Collapses each side to a net balance, as a trial balance shows it.
static void split_saldo(double dr, double cr, double *out_dr, double *out_cr) {
  double saldo = round2(dr - cr);
  *out_dr = saldo > 0 ? saldo : 0;
  *out_cr = saldo < 0 ? -saldo : 0;
}

static void build_trial_balance(database_t *ib, database_t *cfg, const db_report_setting_t *rs, result_node_t *entries) {
  result_node_t *accounts = fetch(cfg, TABLE_ACCOUNTS, NULL, 0);
  EACH_RECORD(db_account_t, a, accounts) {
    totals_t t = {0};
    EACH_RECORD(db_entry_t, e, entries) {
      period_pos_t pos = in_period(rs, e->date);
      add_totals(&t, pos, account_within(cfg, e->debit_id, a) ? e->amount : 0, account_within(cfg, e->credit_id, a) ? e->amount : 0);
    }
    if (!t.open_dr && !t.open_cr && !t.turn_dr && !t.turn_cr) continue;
    db_balance_t row = { .account_id = a->id, .debit_turnover = t.turn_dr, .credit_turnover = t.turn_cr };
    split_saldo(t.open_dr, t.open_cr, &row.opening_dr, &row.opening_cr);
    split_saldo(t.open_dr + t.turn_dr, t.open_cr + t.turn_cr, &row.balance_dr, &row.balance_cr);
    send_db_message(ib, dbInsert, TABLE_BALANCES, &row);
  }
  free_result_list(accounts);
}

static int by_date(const void *a, const void *b) {
  const db_entry_t *x = *(const db_entry_t * const *)a, *y = *(const db_entry_t * const *)b;
  int c = strcmp(x->date, y->date);
  return c ? c : x->id - y->id;
}

// Account card: every movement of the chosen account with a running balance.
static void build_card(database_t *ib, database_t *cfg, const db_report_setting_t *rs, result_node_t *entries) {
  const db_account_t *account = find(cfg, TABLE_ACCOUNTS, rs->account_id);
  int n = count_result_list(entries), count = 0;
  if (!account || !n) return;
  db_entry_t **sorted = malloc((size_t)n * sizeof(*sorted));
  if (!sorted) return;
  EACH_RECORD(db_entry_t, e, entries) sorted[count++] = e;
  qsort(sorted, (size_t)count, sizeof(*sorted), by_date);
  double balance = 0;
  for (int i = 0; i < count; i++) {
    const db_entry_t *e = sorted[i];
    bool dr = account_within(cfg, e->debit_id, account), cr = account_within(cfg, e->credit_id, account);
    if (!dr && !cr) continue;
    period_pos_t pos = in_period(rs, e->date);
    balance += (dr ? e->amount : 0) - (cr ? e->amount : 0);
    if (pos != WITHIN) continue;
    db_card_t row = { .document_id = e->document_id, .corr_id = dr ? e->credit_id : e->debit_id,
                      .counterparty_id = e->counterparty_id, .debit = dr ? e->amount : 0,
                      .credit = cr ? e->amount : 0, .balance = round2(balance) };
    snprintf(row.date, sizeof(row.date), "%s", e->date);
    snprintf(row.content, sizeof(row.content), "%s", e->content);
    send_db_message(ib, dbInsert, TABLE_CARD, &row);
  }
  free(sorted);
}

// Account analysis: the chosen account's balance per counterparty.
static void build_analysis(database_t *ib, database_t *cfg, const db_report_setting_t *rs, result_node_t *entries) {
  const db_account_t *account = find(cfg, TABLE_ACCOUNTS, rs->account_id);
  if (!account) return;
  result_node_t *parties = fetch(ib, TABLE_COUNTERPARTIES, NULL, 0);
  EACH_RECORD(db_counterparty_t, cp, parties) {
    totals_t t = {0};
    EACH_RECORD(db_entry_t, e, entries) {
      if (e->counterparty_id != cp->id) continue;
      add_totals(&t, in_period(rs, e->date), account_within(cfg, e->debit_id, account) ? e->amount : 0,
                 account_within(cfg, e->credit_id, account) ? e->amount : 0);
    }
    if (!t.open_dr && !t.open_cr && !t.turn_dr && !t.turn_cr) continue;
    db_analysis_row_t row = { .counterparty_id = cp->id, .debit_turnover = t.turn_dr, .credit_turnover = t.turn_cr };
    split_saldo(t.open_dr, t.open_cr, &row.opening_dr, &row.opening_cr);
    split_saldo(t.open_dr + t.turn_dr, t.open_cr + t.turn_cr, &row.balance_dr, &row.balance_cr);
    send_db_message(ib, dbInsert, TABLE_ANALYSIS_ROWS, &row);
  }
  free_result_list(parties);
}

// Sales and purchase books list the posted documents their type assigns them.
static void build_vat_books(database_t *ib, database_t *cfg, const db_report_setting_t *rs) {
  result_node_t *docs = fetch(ib, TABLE_DOCUMENTS, NULL, 0);
  EACH_RECORD(db_document_t, d, docs) {
    const db_doc_type_t *type = find(cfg, TABLE_DOC_TYPES, d->doc_type_id);
    if (!d->posted || !type || !type->vat_book || in_period(rs, d->date) != WITHIN) continue;
    db_vat_book_t row = { .document_id = d->id, .counterparty_id = d->counterparty_id, .book = type->vat_book,
                          .total = d->total, .amount = d->amount, .vat = d->vat };
    snprintf(row.date, sizeof(row.date), "%s", d->date);
    send_db_message(ib, dbInsert, TABLE_VAT_BOOK, &row);
  }
  free_result_list(docs);
}

void ledger_build_reports(database_t *ib, database_t *cfg) {
  const int tables[] = { TABLE_BALANCES, TABLE_CARD, TABLE_ANALYSIS_ROWS, TABLE_VAT_BOOK };
  for (size_t i = 0; i < ARRAY_LEN(tables); i++) clear_table(ib, tables[i]);
  const db_report_setting_t *rs = ledger_report_settings(ib, cfg);
  if (!rs) return;
  result_node_t *all = fetch(ib, TABLE_ENTRIES, NULL, 0), *active = NULL, **tail = &active;
  for (result_node_t *n = all, *next; n; n = next) {  // keep posted documents' entries
    next = n->next;
    if (entry_active(ib, *(db_entry_t **)n->data)) { *tail = n; tail = (result_node_t **)&n->next; n->next = NULL; }
    else free(n);
  }
  build_trial_balance(ib, cfg, rs, active);
  build_card(ib, cfg, rs, active);
  build_analysis(ib, cfg, rs, active);
  build_vat_books(ib, cfg, rs);
  free_result_list(active);
}

int ledger_count_references(database_t *ib, int table_id, const char *field, int id) {
  result_node_t *rows = fetch(ib, table_id, field, id);
  int n = count_result_list(rows);
  free_result_list(rows);
  return n;
}

// Posting: turns documents into double-entry records using the rules of the
// active configuration (cfg.doc_types, payroll_taxes, closing_rules), and
// builds the reports. Pure data logic over the cfg and ib databases.

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

static db_setting_t *settings(database_t *cfg) { return find(cfg, TABLE_SETTINGS, 1); }

static bool form_is(const db_doc_type_t *type, const char *form) { return type && !strcmp(type->form, form); }

// 41.01 rolls up into 41.
static bool account_within(database_t *cfg, int account_id, int parent_id) {
  if (account_id == parent_id) return account_id != 0;
  const db_account_t *a = find(cfg, TABLE_ACCOUNTS, account_id), *parent = find(cfg, TABLE_ACCOUNTS, parent_id);
  if (!a || !parent) return false;
  size_t n = strlen(parent->code);
  return !strncmp(a->code, parent->code, n) && (a->code[n] == '\0' || a->code[n] == '.');
}

// ── Analytics ──────────────────────────────────────────────────────────────

// The catalog an account is analysed by ("counterparties"), or "".
const char *ledger_subconto_table(database_t *cfg, int account_id) {
  const db_account_t *a = find(cfg, TABLE_ACCOUNTS, account_id);
  const db_subconto_kind_t *kind = a ? find(cfg, TABLE_SUBCONTO_KINDS, a->subconto_kind_id) : NULL;
  return kind ? kind->table : "";
}

// What a posting is about; each side of an entry takes the object its
// account is analysed by.
typedef struct { int counterparty, item, employee, asset; } subjects_t;

static int subconto_for(database_t *cfg, int account_id, const subjects_t *s) {
  const char *table = ledger_subconto_table(cfg, account_id);
  if (!s) return 0;
  if (!strcmp(table, "counterparties")) return s->counterparty;
  if (!strcmp(table, "items"))          return s->item;
  if (!strcmp(table, "employees"))      return s->employee;
  if (!strcmp(table, "assets"))         return s->asset;
  return 0;
}

static subjects_t subjects_of(database_t *cfg, int account_id, int subconto_id) {
  subjects_t s = {0};
  const char *table = ledger_subconto_table(cfg, account_id);
  if (!strcmp(table, "counterparties")) s.counterparty = subconto_id;
  if (!strcmp(table, "items"))          s.item = subconto_id;
  if (!strcmp(table, "employees"))      s.employee = subconto_id;
  if (!strcmp(table, "assets"))         s.asset = subconto_id;
  return s;
}

// ── Currencies ─────────────────────────────────────────────────────────────

// The latest rate on or before date; 0 when none is known.
static double rate_on(database_t *ib, int currency_id, const char *date) {
  double rate = 0;
  char best[16] = "";
  result_node_t *rates = fetch(ib, TABLE_RATES, "currency_id", currency_id);
  EACH_RECORD(db_rate_t, r, rates)
    if (strcmp(r->date, date) <= 0 && strcmp(r->date, best) >= 0) { rate = r->rate; snprintf(best, sizeof(best), "%s", r->date); }
  free_result_list(rates);
  return rate;
}

// ── Posting context and turnovers ──────────────────────────────────────────

typedef struct {
  database_t *ib, *cfg;
  db_document_t *doc;
  const db_doc_type_t *type;
  double rate;  // national units per document currency unit
} post_t;

// Documents post in date order; on one date month-end closing comes last.
static int doc_order(database_t *cfg, const db_document_t *a, const db_document_t *b) {
  int c = strcmp(a->date, b->date);
  if (c) return c;
  bool ca = form_is(find(cfg, TABLE_DOC_TYPES, a->doc_type_id), "closing");
  bool cb = form_is(find(cfg, TABLE_DOC_TYPES, b->doc_type_id), "closing");
  if (ca != cb) return ca ? 1 : -1;
  return a->id - b->id;
}

typedef struct { double dr, cr, dr_qty, cr_qty, dr_cur, cr_cur; } turnover_t;

// Turnovers of account (with subaccounts) on what precedes the document being
// posted, plus its own entries so far. subconto and currency filter when >= 0.
static turnover_t turnover(const post_t *p, int account_id, int subconto_id, int currency_id) {
  turnover_t t = {0};
  result_node_t *entries = fetch(p->ib, TABLE_ENTRIES, NULL, 0);
  EACH_RECORD(db_entry_t, e, entries) {
    if (currency_id >= 0 && e->currency_id != currency_id) continue;
    if (e->document_id != p->doc->id) {
      const db_document_t *d = find(p->ib, TABLE_DOCUMENTS, e->document_id);
      if (!d || !d->posted || doc_order(p->cfg, d, p->doc) >= 0) continue;
    }
    if (account_within(p->cfg, e->debit_id, account_id) && (subconto_id < 0 || e->debit_subconto_id == subconto_id)) {
      t.dr += e->amount; t.dr_qty += e->quantity; t.dr_cur += e->currency_amount;
    }
    if (account_within(p->cfg, e->credit_id, account_id) && (subconto_id < 0 || e->credit_subconto_id == subconto_id)) {
      t.cr += e->amount; t.cr_qty += e->quantity; t.cr_cur += e->currency_amount;
    }
  }
  free_result_list(entries);
  return t;
}

static void add_entry(const post_t *p, int debit, int credit, double amount, const subjects_t *s, double quantity,
                      int currency_id, double currency_amount, const char *content) {
  amount = round2(amount);
  if (!debit || !credit || amount == 0) return;
  bool by_item = !strcmp(ledger_subconto_table(p->cfg, debit), "items") || !strcmp(ledger_subconto_table(p->cfg, credit), "items");
  db_entry_t e = { .document_id = p->doc->id, .debit_id = debit, .credit_id = credit, .amount = amount,
                   .debit_subconto_id = subconto_for(p->cfg, debit, s), .credit_subconto_id = subconto_for(p->cfg, credit, s),
                   .quantity = by_item ? quantity : 0, .currency_id = currency_id, .currency_amount = currency_amount };
  snprintf(e.date, sizeof(e.date), "%s", p->doc->date);
  if (content) snprintf(e.content, sizeof(e.content), "%s", content);
  else snprintf(e.content, sizeof(e.content), "%s %s", p->type->name, p->doc->number);
  send_db_message(p->ib, dbInsert, TABLE_ENTRIES, &e);
}

// An amount in the document's currency, kept in both currencies.
static void add_doc_entry(const post_t *p, int debit, int credit, double amount, const subjects_t *s, double quantity) {
  if (p->doc->currency_id) add_entry(p, debit, credit, amount * p->rate, s, quantity, p->doc->currency_id, amount, NULL);
  else add_entry(p, debit, credit, amount, s, quantity, 0, 0, NULL);
}

static void delete_entries(database_t *ib, int doc_id) {
  result_node_t *entries = fetch(ib, TABLE_ENTRIES, "document_id", doc_id);
  EACH_RECORD(db_entry_t, e, entries) send_db_message(ib, dbDelete, TABLE_ENTRIES, (void *)(intptr_t)e->id);
  free_result_list(entries);
}

static void delete_rows(database_t *ib, int table_id, int doc_id) {
  result_node_t *rows = fetch(ib, table_id, "document_id", doc_id);
  EACH_RECORD(int, key, rows) send_db_message(ib, dbDelete, (uint32_t)table_id, (void *)(intptr_t)*key);
  free_result_list(rows);
}

// ── Recalculation ──────────────────────────────────────────────────────────

// Payroll taxes of one accrual line: withheld from the employee, and the
// employer's contributions on top.
static void payroll_taxes(database_t *ib, database_t *cfg, const db_payroll_line_t *line, double *withheld, double *contributions) {
  const db_employee_t *emp = find(ib, TABLE_EMPLOYEES, line->employee_id);
  *withheld = *contributions = 0;
  result_node_t *taxes = fetch(cfg, TABLE_PAYROLL_TAXES, NULL, 0);
  EACH_RECORD(db_payroll_tax_t, tax, taxes) {
    double base = line->amount - (tax->allowance && emp ? emp->allowance : 0);
    double amount = round2((base > 0 ? base : 0) * tax->rate / 100.0);
    if (tax->withheld) *withheld += amount; else *contributions += amount;
  }
  free_result_list(taxes);
}

static void sum_entries(database_t *ib, db_document_t *doc) {
  double total = 0;
  result_node_t *entries = fetch(ib, TABLE_ENTRIES, "document_id", doc->id);
  EACH_RECORD(db_entry_t, e, entries) total += e->amount;
  free_result_list(entries);
  doc->amount = doc->total = round2(total);
  doc->vat = 0;
}

// A manual operation's entries are the accountant's; posting only dates them
// and totals the operation.
static void date_manual_entries(database_t *ib, db_document_t *doc) {
  result_node_t *entries = fetch(ib, TABLE_ENTRIES, "document_id", doc->id);
  EACH_RECORD(db_entry_t, e, entries) {
    snprintf(e->date, sizeof(e->date), "%s", doc->date);
    if (!e->content[0]) snprintf(e->content, sizeof(e->content), "%.*s", (int)sizeof(e->content) - 1, doc->comment);
    send_db_message(ib, dbUpdate, TABLE_ENTRIES, e);
  }
  free_result_list(entries);
  sum_entries(ib, doc);
}

static void recalc_goods(database_t *ib, database_t *cfg, db_document_t *doc) {
  result_node_t *lines = fetch(ib, TABLE_DOC_LINES, "document_id", doc->id);
  double amount = 0, vat = 0;
  int count = 0;
  EACH_RECORD(db_doc_line_t, line, lines) {  // amount = quantity x price; tax on top at the line's rate
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
  else       { doc->amount = doc->total - doc->vat; }  // documents without lines keep their entered total
}

static void recalc_payroll(database_t *ib, database_t *cfg, db_document_t *doc, const db_doc_type_t *type) {
  bool payout = !strcmp(type->base, "net");
  double total = 0;
  result_node_t *lines = fetch(ib, TABLE_PAYROLL_LINES, "document_id", doc->id);
  EACH_RECORD(db_payroll_line_t, line, lines) {
    if (payout) line->withheld = line->contributions = 0;
    else payroll_taxes(ib, cfg, line, &line->withheld, &line->contributions);
    line->net = round2(line->amount - line->withheld);
    send_db_message(ib, dbUpdate, TABLE_PAYROLL_LINES, line);
    total += line->amount;
  }
  free_result_list(lines);
  doc->amount = doc->total = round2(total);
  doc->vat = 0;
}

static void recalc_assets(database_t *ib, db_document_t *doc) {
  double total = 0;
  result_node_t *lines = fetch(ib, TABLE_ASSET_LINES, "document_id", doc->id);
  EACH_RECORD(db_asset_line_t, line, lines) {
    const db_asset_t *asset = find(ib, TABLE_ASSETS, line->asset_id);
    line->amount = asset ? asset->cost : 0;
    send_db_message(ib, dbUpdate, TABLE_ASSET_LINES, line);
    total += line->amount;
  }
  free_result_list(lines);
  doc->amount = doc->total = round2(total);
  doc->vat = 0;
}

void ledger_recalc_document(database_t *ib, database_t *cfg, int doc_id) {
  db_document_t *doc = find(ib, TABLE_DOCUMENTS, doc_id);
  if (!doc) return;
  db_doc_type_t *type = find(cfg, TABLE_DOC_TYPES, doc->doc_type_id);
  if (doc->currency_id && doc->rate == 0) doc->rate = rate_on(ib, doc->currency_id, doc->date);
  if (!doc->currency_id) doc->rate = 0;
  if      (form_is(type, "operation")) date_manual_entries(ib, doc);
  else if (form_is(type, "payroll"))   recalc_payroll(ib, cfg, doc, type);
  else if (form_is(type, "assets"))    recalc_assets(ib, doc);
  else if (form_is(type, "closing"))   sum_entries(ib, doc);
  else                                 recalc_goods(ib, cfg, doc);
  send_db_message(ib, dbUpdate, TABLE_DOCUMENTS, doc);
}

// ── Posting by form ────────────────────────────────────────────────────────

// Goods leave stock at their average cost, as desktop suites of the time wrote
// them off when the sale was posted.
static void post_cost_of_sales(const post_t *p, const db_doc_line_t *line) {
  turnover_t t = turnover(p, p->type->cost_credit_id, line->item_id, -1);
  double qty = t.dr_qty - t.cr_qty, value = t.dr - t.cr;
  if (qty <= 0) {
    fprintf(stderr, "[ledger] cost of sales skipped: document=%s item=%d has no stock on the date\n", p->doc->number, line->item_id);
    return;
  }
  double cost = line->quantity >= qty ? value : value * line->quantity / qty;
  subjects_t s = { .counterparty = p->doc->counterparty_id, .item = line->item_id };
  add_entry(p, p->type->cost_debit_id, p->type->cost_credit_id, cost, &s, line->quantity, 0, 0, NULL);
}

static void post_goods(const post_t *p) {
  const db_doc_type_t *type = p->type;
  bool net = !strcmp(type->base, "net");
  result_node_t *lines = fetch(p->ib, TABLE_DOC_LINES, "document_id", p->doc->id);
  EACH_RECORD(db_doc_line_t, line, lines) {  // one set of entries per line, as goods move per item
    subjects_t s = { .counterparty = p->doc->counterparty_id, .item = line->item_id };
    add_doc_entry(p, type->debit_id, type->credit_id, net ? line->amount : line->total, &s, line->quantity);
    add_doc_entry(p, type->vat_debit_id, type->vat_credit_id, line->vat, &s, line->quantity);
    if (type->cost_debit_id && type->cost_credit_id) post_cost_of_sales(p, line);
  }
  if (!lines) {
    subjects_t s = { .counterparty = p->doc->counterparty_id };
    add_doc_entry(p, type->debit_id, type->credit_id, net ? p->doc->amount : p->doc->total, &s, 0);
    add_doc_entry(p, type->vat_debit_id, type->vat_credit_id, p->doc->vat, &s, 0);
  }
  free_result_list(lines);
}

static void post_payroll(const post_t *p) {
  bool payout = !strcmp(p->type->base, "net");
  result_node_t *lines = fetch(p->ib, TABLE_PAYROLL_LINES, "document_id", p->doc->id);
  result_node_t *taxes = payout ? NULL : fetch(p->cfg, TABLE_PAYROLL_TAXES, NULL, 0);
  EACH_RECORD(db_payroll_line_t, line, lines) {
    const db_employee_t *emp = find(p->ib, TABLE_EMPLOYEES, line->employee_id);
    subjects_t s = { .employee = line->employee_id };
    if (payout) { add_entry(p, p->type->debit_id, p->type->credit_id, line->amount, &s, 0, 0, 0, NULL); continue; }
    int expense = emp && emp->account_id ? emp->account_id : p->type->debit_id;
    add_entry(p, expense, p->type->credit_id, line->amount, &s, 0, 0, 0, NULL);
    EACH_RECORD(db_payroll_tax_t, tax, taxes) {
      double base = line->amount - (tax->allowance && emp ? emp->allowance : 0);
      char content[160];
      snprintf(content, sizeof(content), "%.60s: %.96s", tax->name, emp ? emp->name : "");
      add_entry(p, tax->debit_id ? tax->debit_id : expense, tax->credit_id,
                (base > 0 ? base : 0) * tax->rate / 100.0, &s, 0, 0, 0, content);
    }
  }
  free_result_list(taxes);
  free_result_list(lines);
}

static void post_assets(const post_t *p) {
  result_node_t *lines = fetch(p->ib, TABLE_ASSET_LINES, "document_id", p->doc->id);
  EACH_RECORD(db_asset_line_t, line, lines) {
    db_asset_t *asset = find(p->ib, TABLE_ASSETS, line->asset_id);
    if (!asset) continue;
    subjects_t s = { .asset = asset->id };
    add_entry(p, asset->account_id, p->type->credit_id, asset->cost, &s, 0, 0, 0, NULL);
    snprintf(asset->commissioned, sizeof(asset->commissioned), "%s", p->doc->date);
    send_db_message(p->ib, dbUpdate, TABLE_ASSETS, asset);
  }
  free_result_list(lines);
}

// Straight-line, from the month after the asset was put into service (or that
// month, as the rule says) until its cost is written off.
static void close_depreciation(const post_t *p, const db_closing_rule_t *rule) {
  result_node_t *assets = fetch(p->ib, TABLE_ASSETS, NULL, 0);
  EACH_RECORD(db_asset_t, a, assets) {
    int month = a->commissioned[0] ? strncmp(a->commissioned, p->doc->date, 7) : 1;
    if (a->life <= 0 || month > 0 || (month == 0 && strcmp(rule->start, "same"))) continue;
    double written_off = turnover(p, a->depreciation_id, a->id, -1).cr;
    double charge = fmin(round2(a->cost / a->life), round2(a->cost - written_off));
    char content[160];
    snprintf(content, sizeof(content), "%.60s: %.96s", rule->name, a->name);
    subjects_t s = { .asset = a->id };
    if (charge > 0) add_entry(p, a->expense_id, a->depreciation_id, charge, &s, 0, 0, 0, content);
  }
  free_result_list(assets);
}

static void close_transfer(const post_t *p, const db_closing_rule_t *rule) {
  turnover_t t = turnover(p, rule->account_id, -1, -1);
  double balance = round2(t.dr - t.cr);
  int via = rule->via_id ? rule->via_id : rule->account_id;
  if (balance > 0) add_entry(p, rule->target_id, via, balance, NULL, 0, 0, 0, rule->name);
  if (balance < 0) add_entry(p, via, rule->target_id, -balance, NULL, 0, 0, 0, rule->name);
}

// Currency accounts are restated at the closing date's rate, per currency and
// per analytics object.
static void close_revaluation(const post_t *p, const db_closing_rule_t *rule) {
  typedef struct { int account, currency, subconto; } reval_key_t;
  reval_key_t keys[256];
  int count = 0;
  result_node_t *entries = fetch(p->ib, TABLE_ENTRIES, NULL, 0);
  EACH_RECORD(db_entry_t, e, entries) {
    for (int side = 0; side < 2 && e->currency_id; side++) {
      int account = side ? e->credit_id : e->debit_id, subconto = side ? e->credit_subconto_id : e->debit_subconto_id;
      const db_account_t *a = find(p->cfg, TABLE_ACCOUNTS, account);
      if (!a || !a->currency) continue;
      bool known = false;
      for (int i = 0; i < count; i++)
        known |= keys[i].account == account && keys[i].currency == e->currency_id && keys[i].subconto == subconto;
      if (!known && count < (int)ARRAY_LEN(keys)) keys[count++] = (reval_key_t){ account, e->currency_id, subconto };
    }
  }
  free_result_list(entries);
  for (int i = 0; i < count; i++) {
    double rate = rate_on(p->ib, keys[i].currency, p->doc->date);
    if (rate <= 0) {
      fprintf(stderr, "[ledger] revaluation skipped: currency=%d has no rate on %s\n", keys[i].currency, p->doc->date);
      continue;
    }
    turnover_t t = turnover(p, keys[i].account, keys[i].subconto, keys[i].currency);
    double diff = round2((t.dr_cur - t.cr_cur) * rate - (t.dr - t.cr));
    subjects_t s = subjects_of(p->cfg, keys[i].account, keys[i].subconto);
    if (diff > 0) add_entry(p, keys[i].account, rule->gain_id, diff, &s, 0, keys[i].currency, 0, rule->name);
    if (diff < 0) add_entry(p, rule->loss_id, keys[i].account, -diff, &s, 0, keys[i].currency, 0, rule->name);
  }
}

static void post_closing(const post_t *p) {
  result_node_t *rules = fetch(p->cfg, TABLE_CLOSING_RULES, NULL, 0);
  EACH_RECORD(db_closing_rule_t, rule, rules) {
    if      (!strcmp(rule->kind, "depreciation")) close_depreciation(p, rule);
    else if (!strcmp(rule->kind, "transfer"))     close_transfer(p, rule);
    else if (!strcmp(rule->kind, "revaluation"))  close_revaluation(p, rule);
    else fprintf(stderr, "[ledger] closing rule %d has unknown kind '%s'\n", rule->id, rule->kind);
  }
  free_result_list(rules);
  sum_entries(p->ib, p->doc);
}

bool ledger_post(database_t *ib, database_t *cfg, int doc_id) {
  db_document_t *doc = find(ib, TABLE_DOCUMENTS, doc_id);
  db_doc_type_t *type = doc ? find(cfg, TABLE_DOC_TYPES, doc->doc_type_id) : NULL;
  if (!type) {
    fprintf(stderr, "[ledger] post rejected: document=%d has no document type in configuration\n", doc_id);
    return false;
  }
  post_t p = { ib, cfg, doc, type, doc->currency_id && doc->rate > 0 ? doc->rate : 1 };
  if (form_is(type, "operation")) date_manual_entries(ib, doc);
  else {
    delete_entries(ib, doc_id);
    if      (form_is(type, "document")) post_goods(&p);
    else if (form_is(type, "payroll"))  post_payroll(&p);
    else if (form_is(type, "assets"))   post_assets(&p);
    else if (form_is(type, "closing"))  post_closing(&p);
    else {
      fprintf(stderr, "[ledger] post rejected: document=%d type=%d has unknown form '%s'\n", doc_id, type->id, type->form);
      return false;
    }
  }
  doc->posted = true;
  send_db_message(ib, dbUpdate, TABLE_DOCUMENTS, doc);
  return true;
}

// Unposting a manual operation keeps its entries; reports skip them.
void ledger_unpost(database_t *ib, database_t *cfg, int doc_id) {
  db_document_t *doc = find(ib, TABLE_DOCUMENTS, doc_id);
  if (!doc) return;
  if (!form_is(find(cfg, TABLE_DOC_TYPES, doc->doc_type_id), "operation")) delete_entries(ib, doc_id);
  doc->posted = false;
  send_db_message(ib, dbUpdate, TABLE_DOCUMENTS, doc);
}

void ledger_delete_document(database_t *ib, int doc_id) {
  delete_entries(ib, doc_id);
  delete_rows(ib, TABLE_DOC_LINES, doc_id);
  delete_rows(ib, TABLE_PAYROLL_LINES, doc_id);
  delete_rows(ib, TABLE_ASSET_LINES, doc_id);
  send_db_message(ib, dbDelete, TABLE_DOCUMENTS, (void *)(intptr_t)doc_id);
}

// Numbers follow the configuration's prefix per document type: ПТ-0001, ...
void ledger_number_document(database_t *ib, database_t *cfg, int doc_id) {
  db_document_t *doc = find(ib, TABLE_DOCUMENTS, doc_id);
  db_doc_type_t *type = doc ? find(cfg, TABLE_DOC_TYPES, doc->doc_type_id) : NULL;
  if (!type || doc->number[0]) return;
  int last = 0;
  size_t n = strlen(type->prefix);
  result_node_t *docs = fetch(ib, TABLE_DOCUMENTS, NULL, 0);  // types may share a prefix: ПКО-
  EACH_RECORD(db_document_t, d, docs)
    if (!strncmp(d->number, type->prefix, n) && atoi(d->number + n) > last) last = atoi(d->number + n);
  free_result_list(docs);
  snprintf(doc->number, sizeof(doc->number), "%s%04d", type->prefix, last + 1);
  send_db_message(ib, dbUpdate, TABLE_DOCUMENTS, doc);
}

// Fills a payroll document: an accrual with every employee's salary, a
// payout with what each employee is owed on the payroll account.
void ledger_fill_payroll(database_t *ib, database_t *cfg, int doc_id) {
  db_document_t *doc = find(ib, TABLE_DOCUMENTS, doc_id);
  db_doc_type_t *type = doc ? find(cfg, TABLE_DOC_TYPES, doc->doc_type_id) : NULL;
  if (!form_is(type, "payroll")) return;
  bool payout = !strcmp(type->base, "net");
  delete_rows(ib, TABLE_PAYROLL_LINES, doc_id);
  post_t p = { ib, cfg, doc, type, 1 };
  result_node_t *employees = fetch(ib, TABLE_EMPLOYEES, NULL, 0);
  EACH_RECORD(db_employee_t, emp, employees) {
    double amount = emp->salary;
    if (payout) { turnover_t t = turnover(&p, type->debit_id, emp->id, -1); amount = round2(t.cr - t.dr); }
    if (amount <= 0) continue;
    db_payroll_line_t line = { .document_id = doc_id, .employee_id = emp->id, .amount = amount };
    send_db_message(ib, dbInsert, TABLE_PAYROLL_LINES, &line);
  }
  free_result_list(employees);
  ledger_recalc_document(ib, cfg, doc_id);
}

static database_t *g_sort_cfg;
static int by_posting_order(const void *a, const void *b) {
  return doc_order(g_sort_cfg, *(db_document_t * const *)a, *(db_document_t * const *)b);
}

void ledger_repost_all(database_t *ib, database_t *cfg) {
  result_node_t *docs = fetch(ib, TABLE_DOCUMENTS, NULL, 0);
  int n = count_result_list(docs), count = 0;
  db_document_t **order = n ? malloc((size_t)n * sizeof(*order)) : NULL;
  EACH_RECORD(db_document_t, d, docs) if (order) order[count++] = d;
  free_result_list(docs);
  g_sort_cfg = cfg;
  if (order) qsort(order, (size_t)count, sizeof(*order), by_posting_order);
  for (int i = 0; i < count; i++) {
    int id = order[i]->id;
    ledger_recalc_document(ib, cfg, id);
    if (order[i]->posted) ledger_post(ib, cfg, id);
    else ledger_unpost(ib, cfg, id);
  }
  free(order);
  ledger_build_reports(ib, cfg);
}

// ── Reports ────────────────────────────────────────────────────────────────

db_report_setting_t *ledger_report_settings(database_t *ib, database_t *cfg) {
  db_report_setting_t *rs = find(ib, TABLE_REPORT_SETTINGS, 1);
  if (rs) return rs;
  db_report_setting_t fresh = { .id = 1 };
  result_node_t *accounts = fetch(cfg, TABLE_ACCOUNTS, NULL, 0);
  EACH_RECORD(db_account_t, a, accounts)  // analyse the first account with analytics by default
    if (!fresh.account_id && a->subconto_kind_id) fresh.account_id = a->id;
  free_result_list(accounts);
  return (db_report_setting_t *)send_db_message(ib, dbInsert, TABLE_REPORT_SETTINGS, &fresh);
}

static void clear_table(database_t *ib, int table_id) {
  result_node_t *rows = fetch(ib, table_id, NULL, 0);
  EACH_RECORD(int, key, rows) send_db_message(ib, dbDelete, (uint32_t)table_id, (void *)(intptr_t)*key);
  free_result_list(rows);
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

static bool totals_empty(const totals_t *t) { return !t->open_dr && !t->open_cr && !t->turn_dr && !t->turn_cr; }

// Collapses each side to a net balance, as a trial balance shows it.
static void split_saldo(double dr, double cr, double *out_dr, double *out_cr) {
  double saldo = round2(dr - cr);
  *out_dr = saldo > 0 ? saldo : 0;
  *out_cr = saldo < 0 ? -saldo : 0;
}

typedef struct { database_t *ib, *cfg; const db_report_setting_t *rs; db_entry_t **entries; int count; } report_t;

static void build_trial_balance(const report_t *r) {
  result_node_t *accounts = fetch(r->cfg, TABLE_ACCOUNTS, NULL, 0);
  EACH_RECORD(db_account_t, a, accounts) {
    totals_t t = {0};
    for (int i = 0; i < r->count; i++) {
      const db_entry_t *e = r->entries[i];
      add_totals(&t, in_period(r->rs, e->date), account_within(r->cfg, e->debit_id, a->id) ? e->amount : 0,
                 account_within(r->cfg, e->credit_id, a->id) ? e->amount : 0);
    }
    if (totals_empty(&t)) continue;
    db_balance_t row = { .account_id = a->id, .debit_turnover = t.turn_dr, .credit_turnover = t.turn_cr };
    split_saldo(t.open_dr, t.open_cr, &row.opening_dr, &row.opening_cr);
    split_saldo(t.open_dr + t.turn_dr, t.open_cr + t.turn_cr, &row.balance_dr, &row.balance_cr);
    send_db_message(r->ib, dbInsert, TABLE_BALANCES, &row);
  }
  free_result_list(accounts);
}

// Account card: every movement of the chosen account with a running balance.
static void build_card(const report_t *r) {
  int account = r->rs->account_id;
  double balance = 0;
  for (int i = 0; i < r->count && account; i++) {
    const db_entry_t *e = r->entries[i];
    bool dr = account_within(r->cfg, e->debit_id, account), cr = account_within(r->cfg, e->credit_id, account);
    if (!dr && !cr) continue;
    balance += (dr ? e->amount : 0) - (cr ? e->amount : 0);
    if (in_period(r->rs, e->date) != WITHIN) continue;
    db_card_t row = { .document_id = e->document_id, .account_id = dr ? e->debit_id : e->credit_id,
                      .subconto_id = dr ? e->debit_subconto_id : e->credit_subconto_id,
                      .corr_id = dr ? e->credit_id : e->debit_id, .debit = dr ? e->amount : 0,
                      .credit = cr ? e->amount : 0, .balance = round2(balance) };
    snprintf(row.date, sizeof(row.date), "%s", e->date);
    snprintf(row.content, sizeof(row.content), "%s", e->content);
    send_db_message(r->ib, dbInsert, TABLE_CARD, &row);
  }
}

// Account analysis: the chosen account's balance per analytics object.
static void build_analysis(const report_t *r) {
  int account = r->rs->account_id, objects[512], count = 0;
  for (int i = 0; i < r->count && account; i++) {
    const db_entry_t *e = r->entries[i];
    for (int side = 0; side < 2; side++) {
      if (!account_within(r->cfg, side ? e->credit_id : e->debit_id, account)) continue;
      int id = side ? e->credit_subconto_id : e->debit_subconto_id;
      bool known = false;
      for (int k = 0; k < count; k++) known |= objects[k] == id;
      if (!known && count < (int)ARRAY_LEN(objects)) objects[count++] = id;
    }
  }
  for (int k = 0; k < count; k++) {
    totals_t t = {0};
    for (int i = 0; i < r->count; i++) {
      const db_entry_t *e = r->entries[i];
      bool dr = account_within(r->cfg, e->debit_id, account) && e->debit_subconto_id == objects[k];
      bool cr = account_within(r->cfg, e->credit_id, account) && e->credit_subconto_id == objects[k];
      add_totals(&t, in_period(r->rs, e->date), dr ? e->amount : 0, cr ? e->amount : 0);
    }
    if (totals_empty(&t)) continue;
    db_analysis_row_t row = { .account_id = account, .subconto_id = objects[k], .debit_turnover = t.turn_dr, .credit_turnover = t.turn_cr };
    split_saldo(t.open_dr, t.open_cr, &row.opening_dr, &row.opening_cr);
    split_saldo(t.open_dr + t.turn_dr, t.open_cr + t.turn_cr, &row.balance_dr, &row.balance_cr);
    send_db_message(r->ib, dbInsert, TABLE_ANALYSIS_ROWS, &row);
  }
}

// Sales and purchase books list the posted documents their type assigns them,
// in national currency.
static void build_vat_books(const report_t *r) {
  result_node_t *docs = fetch(r->ib, TABLE_DOCUMENTS, NULL, 0);
  EACH_RECORD(db_document_t, d, docs) {
    const db_doc_type_t *type = find(r->cfg, TABLE_DOC_TYPES, d->doc_type_id);
    if (!d->posted || !type || !type->vat_book || in_period(r->rs, d->date) != WITHIN) continue;
    double rate = d->currency_id && d->rate > 0 ? d->rate : 1;
    db_vat_book_t row = { .document_id = d->id, .counterparty_id = d->counterparty_id, .book = type->vat_book,
                          .total = round2(d->total * rate), .amount = round2(d->amount * rate), .vat = round2(d->vat * rate) };
    snprintf(row.date, sizeof(row.date), "%s", d->date);
    send_db_message(r->ib, dbInsert, TABLE_VAT_BOOK, &row);
  }
  free_result_list(docs);
}

static void cash_day_total(const report_t *r, const char *date, double receipt, double expense, double balance) {
  db_cash_book_t row = { .receipt = receipt, .expense = expense, .balance = round2(balance) };
  snprintf(row.date, sizeof(row.date), "%.10s", date);
  snprintf(row.content, sizeof(row.content), "%s", ui_tr("Day total and closing balance"));
  send_db_message(r->ib, dbInsert, TABLE_CASH_BOOK, &row);
}

// Cash book: for each day with cash orders, the opening balance, the orders
// and the day's totals with the closing balance.
static void build_cash_book(const report_t *r) {
  const db_setting_t *s = settings(r->cfg);
  int cash = s ? s->cash_account_id : 0;
  double balance = 0, receipt = 0, expense = 0;
  char day[16] = "";
  for (int i = 0; i < r->count && cash; i++) {
    const db_entry_t *e = r->entries[i];
    bool in = account_within(r->cfg, e->debit_id, cash), out = account_within(r->cfg, e->credit_id, cash);
    if (in == out) continue;
    period_pos_t pos = in_period(r->rs, e->date);
    if (pos == WITHIN && strcmp(day, e->date)) {
      if (day[0]) cash_day_total(r, day, receipt, expense, balance);
      snprintf(day, sizeof(day), "%s", e->date);
      receipt = expense = 0;
      db_cash_book_t open = { .balance = round2(balance) };
      snprintf(open.date, sizeof(open.date), "%.10s", day);
      snprintf(open.content, sizeof(open.content), "%s", ui_tr("Balance at start of day"));
      send_db_message(r->ib, dbInsert, TABLE_CASH_BOOK, &open);
    }
    balance += in ? e->amount : -e->amount;
    if (pos != WITHIN) continue;
    if (in) receipt += e->amount; else expense += e->amount;
    db_cash_book_t row = { .document_id = e->document_id, .corr_id = in ? e->credit_id : e->debit_id,
                           .subconto_id = in ? e->credit_subconto_id : e->debit_subconto_id,
                           .receipt = in ? e->amount : 0, .expense = in ? 0 : e->amount, .balance = round2(balance) };
    snprintf(row.date, sizeof(row.date), "%s", e->date);
    snprintf(row.content, sizeof(row.content), "%s", e->content);
    send_db_message(r->ib, dbInsert, TABLE_CASH_BOOK, &row);
  }
  if (day[0]) cash_day_total(r, day, receipt, expense, balance);
}

// Fixed assets at the end of the period: cost, depreciation charged, residual value.
static void build_asset_rows(const report_t *r) {
  result_node_t *assets = fetch(r->ib, TABLE_ASSETS, NULL, 0);
  EACH_RECORD(db_asset_t, a, assets) {
    double charged = 0;
    for (int i = 0; i < r->count; i++) {
      const db_entry_t *e = r->entries[i];
      if (in_period(r->rs, e->date) != AFTER && e->credit_subconto_id == a->id && account_within(r->cfg, e->credit_id, a->depreciation_id))
        charged += e->amount;
    }
    db_asset_row_t row = { .asset_id = a->id, .cost = a->cost, .monthly = a->life > 0 ? round2(a->cost / a->life) : 0,
                           .depreciation = round2(charged), .residual = round2(a->cost - charged) };
    send_db_message(r->ib, dbInsert, TABLE_ASSET_ROWS, &row);
  }
  free_result_list(assets);
}

// Payroll statement: per employee, what the payroll account owes them.
static void build_payroll_rows(const report_t *r) {
  const db_setting_t *s = settings(r->cfg);
  int account = s ? s->payroll_account_id : 0;
  result_node_t *employees = fetch(r->ib, TABLE_EMPLOYEES, NULL, 0);
  EACH_RECORD(db_employee_t, emp, employees) {
    totals_t t = {0};
    for (int i = 0; i < r->count && account; i++) {
      const db_entry_t *e = r->entries[i];
      bool dr = account_within(r->cfg, e->debit_id, account) && e->debit_subconto_id == emp->id;
      bool cr = account_within(r->cfg, e->credit_id, account) && e->credit_subconto_id == emp->id;
      add_totals(&t, in_period(r->rs, e->date), dr ? e->amount : 0, cr ? e->amount : 0);
    }
    if (totals_empty(&t)) continue;
    db_payroll_row_t row = { .employee_id = emp->id, .opening = round2(t.open_cr - t.open_dr), .accrued = round2(t.turn_cr),
                             .deducted = round2(t.turn_dr), .closing = round2(t.open_cr - t.open_dr + t.turn_cr - t.turn_dr) };
    send_db_message(r->ib, dbInsert, TABLE_PAYROLL_ROWS, &row);
  }
  free_result_list(employees);
}

static int by_date(const void *a, const void *b) {
  const db_entry_t *x = *(const db_entry_t * const *)a, *y = *(const db_entry_t * const *)b;
  int c = strcmp(x->date, y->date);
  return c ? c : x->id - y->id;
}

// Reports count entries of posted documents only, in date order.
void ledger_build_reports(database_t *ib, database_t *cfg) {
  const int tables[] = { TABLE_BALANCES, TABLE_CARD, TABLE_ANALYSIS_ROWS, TABLE_VAT_BOOK, TABLE_CASH_BOOK, TABLE_ASSET_ROWS, TABLE_PAYROLL_ROWS };
  for (size_t i = 0; i < ARRAY_LEN(tables); i++) clear_table(ib, tables[i]);
  const db_report_setting_t *rs = ledger_report_settings(ib, cfg);
  if (!rs) return;
  result_node_t *all = fetch(ib, TABLE_ENTRIES, NULL, 0);
  int n = count_result_list(all);
  report_t r = { ib, cfg, rs, n ? malloc((size_t)n * sizeof(db_entry_t *)) : NULL, 0 };
  EACH_RECORD(db_entry_t, e, all) {
    const db_document_t *doc = find(ib, TABLE_DOCUMENTS, e->document_id);
    if (r.entries && doc && doc->posted) r.entries[r.count++] = e;
  }
  free_result_list(all);
  if (r.entries) qsort(r.entries, (size_t)r.count, sizeof(*r.entries), by_date);
  build_trial_balance(&r);
  build_card(&r);
  build_analysis(&r);
  build_vat_books(&r);
  build_cash_book(&r);
  build_asset_rows(&r);
  build_payroll_rows(&r);
  free(r.entries);
}

int ledger_count_references(database_t *ib, int table_id, const char *field, int id) {
  result_node_t *rows = fetch(ib, table_id, field, id);
  int n = count_result_list(rows);
  free_result_list(rows);
  return n;
}

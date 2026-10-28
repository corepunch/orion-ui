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
bool ledger_account_within(database_t *cfg, int account_id, int parent_id) {
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
typedef ledger_subjects_t subjects_t;

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
double ledger_rate_on(database_t *ib, int currency_id, const char *date) {
  double rate = 0;
  char best[16] = "";
  result_node_t *rates = fetch(ib, TABLE_RATES, "currency_id", currency_id);
  EACH_RECORD(db_rate_t, r, rates)
    if (strcmp(r->date, date) <= 0 && strcmp(r->date, best) >= 0) { rate = r->rate; snprintf(best, sizeof(best), "%s", r->date); }
  free_result_list(rates);
  return rate;
}

// ── Posting context and turnovers ──────────────────────────────────────────

typedef ledger_post_t post_t;

// Documents post in date order; on one date month-end closing comes last.
static int doc_order(database_t *cfg, const db_document_t *a, const db_document_t *b) {
  int c = strcmp(a->date, b->date);
  if (c) return c;
  bool ca = form_is(find(cfg, TABLE_DOC_TYPES, a->doc_type_id), "closing");
  bool cb = form_is(find(cfg, TABLE_DOC_TYPES, b->doc_type_id), "closing");
  if (ca != cb) return ca ? 1 : -1;
  return a->id - b->id;
}

typedef ledger_turnover_t turnover_t;

// Turnovers of account (with subaccounts) on what precedes the document being
// posted, plus its own entries so far. subconto and currency filter when >= 0.
turnover_t ledger_turnover(const post_t *p, int account_id, int subconto_id, int currency_id) {
  turnover_t t = {0};
  result_node_t *entries = fetch(p->ib, TABLE_ENTRIES, NULL, 0);
  EACH_RECORD(db_entry_t, e, entries) {
    if (currency_id >= 0 && e->currency_id != currency_id) continue;
    if (e->document_id != p->doc->id) {
      const db_document_t *d = find(p->ib, TABLE_DOCUMENTS, e->document_id);
      if (!d || !d->posted || doc_order(p->cfg, d, p->doc) >= 0) continue;
    }
    if (ledger_account_within(p->cfg, e->debit_id, account_id) && (subconto_id < 0 || e->debit_subconto_id == subconto_id)) {
      t.dr += e->amount; t.dr_qty += e->quantity; t.dr_cur += e->currency_amount;
    }
    if (ledger_account_within(p->cfg, e->credit_id, account_id) && (subconto_id < 0 || e->credit_subconto_id == subconto_id)) {
      t.cr += e->amount; t.cr_qty += e->quantity; t.cr_cur += e->currency_amount;
    }
  }
  free_result_list(entries);
  return t;
}

void ledger_add_entry(const post_t *p, int debit, int credit, double amount, const subjects_t *s, double quantity,
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
  if (p->doc->currency_id) ledger_add_entry(p, debit, credit, amount * p->rate, s, quantity, p->doc->currency_id, amount, NULL);
  else ledger_add_entry(p, debit, credit, amount, s, quantity, 0, 0, NULL);
}

static void delete_entries(database_t *ib, int doc_id) {
  result_node_t *entries = fetch(ib, TABLE_ENTRIES, "document_id", doc_id);
  EACH_RECORD(db_entry_t, e, entries) send_db_message(ib, dbDelete, TABLE_ENTRIES, (void *)(intptr_t)e->id);
  free_result_list(entries);
}

// ── Payroll taxes ──────────────────────────────────────────────────────────
// The configuration's payroll_taxes rows in force on the document date, in id
// order, each computed on one accrual line: flat rates with monthly or annual
// caps, a progressive scale applied to the year's pay so far (cumulative, as
// НДФЛ) or to twelve monthly bases (annualized, as Lohnsteuer), allowances
// with an annual limit, and a base net of the taxes withheld before.

static bool tax_in_force(const db_payroll_tax_t *tax, const char *date) {
  return (!tax->valid_from[0] || strcmp(date, tax->valid_from) >= 0) && (!tax->valid_to[0] || strcmp(date, tax->valid_to) <= 0);
}

// The employee's accrued pay in the document's year before the document, and
// how many accruals it came in (each carried one monthly allowance).
static double ytd_pay(const post_t *p, int employee_id, int *accruals) {
  char year_start[16];
  snprintf(year_start, sizeof(year_start), "%.4s-01-01", p->doc->date);
  double base = 0;
  *accruals = 0;
  result_node_t *lines = fetch(p->ib, TABLE_PAYROLL_LINES, "employee_id", employee_id);
  EACH_RECORD(db_payroll_line_t, l, lines) {
    if (l->document_id == p->doc->id) continue;
    const db_document_t *d = find(p->ib, TABLE_DOCUMENTS, l->document_id);
    const db_doc_type_t *t = d ? find(p->cfg, TABLE_DOC_TYPES, d->doc_type_id) : NULL;
    if (!d || !d->posted || !form_is(t, "payroll") || strcmp(t->base, "gross") || strcmp(d->date, year_start) < 0 || doc_order(p->cfg, d, p->doc) >= 0) continue;
    base += l->amount;
    (*accruals)++;
  }
  free_result_list(lines);
  return base;
}

// The tax a scale levies on an annual base: the zone with the greatest
// base_from not above x, then fixed + rate % of the excess + curve * (excess / 10 000)^2.
static double scale_tax(result_node_t *zones, int tax_id, double x) {
  const db_tax_scale_t *zone = NULL;
  EACH_RECORD(db_tax_scale_t, z, zones)
    if (z->tax_id == tax_id && z->base_from <= x && (!zone || z->base_from > zone->base_from)) zone = z;
  if (!zone) return 0;
  double excess = x - zone->base_from, y = excess / 10000.0;
  return zone->fixed + excess * zone->rate / 100.0 + zone->curve * y * y;
}

static double round_tax(const db_payroll_tax_t *tax, double v) {
  return !strcmp(tax->rounding, "unit") ? floor(v + 0.5) : round2(v);
}

typedef ledger_tax_amount_t tax_line_t;
#define PAYROLL_TAX_MAX 32

// The taxes of one accrual line, in the configuration's order.
static int line_taxes(const post_t *p, const db_payroll_line_t *line, tax_line_t *out, int max) {
  const db_employee_t *emp = find(p->ib, TABLE_EMPLOYEES, line->employee_id);
  int accruals = 0, count = 0;
  double ytd = ytd_pay(p, line->employee_id, &accruals), withheld = 0;
  result_node_t *taxes = fetch(p->cfg, TABLE_PAYROLL_TAXES, NULL, 0), *zones = fetch(p->cfg, TABLE_TAX_SCALES, NULL, 0);
  EACH_RECORD(db_payroll_tax_t, tax, taxes) {
    if (!tax_in_force(tax, p->doc->date) || count == max) continue;
    if (tax->script[0]) {  // the configuration's rules.lua computes this one
      double scripted = 0;
      if (ledger_script_tax(p, tax, line, ytd, accruals, withheld, out, count, &scripted)) {
        scripted = round_tax(tax, scripted);
        if (tax->withheld) withheld += scripted;
        out[count++] = (tax_line_t){ tax, scripted };
      }
      continue;
    }
    double allowance = tax->allowance && emp ? emp->allowance : 0;
    if (allowance && tax->allowance_limit > 0 && ytd + line->amount > tax->allowance_limit) allowance = 0;  // the month the year's pay passes the limit
    double base = line->amount - (tax->after_withheld ? withheld : 0) - allowance, amount;
    if (base < 0) base = 0;
    if (!strcmp(tax->scale, "cumulative")) {  // the year's taxable pay so far, this month's on top
      double before = ytd - (tax->allowance && emp ? emp->allowance * accruals : 0);  // earlier months carried the allowance
      if (before < 0) before = 0;
      amount = round_tax(tax, scale_tax(zones, tax->id, before + base) - scale_tax(zones, tax->id, before));
    } else if (!strcmp(tax->scale, "annualized")) {  // the year's tax on twelve such months, one twelfth of it
      double annual = scale_tax(zones, tax->id, floor(base * 12.0));
      if (!strcmp(tax->rounding, "floor")) { annual = floor(annual); amount = floor(annual / 12.0 * 100.0 + 1e-6) / 100.0; }
      else amount = round_tax(tax, annual / 12.0);
    } else {
      double within = base, above = 0;
      if (tax->cap_month > 0 && within > tax->cap_month) { above = within - tax->cap_month; within = tax->cap_month; }
      if (tax->cap_year > 0) {
        double room = fmax(tax->cap_year - ytd, 0);
        if (within > room) { above += within - room; within = room; }
      }
      amount = round_tax(tax, within * tax->rate / 100.0 + above * tax->rate_above / 100.0);
    }
    if (tax->withheld) withheld += amount;
    out[count++] = (tax_line_t){ tax, amount };
  }
  free_result_list(zones);
  free_result_list(taxes);
  return count;
}

// ── Recalculation ──────────────────────────────────────────────────────────

// Payroll taxes of one accrual line: withheld from the employee, and the
// employer's contributions on top.
static void payroll_taxes(const post_t *p, const db_payroll_line_t *line, double *withheld, double *contributions) {
  tax_line_t taxes[PAYROLL_TAX_MAX];
  int n = line_taxes(p, line, taxes, PAYROLL_TAX_MAX);
  *withheld = *contributions = 0;
  for (int i = 0; i < n; i++) {
    if (taxes[i].tax->withheld) *withheld += taxes[i].amount; else *contributions += taxes[i].amount;
  }
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
    if (!strcmp(e->date, doc->date) && e->content[0]) continue;
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
  post_t p = { ib, cfg, doc, type, 1 };
  result_node_t *lines = fetch(ib, TABLE_PAYROLL_LINES, "document_id", doc->id);
  EACH_RECORD(db_payroll_line_t, line, lines) {
    if (payout) line->withheld = line->contributions = 0;
    else payroll_taxes(&p, line, &line->withheld, &line->contributions);
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
  if (doc->currency_id && doc->rate == 0) doc->rate = ledger_rate_on(ib, doc->currency_id, doc->date);
  if (!doc->currency_id) doc->rate = 0;
  if      (type && type->script[0])    sum_entries(ib, doc);  // a scripted type's entries are its amounts
  else if (form_is(type, "operation")) date_manual_entries(ib, doc);
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
  turnover_t t = ledger_turnover(p, p->type->cost_credit_id, line->item_id, -1);
  double qty = t.dr_qty - t.cr_qty, value = t.dr - t.cr;
  if (qty <= 0) {
    fprintf(stderr, "[ledger] cost of sales skipped: document=%s item=%d has no stock on the date\n", p->doc->number, line->item_id);
    return;
  }
  double cost = line->quantity >= qty ? value : value * line->quantity / qty;
  subjects_t s = { .counterparty = p->doc->counterparty_id, .item = line->item_id };
  ledger_add_entry(p, p->type->cost_debit_id, p->type->cost_credit_id, cost, &s, line->quantity, 0, 0, NULL);
}

// A tax rate's own account for a side of a sale (book 1) or purchase (book 2),
// as DATEV's automatic accounts route 7 % and 19 % to different accounts; the
// document type's account when the rate names none.
static int rate_account(const db_vat_rate_t *rate, int book, bool tax, int fallback) {
  int own = !rate ? 0 : book == 1 ? (tax ? rate->output_tax_id : rate->revenue_id)
                     : book == 2 ? (tax ? rate->input_tax_id : rate->expense_id) : 0;
  return own ? own : fallback;
}

static void post_goods(const post_t *p) {
  const db_doc_type_t *type = p->type;
  bool net = !strcmp(type->base, "net");
  result_node_t *lines = fetch(p->ib, TABLE_DOC_LINES, "document_id", p->doc->id);
  EACH_RECORD(db_doc_line_t, line, lines) {  // one set of entries per line, as goods move per item
    subjects_t s = { .counterparty = p->doc->counterparty_id, .item = line->item_id };
    const db_vat_rate_t *rate = type->auto_accounts ? find(p->cfg, TABLE_VAT_RATES, line->vat_rate_id) : NULL;
    int book = type->vat_book;
    add_doc_entry(p, book == 2 ? rate_account(rate, book, false, type->debit_id) : type->debit_id,
                  book == 1 ? rate_account(rate, book, false, type->credit_id) : type->credit_id,
                  net ? line->amount : line->total, &s, line->quantity);
    add_doc_entry(p, book == 2 ? rate_account(rate, book, true, type->vat_debit_id) : type->vat_debit_id,
                  book == 1 ? rate_account(rate, book, true, type->vat_credit_id) : type->vat_credit_id,
                  line->vat, &s, line->quantity);
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
  EACH_RECORD(db_payroll_line_t, line, lines) {
    const db_employee_t *emp = find(p->ib, TABLE_EMPLOYEES, line->employee_id);
    subjects_t s = { .employee = line->employee_id };
    if (payout) { ledger_add_entry(p, p->type->debit_id, p->type->credit_id, line->amount, &s, 0, 0, 0, NULL); continue; }
    int expense = emp && emp->account_id ? emp->account_id : p->type->debit_id;
    ledger_add_entry(p, expense, p->type->credit_id, line->amount, &s, 0, 0, 0, NULL);
    tax_line_t taxes[PAYROLL_TAX_MAX];
    int n = line_taxes(p, line, taxes, PAYROLL_TAX_MAX);
    for (int i = 0; i < n; i++) {
      char content[160];
      snprintf(content, sizeof(content), "%.60s: %.96s", taxes[i].tax->name, emp ? emp->name : "");
      ledger_add_entry(p, taxes[i].tax->debit_id ? taxes[i].tax->debit_id : expense, taxes[i].tax->credit_id, taxes[i].amount, &s, 0, 0, 0, content);
    }
  }
  free_result_list(lines);
}

static void post_assets(const post_t *p) {
  result_node_t *lines = fetch(p->ib, TABLE_ASSET_LINES, "document_id", p->doc->id);
  EACH_RECORD(db_asset_line_t, line, lines) {
    db_asset_t *asset = find(p->ib, TABLE_ASSETS, line->asset_id);
    if (!asset) continue;
    subjects_t s = { .asset = asset->id };
    ledger_add_entry(p, asset->account_id, p->type->credit_id, asset->cost, &s, 0, 0, 0, NULL);
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
    double written_off = ledger_turnover(p, a->depreciation_id, a->id, -1).cr;
    double charge = fmin(round2(a->cost / a->life), round2(a->cost - written_off));
    char content[160];
    snprintf(content, sizeof(content), "%.60s: %.96s", rule->name, a->name);
    subjects_t s = { .asset = a->id };
    if (charge > 0) ledger_add_entry(p, a->expense_id, a->depreciation_id, charge, &s, 0, 0, 0, content);
  }
  free_result_list(assets);
}

static void close_transfer(const post_t *p, const db_closing_rule_t *rule) {
  turnover_t t = ledger_turnover(p, rule->account_id, -1, -1);
  double balance = round2(t.dr - t.cr);
  int via = rule->via_id ? rule->via_id : rule->account_id;
  if (balance > 0) ledger_add_entry(p, rule->target_id, via, balance, NULL, 0, 0, 0, rule->name);
  if (balance < 0) ledger_add_entry(p, via, rule->target_id, -balance, NULL, 0, 0, 0, rule->name);
}

// A growing array of distinct keys (account, currency, analytics object).
typedef struct { int account, currency, subconto; } id_triple_t;
typedef struct { id_triple_t *v; int n, cap; } key_set_t;

static void key_set_add(key_set_t *s, id_triple_t k) {
  for (int i = 0; i < s->n; i++) if (!memcmp(&s->v[i], &k, sizeof(k))) return;
  if (s->n == s->cap) {
    int cap = s->cap ? s->cap * 2 : 64;
    id_triple_t *grown = realloc(s->v, (size_t)cap * sizeof(*grown));
    if (!grown) { fprintf(stderr, "[ledger] out of memory collecting %d keys\n", s->n); return; }
    s->v = grown; s->cap = cap;
  }
  s->v[s->n++] = k;
}

// Currency accounts are restated at the closing date's rate, per currency and
// per analytics object.
static void close_revaluation(const post_t *p, const db_closing_rule_t *rule) {
  key_set_t set = {0};
  result_node_t *entries = fetch(p->ib, TABLE_ENTRIES, NULL, 0);
  EACH_RECORD(db_entry_t, e, entries) {
    for (int side = 0; side < 2 && e->currency_id; side++) {
      int account = side ? e->credit_id : e->debit_id, subconto = side ? e->credit_subconto_id : e->debit_subconto_id;
      const db_account_t *a = find(p->cfg, TABLE_ACCOUNTS, account);
      if (a && a->currency) key_set_add(&set, (id_triple_t){ account, e->currency_id, subconto });
    }
  }
  free_result_list(entries);
  id_triple_t *keys = set.v;
  for (int i = 0; i < set.n; i++) {
    double rate = ledger_rate_on(p->ib, keys[i].currency, p->doc->date);
    if (rate <= 0) {
      fprintf(stderr, "[ledger] revaluation skipped: currency=%d has no rate on %s\n", keys[i].currency, p->doc->date);
      continue;
    }
    turnover_t t = ledger_turnover(p, keys[i].account, keys[i].subconto, keys[i].currency);
    double diff = round2((t.dr_cur - t.cr_cur) * rate - (t.dr - t.cr));
    subjects_t s = subjects_of(p->cfg, keys[i].account, keys[i].subconto);
    if (diff > 0) ledger_add_entry(p, keys[i].account, rule->gain_id, diff, &s, 0, keys[i].currency, 0, rule->name);
    if (diff < 0) ledger_add_entry(p, rule->loss_id, keys[i].account, -diff, &s, 0, keys[i].currency, 0, rule->name);
  }
  free(set.v);
}

static void post_closing(const post_t *p) {
  result_node_t *rules = fetch(p->cfg, TABLE_CLOSING_RULES, NULL, 0);
  EACH_RECORD(db_closing_rule_t, rule, rules) {
    if      (!strcmp(rule->kind, "depreciation")) close_depreciation(p, rule);
    else if (!strcmp(rule->kind, "transfer"))     close_transfer(p, rule);
    else if (!strcmp(rule->kind, "revaluation"))  close_revaluation(p, rule);
    else if (!strcmp(rule->kind, "script"))       ledger_script_closing(p, rule);
    else fprintf(stderr, "[ledger] closing rule %d has unknown kind '%s'\n", rule->id, rule->kind);
  }
  free_result_list(rules);
  sum_entries(p->ib, p->doc);
}

// Writes the entries the document's type posts; the posted flag is the caller's.
static bool write_entries(const post_t *p) {
  database_t *ib = p->ib;
  db_document_t *doc = p->doc;
  const db_doc_type_t *type = p->type;
  int doc_id = doc->id;
  if (form_is(type, "operation")) date_manual_entries(ib, doc);
  else {
    delete_entries(ib, doc_id);
    if (type->script[0]) {  // the configuration's rules.lua posts this type
      if (!ledger_script_post(p, type->script)) return false;
    }
    else if (form_is(type, "document")) post_goods(p);
    else if (form_is(type, "payroll"))  post_payroll(p);
    else if (form_is(type, "assets"))   post_assets(p);
    else if (form_is(type, "closing"))  post_closing(p);
    else {
      fprintf(stderr, "[ledger] post rejected: document=%d type=%d has unknown form '%s'\n", doc_id, type->id, type->form);
      return false;
    }
    if (type->script[0] || form_is(type, "closing")) sum_entries(ib, doc);
  }
  return true;
}

static bool posting_of(database_t *ib, database_t *cfg, int doc_id, post_t *p, const char *what) {
  db_document_t *doc = find(ib, TABLE_DOCUMENTS, doc_id);
  db_doc_type_t *type = doc ? find(cfg, TABLE_DOC_TYPES, doc->doc_type_id) : NULL;
  if (!type) {
    fprintf(stderr, "[ledger] %s rejected: document=%d has no document type in configuration\n", what, doc_id);
    return false;
  }
  *p = (post_t){ ib, cfg, doc, type, doc->currency_id && doc->rate > 0 ? doc->rate : 1 };
  return true;
}

bool ledger_post(database_t *ib, database_t *cfg, int doc_id) {
  post_t p;
  if (!posting_of(ib, cfg, doc_id, &p, "post") || !write_entries(&p)) return false;
  ledger_script_event("post", &p);  // plugins see every posted document
  p.doc->posted = true;
  return send_db_message(ib, dbUpdate, TABLE_DOCUMENTS, p.doc) != 0;
}

// The document form shows what posting would write. It runs in the form's
// editing context, so the books change only when the document is saved.
bool ledger_preview_entries(database_t *ib, database_t *cfg, int doc_id) {
  post_t p;
  if (!posting_of(ib, cfg, doc_id, &p, "preview")) return false;
  return form_is(p.type, "operation") || write_entries(&p);
}

// Unposting a manual operation keeps its entries; reports skip them.
void ledger_unpost(database_t *ib, database_t *cfg, int doc_id) {
  db_document_t *doc = find(ib, TABLE_DOCUMENTS, doc_id);
  if (!doc) return;
  if (!form_is(find(cfg, TABLE_DOC_TYPES, doc->doc_type_id), "operation")) delete_entries(ib, doc_id);
  doc->posted = false;
  send_db_message(ib, dbUpdate, TABLE_DOCUMENTS, doc);
}

// Numbers follow the configuration's prefix per document type: ПТ-0001, ...
bool ledger_number_document(database_t *ib, database_t *cfg, int doc_id) {
  return ledger_execute_document(ib, cfg, "documents.number", doc_id);
}

// Fills a payroll document: an accrual with every employee's salary, a
// payout with what each employee is owed on the payroll account.
bool ledger_fill_payroll(database_t *ib, database_t *cfg, int doc_id) {
  return ledger_execute_document(ib, cfg, "payroll.fill", doc_id);
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
      add_totals(&t, in_period(r->rs, e->date), ledger_account_within(r->cfg, e->debit_id, a->id) ? e->amount : 0,
                 ledger_account_within(r->cfg, e->credit_id, a->id) ? e->amount : 0);
    }
    if (totals_empty(&t)) continue;
    db_balance_t row = { .account_id = a->id, .subaccount = strchr(a->code, '.') != NULL,
                         .debit_turnover = t.turn_dr, .credit_turnover = t.turn_cr };
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
    bool dr = ledger_account_within(r->cfg, e->debit_id, account), cr = ledger_account_within(r->cfg, e->credit_id, account);
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
  int account = r->rs->account_id;
  key_set_t objects = {0};
  for (int i = 0; i < r->count && account; i++) {
    const db_entry_t *e = r->entries[i];
    for (int side = 0; side < 2; side++)
      if (ledger_account_within(r->cfg, side ? e->credit_id : e->debit_id, account))
        key_set_add(&objects, (id_triple_t){ account, 0, side ? e->credit_subconto_id : e->debit_subconto_id });
  }
  for (int k = 0; k < objects.n; k++) {
    int object = objects.v[k].subconto;
    totals_t t = {0};
    for (int i = 0; i < r->count; i++) {
      const db_entry_t *e = r->entries[i];
      bool dr = ledger_account_within(r->cfg, e->debit_id, account) && e->debit_subconto_id == object;
      bool cr = ledger_account_within(r->cfg, e->credit_id, account) && e->credit_subconto_id == object;
      add_totals(&t, in_period(r->rs, e->date), dr ? e->amount : 0, cr ? e->amount : 0);
    }
    if (totals_empty(&t)) continue;
    db_analysis_row_t row = { .account_id = account, .subconto_id = object, .debit_turnover = t.turn_dr, .credit_turnover = t.turn_cr };
    split_saldo(t.open_dr, t.open_cr, &row.opening_dr, &row.opening_cr);
    split_saldo(t.open_dr + t.turn_dr, t.open_cr + t.turn_cr, &row.balance_dr, &row.balance_cr);
    send_db_message(r->ib, dbInsert, TABLE_ANALYSIS_ROWS, &row);
  }
  free(objects.v);
}

static void add_vat_row(const report_t *r, const db_document_t *d, int book, int rate_id, double amount, double vat, double rate) {
  db_vat_book_t row = { .document_id = d->id, .counterparty_id = d->counterparty_id, .vat_rate_id = rate_id, .book = book,
                        .total = round2((amount + vat) * rate), .amount = round2(amount * rate), .vat = round2(vat * rate) };
  snprintf(row.date, sizeof(row.date), "%s", d->date);
  send_db_message(r->ib, dbInsert, TABLE_VAT_BOOK, &row);
}

// Sales and purchase books list the posted documents their type assigns them,
// one row per tax rate, in national currency; the summary totals the books
// per rate, as the tax return's lines do.
static void build_vat_books(const report_t *r) {
  result_node_t *docs = fetch(r->ib, TABLE_DOCUMENTS, NULL, 0);
  EACH_RECORD(db_document_t, d, docs) {
    const db_doc_type_t *type = find(r->cfg, TABLE_DOC_TYPES, d->doc_type_id);
    if (!d->posted || !type || !type->vat_book || in_period(r->rs, d->date) != WITHIN) continue;
    double rate = d->currency_id && d->rate > 0 ? d->rate : 1;
    result_node_t *lines = fetch(r->ib, TABLE_DOC_LINES, "document_id", d->id);
    if (!lines) { add_vat_row(r, d, type->vat_book, 0, d->amount, d->vat, rate); continue; }
    key_set_t rates = {0};
    EACH_RECORD(db_doc_line_t, line, lines) key_set_add(&rates, (id_triple_t){ line->vat_rate_id, 0, 0 });
    for (int k = 0; k < rates.n; k++) {
      double amount = 0, vat = 0;
      EACH_RECORD(db_doc_line_t, line, lines) if (line->vat_rate_id == rates.v[k].account) { amount += line->amount; vat += line->vat; }
      add_vat_row(r, d, type->vat_book, rates.v[k].account, amount, vat, rate);
    }
    free(rates.v);
    free_result_list(lines);
  }
  free_result_list(docs);
  result_node_t *rows = fetch(r->ib, TABLE_VAT_BOOK, NULL, 0);
  key_set_t groups = {0};
  EACH_RECORD(db_vat_book_t, row, rows) key_set_add(&groups, (id_triple_t){ row->book, row->vat_rate_id, 0 });
  for (int k = 0; k < groups.n; k++) {
    db_vat_summary_t sum = { .book = groups.v[k].account, .vat_rate_id = groups.v[k].currency };
    EACH_RECORD(db_vat_book_t, row, rows) {
      if (row->book != sum.book || row->vat_rate_id != sum.vat_rate_id) continue;
      sum.documents++; sum.total += row->total; sum.amount += row->amount; sum.vat += row->vat;
    }
    sum.total = round2(sum.total); sum.amount = round2(sum.amount); sum.vat = round2(sum.vat);
    send_db_message(r->ib, dbInsert, TABLE_VAT_SUMMARY, &sum);
  }
  free(groups.v);
  free_result_list(rows);
}

// The journal: every entry of the period, for printing and export.
static void build_journal(const report_t *r) {
  for (int i = 0; i < r->count; i++) {
    const db_entry_t *e = r->entries[i];
    if (in_period(r->rs, e->date) != WITHIN) continue;
    db_journal_row_t row = { .document_id = e->document_id, .debit_id = e->debit_id, .credit_id = e->credit_id,
                             .debit_subconto_id = e->debit_subconto_id, .credit_subconto_id = e->credit_subconto_id,
                             .currency_id = e->currency_id, .quantity = e->quantity, .currency_amount = e->currency_amount, .amount = e->amount };
    snprintf(row.date, sizeof(row.date), "%s", e->date);
    snprintf(row.content, sizeof(row.content), "%s", e->content);
    send_db_message(r->ib, dbInsert, TABLE_JOURNAL_ROWS, &row);
  }
}

static void cash_day_total(const report_t *r, const char *date, double receipt, double expense, double balance) {
  db_cash_book_t row = { .receipt = receipt, .expense = expense, .balance = round2(balance), .summary = true };
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
    bool in = ledger_account_within(r->cfg, e->debit_id, cash), out = ledger_account_within(r->cfg, e->credit_id, cash);
    if (in == out) continue;
    period_pos_t pos = in_period(r->rs, e->date);
    if (pos == AFTER) continue;
    if (pos == WITHIN && strcmp(day, e->date)) {
      if (day[0]) cash_day_total(r, day, receipt, expense, balance);
      snprintf(day, sizeof(day), "%s", e->date);
      receipt = expense = 0;
      db_cash_book_t open = { .balance = round2(balance), .summary = true };
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
      if (in_period(r->rs, e->date) != AFTER && e->credit_subconto_id == a->id && ledger_account_within(r->cfg, e->credit_id, a->depreciation_id))
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
      bool dr = ledger_account_within(r->cfg, e->debit_id, account) && e->debit_subconto_id == emp->id;
      bool cr = ledger_account_within(r->cfg, e->credit_id, account) && e->credit_subconto_id == emp->id;
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

int ledger_posted_entries(database_t *ib, db_entry_t ***out) {
  result_node_t *all = fetch(ib, TABLE_ENTRIES, NULL, 0);
  int n = count_result_list(all), count = 0;
  db_entry_t **entries = n ? malloc((size_t)n * sizeof(*entries)) : NULL;
  EACH_RECORD(db_entry_t, e, all) {
    const db_document_t *doc = find(ib, TABLE_DOCUMENTS, e->document_id);
    if (entries && doc && doc->posted) entries[count++] = e;
  }
  free_result_list(all);
  if (entries) qsort(entries, (size_t)count, sizeof(*entries), by_date);
  *out = entries;
  return count;
}

// The account group an account belongs to: 62 for 62.01.
static int account_group(database_t *cfg, int account_id) {
  const db_account_t *a = find(cfg, TABLE_ACCOUNTS, account_id);
  char code[32];
  if (!a) return 0;
  snprintf(code, sizeof(code), "%.*s", (int)strcspn(a->code, "."), a->code);
  const db_account_t *group = (const db_account_t *)send_db_message(cfg, dbFind,
    MAKEDWORD(TABLE_ACCOUNTS, ledger_field(cfg, TABLE_ACCOUNTS, "code")), code);
  return group ? group->id : account_id;
}

// How far a posted document is settled. Documents in the sales book raise a
// receivable on their type's debit account, those in the purchase book a
// payable on its credit account. The document's movements on that account
// group are matched first in, first out against the counterparty's movements
// the other way, in posting order.
bool ledger_document_settlement(database_t *ib, database_t *cfg, int doc_id, double *due, double *paid) {
  const db_document_t *doc = find(ib, TABLE_DOCUMENTS, doc_id);
  const db_doc_type_t *type = doc ? find(cfg, TABLE_DOC_TYPES, doc->doc_type_id) : NULL;
  *due = *paid = 0;
  if (!type || !doc->posted || !doc->counterparty_id || !type->vat_book) return false;
  bool debit = type->vat_book == 1;
  int group = account_group(cfg, debit ? type->debit_id : type->credit_id), cp = doc->counterparty_id;
  if (!group) return false;
  db_entry_t **entries = NULL;
  int count = ledger_posted_entries(ib, &entries);
  double before = 0, settled = 0;
  bool reached = false;
  for (int i = 0; i < count; i++) {
    const db_entry_t *e = entries[i];
    bool on_debit = ledger_account_within(cfg, e->debit_id, group) && e->debit_subconto_id == cp;
    bool on_credit = ledger_account_within(cfg, e->credit_id, group) && e->credit_subconto_id == cp;
    bool raises = debit ? on_debit : on_credit, settles = debit ? on_credit : on_debit;
    if (raises && e->document_id == doc_id) { reached = true; *due += e->amount; }
    else if (raises && !reached) before += e->amount;
    if (settles) settled += e->amount;
  }
  free(entries);
  if (*due <= 0) return false;
  *paid = round2(fmin(fmax(settled - before, 0), *due));
  return true;
}

// Reports count entries of posted documents only, in date order.
void ledger_build_reports(database_t *ib, database_t *cfg) {
  const int tables[] = { TABLE_BALANCES, TABLE_CARD, TABLE_ANALYSIS_ROWS, TABLE_VAT_BOOK, TABLE_VAT_SUMMARY, TABLE_JOURNAL_ROWS,
                         TABLE_CASH_BOOK, TABLE_ASSET_ROWS, TABLE_PAYROLL_ROWS };
  for (size_t i = 0; i < ARRAY_LEN(tables); i++) clear_table(ib, tables[i]);
  const db_report_setting_t *rs = ledger_report_settings(ib, cfg);
  if (!rs) return;
  report_t r = { ib, cfg, rs, NULL, 0 };
  r.count = ledger_posted_entries(ib, &r.entries);
  build_trial_balance(&r);
  build_card(&r);
  build_analysis(&r);
  build_vat_books(&r);
  build_journal(&r);
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

// The start page's data (Главное): what needs attention, home_money and
// settlements as of a date, documents to create and the latest documents.
// Rebuilt with the reports from posted entries into ib.home_rows and
// ib.recent_documents; the page and the assistant read those tables.
//
// The attention list holds the checks accountants otherwise run by hand or
// call a 1C programmer for (1C's Экспресс-проверка, the month-end assistant):
// documents left unposted, a finished month not closed, cash or stock below
// zero at the end of a day, input tax not yet deducted, and a counterparty
// whose debt and advance stand on two subaccounts without being offset.
// Accounts come from the configuration's document types, never by code.

#include "ledger.h"
#include <math.h>
#include <time.h>

#define HOME_MAX_ACCOUNTS 32
#define HOME_LIST_LIMIT   6

typedef struct {
  database_t *ib, *cfg;
  db_entry_t **entries;
  int count;
  int next_id;
} home_t;

// A running balance of one account and analytics object.
typedef struct { int account, object; double amount, quantity, currency; bool flagged; } home_balance_t;
typedef struct { home_balance_t *items; int count, capacity; } home_balances_t;

typedef struct { int ids[HOME_MAX_ACCOUNTS]; int count; } home_accounts_t;

static char g_home_asof[11];

const char *ledger_home_asof(void) { return g_home_asof; }

static void *home_find(database_t *db, int table_id, int id) {
  return id ? (void *)send_db_message(db, dbFind, MAKEDWORD(table_id, 0), (void *)(intptr_t)id) : NULL;
}

static result_node_t *home_fetch(database_t *db, int table_id) {
  return (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(table_id, 0), NULL);
}

#define HOME_EACH(type, var, list) \
  for (result_node_t *var##_node = (list); var##_node; var##_node = var##_node->next) \
    for (type *var = *(type **)var##_node->data; var; var = NULL)

static double home_round2(double v) { return v < 0 ? -floor(-v * 100.0 + 0.5) / 100.0 : floor(v * 100.0 + 0.5) / 100.0; }

static const char *home_code(const home_t *h, int account_id) {
  const db_account_t *a = home_find(h->cfg, TABLE_ACCOUNTS, account_id);
  return a ? a->code : "";
}

static const char *home_money(double v, char *buf, size_t sz) { return ui_format_number(v, 2, buf, sz); }

static void accounts_add(home_accounts_t *set, int id) {
  if (!id) return;
  for (int i = 0; i < set->count; i++) if (set->ids[i] == id) return;
  if (set->count < HOME_MAX_ACCOUNTS) set->ids[set->count++] = id;
}

static bool accounts_hold(database_t *cfg, const home_accounts_t *set, int account_id) {
  for (int i = 0; i < set->count; i++) if (ledger_account_within(cfg, account_id, set->ids[i])) return true;
  return false;
}

// 62 for 62.01: settlements are read on the whole group.
static int home_group(database_t *cfg, int account_id) {
  const db_account_t *a = home_find(cfg, TABLE_ACCOUNTS, account_id);
  char code[32];
  if (!a) return 0;
  snprintf(code, sizeof(code), "%.*s", (int)strcspn(a->code, "."), a->code);
  const db_account_t *group = (const db_account_t *)send_db_message(cfg, dbFind,
    MAKEDWORD(TABLE_ACCOUNTS, ledger_field(cfg, TABLE_ACCOUNTS, "code")), code);
  return group ? group->id : account_id;
}

static home_balance_t *balance_of(home_balances_t *b, int account, int object) {
  for (int i = 0; i < b->count; i++)
    if (b->items[i].account == account && b->items[i].object == object) return &b->items[i];
  if (b->count == b->capacity) {
    int capacity = b->capacity ? b->capacity * 2 : 32;
    home_balance_t *grown = realloc(b->items, (size_t)capacity * sizeof(*grown));
    if (!grown) {
      fprintf(stderr, "[ledger] home: balance allocation failed count=%d\n", b->count);
      fflush(stderr);
      return NULL;
    }
    b->items = grown;
    b->capacity = capacity;
  }
  b->items[b->count] = (home_balance_t){ account, object, 0, 0, 0, false };
  return &b->items[b->count++];
}

static void add_row(home_t *h, db_home_row_t *row, const char *kind, const char *title, const char *detail,
                    const char *action, const char *command) {
  row->id = ++h->next_id;
  snprintf(row->kind, sizeof(row->kind), "%s", kind);
  snprintf(row->title, sizeof(row->title), "%s", title ? title : "");
  snprintf(row->detail, sizeof(row->detail), "%s", detail ? detail : "");
  snprintf(row->action, sizeof(row->action), "%s", action ? action : "");
  snprintf(row->command, sizeof(row->command), "%s", command ? command : "");
  send_db_message(h->ib, dbInsert, TABLE_HOME_ROWS, row);
}

static void home_clear(database_t *ib, int table_id) {
  result_node_t *rows = home_fetch(ib, table_id);
  HOME_EACH(int, key, rows) send_db_message(ib, dbDelete, (uint32_t)table_id, (void *)(intptr_t)*key);
  free_result_list(rows);
}

// ── Which accounts are which ───────────────────────────────────────────────

// Money: the cash account and the side of a payment document that is not a
// counterparty's settlement (51 in Dt 51 Kt 62, 1200 in Dt 1200 Kt 1400).
static void money_accounts(const home_t *h, home_accounts_t *out) {
  const db_setting_t *s = home_find(h->cfg, TABLE_SETTINGS, 1);
  if (s) accounts_add(out, s->cash_account_id);
  result_node_t *types = home_fetch(h->cfg, TABLE_DOC_TYPES);
  HOME_EACH(db_doc_type_t, t, types) {
    if (strcmp(t->form, "document") || t->vat_book || !t->debit_id || !t->credit_id) continue;
    bool dr_party = !strcmp(ledger_subconto_table(h->cfg, t->debit_id), "counterparties");
    bool cr_party = !strcmp(ledger_subconto_table(h->cfg, t->credit_id), "counterparties");
    if (dr_party && !*ledger_subconto_table(h->cfg, t->credit_id)) accounts_add(out, t->credit_id);
    if (cr_party && !*ledger_subconto_table(h->cfg, t->debit_id)) accounts_add(out, t->debit_id);
  }
  free_result_list(types);
}

// Receivables are the sales book's debit groups, payables the purchase book's
// credit groups; their tax sides are output and input tax.
static void settlement_accounts(const home_t *h, home_accounts_t *receivable, home_accounts_t *payable,
                                home_accounts_t *output_tax, home_accounts_t *input_tax) {
  result_node_t *types = home_fetch(h->cfg, TABLE_DOC_TYPES);
  HOME_EACH(db_doc_type_t, t, types) {
    if (t->vat_book == 1) { accounts_add(receivable, home_group(h->cfg, t->debit_id)); accounts_add(output_tax, t->vat_credit_id); }
    if (t->vat_book == 2) { accounts_add(payable, home_group(h->cfg, t->credit_id)); accounts_add(input_tax, t->vat_debit_id); }
  }
  free_result_list(types);
  result_node_t *rates = home_fetch(h->cfg, TABLE_VAT_RATES);
  HOME_EACH(db_vat_rate_t, r, rates) { accounts_add(output_tax, r->output_tax_id); accounts_add(input_tax, r->input_tax_id); }
  free_result_list(rates);
}

static double net_on(const home_t *h, int account_id) {
  double net = 0;
  for (int i = 0; i < h->count; i++) {
    const db_entry_t *e = h->entries[i];
    if (g_home_asof[0] && strcmp(e->date, g_home_asof) > 0) break;
    if (ledger_account_within(h->cfg, e->debit_id, account_id)) net += e->amount;
    if (ledger_account_within(h->cfg, e->credit_id, account_id)) net -= e->amount;
  }
  return home_round2(net);
}

// The accounts of a set that are not inside another of the set, joined.
static void codes_of(const home_t *h, const home_accounts_t *set, char *out, size_t sz) {
  out[0] = 0;
  for (int i = 0; i < set->count; i++) {
    bool nested = false;
    for (int j = 0; j < set->count; j++)
      if (i != j && set->ids[i] != set->ids[j] && ledger_account_within(h->cfg, set->ids[i], set->ids[j])) nested = true;
    if (nested) continue;
    size_t used = strlen(out);
    snprintf(out + used, sz - used, "%s%s", used ? ", " : "", home_code(h, set->ids[i]));
  }
}

// Net balance of the accounts of a set on the credit side (taxes owed) or the debit side.
static double set_net(const home_t *h, const home_accounts_t *set, bool credit) {
  double total = 0;
  for (int i = 0; i < set->count; i++) {
    bool nested = false;
    for (int j = 0; j < set->count; j++)
      if (i != j && set->ids[i] != set->ids[j] && ledger_account_within(h->cfg, set->ids[i], set->ids[j])) nested = true;
    if (nested) continue;
    double net = net_on(h, set->ids[i]) * (credit ? -1 : 1);
    if (net > 0) total += net;
  }
  return home_round2(total);
}

// ── Money ──────────────────────────────────────────────────────────────────

static bool has_subaccounts(const home_t *h, const db_account_t *a) {
  bool found = false;
  size_t n = strlen(a->code);
  result_node_t *accounts = home_fetch(h->cfg, TABLE_ACCOUNTS);
  HOME_EACH(db_account_t, other, accounts)
    if (!strncmp(other->code, a->code, n) && other->code[n] == '.') found = true;
  free_result_list(accounts);
  return found;
}

static void build_money(home_t *h, const home_accounts_t *cash) {
  for (int i = 0; i < cash->count; i++) {
    const db_account_t *a = home_find(h->cfg, TABLE_ACCOUNTS, cash->ids[i]);
    if (!a) continue;
    double amount = 0, foreign = 0;
    int currency = 0;
    for (int k = 0; k < h->count; k++) {
      const db_entry_t *e = h->entries[k];
      if (g_home_asof[0] && strcmp(e->date, g_home_asof) > 0) break;
      bool dr = ledger_account_within(h->cfg, e->debit_id, a->id), cr = ledger_account_within(h->cfg, e->credit_id, a->id);
      if (dr == cr) continue;
      amount += dr ? e->amount : -e->amount;
      if (e->currency_id) { currency = e->currency_id; foreign += dr ? e->currency_amount : -e->currency_amount; }
    }
    if (fabs(amount) < 0.005 && (has_subaccounts(h, a) || fabs(foreign) < 0.005)) continue;
    char detail[96], number[48];
    const db_currency_t *cur = a->currency ? home_find(h->ib, TABLE_CURRENCIES, currency) : NULL;
    if (cur && fabs(foreign) >= 0.005) snprintf(detail, sizeof(detail), "%s · %s %s", a->code, home_money(foreign, number, sizeof(number)), cur->code);
    else snprintf(detail, sizeof(detail), "%s", a->code);
    db_home_row_t row = { .card = 2, .account_id = a->id, .amount = home_round2(amount) };
    add_row(h, &row, "money", a->name, detail, NULL, "card");
  }
}

// ── Settlements ────────────────────────────────────────────────────────────

typedef struct { double total; int count, top_party, top_account; double top; } home_side_t;

static void side_add(home_side_t *s, int party, int account, double amount) {
  s->total += amount;
  s->count++;
  if (amount > s->top) { s->top = amount; s->top_party = party; s->top_account = account; }
}

static void side_row(home_t *h, const home_side_t *s, const char *kind, const char *title, int group) {
  if (s->total < 0.005) return;
  const db_counterparty_t *c = home_find(h->ib, TABLE_COUNTERPARTIES, s->top_party);
  char who[200], detail[256];
  if (!c) snprintf(who, sizeof(who), "%s", "");
  else if (s->count > 1) snprintf(who, sizeof(who), ui_tr("%s and %d more"), c->name, s->count - 1);
  else snprintf(who, sizeof(who), "%s", c->name);
  snprintf(detail, sizeof(detail), "%s%s%s", who, who[0] ? " · " : "", home_code(h, s->top_account ? s->top_account : group));
  db_home_row_t row = { .card = 3, .account_id = group, .counterparty_id = s->top_party, .amount = home_round2(s->total) };
  add_row(h, &row, kind, ui_tr(title), detail, NULL, "card");
}

// Per counterparty on each settlement group: the net is a debt or an
// advance, and a debt on one subaccount beside an advance on another is the
// развёрнутое сальдо nobody offset.
static void build_settlements(home_t *h, const home_accounts_t *receivable, const home_accounts_t *payable,
                              home_balances_t *unoffset) {
  for (int pass = 0; pass < 2; pass++) {
    const home_accounts_t *groups = pass ? payable : receivable;
    for (int g = 0; g < groups->count; g++) {
      int group = groups->ids[g];
      if (pass && accounts_hold(h->cfg, receivable, group)) continue;
      home_balances_t by_party = {0}, by_account = {0};
      for (int i = 0; i < h->count; i++) {
        const db_entry_t *e = h->entries[i];
        if (g_home_asof[0] && strcmp(e->date, g_home_asof) > 0) break;
        for (int side = 0; side < 2; side++) {
          int account = side ? e->credit_id : e->debit_id, party = side ? e->credit_subconto_id : e->debit_subconto_id;
          if (!party || !ledger_account_within(h->cfg, account, group)) continue;
          double amount = side ? -e->amount : e->amount;
          home_balance_t *b = balance_of(&by_party, group, party), *leaf = balance_of(&by_account, account, party);
          if (b) b->amount += amount;
          if (leaf) leaf->amount += amount;
        }
      }
      home_side_t debt = {0}, advance = {0};
      for (int i = 0; i < by_party.count; i++) {
        double net = home_round2(by_party.items[i].amount);
        int top = 0;
        double top_amount = 0;
        for (int k = 0; k < by_account.count; k++)   // the subaccount that holds most of it
          if (by_account.items[k].object == by_party.items[i].object && fabs(by_account.items[k].amount) > top_amount &&
              (by_account.items[k].amount > 0) == (net > 0)) { top_amount = fabs(by_account.items[k].amount); top = by_account.items[k].account; }
        if (net > 0.005) side_add(pass ? &advance : &debt, by_party.items[i].object, top, net);
        if (net < -0.005) side_add(pass ? &debt : &advance, by_party.items[i].object, top, -net);
      }
      for (int i = 0; i < by_account.count; i++)
        for (int k = 0; k < by_account.count; k++) {
          const home_balance_t *dr = &by_account.items[i], *cr = &by_account.items[k];
          if (dr->object != cr->object || dr->amount < 0.005 || cr->amount > -0.005) continue;
          home_balance_t *u = balance_of(unoffset, dr->account, dr->object);
          if (u) { u->amount = home_round2(fmin(dr->amount, -cr->amount)); u->quantity = cr->account; }
        }
      if (pass) {
        side_row(h, &debt, "payable", "We owe", group);
        side_row(h, &advance, "advance_paid", "Advances paid", group);
      } else {
        side_row(h, &debt, "receivable", "Owed to us", group);
        side_row(h, &advance, "advance", "Advances received", group);
      }
      free(by_party.items);
      free(by_account.items);
    }
  }
}

static void build_taxes(home_t *h, const home_accounts_t *output_tax, const home_accounts_t *input_tax) {
  home_accounts_t owed = *output_tax;
  result_node_t *taxes = home_fetch(h->cfg, TABLE_PAYROLL_TAXES);
  HOME_EACH(db_payroll_tax_t, t, taxes) accounts_add(&owed, t->credit_id);
  free_result_list(taxes);
  char codes[128];
  double payable = set_net(h, &owed, true);
  if (payable > 0) {
    codes_of(h, &owed, codes, sizeof(codes));
    db_home_row_t row = { .card = 3, .amount = payable };
    add_row(h, &row, "taxes", ui_tr("Taxes payable"), codes, NULL, "");
  }
  const db_setting_t *s = home_find(h->cfg, TABLE_SETTINGS, 1);
  double wages = s && s->payroll_account_id ? -net_on(h, s->payroll_account_id) : 0;
  if (wages > 0.005) {
    db_home_row_t row = { .card = 3, .account_id = s->payroll_account_id, .amount = wages };
    add_row(h, &row, "wages", ui_tr("Wages to pay"), home_code(h, s->payroll_account_id), NULL, "card");
  }
  double deductible = set_net(h, input_tax, false);
  if (deductible > 0) {
    codes_of(h, input_tax, codes, sizeof(codes));
    db_home_row_t row = { .card = 3, .amount = deductible };
    add_row(h, &row, "input_tax", ui_tr("Input tax to deduct"), codes, NULL, "");
  }
}

// ── Checks ─────────────────────────────────────────────────────────────────

static void home_describe(const db_document_t *d, char *out, size_t sz) {
  char date[16], total[48];
  ui_format_date(d->date, date, sizeof(date));
  snprintf(out, sz, ui_tr("%s of %s for %s"), d->number, date, home_money(d->total, total, sizeof(total)));
}

static int home_newest_first(const void *a, const void *b) {
  const db_document_t *x = *(db_document_t * const *)a, *y = *(db_document_t * const *)b;
  int c = strcmp(y->date, x->date);
  return c ? c : y->id - x->id;
}

static void check_drafts(home_t *h) {
  int count = 0;
  char detail[256] = "", one[96];
  result_node_t *docs = home_fetch(h->ib, TABLE_DOCUMENTS);
  HOME_EACH(db_document_t, d, docs) {
    if (d->posted) continue;
    if (++count > 2) continue;
    home_describe(d, one, sizeof(one));
    size_t used = strlen(detail);
    snprintf(detail + used, sizeof(detail) - used, "%s%s", used ? ", " : "", one);
  }
  free_result_list(docs);
  if (!count) return;
  if (count > 2) { size_t used = strlen(detail); snprintf(detail + used, sizeof(detail) - used, " …"); }
  char title[96];
  snprintf(title, sizeof(title), ui_tr("Documents not posted: %d"), count);
  db_home_row_t row = { .card = 1, .amount = count };
  add_row(h, &row, "draft", title, detail, ui_tr("Post"), "post");
}

static void home_month_name(const char *ym, char *out, size_t sz) {
  const char *names = ui_tr("January|February|March|April|May|June|July|August|September|October|November|December");
  int month = atoi(ym + 5), i = 1;
  const char *p = names;
  while (i < month && (p = strchr(p, '|'))) { p++; i++; }
  if (!p || month < 1 || month > 12) { snprintf(out, sz, "%.7s", ym); return; }
  snprintf(out, sz, "%.*s %.4s", (int)strcspn(p, "|"), p, ym);
}

// A month with posted entries and no posted closing is not closed, unless it
// is the month that is still running.
static void check_closing(home_t *h) {
  char months[64][8];
  int count = 0;
  for (int i = 0; i < h->count; i++) {
    const db_entry_t *e = h->entries[i];
    if (g_home_asof[0] && strcmp(e->date, g_home_asof) > 0) break;
    if (count && !strncmp(months[count - 1], e->date, 7)) continue;
    if (count < (int)ARRAY_LEN(months)) snprintf(months[count++], 8, "%.7s", e->date);
  }
  char today[8];
  time_t now = time(NULL);
  strftime(today, sizeof(today), "%Y-%m", localtime(&now));
  result_node_t *docs = home_fetch(h->ib, TABLE_DOCUMENTS);
  int open = 0, last = -1;
  for (int m = 0; m < count; m++) {
    bool closed = false;
    HOME_EACH(db_document_t, d, docs) {
      const db_doc_type_t *t = home_find(h->cfg, TABLE_DOC_TYPES, d->doc_type_id);
      if (d->posted && t && !strcmp(t->form, "closing") && !strncmp(d->date, months[m], 7)) closed = true;
    }
    if (!closed && strcmp(months[m], today) < 0) { open++; last = m; }
  }
  free_result_list(docs);
  if (last < 0) return;
  char name[48], title[128], steps[200] = "", detail[256];
  home_month_name(months[last], name, sizeof(name));
  snprintf(title, sizeof(title), ui_tr("Month not closed: %s"), name);
  result_node_t *rules = home_fetch(h->cfg, TABLE_CLOSING_RULES);
  int n = 0;
  HOME_EACH(db_closing_rule_t, r, rules) {
    if (!r->name[0] || ++n > 3) continue;
    size_t used = strlen(steps);
    snprintf(steps + used, sizeof(steps) - used, "%s%s", used ? ", " : "", r->name);
  }
  free_result_list(rules);
  if (open > 1) snprintf(detail, sizeof(detail), ui_tr("%d months are not closed. Not done yet: %s"), open, steps);
  else snprintf(detail, sizeof(detail), ui_tr("Not done yet: %s"), steps);
  static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  int y = atoi(months[last]), m = atoi(months[last] + 5);
  db_home_row_t row = { .card = 1 };
  snprintf(row.date, sizeof(row.date), "%04d-%02d-%02d", y, m, days[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)));
  add_row(h, &row, "month", title, detail, ui_tr("Close Month"), "close");
}

// A closing whose month changed after it: the accounts its transfers close
// are no longer zero on its date (1C's «нарушена последовательность»).
// Reposting in date order closes them again.
static void check_stale_closing(home_t *h) {
  result_node_t *docs = home_fetch(h->ib, TABLE_DOCUMENTS), *rules = home_fetch(h->cfg, TABLE_CLOSING_RULES);
  HOME_EACH(db_document_t, d, docs) {
    const db_doc_type_t *t = home_find(h->cfg, TABLE_DOC_TYPES, d->doc_type_id);
    if (!d->posted || !t || strcmp(t->form, "closing")) continue;
    char codes[128] = "";
    HOME_EACH(db_closing_rule_t, r, rules) {
      if (strcmp(r->kind, "transfer") || !r->account_id) continue;
      double net = 0;
      for (int i = 0; i < h->count && strcmp(h->entries[i]->date, d->date) <= 0; i++) {
        if (ledger_account_within(h->cfg, h->entries[i]->debit_id, r->account_id)) net += h->entries[i]->amount;
        if (ledger_account_within(h->cfg, h->entries[i]->credit_id, r->account_id)) net -= h->entries[i]->amount;
      }
      if (fabs(net) < 0.005 || strstr(codes, home_code(h, r->account_id))) continue;
      size_t used = strlen(codes);
      snprintf(codes + used, sizeof(codes) - used, "%s%s", used ? ", " : "", home_code(h, r->account_id));
    }
    if (!codes[0]) continue;
    char name[48], date[16], title[160], detail[256];
    home_month_name(d->date, name, sizeof(name));
    ui_format_date(d->date, date, sizeof(date));
    snprintf(title, sizeof(title), ui_tr("Closing of %s is out of date"), name);
    snprintf(detail, sizeof(detail), ui_tr("Documents changed after %s: %s are not zero on %s"), d->number, codes, date);
    db_home_row_t row = { .card = 1, .document_id = d->id };
    snprintf(row.date, sizeof(row.date), "%s", d->date);
    add_row(h, &row, "error", title, detail, ui_tr("Repost"), "repost");
  }
  free_result_list(docs);
  free_result_list(rules);
}

// Money and goods cannot go below zero: the first day each one ends below
// zero, with the document that took it there.
static void check_negative(home_t *h, const home_accounts_t *cash) {
  home_balances_t b = {0};
  for (int i = 0; i < h->count; i++) {
    const db_entry_t *e = h->entries[i];
    for (int side = 0; side < 2; side++) {
      int account = side ? e->credit_id : e->debit_id, object = side ? e->credit_subconto_id : e->debit_subconto_id;
      double sign = side ? -1 : 1;
      bool goods = !strcmp(ledger_subconto_table(h->cfg, account), "items") && object;
      if (!goods && !accounts_hold(h->cfg, cash, account)) continue;
      home_balance_t *r = balance_of(&b, account, goods ? object : 0);
      if (!r) continue;
      r->amount += sign * e->amount;
      r->quantity += sign * e->quantity;
      r->currency = e->document_id;   // the latest document that moved it
    }
    bool day_end = i + 1 == h->count || strcmp(h->entries[i + 1]->date, e->date);
    for (int k = 0; day_end && k < b.count; k++) {
      home_balance_t *r = &b.items[k];
      bool goods = r->object != 0;
      bool below = goods ? r->quantity < -0.0005 : r->amount < -0.005;
      if (!below || r->flagged) continue;
      r->flagged = true;
      const db_document_t *doc = home_find(h->ib, TABLE_DOCUMENTS, (int)r->currency);
      const db_account_t *a = home_find(h->cfg, TABLE_ACCOUNTS, r->account);
      const db_item_t *item = goods ? home_find(h->ib, TABLE_ITEMS, r->object) : NULL;
      char date[16], value[48], title[200], detail[256];
      ui_format_date(e->date, date, sizeof(date));
      if (goods) {
        ui_format_number(r->quantity, 3, value, sizeof(value));
        snprintf(title, sizeof(title), ui_tr("%s: stock below zero on %s"), item ? item->name : "", date);
      } else {
        home_money(r->amount, value, sizeof(value));
        snprintf(title, sizeof(title), ui_tr("%s: balance below zero on %s"), a ? a->name : "", date);
      }
      snprintf(detail, sizeof(detail), ui_tr("%s on %s after %s"), value, a ? a->code : "", doc ? doc->number : "");
      db_home_row_t row = { .card = 1, .document_id = doc ? doc->id : 0, .account_id = r->account };
      snprintf(row.date, sizeof(row.date), "%s", e->date);
      add_row(h, &row, "error", title, detail, ui_tr("Open"), "open");
    }
  }
  free(b.items);
}

// Input tax waiting on its account, and the document type that deducts it.
static void check_input_tax(home_t *h, const home_accounts_t *input_tax) {
  double waiting = set_net(h, input_tax, false);
  if (waiting < 0.005) return;
  const db_doc_type_t *deduction = NULL;
  result_node_t *types = home_fetch(h->cfg, TABLE_DOC_TYPES);
  HOME_EACH(db_doc_type_t, t, types)
    if (!deduction && !strcmp(t->form, "document") && t->credit_id && accounts_hold(h->cfg, input_tax, t->credit_id)) deduction = t;
  free_result_list(types);
  if (!deduction) return;
  char amount[48], title[160], codes[128], detail[256];
  snprintf(title, sizeof(title), ui_tr("Input tax %s is not deducted"), home_money(waiting, amount, sizeof(amount)));
  codes_of(h, input_tax, codes, sizeof(codes));
  snprintf(detail, sizeof(detail), ui_tr("It waits on %s. Deduct it with «%s»."), codes, deduction->name);
  db_home_row_t row = { .card = 1, .doc_type_id = deduction->id, .amount = waiting };
  add_row(h, &row, "tax", title, detail, ui_tr("Create"), "new");
}

static void check_unoffset(home_t *h, const home_balances_t *unoffset) {
  for (int i = 0; i < unoffset->count; i++) {
    const home_balance_t *u = &unoffset->items[i];
    const db_counterparty_t *c = home_find(h->ib, TABLE_COUNTERPARTIES, u->object);
    char amount[48], title[200], detail[256];
    snprintf(title, sizeof(title), ui_tr("%s: debt and advance are not offset"), c ? c->name : "");
    snprintf(detail, sizeof(detail), ui_tr("%s on %s against an advance on %s"), home_money(u->amount, amount, sizeof(amount)),
             home_code(h, u->account), home_code(h, (int)u->quantity));
    db_home_row_t row = { .card = 1, .account_id = home_group(h->cfg, u->account), .counterparty_id = u->object, .amount = u->amount };
    add_row(h, &row, "warning", title, detail, ui_tr("Account Card"), "card");
  }
}

// A draft sale to a counterparty who paid in advance: posting it offsets the advance.
static void tip_advances(home_t *h, const home_accounts_t *receivable) {
  result_node_t *docs = home_fetch(h->ib, TABLE_DOCUMENTS);
  HOME_EACH(db_document_t, d, docs) {
    const db_doc_type_t *t = home_find(h->cfg, TABLE_DOC_TYPES, d->doc_type_id);
    if (d->posted || !t || t->vat_book != 1 || !d->counterparty_id) continue;
    double advance = 0;
    for (int g = 0; g < receivable->count; g++)
      for (int i = 0; i < h->count; i++) {
        const db_entry_t *e = h->entries[i];
        if (ledger_account_within(h->cfg, e->debit_id, receivable->ids[g]) && e->debit_subconto_id == d->counterparty_id) advance -= e->amount;
        if (ledger_account_within(h->cfg, e->credit_id, receivable->ids[g]) && e->credit_subconto_id == d->counterparty_id) advance += e->amount;
      }
    if (advance < 0.005) continue;
    const db_counterparty_t *c = home_find(h->ib, TABLE_COUNTERPARTIES, d->counterparty_id);
    char title[200], detail[256], offset[48], whole[48];
    snprintf(title, sizeof(title), ui_tr("Tip: the advance from %s is not offset"), c ? c->name : "");
    snprintf(detail, sizeof(detail), ui_tr("Post %s: it offsets %s of the %s advance"), d->number,
             home_money(fmin(advance, d->total), offset, sizeof(offset)), home_money(advance, whole, sizeof(whole)));
    db_home_row_t row = { .card = 1, .document_id = d->id, .counterparty_id = d->counterparty_id, .amount = home_round2(advance) };
    add_row(h, &row, "tip", title, detail, ui_tr("Open"), "open");
  }
  free_result_list(docs);
}

// ── Documents ──────────────────────────────────────────────────────────────

static void build_create(home_t *h) {
  typedef struct { const db_doc_type_t *type; int uses; } use_t;
  use_t uses[64];
  int n = 0;
  result_node_t *types = home_fetch(h->cfg, TABLE_DOC_TYPES), *docs = home_fetch(h->ib, TABLE_DOCUMENTS);
  HOME_EACH(db_doc_type_t, t, types) {
    if (!strcmp(t->form, "closing") || n >= (int)ARRAY_LEN(uses)) continue;  // closing is the attention list's
    int count = 0;
    HOME_EACH(db_document_t, d, docs) if (d->doc_type_id == t->id) count++;
    uses[n++] = (use_t){ t, count };
  }
  for (int i = 0; i < n; i++)   // most used first, the configuration's order among equals
    for (int k = i + 1; k < n; k++)
      if (uses[k].uses > uses[i].uses) { use_t tmp = uses[i]; uses[i] = uses[k]; uses[k] = tmp; }
  for (int i = 0; i < n && i < HOME_LIST_LIMIT; i++) {
    db_home_row_t row = { .card = 4, .doc_type_id = uses[i].type->id };
    add_row(h, &row, uses[i].type->form, uses[i].type->name, "", NULL, "new");
  }
  free_result_list(types);
  free_result_list(docs);
}

static void build_recent(home_t *h) {
  result_node_t *docs = home_fetch(h->ib, TABLE_DOCUMENTS);
  int n = count_result_list(docs), count = 0;
  db_document_t **order = n ? malloc((size_t)n * sizeof(*order)) : NULL;
  HOME_EACH(db_document_t, d, docs) if (order) order[count++] = d;
  if (order) qsort(order, (size_t)count, sizeof(*order), home_newest_first);
  for (int i = 0; i < count && i < HOME_LIST_LIMIT; i++) {
    db_recent_document_t row = { .id = i + 1, .document_id = order[i]->id, .posted = order[i]->posted };
    send_db_message(h->ib, dbInsert, TABLE_RECENT_DOCUMENTS, &row);
  }
  free(order);
  free_result_list(docs);
}

// The date the page speaks of: the report period's end, else the latest document's.
static void choose_date(home_t *h, const char *period_end) {
  g_home_asof[0] = 0;
  if (period_end && *period_end) { snprintf(g_home_asof, sizeof(g_home_asof), "%s", period_end); return; }
  result_node_t *docs = home_fetch(h->ib, TABLE_DOCUMENTS);
  HOME_EACH(db_document_t, d, docs) if (strcmp(d->date, g_home_asof) > 0) snprintf(g_home_asof, sizeof(g_home_asof), "%s", d->date);
  free_result_list(docs);
}

void ledger_build_home(database_t *ib, database_t *cfg, db_entry_t **entries, int count, const char *period_end) {
  home_clear(ib, TABLE_HOME_ROWS);
  home_clear(ib, TABLE_RECENT_DOCUMENTS);
  home_t h = { ib, cfg, entries, count, 0 };
  choose_date(&h, period_end);
  home_accounts_t cash = {0}, receivable = {0}, payable = {0}, output_tax = {0}, input_tax = {0};
  money_accounts(&h, &cash);
  settlement_accounts(&h, &receivable, &payable, &output_tax, &input_tax);
  home_balances_t unoffset = {0};
  check_negative(&h, &cash);
  check_stale_closing(&h);
  check_drafts(&h);
  check_closing(&h);
  check_input_tax(&h, &input_tax);
  build_settlements(&h, &receivable, &payable, &unoffset);   // card 3, and the pairs nobody offset
  check_unoffset(&h, &unoffset);
  tip_advances(&h, &receivable);
  build_money(&h, &cash);
  build_taxes(&h, &output_tax, &input_tax);
  build_create(&h);
  build_recent(&h);
  free(unoffset.items);
}

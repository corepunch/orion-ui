// import_stage.c — loads the staging file `inspect_1cd.py export-stage` writes
// from a 1CD database (orion-1cd-stage-v1) into the infobase.
//
// Imported postings keep their original 1C accounts: an account the
// configuration lacks (matched by code) is added to the infobase as an
// ext_account with its analytics kinds. A kind whose 1C type is a catalog
// Ledger has (counterparties, items ...) maps onto that table; any other kind
// gets a kind of its own over the general analytic_items catalog.
//
// Every record imported keeps its source metadata name and GUID in
// import_sources, apart from the Ledger record, so importing again finds it.
// The import is one editing context: a file that fails leaves nothing behind.

#include "ledger.h"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <orion/user/money.h>
#include <stdalign.h>
#include <stddef.h>

#define STAGE_FORMAT "orion-1cd-stage-v1"

#define EACH_RECORD(type, var, list) \
  for (result_node_t *var##_node = (list); var##_node; var##_node = var##_node->next) \
    for (type *var = *(type **)var##_node->data; var; var = NULL)

typedef struct { const char *ref_type, *table; } stage_table_t;
static const stage_table_t kKindTables[] = {
  { "Справочник.Контрагенты",         "counterparties" },
  { "Справочник.Номенклатура",        "items" },
  { "Справочник.ФизическиеЛица",      "employees" },
  { "Справочник.ОсновныеСредства",    "assets" },
  { "Справочник.ДоговорыКонтрагентов", "contracts" },
  { "Справочник.Склады",              "warehouses" },
  { "Справочник.БанковскиеСчета",     "bank_accounts" },
};

typedef struct {
  database_t *ib, *cfg;
  ledger_stage_import_t *out;
  xmlNodePtr records;                            // <records>: the catalogs
  struct { char ref_type[96]; int kind_id; } kinds[128];  // 1C type an analytics kind holds -> Ledger kind
  int kind_count;
  xmlNodePtr documents;                          // <documents>
  struct { xmlNodePtr node; int next; } *movements;  // register movements, chained by recorder
  int movement_count, buckets[1024];             // buckets hold index + 1; 0 is empty
} stage_t;

void ledger_copy_text(char *dst, size_t sz, const char *src) {
  size_t n = strlen(src);
  if (n >= sz) {
    n = sz - 1;
    while (n && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
  }
  memcpy(dst, src, n);
  dst[n] = '\0';
}

static bool is_el(xmlNodePtr n, const char *name) {
  return n && n->type == XML_ELEMENT_NODE && !strcmp((const char *)n->name, name);
}

static xmlNodePtr child(xmlNodePtr n, const char *name) {
  for (xmlNodePtr c = n ? n->children : NULL; c; c = c->next) if (is_el(c, name)) return c;
  return NULL;
}

static void attr(xmlNodePtr n, const char *name, char *buf, size_t sz) {
  xmlChar *v = n ? xmlGetProp(n, (const xmlChar *)name) : NULL;
  ledger_copy_text(buf, sz, v ? (const char *)v : "");
  if (v) xmlFree(v);
}

static void note(stage_t *st, const char *fmt, const char *a, const char *b) {
  char line[256];
  snprintf(line, sizeof(line), ui_tr(fmt), a, b);  // the format is a whole sentence
  if (strstr(st->out->notes, line)) return;  // one line per distinct item
  size_t used = strlen(st->out->notes), n = strlen(line);
  if (used + n + 2 >= sizeof(st->out->notes)) return;
  memcpy(st->out->notes + used, line, n);
  st->out->notes[used + n] = '\n'; st->out->notes[used + n + 1] = '\0';
}

static void tally(stage_t *st, const char *type, int added, int updated, int held) {
  ledger_stage_import_t *o = st->out;
  char key[sizeof(o->counts[0].type)];
  ledger_copy_text(key, sizeof(key), type);
  int i = 0;
  while (i < o->count_types && strcmp(o->counts[i].type, key)) i++;
  if (i == o->count_types) {
    if (i == (int)ARRAY_LEN(o->counts)) return;
    memcpy(o->counts[i].type, key, sizeof(key));
    o->count_types++;
  }
  o->counts[i].added += added; o->counts[i].updated += updated; o->counts[i].held += held;
}

static void *find_by_text(database_t *db, int table, const char *field, const char *value) {
  return (void *)send_db_message(db, dbFind, MAKEDWORD(table, ledger_field(db, table, field)), (void *)value);
}

// ── Source identity ──────────────────────────────────────────────────────────
static db_import_source_t *source_find(stage_t *st, const char *guid) {
  return guid[0] ? (db_import_source_t *)find_by_text(st->ib, TABLE_IMPORT_SOURCES, "guid", guid) : NULL;
}

static bool source_remember(stage_t *st, const char *metadata, const char *guid, const char *table, int record_id) {
  if (!guid[0] || source_find(st, guid)) return true;
  db_import_source_t s = { .record_id = record_id };
  ledger_copy_text(s.metadata, sizeof(s.metadata), metadata);
  ledger_copy_text(s.guid, sizeof(s.guid), guid);
  ledger_copy_text(s.table_name, sizeof(s.table_name), table);
  return send_db_message(st->ib, dbInsert, TABLE_IMPORT_SOURCES, &s) != 0;
}

// Next free id above LEDGER_EXT_ID_BASE among the infobase's extension records.
static int next_ext_id(database_t *ib, int table_id) {
  int top = LEDGER_EXT_ID_BASE;
  result_node_t *all = (result_node_t *)send_db_message(ib, dbFetch, MAKEDWORD(table_id, 0), NULL);
  EACH_RECORD(int, key, all) if (*key > top) top = *key;
  free_result_list(all);
  return top + 1;
}

// ── Analytics kinds ──────────────────────────────────────────────────────────
static const char *kind_table_for(xmlNodePtr rec) {
  xmlNodePtr types = child(rec, "types"), only = NULL;
  int count = 0;
  for (xmlNodePtr t = types ? types->children : NULL; t; t = t->next) if (is_el(t, "type")) { only = t; count++; }
  if (count != 1) return NULL;
  char ref[96];
  attr(only, "type", ref, sizeof(ref));
  for (size_t i = 0; i < ARRAY_LEN(kKindTables); i++) if (!strcmp(ref, kKindTables[i].ref_type)) return kKindTables[i].table;
  return NULL;
}

static void map_kind(stage_t *st, xmlNodePtr rec, int kind_id) {
  xmlNodePtr types = child(rec, "types"), only = NULL;
  int count = 0;
  for (xmlNodePtr t = types ? types->children : NULL; t; t = t->next) if (is_el(t, "type")) { only = t; count++; }
  if (count != 1 || st->kind_count == (int)ARRAY_LEN(st->kinds)) return;
  attr(only, "type", st->kinds[st->kind_count].ref_type, sizeof(st->kinds[0].ref_type));
  st->kinds[st->kind_count++].kind_id = kind_id;
}

static void import_kind(stage_t *st, xmlNodePtr rec) {
  char metadata[256], guid[40], code[64], name[512];
  attr(rec, "type", metadata, sizeof(metadata)); attr(rec, "id", guid, sizeof(guid));
  attr(rec, "code", code, sizeof(code)); attr(rec, "description", name, sizeof(name));
  db_import_source_t *seen = source_find(st, guid);
  if (seen) { st->out->updated++; map_kind(st, rec, seen->record_id); return; }
  const char *table = kind_table_for(rec);
  db_subconto_kind_t *known = table ? (db_subconto_kind_t *)find_by_text(st->cfg, TABLE_SUBCONTO_KINDS, "table", table) : NULL;
  if (known) {
    st->out->kinds_mapped++;
    map_kind(st, rec, known->id);
    if (!source_remember(st, metadata, guid, "subconto_kinds", known->id)) snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("Cannot record the source of an imported record."));
    return;
  }
  db_ext_subconto_kind_t k = { .id = next_ext_id(st->ib, TABLE_EXT_SUBCONTO_KINDS) };
  ledger_copy_text(k.code, sizeof(k.code), code);
  ledger_copy_text(k.name, sizeof(k.name), name);
  snprintf(k.table, sizeof(k.table), "analytic_items");
  if (!send_db_message(st->ib, dbInsert, TABLE_EXT_SUBCONTO_KINDS, &k) || !source_remember(st, metadata, guid, "ext_subconto_kinds", k.id)) {
    snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("Cannot store an imported analytics kind."));
    return;
  }
  st->out->kinds_added++;
  map_kind(st, rec, k.id);
}

static int ledger_kind_for(stage_t *st, const char *guid) {
  db_import_source_t *s = source_find(st, guid);
  return s ? s->record_id : 0;
}

// ── Accounts ─────────────────────────────────────────────────────────────────
static const char *ledger_account_kind(const char *kind) {
  return !strcmp(kind, "active") ? "А" : !strcmp(kind, "passive") ? "П" : "АП";
}

// 1C's analytics slots the configuration's account lacks are kept as an
// extension of the matched account: an ext_account row carrying the account's
// own id, which ledger_apply_extensions lays over the configuration's record.
static void extend_matched_account(stage_t *st, xmlNodePtr rec, const db_account_t *known, const char *code) {
  db_ext_account_t a = { .id = known->id, .currency = known->currency };
  ledger_copy_text(a.code, sizeof(a.code), known->code);
  ledger_copy_text(a.name, sizeof(a.name), known->name);
  ledger_copy_text(a.kind, sizeof(a.kind), known->kind);
  ledger_copy_text(a.section, sizeof(a.section), known->section);
  int *slots[3] = { &a.subconto_kind_id, &a.subconto2_kind_id, &a.subconto3_kind_id };
  const int have[3] = { known->subconto_kind_id, known->subconto2_kind_id, known->subconto3_kind_id };
  for (int i = 0; i < 3; i++) *slots[i] = have[i];
  bool fill = false;
  for (xmlNodePtr s = rec->children; s; s = s->next) {
    if (!is_el(s, "subkonto")) continue;
    char n[8], kind_guid[40];
    attr(s, "n", n, sizeof(n)); attr(s, "kind", kind_guid, sizeof(kind_guid));
    int slot = atoi(n) - 1, id = ledger_kind_for(st, kind_guid);
    if (slot < 0 || slot > 2 || !id) { note(st, "account %s: analytics slot %s has no usable kind", code, n); continue; }
    if (!*slots[slot]) { *slots[slot] = id; fill = true; }
  }
  if (fill && !send_db_message(st->ib, dbInsert, TABLE_EXT_ACCOUNTS, &a))
    snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("Cannot store an imported account."));
}

static void import_account(stage_t *st, xmlNodePtr rec) {
  char metadata[256], guid[40], code[64], name[512], kind[32], currency[8];
  attr(rec, "type", metadata, sizeof(metadata)); attr(rec, "id", guid, sizeof(guid));
  attr(rec, "code", code, sizeof(code)); attr(rec, "description", name, sizeof(name));
  attr(rec, "kind", kind, sizeof(kind)); attr(rec, "currency", currency, sizeof(currency));
  if (source_find(st, guid)) { st->out->updated++; return; }
  db_account_t *known = (db_account_t *)find_by_text(st->cfg, TABLE_ACCOUNTS, "code", code);
  if (known) {
    st->out->accounts_matched++;
    if (!source_remember(st, metadata, guid, "accounts", known->id)) snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("Cannot record the source of an imported record."));
    else extend_matched_account(st, rec, known, code);
    return;
  }
  db_ext_account_t a = { .id = next_ext_id(st->ib, TABLE_EXT_ACCOUNTS), .currency = !strcmp(currency, "true") };
  ledger_copy_text(a.code, sizeof(a.code), code);
  ledger_copy_text(a.name, sizeof(a.name), name);
  ledger_copy_text(a.kind, sizeof(a.kind), ledger_account_kind(kind));
  int *slots[3] = { &a.subconto_kind_id, &a.subconto2_kind_id, &a.subconto3_kind_id };
  for (xmlNodePtr s = rec->children; s; s = s->next) {
    if (!is_el(s, "subkonto")) continue;
    char n[8], kind_guid[40];
    attr(s, "n", n, sizeof(n)); attr(s, "kind", kind_guid, sizeof(kind_guid));
    int slot = atoi(n) - 1, id = ledger_kind_for(st, kind_guid);
    if (slot < 0 || slot > 2 || !id) { note(st, "account %s: analytics slot %s has no usable kind", code, n); continue; }
    *slots[slot] = id;
  }
  if (!send_db_message(st->ib, dbInsert, TABLE_EXT_ACCOUNTS, &a) || !source_remember(st, metadata, guid, "ext_accounts", a.id)) {
    snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("Cannot store an imported account."));
    return;
  }
  st->out->accounts_added++;
}


// ── Catalogs ─────────────────────────────────────────────────────────────────
static xmlNodePtr v_node(xmlNodePtr rec, const char *name) {
  for (xmlNodePtr c = rec ? rec->children : NULL; c; c = c->next) {
    if (!is_el(c, "v")) continue;
    char n[160];
    attr(c, "n", n, sizeof(n));
    if (!strcmp(n, name)) return c;
  }
  return NULL;
}

static void v_text(xmlNodePtr rec, const char *name, char *buf, size_t sz) {
  buf[0] = '\0';
  xmlNodePtr n = v_node(rec, name);
  char t[16];
  attr(n, "t", t, sizeof(t));
  xmlChar *v = n && strcmp(t, "null") && strcmp(t, "blob") ? xmlNodeGetContent(n) : NULL;
  if (v) { ledger_copy_text(buf, sz, (const char *)v); xmlFree(v); }
}

static void v_enum(xmlNodePtr rec, const char *name, char *buf, size_t sz) { attr(v_node(rec, name), "name", buf, sz); }
static void v_ref(xmlNodePtr rec, const char *name, char *guid, size_t sz) { attr(v_node(rec, name), "id", guid, sz); }

// The Ledger id of the record a 1C reference names; 0 when it was not imported or is the national currency.
static int ref_id(stage_t *st, xmlNodePtr rec, const char *name) {
  char guid[40];
  v_ref(rec, name, guid, sizeof(guid));
  db_import_source_t *s = source_find(st, guid);
  return s ? s->record_id : 0;
}

static xmlNodePtr record_by_guid(stage_t *st, const char *guid) {
  for (xmlNodePtr r = guid[0] && st->records ? st->records->children : NULL; r; r = r->next) {
    char id[40];
    if (!is_el(r, "record")) continue;
    attr(r, "id", id, sizeof(id));
    if (!strcmp(id, guid)) return r;
  }
  return NULL;
}

typedef struct stage_catalog {
  const char *type;     // 1C metadata name of the catalog
  int table_id;
  const char *table;    // Ledger table, also the source identity's table name
  size_t size;          // record size
  // Fills the record from the 1C one; false holds the record back, saying why in `why`.
  bool (*fill)(stage_t *, xmlNodePtr rec, void *out, const char **why);
  const char *match_field;  // adopt a Ledger record with the same value instead of adding one
} stage_catalog_t;

static void text_field(char *dst, size_t sz, xmlNodePtr rec, const char *name) {
  char buf[512];
  v_text(rec, name, buf, sizeof(buf));
  ledger_copy_text(dst, sz, buf);
}

static bool fill_counterparty(stage_t *st, xmlNodePtr rec, void *out, const char **why) {
  db_counterparty_t *c = out;
  char name[512];
  attr(rec, "description", name, sizeof(name));
  ledger_copy_text(c->name, sizeof(c->name), name);
  text_field(c->tax_id, sizeof(c->tax_id), rec, "ИНН");
  text_field(c->kpp, sizeof(c->kpp), rec, "КПП");
  return true;
}

static bool fill_item(stage_t *st, xmlNodePtr rec, void *out, const char **why) {
  db_item_t *it = out;
  char name[512], guid[40];
  attr(rec, "description", name, sizeof(name));
  ledger_copy_text(it->name, sizeof(it->name), name);
  text_field(it->sku, sizeof(it->sku), rec, "Артикул");
  v_ref(rec, "ЕдиницаИзмерения", guid, sizeof(guid));
  xmlNodePtr unit = record_by_guid(st, guid);
  if (unit) {
    attr(unit, "description", name, sizeof(name));
    db_unit_t *u = (db_unit_t *)find_by_text(st->cfg, TABLE_UNITS, "name", name);
    if (u) it->unit_id = u->id;
  }
  return true;
}

static bool fill_employee(stage_t *st, xmlNodePtr rec, void *out, const char **why) {
  db_employee_t *e = out;
  char name[512];
  text_field(e->name, sizeof(e->name), rec, "ФИО");
  if (!e->name[0]) { attr(rec, "description", name, sizeof(name)); ledger_copy_text(e->name, sizeof(e->name), name); }
  text_field(e->tax_id, sizeof(e->tax_id), rec, "ИНН");
  return true;
}

static bool fill_warehouse(stage_t *st, xmlNodePtr rec, void *out, const char **why) {
  db_warehouse_t *w = out;
  char name[512], type[64];
  attr(rec, "description", name, sizeof(name));
  ledger_copy_text(w->name, sizeof(w->name), name);
  v_enum(rec, "ТипСклада", type, sizeof(type));
  ledger_copy_text(w->kind, sizeof(w->kind), !strcmp(type, "ОптовыйСклад") ? "wholesale" : !strcmp(type, "РозничныйМагазин") ? "retail" : "");
  return true;
}

static bool fill_contract(stage_t *st, xmlNodePtr rec, void *out, const char **why) {
  db_contract_t *c = out;
  char owner[40], name[512], date[32], kind[64];
  attr(rec, "owner", owner, sizeof(owner));
  db_import_source_t *cp = source_find(st, owner);
  if (!cp) { *why = ui_tr("its counterparty was not imported"); return false; }
  c->counterparty_id = cp->record_id;
  c->currency_id = ref_id(st, rec, "ВалютаВзаиморасчетов");
  attr(rec, "description", name, sizeof(name));
  ledger_copy_text(c->name, sizeof(c->name), name);
  text_field(c->number, sizeof(c->number), rec, "Номер");
  v_text(rec, "Дата", date, sizeof(date));
  ledger_copy_text(c->date, sizeof(c->date), date);
  v_text(rec, "СрокДействия", date, sizeof(date));
  if (strlen(date) >= 10) { date[10] = '\0'; ledger_copy_text(c->valid_until, sizeof(c->valid_until), date); }
  v_enum(rec, "ВидДоговора", kind, sizeof(kind));
  ledger_copy_text(c->kind, sizeof(c->kind), !strcmp(kind, "СПоставщиком") ? "supplier" : !strcmp(kind, "СПокупателем") ? "customer" : "other");
  return true;
}

static bool fill_bank_account(stage_t *st, xmlNodePtr rec, void *out, const char **why) {
  db_bank_account_t *b = out;
  char owner[40], owner_type[64], name[512], guid[40];
  attr(rec, "owner", owner, sizeof(owner)); attr(rec, "owner-type", owner_type, sizeof(owner_type));
  db_import_source_t *own = source_find(st, owner);
  if (!own) { *why = ui_tr("its owner was not imported"); return false; }
  if (!strcmp(owner_type, "Справочник.Организации")) b->organization_id = own->record_id; else b->counterparty_id = own->record_id;
  b->currency_id = ref_id(st, rec, "ВалютаДенежныхСредств");
  attr(rec, "description", name, sizeof(name));
  ledger_copy_text(b->name, sizeof(b->name), name);
  text_field(b->number, sizeof(b->number), rec, "НомерСчета");
  v_ref(rec, "Банк", guid, sizeof(guid));
  xmlNodePtr bank = record_by_guid(st, guid);
  if (bank) {
    attr(bank, "description", name, sizeof(name)); ledger_copy_text(b->bank, sizeof(b->bank), name);
    attr(bank, "code", name, sizeof(name)); ledger_copy_text(b->bik, sizeof(b->bik), name);
    text_field(b->corr_account, sizeof(b->corr_account), bank, "КоррСчет");
    text_field(b->swift, sizeof(b->swift), bank, "СВИФТБИК");
  }
  return true;
}

static bool fill_currency(stage_t *st, xmlNodePtr rec, void *out, const char **why) {
  db_currency_t *c = out;
  char name[512];
  attr(rec, "code", name, sizeof(name)); ledger_copy_text(c->code, sizeof(c->code), name);
  attr(rec, "description", name, sizeof(name)); ledger_copy_text(c->name, sizeof(c->name), name);
  return true;
}

// The organization is the infobase's one: its record takes the 1C organization's details.
static void import_organization(stage_t *st, xmlNodePtr rec) {
  char guid[40], text[512];
  attr(rec, "id", guid, sizeof(guid));
  db_organization_t *own = (db_organization_t *)send_db_message(st->ib, dbFind, MAKEDWORD(TABLE_ORGANIZATIONS, 0), (void *)(intptr_t)1);
  db_import_source_t *seen = source_find(st, guid);
  if (!own || (!seen && own->tax_id[0])) {
    note(st, "%s: the infobase already has an organization; not replaced", "Справочник.Организации", "");
    tally(st, "Справочник.Организации", 0, 0, 1);
    return;
  }
  if (seen && seen->record_id != own->id) return;
  db_organization_t o = *own;
  attr(rec, "description", text, sizeof(text)); ledger_copy_text(o.name, sizeof(o.name), text);
  text_field(o.tax_id, sizeof(o.tax_id), rec, "ИНН");
  text_field(o.kpp, sizeof(o.kpp), rec, "КПП");
  char bank_guid[40];
  v_ref(rec, "ОсновнойБанковскийСчет", bank_guid, sizeof(bank_guid));
  xmlNodePtr account = record_by_guid(st, bank_guid), bank = NULL;
  if (account) {
    text_field(o.bank_account, sizeof(o.bank_account), account, "НомерСчета");
    v_ref(account, "Банк", bank_guid, sizeof(bank_guid));
    bank = record_by_guid(st, bank_guid);
  }
  if (bank) {
    attr(bank, "description", text, sizeof(text)); ledger_copy_text(o.bank, sizeof(o.bank), text);
    attr(bank, "code", text, sizeof(text)); ledger_copy_text(o.bik, sizeof(o.bik), text);
    text_field(o.corr_account, sizeof(o.corr_account), bank, "КоррСчет");
  }
  if (!send_db_message(st->ib, dbUpdate, TABLE_ORGANIZATIONS, &o) || !source_remember(st, "Справочник.Организации", guid, "organizations", o.id))
    snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("Cannot store the imported organization."));
  else tally(st, "Справочник.Организации", 0, 1, 0);
}

// Order matters: a record refers to those before it.
static const stage_catalog_t kStageCatalogs[] = {
  { "Справочник.Валюты",                TABLE_CURRENCIES,    "currencies",    sizeof(db_currency_t),     fill_currency,     "code" },
  { "Справочник.Контрагенты",           TABLE_COUNTERPARTIES, "counterparties", sizeof(db_counterparty_t), fill_counterparty, "tax_id" },
  { "Справочник.ФизическиеЛица",        TABLE_EMPLOYEES,     "employees",     sizeof(db_employee_t),     fill_employee,     "tax_id" },
  { "Справочник.Склады",                TABLE_WAREHOUSES,    "warehouses",    sizeof(db_warehouse_t),    fill_warehouse,    NULL },
  { "Справочник.Номенклатура",          TABLE_ITEMS,         "items",         sizeof(db_item_t),         fill_item,         "sku" },
  { "Справочник.БанковскиеСчета",       TABLE_BANK_ACCOUNTS, "bank_accounts", sizeof(db_bank_account_t), fill_bank_account, NULL },
  { "Справочник.ДоговорыКонтрагентов",  TABLE_CONTRACTS,     "contracts",     sizeof(db_contract_t),     fill_contract,     NULL },
};

// The key of a Ledger record found by the catalog's match field, 0 when none or nothing to match on.
static int match_existing(stage_t *st, const stage_catalog_t *cat, const void *rec) {
  if (!cat->match_field) return 0;
  int f = ledger_field(st->ib, cat->table_id, cat->match_field);
  const db_table_schema_t *t = db_schema_table(st->ib->schema, cat->table_id);
  if (f < 0 || !t) return 0;
  const char *value = (const char *)rec + t->meta[f].offset;
  if (!value[0]) return 0;
  const int *found = (const int *)send_db_message(st->ib, dbFind, MAKEDWORD(cat->table_id, f), (void *)value);
  return found ? *found : 0;
}

static void import_catalog_record(stage_t *st, const stage_catalog_t *cat, xmlNodePtr rec) {
  char guid[40], folder[8], deleted[8];
  attr(rec, "id", guid, sizeof(guid)); attr(rec, "folder", folder, sizeof(folder)); attr(rec, "deleted", deleted, sizeof(deleted));
  if (!strcmp(folder, "true")) { tally(st, cat->type, 0, 0, 1); return; }  // groups have no Ledger counterpart
  alignas(max_align_t) char buf[2048] = {0};
  _Static_assert(sizeof(db_contract_t) <= 2048 && sizeof(db_counterparty_t) <= 2048 && sizeof(db_item_t) <= 2048 &&
                 sizeof(db_bank_account_t) <= 2048 && sizeof(db_employee_t) <= 2048, "catalog records fit the buffer");
  const char *why = "";
  if (!cat->fill(st, rec, buf, &why)) {
    note(st, "%s: held back (%s)", cat->type, why);
    tally(st, cat->type, 0, 0, 1);
    return;
  }
  db_import_source_t *seen = source_find(st, guid);
  const db_table_schema_t *t = db_schema_table(st->ib->schema, cat->table_id);
  int key = seen ? seen->record_id : match_existing(st, cat, buf);
  // The national currency is not in the currencies catalog: remembered as id 0.
  if (cat->table_id == TABLE_CURRENCIES && !strcmp(((db_currency_t *)buf)->code, "643")) {
    if (!seen && !source_remember(st, cat->type, guid, cat->table, 0)) goto failed;
    tally(st, cat->type, 0, seen != NULL, 0);
    return;
  }
  if (key) {
    *(int *)((char *)buf + t->meta[0].offset) = key;
    if (!send_db_message(st->ib, dbUpdate, (uint32_t)cat->table_id, buf)) goto failed;
    if (!seen && !source_remember(st, cat->type, guid, cat->table, key)) goto failed;
    tally(st, cat->type, 0, 1, 0);
    return;
  }
  const int *added = (const int *)send_db_message(st->ib, dbInsert, (uint32_t)cat->table_id, buf);
  if (!added || !source_remember(st, cat->type, guid, cat->table, *added)) goto failed;
  tally(st, cat->type, 1, 0, 0);
  return;
failed:
  snprintf(st->out->error, sizeof(st->out->error), ui_tr("Cannot store an imported record of %s."), cat->type);
}

// Records of the types analytics kinds hold become analytic_items of that kind.
static void import_analytic_records(stage_t *st) {
  for (xmlNodePtr rec = st->records ? st->records->children : NULL; rec && !st->out->error[0]; rec = rec->next) {
    char type[96], guid[40], folder[8];
    if (!is_el(rec, "record")) continue;
    attr(rec, "type", type, sizeof(type)); attr(rec, "id", guid, sizeof(guid)); attr(rec, "folder", folder, sizeof(folder));
    bool own = !strcmp(type, "Справочник.Организации");  // catalogs with a Ledger table of their own
    for (size_t i = 0; i < ARRAY_LEN(kStageCatalogs); i++) own |= !strcmp(type, kStageCatalogs[i].type);
    if (own) continue;
    int kind = 0;
    for (int i = 0; i < st->kind_count && !kind; i++) if (!strcmp(st->kinds[i].ref_type, type)) kind = st->kinds[i].kind_id;
    db_ext_subconto_kind_t *ext = kind ? (db_ext_subconto_kind_t *)send_db_message(st->ib, dbFind, MAKEDWORD(TABLE_EXT_SUBCONTO_KINDS, 0), (void *)(intptr_t)kind) : NULL;
    if (!ext || strcmp(ext->table, "analytic_items")) continue;
    if (!strcmp(folder, "true")) { tally(st, type, 0, 0, 1); continue; }
    char text[512];
    db_analytic_item_t a = { .kind_id = kind };
    attr(rec, "code", text, sizeof(text)); ledger_copy_text(a.code, sizeof(a.code), text);
    attr(rec, "description", text, sizeof(text)); ledger_copy_text(a.name, sizeof(a.name), text);
    db_import_source_t *seen = source_find(st, guid);
    if (seen && strcmp(seen->table_name, "analytic_items")) {
      note(st, "%s: also a catalog of its own; as analytics it is held back", type, "");
      tally(st, type, 0, 0, 1);
      continue;
    }
    if (seen) {
      a.id = seen->record_id;
      if (!send_db_message(st->ib, dbUpdate, TABLE_ANALYTIC_ITEMS, &a)) snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("Cannot store an imported analytics record."));
      tally(st, type, 0, 1, 0);
      continue;
    }
    const int *added = (const int *)send_db_message(st->ib, dbInsert, TABLE_ANALYTIC_ITEMS, &a);
    if (!added || !source_remember(st, type, guid, "analytic_items", *added)) snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("Cannot store an imported analytics record."));
    else tally(st, type, 1, 0, 0);
  }
}

// Catalog types nothing imports are listed, not dropped silently.
static void report_unimported(stage_t *st) {
  char seen[48][96];
  int count = 0;
  for (xmlNodePtr rec = st->records ? st->records->children : NULL; rec; rec = rec->next) {
    char type[96];
    if (!is_el(rec, "record")) continue;
    attr(rec, "type", type, sizeof(type));
    bool known = false;
    for (int i = 0; i < st->out->count_types && !known; i++) known = !strcmp(st->out->counts[i].type, type);
    for (int i = 0; i < count && !known; i++) known = !strcmp(seen[i], type);
    if (known || count == 48) continue;
    ledger_copy_text(seen[count++], sizeof(seen[0]), type);
    note(st, "%s: no Ledger catalog for this type, not imported", type, "");
  }
}

// ── Documents and their register movements ──────────────────────────────────
static unsigned guid_bucket(const char *guid) {
  unsigned h = 2166136261u;
  for (; *guid; guid++) h = (h ^ (unsigned char)*guid) * 16777619u;
  return h % 1024;
}

static bool index_movements(stage_t *st, xmlNodePtr root) {
  int count = 0;
  xmlNodePtr registers = child(root, "registers");
  for (xmlNodePtr reg = registers ? registers->children : NULL; reg; reg = reg->next)
    for (xmlNodePtr m = reg->children; m; m = m->next) count += is_el(m, "movement");
  st->movements = calloc((size_t)count + 1, sizeof(*st->movements));
  if (!st->movements) return false;
  for (xmlNodePtr reg = registers ? registers->children : NULL; reg; reg = reg->next)
    for (xmlNodePtr m = reg->children; m; m = m->next) {
      char recorder[40], active[8];
      if (!is_el(m, "movement")) continue;
      attr(m, "active", active, sizeof(active));
      if (!strcmp(active, "false")) continue;  // 1C ignores inactive records
      attr(m, "recorder", recorder, sizeof(recorder));
      unsigned b = guid_bucket(recorder);
      st->movements[st->movement_count].node = m;
      st->movements[st->movement_count].next = st->buckets[b];
      st->buckets[b] = ++st->movement_count;
    }
  return true;
}

static xmlNodePtr document_by_guid(stage_t *st, const char *guid) {
  for (xmlNodePtr d = guid[0] && st->documents ? st->documents->children : NULL; d; d = d->next) {
    char id[40];
    if (!is_el(d, "document")) continue;
    attr(d, "id", id, sizeof(id));
    if (!strcmp(id, guid)) return d;
  }
  return NULL;
}

// What an analytics value names: a catalog record, a document, or an enum value.
// Kinds over the general catalog take a record of it, found again through the
// value's GUID joined with the kind, since one GUID may serve several kinds.
static int analytics_value(stage_t *st, const db_account_t *account, int slot, xmlNodePtr node) {
  int kind_id = slot == 1 ? account->subconto_kind_id : slot == 2 ? account->subconto2_kind_id : account->subconto3_kind_id;
  char t[16], guid[40], empty[8], type[128], name[160];
  attr(node, "t", t, sizeof(t)); attr(node, "id", guid, sizeof(guid)); attr(node, "empty", empty, sizeof(empty));
  attr(node, "type", type, sizeof(type)); attr(node, "name", name, sizeof(name));
  if (!guid[0] || !strcmp(empty, "true") || !strcmp(t, "null")) return 0;
  const db_subconto_kind_t *kind = kind_id ? (const db_subconto_kind_t *)send_db_message(st->cfg, dbFind, MAKEDWORD(TABLE_SUBCONTO_KINDS, 0), (void *)(intptr_t)kind_id) : NULL;
  if (!kind) { note(st, "account %s: analytics value with no kind in slot %s", account->code, slot == 1 ? "1" : slot == 2 ? "2" : "3"); return 0; }
  bool general = !strcmp(kind->table, "analytic_items");
  db_import_source_t *seen = source_find(st, guid);
  if (seen && (!strcmp(seen->table_name, kind->table))) return seen->record_id;
  if (!general) { note(st, "%s: analytics value not imported (no record in the file)", type, ""); return 0; }
  char key[64];
  snprintf(key, sizeof(key), "%s@%d", guid, kind_id);
  db_import_source_t *alias = source_find(st, key);
  if (alias) return alias->record_id;
  db_analytic_item_t a = { .kind_id = kind_id };
  if (!strcmp(t, "ref") && !strncmp(type, "Документ.", strlen("Документ."))) {
    xmlNodePtr d = document_by_guid(st, guid);
    char number[40] = "", date[40] = "";
    attr(d, "number", number, sizeof(number)); attr(d, "date", date, sizeof(date));
    char caption[192];  // the parts fit: type is 127, number 39, date 10
    snprintf(caption, sizeof(caption), "%s %s %.10s", type + strlen("Документ."), number, date);
    ledger_copy_text(name, sizeof(name), caption);
    ledger_copy_text(a.code, sizeof(a.code), number);
  } else if (!strcmp(t, "ref")) {
    xmlNodePtr r = record_by_guid(st, guid);
    if (!r) { note(st, "%s: analytics value not imported (no record in the file)", type, ""); return 0; }
    attr(r, "description", name, sizeof(name));
  }
  ledger_copy_text(a.name, sizeof(a.name), name);
  const int *added = (const int *)send_db_message(st->ib, dbInsert, TABLE_ANALYTIC_ITEMS, &a);
  if (!added || !source_remember(st, type, key, "analytic_items", *added)) {
    snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("Cannot store an imported analytics record."));
    return 0;
  }
  return *added;
}

typedef struct {
  int account, currency, subconto[3];
  bool has_quantity, has_currency_amount, tax_set;
  double quantity, currency_amount;
  int64_t quantity_milli, tax, permanent, temporary;
} side_t;

// A decimal value of a side; false for NULL, which is not zero.
static bool side_decimal(xmlNodePtr side, const char *name, double *value, int64_t *minor, unsigned scale) {
  xmlNodePtr n = v_node(side, name);
  char t[16], text[64];
  attr(n, "t", t, sizeof(t));
  if (!n || !strcmp(t, "null")) return false;
  v_text(side, name, text, sizeof(text));
  int64_t m = 0;
  if (!text[0] || !money_parse_decimal(text, scale, &m)) return false;
  if (value) *value = strtod(text, NULL);
  if (minor) *minor = m;
  return true;
}

static void read_side(stage_t *st, xmlNodePtr side, side_t *out) {
  memset(out, 0, sizeof(*out));
  char guid[40];
  attr(side, "account", guid, sizeof(guid));
  db_import_source_t *acc = source_find(st, guid);
  out->account = acc ? acc->record_id : 0;
  if (guid[0] && !acc) note(st, "movement account %s was not imported", guid, "");
  out->currency = ref_id(st, side, "Валюта");
  out->has_quantity = side_decimal(side, "Количество", &out->quantity, &out->quantity_milli, 3);
  out->has_currency_amount = side_decimal(side, "ВалютнаяСумма", &out->currency_amount, NULL, 2);
  bool a = side_decimal(side, "СуммаНУ", NULL, &out->tax, 2), b = side_decimal(side, "СуммаПР", NULL, &out->permanent, 2),
       c = side_decimal(side, "СуммаВР", NULL, &out->temporary, 2);
  out->tax_set = a || b || c;
  const db_account_t *account = out->account ? (const db_account_t *)send_db_message(st->cfg, dbFind, MAKEDWORD(TABLE_ACCOUNTS, 0), (void *)(intptr_t)out->account) : NULL;
  for (xmlNodePtr s = side->children; s && account; s = s->next) {
    char n[8];
    if (!is_el(s, "subkonto")) continue;
    attr(s, "n", n, sizeof(n));
    int slot = atoi(n);
    if (slot >= 1 && slot <= 3) out->subconto[slot - 1] = analytics_value(st, account, slot, s);
  }
}

static bool import_movement(stage_t *st, int doc_id, xmlNodePtr mv) {
  side_t d, c;
  xmlNodePtr dn = child(mv, "debit"), cn = child(mv, "credit");
  read_side(st, dn, &d);
  read_side(st, cn, &c);
  if (!d.account && !c.account) { note(st, "a movement with neither account was held back", "", ""); return true; }
  db_entry_t e = {0};
  send_db_message(st->ib, dbInitRecord, TABLE_ENTRIES, &e);
  char text[512];
  e.document_id = doc_id;
  e.debit_id = d.account; e.credit_id = c.account;
  e.debit_subconto_id = d.subconto[0]; e.debit_subconto2_id = d.subconto[1]; e.debit_subconto3_id = d.subconto[2];
  e.credit_subconto_id = c.subconto[0]; e.credit_subconto2_id = c.subconto[1]; e.credit_subconto3_id = c.subconto[2];
  attr(mv, "period", text, sizeof(text));
  ledger_copy_text(e.date, sizeof(e.date), text[0] ? (text[10] = '\0', text) : "");
  v_text(mv, "Сумма", text, sizeof(text));
  e.amount = strtod(text, NULL);
  money_parse_decimal(text, 2, &e.amount_minor);
  v_text(mv, "Содержание", text, sizeof(text));
  ledger_copy_text(e.content, sizeof(e.content), text);
  const side_t *q = d.has_quantity ? &d : &c;
  if (q->has_quantity) {
    e.quantity = q->quantity; e.quantity_milli = q->quantity_milli;
    if (d.has_quantity && c.has_quantity && d.quantity_milli != c.quantity_milli) note(st, "a movement has different quantities on its two sides; the debit's is kept", "", "");
  }
  const side_t *cur = d.has_currency_amount || d.currency ? &d : &c;
  e.currency_id = cur->currency;
  if (cur->has_currency_amount) e.currency_amount = cur->currency_amount;
  e.debit_tax_set = d.tax_set; e.debit_tax_minor = d.tax; e.debit_permanent_minor = d.permanent; e.debit_temporary_minor = d.temporary;
  e.credit_tax_set = c.tax_set; e.credit_tax_minor = c.tax; e.credit_permanent_minor = c.permanent; e.credit_temporary_minor = c.temporary;
  return send_db_message(st->ib, dbInsert, TABLE_ENTRIES, &e) != 0;
}

static int document_type_for(stage_t *st, const char *source_type) {
  db_import_document_t *map = (db_import_document_t *)find_by_text(st->cfg, TABLE_IMPORT_DOCUMENTS, "source_type", source_type);
  db_doc_type_t *type = map ? (db_doc_type_t *)send_db_message(st->cfg, dbFind, MAKEDWORD(TABLE_DOC_TYPES, 0), (void *)(intptr_t)map->doc_type_id) : NULL;
  if (type && strcmp(type->form, "operation")) {
    note(st, "%s: mapped to a type that is not an operation; the general operation is used", source_type, "");
    type = NULL;
  }
  if (!type) type = (db_doc_type_t *)find_by_text(st->cfg, TABLE_DOC_TYPES, "code", "operation");
  return type ? type->id : 0;
}

// The configuration's rate for a 1C VAT enum value (НДС18, БезНДС ...) valid on the date.
static int vat_rate_for(stage_t *st, const char *enum_name, const char *date) {
  if (!enum_name[0]) return 0;
  const char *digits = strstr(enum_name, "НДС");
  int percent = digits ? atoi(digits + strlen("НДС")) : -1;
  if (!strcmp(enum_name, "БезНДС")) {
    db_vat_rate_t *r = (db_vat_rate_t *)find_by_text(st->cfg, TABLE_VAT_RATES, "name", "Без НДС");
    return r ? r->id : 0;
  }
  int found = 0;
  result_node_t *rates = (result_node_t *)send_db_message(st->cfg, dbFetch, MAKEDWORD(TABLE_VAT_RATES, 0), NULL);
  EACH_RECORD(db_vat_rate_t, r, rates) {
    bool valid = (!r->valid_from[0] || strcmp(date, r->valid_from) >= 0) && (!r->valid_to[0] || strcmp(date, r->valid_to) <= 0);
    if (!found && valid && percent >= 0 && (int)(r->rate + 0.5) == percent && strcmp(r->treatment, "exempt")) found = r->id;
  }
  free_result_list(rates);
  if (!found) note(st, "VAT rate %s has no rate in the configuration; lines keep none", enum_name, "");
  return found;
}

// A tabular section whose rows name an item becomes lines of the document;
// other sections (settlements, payment breakdowns) are listed, not imported.
static bool import_lines(stage_t *st, int doc_id, xmlNodePtr doc, const char *type, const char *date, bool vat_included) {
  for (xmlNodePtr t = doc->children; t; t = t->next) {
    if (!is_el(t, "table")) continue;
    char tname[96];
    attr(t, "n", tname, sizeof(tname));
    bool items = false;
    for (xmlNodePtr row = t->children; row && !items; row = row->next) items = is_el(row, "row") && v_node(row, "Номенклатура");
    if (!items) { if (child(t, "row")) note(st, "%s: table %s is not imported as lines", type, tname); continue; }
    for (xmlNodePtr row = t->children; row; row = row->next) {
      if (!is_el(row, "row")) continue;
      db_doc_line_t l = {0};
      send_db_message(st->ib, dbInitRecord, TABLE_DOC_LINES, &l);
      l.document_id = doc_id;
      l.item_id = ref_id(st, row, "Номенклатура");
      double quantity = 0, price = 0, amount = 0, vat = 0;
      int64_t q = 0, p = 0, a = 0, v = 0;
      side_decimal(row, "Количество", &quantity, &q, 3);
      side_decimal(row, "Цена", &price, &p, 2);
      side_decimal(row, "Сумма", &amount, &a, 2);
      side_decimal(row, "СуммаНДС", &vat, &v, 2);
      if (!q && a) { quantity = 1; q = 1000; price = amount; p = a; }  // a service row has no quantity
      if (vat_included) { amount -= vat; a -= v; }
      l.quantity = quantity; l.quantity_milli = q;
      l.price = price; l.price_minor = p;
      l.amount = amount; l.amount_minor = a;
      l.vat = vat; l.vat_minor = v;
      l.total = amount + vat; l.total_minor = a + v;
      char rate[64];
      v_enum(row, "СтавкаНДС", rate, sizeof(rate));
      l.vat_rate_id = vat_rate_for(st, rate, date);
      if (!l.item_id) note(st, "%s: a line names an item that was not imported", type, "");
      if (!send_db_message(st->ib, dbInsert, TABLE_DOC_LINES, &l)) return false;
      st->out->lines++;
    }
  }
  return true;
}

static void import_document(stage_t *st, xmlNodePtr doc) {
  char type[128], guid[40], number[40], date[40], posted[8], deleted[8], key[80];
  attr(doc, "type", type, sizeof(type)); attr(doc, "id", guid, sizeof(guid)); attr(doc, "number", number, sizeof(number));
  attr(doc, "date", date, sizeof(date)); attr(doc, "posted", posted, sizeof(posted)); attr(doc, "deleted", deleted, sizeof(deleted));
  if (!strcmp(deleted, "true")) { tally(st, type, 0, 0, 1); return; }
  if (source_find(st, guid)) { tally(st, type, 0, 1, 0); return; }  // an imported document is not rewritten
  int first = st->buckets[guid_bucket(guid)], moves = 0;
  for (int i = first; i; i = st->movements[i - 1].next) {
    char recorder[40];
    attr(st->movements[i - 1].node, "recorder", recorder, sizeof(recorder));
    moves += !strcmp(recorder, guid);
  }
  bool is_posted = !strcmp(posted, "true");
  if (!is_posted && !moves) {
    note(st, "%s: documents not posted in 1C and without movements are not imported", type, "");
    tally(st, type, 0, 0, 1);
    return;
  }
  if (!is_posted) note(st, "%s: not posted in 1C but with register movements; imported posted", type, "");
  int type_id = document_type_for(st, type);
  if (!type_id) { snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("The configuration has no operation document type.")); return; }
  db_document_t d = {0};
  send_db_message(st->ib, dbInitRecord, TABLE_DOCUMENTS, &d);
  d.doc_type_id = type_id;
  ledger_copy_text(d.number, sizeof(d.number), number);
  ledger_copy_text(d.date, sizeof(d.date), date[0] ? (date[10] = '\0', date) : "");
  d.counterparty_id = ref_id(st, doc, "Контрагент");
  d.contract_id = ref_id(st, doc, "ДоговорКонтрагента");
  d.warehouse_id = ref_id(st, doc, "Склад");
  d.bank_account_id = ref_id(st, doc, "СчетОрганизации") ? ref_id(st, doc, "СчетОрганизации") : ref_id(st, doc, "БанковскийСчет");
  d.currency_id = ref_id(st, doc, "ВалютаДокумента");
  char text[512];
  v_text(doc, "Комментарий", text, sizeof(text));
  ledger_copy_text(d.comment, sizeof(d.comment), text);
  snprintf(key, sizeof(key), "1cd:%s", guid);
  ledger_copy_text(d.import_source, sizeof(d.import_source), key);
  const db_document_t *added = (const db_document_t *)send_db_message(st->ib, dbInsert, TABLE_DOCUMENTS, &d);
  if (!added) { snprintf(st->out->error, sizeof(st->out->error), ui_tr("Cannot store the imported document %s."), number); return; }
  int doc_id = added->id;
  char included[8];
  v_text(doc, "СуммаВключаетНДС", included, sizeof(included));
  if (!import_lines(st, doc_id, doc, type, d.date, !strcmp(included, "true")))
    snprintf(st->out->error, sizeof(st->out->error), ui_tr("Cannot store a line of the imported document %s."), number);
  for (int i = first; i && !st->out->error[0]; i = st->movements[i - 1].next) {
    char recorder[40];
    attr(st->movements[i - 1].node, "recorder", recorder, sizeof(recorder));
    if (!strcmp(recorder, guid) && !import_movement(st, doc_id, st->movements[i - 1].node))
      snprintf(st->out->error, sizeof(st->out->error), ui_tr("Cannot store an entry of the imported document %s."), number);
  }
  if (!st->out->error[0] && !ledger_post(st->ib, st->cfg, doc_id))
    snprintf(st->out->error, sizeof(st->out->error), ui_tr("Cannot post the imported document %s."), number);
  if (!st->out->error[0] && !source_remember(st, type, guid, "documents", doc_id))
    snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("Cannot record the source of an imported record."));
  if (!st->out->error[0]) { tally(st, type, 1, 0, 0); st->out->entries += moves; }
}

// ── Entry point ──────────────────────────────────────────────────────────────
bool ledger_import_stage(database_t *ib, database_t *cfg, const char *path, ledger_stage_import_t *out) {
  memset(out, 0, sizeof(*out));
  xmlDocPtr xml = path ? xmlReadFile(path, NULL, XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_HUGE) : NULL;
  xmlNodePtr root = xml ? xmlDocGetRootElement(xml) : NULL;
  char format[64];
  attr(root, "format", format, sizeof(format));
  if (!is_el(root, "stage") || strcmp(format, STAGE_FORMAT)) {
    snprintf(out->error, sizeof(out->error), "%s", ui_tr("The file is not a 1C staging file (orion-1cd-stage-v1)."));
    if (xml) xmlFreeDoc(xml);
    return false;
  }
  stage_t st = { .ib = ib, .cfg = cfg, .out = out };
  if (!send_db_message(ib, dbBeginChanges, 0, NULL)) {
    snprintf(out->error, sizeof(out->error), "%s", ui_tr("Cannot begin the import transaction."));
    xmlFreeDoc(xml);
    return false;
  }
  // Kinds first: accounts name them.
  xmlNodePtr kinds = child(root, "subkonto-kinds"), chart = child(root, "chart-of-accounts");
  st.records = child(root, "records");
  for (xmlNodePtr r = kinds ? kinds->children : NULL; r && !out->error[0]; r = r->next) if (is_el(r, "record")) import_kind(&st, r);
  for (xmlNodePtr r = chart ? chart->children : NULL; r && !out->error[0]; r = r->next) if (is_el(r, "account")) import_account(&st, r);
  for (xmlNodePtr r = st.records ? st.records->children : NULL; r && !out->error[0]; r = r->next) {
    char type[96];
    attr(r, "type", type, sizeof(type));
    if (is_el(r, "record") && !strcmp(type, "Справочник.Организации")) import_organization(&st, r);
  }
  if (!out->error[0] && !ledger_apply_extensions(ib, cfg))  // movements name the new accounts
    snprintf(out->error, sizeof(out->error), "%s", ui_tr("The imported accounts could not join the configuration."));
  for (size_t i = 0; i < ARRAY_LEN(kStageCatalogs) && !out->error[0]; i++)
    for (xmlNodePtr r = st.records ? st.records->children : NULL; r && !out->error[0]; r = r->next) {
      char type[96];
      attr(r, "type", type, sizeof(type));
      if (is_el(r, "record") && !strcmp(type, kStageCatalogs[i].type)) import_catalog_record(&st, &kStageCatalogs[i], r);
    }
  if (!out->error[0]) import_analytic_records(&st);
  if (!out->error[0]) report_unimported(&st);
  st.documents = child(root, "documents");
  if (!out->error[0] && !index_movements(&st, root)) snprintf(out->error, sizeof(out->error), "%s", ui_tr("Out of memory."));
  for (xmlNodePtr d = st.documents ? st.documents->children : NULL; d && !out->error[0]; d = d->next) if (is_el(d, "document")) import_document(&st, d);
  if (!out->error[0] && !send_db_message(ib, dbCommitChanges, 0, NULL))
    snprintf(out->error, sizeof(out->error), "%s", ui_tr("Cannot commit the import transaction."));
  if (out->error[0]) {
    send_db_message(ib, dbRevertChanges, 0, NULL);
    char error[sizeof(out->error)];
    snprintf(error, sizeof(error), "%s", out->error);
    memset(out, 0, sizeof(*out));
    snprintf(out->error, sizeof(out->error), "%s", error);
    ledger_apply_extensions(ib, cfg);  // the configuration forgets what the infobase rolled back
  }
  free(st.movements);
  xmlFreeDoc(xml);
  return !out->error[0];
}

// A staging file announces itself on its first line; the file itself can be large.
bool ledger_file_is_stage(const char *path) {
  char head[1024] = "";
  FILE *f = path ? fopen(path, "rb") : NULL;
  if (!f) return false;
  size_t n = fread(head, 1, sizeof(head) - 1, f);
  fclose(f);
  head[n] = '\0';
  const char *root = strstr(head, "<stage");
  return root && strstr(root, "format=\"" STAGE_FORMAT "\"");
}

// The totals, a line per 1C type, then every held-back or unmapped item.
size_t ledger_stage_report(const ledger_stage_import_t *r, char *buf, size_t sz, bool summary) {
  size_t n = 0;
#define ADD(...) do { if (n < sz) { int w = snprintf(buf + n, sz - n, __VA_ARGS__); if (w > 0) n += (size_t)w < sz - n ? (size_t)w : sz - n - 1; } } while (0)
  if (sz) buf[0] = '\0';
  int held = 0;
  for (int i = 0; i < r->count_types; i++) held += r->counts[i].held;
  ADD(ui_tr("Accounts added: %d, found in the configuration: %d\n"), r->accounts_added, r->accounts_matched);
  ADD(ui_tr("Analytics kinds added: %d, mapped onto Ledger catalogs: %d\n"), r->kinds_added, r->kinds_mapped);
  ADD(ui_tr("Entries written: %d\n"), r->entries);
  ADD(ui_tr("Document lines written: %d\n"), r->lines);
  if (summary) { ADD(ui_tr("Held back or not imported: %d"), held); return n; }
  ADD("\n");
  for (int i = 0; i < r->count_types; i++)
    ADD(ui_tr("%s: added %d, updated %d, held back %d\n"), r->counts[i].type, r->counts[i].added, r->counts[i].updated, r->counts[i].held);
  if (r->notes[0]) ADD("\n%s\n%s", ui_tr("Held back or not imported:"), r->notes);
#undef ADD
  return n;
}

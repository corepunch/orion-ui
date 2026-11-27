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
// A document found again is rewritten from the file — same document, its
// lines and entries replaced with the source history; one the file marks deleted is
// removed with its source identity. A closed period or a linked reversal
// keeps it untouched, with a note saying which.
// The import is one editing context: a file that fails leaves nothing behind.

#include "ledger.h"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <orion/user/money.h>
#include <stdalign.h>
#include <stddef.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef _WIN32
#include <io.h>
#ifndef O_BINARY
#define O_BINARY _O_BINARY
#endif
#else
#define O_BINARY 0
#endif

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
  { "Справочник.Организации",         "organizations" },
  { "Справочник.ПодразделенияОрганизаций", "departments" },
};

typedef struct {
  database_t *ib, *cfg;
  ledger_stage_import_t *out;
  bool unf_catalogs;                             // insert-only OData catalog handoff
  xmlNodePtr records;                            // <records>: the catalogs
  struct { char ref_type[96]; int kind_id; } kinds[128];  // 1C type an analytics kind holds -> Ledger kind
  int kind_count;
  xmlNodePtr documents, registers;               // source documents and registers
  struct { xmlNodePtr node; int next; bool depreciation; } *movements;  // register movements, chained by recorder
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

static int tally_slot(stage_t *st, const char *type) {
  ledger_stage_import_t *o = st->out;
  char key[sizeof(o->counts[0].type)];
  ledger_copy_text(key, sizeof(key), type);
  int i = 0;
  while (i < o->count_types && strcmp(o->counts[i].type, key)) i++;
  if (i == o->count_types) {
    if (i == (int)ARRAY_LEN(o->counts)) return -1;
    memcpy(o->counts[i].type, key, sizeof(key));
    o->count_types++;
  }
  return i;
}

static void tally(stage_t *st, const char *type, int added, int updated, int held) {
  int i = tally_slot(st, type);
  if (i < 0) return;
  st->out->counts[i].added += added;
  st->out->counts[i].updated += updated;
  st->out->counts[i].held += held;
}

static void tally_removed(stage_t *st, const char *type) {
  int i = tally_slot(st, type);
  if (i >= 0) st->out->counts[i].removed++;
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
  if (seen) {
    if (!strcmp(seen->table_name, "ext_subconto_kinds")) {
      db_ext_subconto_kind_t *old = (void *)send_db_message(st->ib, dbFind, MAKEDWORD(TABLE_EXT_SUBCONTO_KINDS, 0), (void *)(intptr_t)seen->record_id);
      if (!old) { snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("Cannot store an imported analytics kind.")); return; }
      db_ext_subconto_kind_t k = *old;
      ledger_copy_text(k.code, sizeof(k.code), code);
      ledger_copy_text(k.name, sizeof(k.name), name);
      const char *table = kind_table_for(rec);
      if (strcmp(k.table, table ? table : "analytic_items")) note(st, "%s: analytics type changed; existing type retained", name, "");
      if (!send_db_message(st->ib, dbUpdate, TABLE_EXT_SUBCONTO_KINDS, &k)) { snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("Cannot store an imported analytics kind.")); return; }
    }
    st->out->updated++; map_kind(st, rec, seen->record_id); return;
  }
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
  snprintf(k.table, sizeof(k.table), "%s", table ? table : "analytic_items");
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

static bool account_slot_has_entries(database_t *ib, int account_id, int slot) {
  for (int side = 0; side < 2; side++) {
    int field = ledger_field(ib, TABLE_ENTRIES, side ? "credit_id" : "debit_id");
    result_node_t *entries = (result_node_t *)send_db_message(ib, dbFetch, MAKEDWORD(TABLE_ENTRIES, field), (void *)(intptr_t)account_id);
    bool used = false;
    EACH_RECORD(db_entry_t, e, entries) {
      const int debit[] = {e->debit_subconto_id, e->debit_subconto2_id, e->debit_subconto3_id};
      const int credit[] = {e->credit_subconto_id, e->credit_subconto2_id, e->credit_subconto3_id};
      if ((side ? credit : debit)[slot]) { used = true; break; }
    }
    free_result_list(entries);
    if (used) return true;
  }
  return false;
}

static void import_account(stage_t *st, xmlNodePtr rec) {
  char metadata[256], guid[40], code[64], name[512], kind[32], currency[8];
  attr(rec, "type", metadata, sizeof(metadata)); attr(rec, "id", guid, sizeof(guid));
  attr(rec, "code", code, sizeof(code)); attr(rec, "description", name, sizeof(name));
  attr(rec, "kind", kind, sizeof(kind)); attr(rec, "currency", currency, sizeof(currency));
  db_import_source_t *seen = source_find(st, guid);
  if (seen && strcmp(seen->table_name, "ext_accounts")) { st->out->updated++; return; }
  db_account_t *known = seen ? NULL : (db_account_t *)find_by_text(st->cfg, TABLE_ACCOUNTS, "code", code);
  if (known) {
    st->out->accounts_matched++;
    if (!source_remember(st, metadata, guid, "accounts", known->id)) snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("Cannot record the source of an imported record."));
    else extend_matched_account(st, rec, known, code);
    return;
  }
  db_ext_account_t a = { .id = seen ? seen->record_id : next_ext_id(st->ib, TABLE_EXT_ACCOUNTS), .currency = !strcmp(currency, "true") };
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
  if (seen) {
    const db_ext_account_t *old = (const db_ext_account_t *)send_db_message(st->ib, dbFind,
      MAKEDWORD(TABLE_EXT_ACCOUNTS, 0), (void *)(intptr_t)a.id);
    if (old) {
      const int previous[] = {old->subconto_kind_id, old->subconto2_kind_id, old->subconto3_kind_id};
      for (int slot = 0; slot < 3; slot++) {
        if (previous[slot] == *slots[slot] || !account_slot_has_entries(st->ib, old->id, slot)) continue;
        const char *numbers[] = {"1", "2", "3"};
        note(st, "account %s: analytics slot %s is used by existing entries and was retained", code, numbers[slot]);
        *slots[slot] = previous[slot];
      }
    }
  }
  if (!send_db_message(st->ib, seen ? dbUpdate : dbInsert, TABLE_EXT_ACCOUNTS, &a) || !source_remember(st, metadata, guid, "ext_accounts", a.id)) {
    snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("Cannot store an imported account."));
    return;
  }
  if (seen) st->out->updated++; else st->out->accounts_added++;
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

static void import_unit(stage_t *st, xmlNodePtr rec) {
  const char *type = "Справочник.КлассификаторЕдиницИзмерения";
  char guid[40], code[512], name[512], folder[8];
  attr(rec, "id", guid, sizeof(guid)); attr(rec, "code", code, sizeof(code));
  attr(rec, "description", name, sizeof(name)); attr(rec, "folder", folder, sizeof(folder));
  if (!strcmp(folder, "true")) return;
  if (!code[0] && !name[0]) { tally(st, type, 0, 0, 1); return; }
  db_import_source_t *seen = source_find(st, guid);
  if (seen && strcmp(seen->table_name, "units") && strcmp(seen->table_name, "ext_units")) {
    note(st, "%s: existing source belongs to another catalog; retained", type, "");
    tally(st, type, 0, 0, 1); return;
  }
  if (seen && !strcmp(seen->table_name, "units")) { tally(st, type, 0, 1, 0); return; }
  db_unit_t *known = !seen && code[0] ? find_by_text(st->cfg, TABLE_UNITS, "code", code) : NULL;
  db_ext_unit_t *existing = !seen && !known && code[0] ? find_by_text(st->ib, TABLE_EXT_UNITS, "code", code) : NULL;
  if (!seen && !known && !existing && name[0]) known = find_by_text(st->cfg, TABLE_UNITS, "name", name);
  if (known) {
    if (!source_remember(st, type, guid, known->id >= LEDGER_EXT_ID_BASE ? "ext_units" : "units", known->id)) goto failed;
    tally(st, type, 0, 1, 0); return;
  }
  if (seen) existing = (void *)send_db_message(st->ib, dbFind, MAKEDWORD(TABLE_EXT_UNITS, 0), (void *)(intptr_t)seen->record_id);
  if (!seen && !existing && name[0]) existing = find_by_text(st->ib, TABLE_EXT_UNITS, "name", name);
  if (seen && !existing) goto failed;
  db_ext_unit_t unit = { .id = existing ? existing->id : next_ext_id(st->ib, TABLE_EXT_UNITS) };
  ledger_copy_text(unit.code, sizeof(unit.code), code); ledger_copy_text(unit.name, sizeof(unit.name), name);
  if (!send_db_message(st->ib, existing ? dbUpdate : dbInsert, TABLE_EXT_UNITS, &unit) ||
      !source_remember(st, type, guid, "ext_units", unit.id)) goto failed;
  tally(st, type, existing ? 0 : 1, existing ? 1 : 0, 0); return;
failed:
  snprintf(st->out->error, sizeof(st->out->error), ui_tr("Cannot store an imported record of %s."), type);
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

static int vat_rate_for(stage_t *st, const char *enum_name, const char *date);

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
  text_field(c->full_name, sizeof(c->full_name), rec, "НаименованиеПолное");
  text_field(c->address, sizeof(c->address), rec, "ЮридическийАдрес");
  if (!c->address[0]) text_field(c->address, sizeof(c->address), rec, "ФактическийАдрес");
  if (!c->address[0]) text_field(c->address, sizeof(c->address), rec, "Адрес");
  text_field(c->phone, sizeof(c->phone), rec, "Телефон");
  return true;
}

static bool fill_item(stage_t *st, xmlNodePtr rec, void *out, const char **why) {
  db_item_t *it = out;
  char name[512], guid[40];
  attr(rec, "description", name, sizeof(name));
  ledger_copy_text(it->name, sizeof(it->name), name);
  text_field(it->sku, sizeof(it->sku), rec, "Артикул");
  char kind[64];
  v_enum(rec, "ВидНоменклатуры", kind, sizeof(kind));
  if (!kind[0]) v_text(rec, "ВидНоменклатуры", kind, sizeof(kind));
  ledger_copy_text(it->kind, sizeof(it->kind), !strcmp(kind, "Товар") ? "goods" : !strcmp(kind, "Услуга") || !strcmp(kind, "Работа") ? "service" : "");
  v_ref(rec, "ЕдиницаИзмерения", guid, sizeof(guid));
  db_import_source_t *unit = source_find(st, guid);
  if (unit && (!strcmp(unit->table_name, "units") || !strcmp(unit->table_name, "ext_units"))) it->unit_id = unit->record_id;
  char rate[64];
  v_enum(rec, "СтавкаНДС", rate, sizeof(rate));
  it->vat_rate_id = vat_rate_for(st, rate, "");
  return true;
}

static bool fill_employee(stage_t *st, xmlNodePtr rec, void *out, const char **why) {
  db_employee_t *e = out;
  char name[512], date[32];
  text_field(e->name, sizeof(e->name), rec, "ФИО");
  if (!e->name[0]) { attr(rec, "description", name, sizeof(name)); ledger_copy_text(e->name, sizeof(e->name), name); }
  text_field(e->tax_id, sizeof(e->tax_id), rec, "ИНН");
  text_field(e->position, sizeof(e->position), rec, "Должность");
  v_text(rec, "ДатаРождения", date, sizeof(date));
  if (strlen(date) >= 10) { date[10] = '\0'; ledger_copy_text(e->birth_date, sizeof(e->birth_date), date); }
  v_text(rec, "ДатаПриема", date, sizeof(date));
  if (strlen(date) >= 10) { date[10] = '\0'; ledger_copy_text(e->hired, sizeof(e->hired), date); }
  return true;
}

static bool fill_warehouse(stage_t *st, xmlNodePtr rec, void *out, const char **why) {
  db_warehouse_t *w = out;
  char name[512], type[64];
  attr(rec, "description", name, sizeof(name));
  ledger_copy_text(w->name, sizeof(w->name), name);
  v_enum(rec, "ТипСклада", type, sizeof(type));
  ledger_copy_text(w->kind, sizeof(w->kind), !strcmp(type, "ОптовыйСклад") ? "wholesale" : !strcmp(type, "РозничныйМагазин") ? "retail" : "");
  text_field(w->address, sizeof(w->address), rec, "Адрес");
  return true;
}

static bool fill_contract(stage_t *st, xmlNodePtr rec, void *out, const char **why) {
  db_contract_t *c = out;
  char owner[40], name[512], date[32], kind[64];
  attr(rec, "owner", owner, sizeof(owner));
  db_import_source_t *cp = source_find(st, owner);
  if (!cp) { *why = ui_tr("its counterparty was not imported"); return false; }
  c->counterparty_id = cp->record_id;
  c->organization_id = ref_id(st, rec, "Организация");
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
  text_field(c->subject, sizeof(c->subject), rec, "Предмет");
  return true;
}

static bool fill_department(stage_t *st, xmlNodePtr rec, void *out, const char **why) {
  db_department_t *d = out;
  char text[512], owner[40];
  attr(rec, "owner", owner, sizeof(owner));
  db_import_source_t *src = source_find(st, owner);
  if (!src || strcmp(src->table_name, "organizations")) { *why = ui_tr("its organization was not imported"); return false; }
  d->organization_id = src->record_id;
  attr(rec, "code", text, sizeof(text)); ledger_copy_text(d->code, sizeof(d->code), text);
  attr(rec, "description", text, sizeof(text)); ledger_copy_text(d->name, sizeof(d->name), text);
  return true;
}

static bool fill_bank(stage_t *st, xmlNodePtr rec, void *out, const char **why) {
  db_bank_t *b = out;
  char text[512];
  attr(rec, "description", text, sizeof(text)); ledger_copy_text(b->name, sizeof(b->name), text);
  attr(rec, "code", text, sizeof(text)); ledger_copy_text(b->bik, sizeof(b->bik), text);
  text_field(b->corr_account, sizeof(b->corr_account), rec, "КоррСчет");
  text_field(b->city, sizeof(b->city), rec, "Город");
  text_field(b->swift, sizeof(b->swift), rec, "СВИФТБИК");
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
  db_import_source_t *bank_source = source_find(st, guid);
  if (bank_source && !strcmp(bank_source->table_name, "banks")) b->bank_id = bank_source->record_id;
  xmlNodePtr bank = record_by_guid(st, guid);
  if (bank) {
    attr(bank, "description", name, sizeof(name)); ledger_copy_text(b->bank, sizeof(b->bank), name);
    attr(bank, "code", name, sizeof(name)); ledger_copy_text(b->bik, sizeof(b->bik), name);
    text_field(b->corr_account, sizeof(b->corr_account), bank, "КоррСчет");
    text_field(b->swift, sizeof(b->swift), bank, "СВИФТБИК");
  }
  return true;
}

static const char *asset_depreciation_method(const char *enum_name) {
  if (!strcmp(enum_name, "ЛинейныйСпособ") || !strcmp(enum_name, "Линейный"))                              return "linear";
  if (!strcmp(enum_name, "СпособУменьшаемогоОстатка"))                   return "declining";
  if (!strcmp(enum_name, "СпособСуммыЧиселЛет"))                         return "sum_of_years";
  if (!strcmp(enum_name, "СпособСписанияПропорциональноОбъемуПродукции")) return "output";
  return "";
}

// A card has no organization dimension: do not combine register records from different organizations.
static xmlNodePtr asset_register(stage_t *st, xmlNodePtr rec, const char *type) {
  char id[40], name[160], guid[40], organization[40] = "", period[40], latest[40] = "", active[8];
  attr(rec, "id", id, sizeof(id));
  xmlNodePtr found = NULL;
  for (xmlNodePtr reg = st->registers ? st->registers->children : NULL; reg; reg = reg->next) {
    attr(reg, "type", name, sizeof(name));
    if (!is_el(reg, "register") || strcmp(name, type)) continue;
    for (xmlNodePtr m = reg->children; m; m = m->next) {
      if (!is_el(m, "movement")) continue;
      attr(m, "active", active, sizeof(active)); v_ref(m, "ОсновноеСредство", guid, sizeof(guid));
      if (!id[0] || strcmp(id, guid) || !strcmp(active, "false")) continue;
      v_ref(m, "Организация", guid, sizeof(guid));
      if (organization[0] && strcmp(organization, guid)) return NULL;
      if (!guid[0]) return NULL;
      ledger_copy_text(organization, sizeof(organization), guid);
      attr(m, "period", period, sizeof(period));
      if (!period[0]) continue;
      if (found && !strcmp(period, latest)) return NULL;
      if (!found || strcmp(period, latest) > 0) { found = m; ledger_copy_text(latest, sizeof(latest), period); }
    }
  }
  return found;
}

// Acceptance headers describe the listed assets; their total cost is safe only for one row.
static xmlNodePtr asset_acceptance(stage_t *st, xmlNodePtr rec, int *row_count) {
  char id[40], type[96], posted[8], deleted[8], guid[40];
  attr(rec, "id", id, sizeof(id));
  xmlNodePtr found = NULL;
  for (xmlNodePtr d = st->documents ? st->documents->children : NULL; d; d = d->next) {
    attr(d, "type", type, sizeof(type)); attr(d, "posted", posted, sizeof(posted)); attr(d, "deleted", deleted, sizeof(deleted));
    if (!is_el(d, "document") || strcmp(type, "Документ.ПринятиеКУчетуОС") || strcmp(posted, "true") || !strcmp(deleted, "true")) continue;
    for (xmlNodePtr t = d->children; t; t = t->next) {
      char name[32]; attr(t, "n", name, sizeof(name));
      if (!is_el(t, "table") || strcmp(name, "ОС")) continue;
      int count = 0; bool match = false;
      for (xmlNodePtr r = t->children; r; r = r->next) if (is_el(r, "row")) {
        count++; v_ref(r, "ОсновноеСредство", guid, sizeof(guid));
        if (id[0] && !strcmp(id, guid)) match = true;
      }
      if (match) { if (found) return NULL; found = d; *row_count = count; }
    }
  }
  return found;
}

static bool fill_asset(stage_t *st, xmlNodePtr rec, void *out, const char **why) {
  db_asset_t *a = out;
  char name[512], date[32], buf[64], method[128];
  attr(rec, "description", name, sizeof(name));
  ledger_copy_text(a->name, sizeof(a->name), name);
  text_field(a->inventory, sizeof(a->inventory), rec, "ИнвентарныйНомер");
  v_text(rec, "ДатаВводаВЭксплуатацию", date, sizeof(date));
  if (strlen(date) >= 10) { date[10] = '\0'; ledger_copy_text(a->commissioned, sizeof(a->commissioned), date); }
  v_text(rec, "СрокПолезногоИспользования", buf, sizeof(buf));
  if (buf[0]) a->life = atoi(buf);
  v_text(rec, "ПервоначальнаяСтоимость", buf, sizeof(buf));
  if (buf[0]) a->cost = strtod(buf, NULL);
  v_enum(rec, "СпособНачисленияАмортизации", method, sizeof(method));
  ledger_copy_text(a->depreciation_method, sizeof(a->depreciation_method), asset_depreciation_method(method));
  xmlNodePtr initial = asset_register(st, rec, "РегистрСведений.ПервоначальныеСведенияОСБухгалтерскийУчет");
  xmlNodePtr params = asset_register(st, rec, "РегистрСведений.ПараметрыАмортизацииОСБухгалтерскийУчет");
  if (initial) {
    v_text(initial, "ПервоначальнаяСтоимость", buf, sizeof(buf));
    if (buf[0] && strtod(buf, NULL) > 0) a->cost = strtod(buf, NULL);
    if (!a->inventory[0]) text_field(a->inventory, sizeof(a->inventory), initial, "ИнвентарныйНомер");
    v_enum(initial, "СпособНачисленияАмортизации", method, sizeof(method));
    if (method[0]) ledger_copy_text(a->depreciation_method, sizeof(a->depreciation_method), asset_depreciation_method(method));
  }
  if (params) {
    v_text(params, "СрокПолезногоИспользования", buf, sizeof(buf));
    if (buf[0]) a->life = atoi(buf);
    v_enum(params, "СпособНачисленияАмортизации", method, sizeof(method));
    if (method[0]) ledger_copy_text(a->depreciation_method, sizeof(a->depreciation_method), asset_depreciation_method(method));
  }
  int rows = 0;
  xmlNodePtr acceptance = asset_acceptance(st, rec, &rows);
  if (acceptance) {
    attr(acceptance, "date", date, sizeof(date));
    if (strlen(date) >= 10) { date[10] = '\0'; ledger_copy_text(a->commissioned, sizeof(a->commissioned), date); }
    v_text(acceptance, "СрокПолезногоИспользованияБУ", buf, sizeof(buf));
    if (!params && buf[0]) a->life = atoi(buf);
    v_text(acceptance, "СтоимостьБУ", buf, sizeof(buf));
    if (!initial && rows == 1 && buf[0] && strtod(buf, NULL) > 0) a->cost = strtod(buf, NULL);
    v_enum(acceptance, "СпособНачисленияАмортизацииБУ", method, sizeof(method));
    if (!params && !initial && method[0]) ledger_copy_text(a->depreciation_method, sizeof(a->depreciation_method), asset_depreciation_method(method));
    a->account_id = ref_id(st, acceptance, "СчетУчета");
    a->depreciation_id = ref_id(st, acceptance, "СчетНачисленияАмортизации");
  }
  if (!a->account_id || !a->depreciation_id) {
    xmlNodePtr accounts = asset_register(st, rec, "РегистрСведений.СчетаБухгалтерскогоУчетаОС");
    if (!a->account_id) a->account_id = ref_id(st, accounts, "СчетУчета");
    if (!a->depreciation_id) a->depreciation_id = ref_id(st, accounts, "СчетНачисленияАмортизации");
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

// The organization is the infobase's one: a fresh record takes the 1C
// organization's details, and an already named one is joined when their tax
// IDs agree, so a base imported before keeps its own organization.
static void import_organization(stage_t *st, xmlNodePtr rec) {
  char guid[40], text[512], inn[32];
  attr(rec, "id", guid, sizeof(guid));
  db_organization_t *own = (db_organization_t *)send_db_message(st->ib, dbFind, MAKEDWORD(TABLE_ORGANIZATIONS, 0), (void *)(intptr_t)1);
  db_import_source_t *seen = source_find(st, guid);
  text_field(inn, sizeof(inn), rec, "ИНН");
  const bool joined = own && (!own->tax_id[0] || (inn[0] && !strcmp(own->tax_id, inn)));
  if (!joined) {
    note(st, "%s: the infobase organization has a different tax ID (%s); not replaced", inn, own ? own->tax_id : "");
    tally(st, "Справочник.Организации", 0, 0, 1);
    return;
  }
  if (seen && seen->record_id != own->id) return;
  db_organization_t o = *own;
  attr(rec, "description", text, sizeof(text)); ledger_copy_text(o.name, sizeof(o.name), text);
  if (inn[0]) ledger_copy_text(o.tax_id, sizeof(o.tax_id), inn);
  text_field(o.kpp, sizeof(o.kpp), rec, "КПП");
  text_field(o.address, sizeof(o.address), rec, "ЮридическийАдрес");
  if (!o.address[0]) text_field(o.address, sizeof(o.address), rec, "ФактическийАдрес");
  text_field(o.director, sizeof(o.director), rec, "Руководитель");
  text_field(o.accountant, sizeof(o.accountant), rec, "ГлавныйБухгалтер");
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
  { "Справочник.ОсновныеСредства",      TABLE_ASSETS,        "assets",        sizeof(db_asset_t),        fill_asset,        "inventory" },
  { "Справочник.Склады",                TABLE_WAREHOUSES,    "warehouses",    sizeof(db_warehouse_t),    fill_warehouse,    "name" },
  { "Справочник.Номенклатура",          TABLE_ITEMS,         "items",         sizeof(db_item_t),         fill_item,         "sku" },
  { "Справочник.ПодразделенияОрганизаций", TABLE_DEPARTMENTS, "departments", sizeof(db_department_t), fill_department, NULL },
  { "Справочник.Банки",                TABLE_BANKS,          "banks",          sizeof(db_bank_t),         fill_bank,         "bik" },
  { "Справочник.БанковскиеСчета",       TABLE_BANK_ACCOUNTS, "bank_accounts", sizeof(db_bank_account_t), fill_bank_account, "number" },
  { "Справочник.ДоговорыКонтрагентов",  TABLE_CONTRACTS,     "contracts",     sizeof(db_contract_t),     fill_contract,     "number" },
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
  if (!strcmp(folder, "true")) return;
  if (!strcmp(deleted, "true")) note(st, "%s: record marked for deletion in source retained for historical references", cat->type, "");
  alignas(max_align_t) char buf[2048] = {0};
  _Static_assert(sizeof(db_contract_t) <= 2048 && sizeof(db_counterparty_t) <= 2048 && sizeof(db_item_t) <= 2048 &&
                 sizeof(db_bank_account_t) <= 2048 && sizeof(db_employee_t) <= 2048 && sizeof(db_asset_t) <= 2048, "catalog records fit the buffer");
  const char *why = "";
  if (!cat->fill(st, rec, buf, &why)) {
    note(st, "%s: held back (%s)", cat->type, why);
    tally(st, cat->type, 0, 0, 1);
    return;
  }
  db_import_source_t *seen = source_find(st, guid);
  if (seen && strcmp(seen->table_name, cat->table)) {
    note(st, "%s: existing source belongs to another catalog; retained", cat->type, "");
    tally(st, cat->type, 0, 0, 1);
    return;
  }
  const db_table_schema_t *t = db_schema_table(st->ib->schema, cat->table_id);
  int key = seen ? seen->record_id : match_existing(st, cat, buf);
  if (st->unf_catalogs) {
    // OData catalogs lack Ledger-specific price, VAT, address, etc. Never
    // replace an existing row with the sparse imported record or adopt it.
    if (seen || key) {
      note(st, "%s: existing catalog record %s left unchanged", cat->type, guid);
      tally(st, cat->type, 0, 0, 1);
      return;
    }
  }
  // The national currency is not in the currencies catalog: remembered as id 0.
  if (cat->table_id == TABLE_CURRENCIES && !strcmp(((db_currency_t *)buf)->code, "643")) {
    if (!seen && !source_remember(st, cat->type, guid, cat->table, 0)) goto failed;
    tally(st, cat->type, 0, seen != NULL, 0);
    return;
  }
  if (key) {
    *(int *)((char *)buf + t->meta[0].offset) = key;
    const void *existing = (const void *)send_db_message(st->ib, dbFind, MAKEDWORD(cat->table_id, 0), (void *)(intptr_t)key);
    if (existing) {
      if (cat->table_id == TABLE_ASSETS) {
        db_asset_t *a = (db_asset_t *)buf; const db_asset_t *old = existing;
        if (!a->cost) a->cost = old->cost;
        if (!a->life) a->life = old->life;
        if (!a->account_id) a->account_id = old->account_id;
        if (!a->depreciation_id) a->depreciation_id = old->depreciation_id;
        a->expense_id = old->expense_id;
      }
      for (int i = 0; i < t->field_count; i++) {
        if (t->meta[i].type != DB_TYPE_STRING && t->meta[i].type != DB_TYPE_DATE) continue;
        const char *src = (const char *)buf + t->meta[i].offset;
        const char *old = (const char *)existing + t->meta[i].offset;
        int sz = t->meta[i].length ? t->meta[i].length : 11;
        if (!src[0] && old[0]) memcpy((char *)buf + t->meta[i].offset, old, sz);
      }
    }
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
    if (!strcmp(folder, "true")) continue;
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

static bool folder_would_cycle(stage_t *st, int id, int parent) {
  for (int depth = 0; parent && depth < 10000; depth++) {
    if (parent == id) return true;
    db_import_catalog_folder_t *p = (void *)send_db_message(st->ib, dbFind, MAKEDWORD(TABLE_IMPORT_CATALOG_FOLDERS, 0), (void *)(intptr_t)parent);
    if (!p) return true;
    parent = p->parent_id;
  }
  return parent != 0;
}

// Folder identities and source flags are kept separately from the Ledger records.
static void import_catalog_folders(stage_t *st) {
  for (xmlNodePtr rec = st->records ? st->records->children : NULL; rec && !st->out->error[0]; rec = rec->next) {
    char type[96], guid[40], folder[8], text[512];
    if (!is_el(rec, "record")) continue;
    attr(rec, "type", type, sizeof(type)); attr(rec, "id", guid, sizeof(guid)); attr(rec, "folder", folder, sizeof(folder));
    if (strncmp(type, "Справочник.", strlen("Справочник.")) || strcmp(folder, "true") || !guid[0]) continue;
    db_import_source_t *src = source_find(st, guid);
    if (src && strcmp(src->table_name, "import_catalog_folders")) {
      note(st, "%s: existing source belongs to another catalog; retained", type, "");
      tally(st, type, 0, 0, 1); continue;
    }
    db_import_catalog_folder_t f = {0};
    if (src) {
      db_import_catalog_folder_t *old = (void *)send_db_message(st->ib, dbFind, MAKEDWORD(TABLE_IMPORT_CATALOG_FOLDERS, 0), (void *)(intptr_t)src->record_id);
      if (!old) goto failed;
      f = *old;
    }
    ledger_copy_text(f.metadata, sizeof(f.metadata), type);
    attr(rec, "code", text, sizeof(text)); ledger_copy_text(f.code, sizeof(f.code), text);
    attr(rec, "description", text, sizeof(text)); ledger_copy_text(f.name, sizeof(f.name), text);
    if (src) {
      if (!send_db_message(st->ib, dbUpdate, TABLE_IMPORT_CATALOG_FOLDERS, &f)) goto failed;
    } else {
      const db_import_catalog_folder_t *added = (const void *)send_db_message(st->ib, dbInsert, TABLE_IMPORT_CATALOG_FOLDERS, &f);
      if (!added || !source_remember(st, type, guid, "import_catalog_folders", added->id)) goto failed;
    }
    tally(st, type, src ? 0 : 1, src ? 1 : 0, 0);
    continue;
failed:
    snprintf(st->out->error, sizeof(st->out->error), ui_tr("Cannot store an imported record of %s."), type);
  }
  for (xmlNodePtr rec = st->records ? st->records->children : NULL; rec && !st->out->error[0]; rec = rec->next) {
    char type[96], guid[40], parent[40], deleted[8];
    if (!is_el(rec, "record")) continue;
    attr(rec, "type", type, sizeof(type)); attr(rec, "id", guid, sizeof(guid));
    if (strncmp(type, "Справочник.", strlen("Справочник."))) continue;
    db_import_source_t *src = source_find(st, guid);
    if (!src || strcmp(src->metadata, type)) continue;
    db_import_source_t source = *src;
    attr(rec, "parent", parent, sizeof(parent)); attr(rec, "deleted", deleted, sizeof(deleted));
    int parent_id = 0;
    if (parent[0]) {
      db_import_source_t *p = source_find(st, parent);
      if (!p || strcmp(p->table_name, "import_catalog_folders") || strcmp(p->metadata, type) || !strcmp(guid, parent)) {
        note(st, "%s: held back (%s)", type, ui_tr("its parent folder was not imported"));
        parent_id = source.catalog_folder_id;
      } else parent_id = p->record_id;
    }
    if (!strcmp(source.table_name, "import_catalog_folders")) {
      db_import_catalog_folder_t *old = (void *)send_db_message(st->ib, dbFind, MAKEDWORD(TABLE_IMPORT_CATALOG_FOLDERS, 0), (void *)(intptr_t)source.record_id);
      if (!old) goto link_failed;
      db_import_catalog_folder_t f = *old;
      if (folder_would_cycle(st, f.id, parent_id)) {
        note(st, "%s: held back (%s)", type, ui_tr("its folder hierarchy is invalid"));
        parent_id = f.parent_id;
      }
      f.parent_id = parent_id;
      if (!send_db_message(st->ib, dbUpdate, TABLE_IMPORT_CATALOG_FOLDERS, &f)) goto link_failed;
    }
    source.catalog_folder_id = parent_id;
    source.source_deleted = !strcmp(deleted, "true");
    if (source.source_deleted) note(st, "%s: record marked for deletion in source retained for historical references", type, "");
    if (!send_db_message(st->ib, dbUpdate, TABLE_IMPORT_SOURCES, &source)) goto link_failed;
    continue;
link_failed:
    snprintf(st->out->error, sizeof(st->out->error), ui_tr("Cannot store an imported record of %s."), type);
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
  for (xmlNodePtr reg = registers ? registers->children : NULL; reg; reg = reg->next) {
    char reg_type[96];
    if (!is_el(reg, "register")) continue;
    attr(reg, "type", reg_type, sizeof(reg_type));
    bool accounting = !strncmp(reg_type, "РегистрБухгалтерии", strlen("РегистрБухгалтерии"));
    bool depreciation = !strcmp(reg_type, "РегистрСведений.РасчетАмортизации");
    for (xmlNodePtr m = reg->children; m; m = m->next) {
      char recorder[40];
      if (!is_el(m, "movement")) continue;
      attr(m, "recorder", recorder, sizeof(recorder));
      if ((!accounting && !depreciation) || (depreciation && !recorder[0])) {
        tally(st, reg_type, 0, 0, 1);
        continue;
      }
      unsigned b = guid_bucket(recorder);
      st->movements[st->movement_count].node = m;
      st->movements[st->movement_count].depreciation = depreciation;
      st->movements[st->movement_count].next = st->buckets[b];
      st->buckets[b] = ++st->movement_count;
    }
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
  ledger_copy_text(e.source_period, sizeof(e.source_period), text);
  ledger_copy_text(e.date, sizeof(e.date), text[0] ? (text[10] = '\0', text) : "");
  v_text(mv, "Сумма", text, sizeof(text));
  e.amount = strtod(text, NULL);
  e.source_amount_exact = money_parse_decimal(text, 2, &e.amount_minor);
  v_text(mv, "Содержание", text, sizeof(text));
  ledger_copy_text(e.content, sizeof(e.content), text);
  const side_t *q = d.has_quantity ? &d : &c;
  if (q->has_quantity) {
    e.quantity = q->quantity; e.quantity_milli = q->quantity_milli;
  }
  e.debit_quantity_set = d.has_quantity; e.debit_quantity_milli = d.quantity_milli;
  e.credit_quantity_set = c.has_quantity; e.credit_quantity_milli = c.quantity_milli;
  const side_t *cur = d.has_currency_amount || d.currency ? &d : &c;
  e.currency_id = cur->currency;
  if (cur->has_currency_amount) e.currency_amount = cur->currency_amount;
  e.debit_tax_set = d.tax_set; e.debit_tax_minor = d.tax; e.debit_permanent_minor = d.permanent; e.debit_temporary_minor = d.temporary;
  e.credit_tax_set = c.tax_set; e.credit_tax_minor = c.tax; e.credit_permanent_minor = c.permanent; e.credit_temporary_minor = c.temporary;
  char line_attr[8], active_attr[8];
  attr(mv, "line", line_attr, sizeof(line_attr)); attr(mv, "active", active_attr, sizeof(active_attr));
  e.source_line = line_attr[0] ? atoi(line_attr) : 0;
  e.source_inactive = !strcmp(active_attr, "false");
  e.source_currencies_set = true;
  e.debit_currency_id  = d.currency; e.credit_currency_id  = c.currency;
  e.debit_currency_amount  = d.has_currency_amount ? d.currency_amount : 0;
  e.credit_currency_amount = c.has_currency_amount ? c.currency_amount : 0;
  return send_db_message(st->ib, dbInsert, TABLE_ENTRIES, &e) != 0;
}

static int document_type_for(stage_t *st, const char *source_type) {
  db_import_document_t *map = (db_import_document_t *)find_by_text(st->cfg, TABLE_IMPORT_DOCUMENTS, "source_type", source_type);
  db_doc_type_t *type = map ? (db_doc_type_t *)send_db_message(st->cfg, dbFind, MAKEDWORD(TABLE_DOC_TYPES, 0), (void *)(intptr_t)map->doc_type_id) : NULL;
  if (type && strcmp(type->form, "document") && strcmp(type->form, "operation") &&
      strcmp(type->form, "assets") && strcmp(type->form, "payroll")) {
    note(st, "%s: mapped to an unsupported form; the general operation is used", source_type, "");
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
    bool valid = !date[0] || ((!r->valid_from[0] || strcmp(date, r->valid_from) >= 0) && (!r->valid_to[0] || strcmp(date, r->valid_to) <= 0));
    if (!found && valid && percent >= 0 && (int)(r->rate + 0.5) == percent && strcmp(r->treatment, "exempt")) found = r->id;
  }
  free_result_list(rates);
  if (!found) note(st, "VAT rate %s has no rate in the configuration; lines keep none", enum_name, "");
  return found;
}

static bool supported_detail_section(const char *name) {
  const char *sections[] = { "РасчетыСКонтрагентами", "РасшифровкаПлатежа", "БухСправка", "РасчетыПоНалогамИСборам", "Взносы" };
  for (size_t i = 0; i < ARRAY_LEN(sections); i++) if (!strcmp(sections[i], name)) return true;
  return false;
}

static bool import_detail_row(stage_t *st, int doc_id, xmlNodePtr row, const char *section, const char *type) {
  char number[16];
  attr(row, "line", number, sizeof(number));
  for (xmlNodePtr value = row->children; value; value = value->next) {
    if (!is_el(value, "v")) continue;
    db_import_document_detail_t d = { .document_id = doc_id, .line_number = atoi(number) };
    ledger_copy_text(d.section_name, sizeof(d.section_name), section);
    attr(value, "n", d.field_name, sizeof(d.field_name));
    attr(value, "t", d.value_type, sizeof(d.value_type));
    attr(value, "type", d.reference_type, sizeof(d.reference_type));
    attr(value, "id", d.reference_guid, sizeof(d.reference_guid));
    char flag[8];
    attr(value, "empty", flag, sizeof(flag)); d.is_empty = !strcmp(flag, "true");
    attr(value, "dangling", flag, sizeof(flag)); d.is_dangling = !strcmp(flag, "true");
    xmlChar *text = xmlChildElementCount(value) ? NULL : xmlNodeGetContent(value);
    if (!text || xmlStrlen(text) >= (int)sizeof(d.value)) {
      note(st, "%s: a field in %s exceeds the source detail format", type, section);
      xmlFree(text);
      continue;
    }
    ledger_copy_text(d.value, sizeof(d.value), (const char *)text);
    xmlFree(text);
    if (!strcmp(d.value_type, "enum"))
      attr(value, "name", d.value, sizeof(d.value));
    else if (!strcmp(d.value_type, "ref")) {
      xmlNodePtr record = record_by_guid(st, d.reference_guid);
      if (record) attr(record, "description", d.value, sizeof(d.value));
    } else if (!strcmp(d.value_type, "undecoded"))
      attr(value, "hex", d.value, sizeof(d.value));
    if (!send_db_message(st->ib, dbInsert, TABLE_IMPORT_DOCUMENT_DETAILS, &d)) return false;
    st->out->detail_fields++;
  }
  return true;
}

static bool import_section_details(stage_t *st, int doc_id, xmlNodePtr table, const char *section, const char *type) {
  for (xmlNodePtr row = table->children; row; row = row->next)
    if (is_el(row, "row") && !import_detail_row(st, doc_id, row, section, type)) return false;
  return true;
}

// Item sections become document lines. Known non-item sections keep their original field values.
static bool import_lines(stage_t *st, int doc_id, xmlNodePtr doc, const char *type, const char *date, bool vat_included) {
  bool vat_book = !strcmp(type, "Документ.КнигаПокупокДляПередачиВЭлектронномВиде") ||
                  !strcmp(type, "Документ.КнигаПродажДляПередачиВЭлектронномВиде");
  for (xmlNodePtr t = doc->children; t; t = t->next) {
    if (!is_el(t, "table")) continue;
    char tname[96];
    attr(t, "n", tname, sizeof(tname));
    if (vat_book) {
      if (!import_section_details(st, doc_id, t, tname, type)) return false;
      continue;
    }
    bool items = false;
    for (xmlNodePtr row = t->children; row && !items; row = row->next) items = is_el(row, "row") && v_node(row, "Номенклатура");
    if (!items) {
      if (supported_detail_section(tname)) {
        if (!import_section_details(st, doc_id, t, tname, type)) return false;
      } else if (child(t, "row")) note(st, "%s: table %s is not imported as lines", type, tname);
      continue;
    }
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

static bool import_asset_lines(stage_t *st, int doc_id, xmlNodePtr doc, const char *type) {
  for (xmlNodePtr t = doc->children; t; t = t->next) {
    if (!is_el(t, "table")) continue;
    bool assets = false;
    for (xmlNodePtr row = t->children; row && !assets; row = row->next)
      assets = is_el(row, "row") && v_node(row, "ОсновноеСредство");
    if (!assets) continue;
    for (xmlNodePtr row = t->children; row; row = row->next) {
      if (!is_el(row, "row")) continue;
      int asset_id = ref_id(st, row, "ОсновноеСредство");
      if (!asset_id) { note(st, "%s: an asset line names an asset that was not imported", type, ""); continue; }
      db_asset_line_t l = {0};
      send_db_message(st->ib, dbInitRecord, TABLE_ASSET_LINES, &l);
      l.document_id = doc_id;
      l.asset_id = asset_id;
      char buf[64];
      v_text(row, "ПервоначальнаяСтоимость", buf, sizeof(buf));
      if (!buf[0]) v_text(row, "Сумма", buf, sizeof(buf));
      if (buf[0]) l.amount = strtod(buf, NULL);
      if (!send_db_message(st->ib, dbInsert, TABLE_ASSET_LINES, &l)) return false;
    }
  }
  return true;
}

static double payroll_value(xmlNodePtr row, const char *name) {
  char buf[64];
  v_text(row, name, buf, sizeof(buf));
  return buf[0] ? strtod(buf, NULL) : 0;
}

// 1C stores accruals by employment, but tax by person. Ledger's employee
// catalog represents people, so resolve employment through its source catalog.
static bool import_1c_payroll(stage_t *st, int doc_id, xmlNodePtr doc, const char *type) {
  typedef struct { db_payroll_line_t line; int64_t contributions_minor; } staged_payroll_line_t;
  staged_payroll_line_t *lines = NULL;
  size_t count = 0;
  bool ok = true, payout = !strcmp(type, "Документ.ВедомостьНаВыплатуЗарплатыВКассу");
  static const char *payout_tax_fields[] = { "Сумма", "СуммаСПревышения", "СуммаСПревышенияПоСтавке18",
    "СуммаСПревышенияПоСтавке20", "СуммаСПревышенияПоСтавке22" };
  static const char *contribution_fields[] = { "ПФРДоПредельнойВеличины", "ФФОМС", "ФСС", "ФССНесчастныеСлучаи" };
  static const char *tax_fields[] = { "Налог", "НалогСПревышения", "НалогСПревышенияПоСтавке18",
    "НалогСПревышенияПоСтавке20", "НалогСПревышенияПоСтавке22" };
  for (xmlNodePtr t = doc->children; t && ok; t = t->next) {
    if (!is_el(t, "table")) continue;
    char name[80]; attr(t, "n", name, sizeof(name));
    bool accrual = !strcmp(name, payout ? "Зарплата" : "Начисления"), tax = !strcmp(name, "НДФЛ");
    bool contributions = !payout && !strcmp(name, "Взносы");
    if (!accrual && !tax && !contributions) continue;
    for (xmlNodePtr row = t->children; row; row = row->next) {
      if (!is_el(row, "row")) continue;
      int emp_id = ref_id(st, row, "ФизическоеЛицо");
      if (accrual && !emp_id) {
        char guid[40]; v_ref(row, "Сотрудник", guid, sizeof(guid));
        emp_id = ref_id(st, record_by_guid(st, guid), "ФизическоеЛицо");
      }
      if (!emp_id) { note(st, "%s: a payroll line names an employee that was not imported", type, ""); continue; }
      size_t i = 0;
      while (i < count && lines[i].line.employee_id != emp_id) i++;
      if (i == count) {
        staged_payroll_line_t *more = realloc(lines, (count + 1) * sizeof(*lines));
        if (!more) { ok = false; break; }
        lines = more; memset(&lines[count], 0, sizeof(*lines));
        send_db_message(st->ib, dbInitRecord, TABLE_PAYROLL_LINES, &lines[count].line);
        lines[count].line.document_id = doc_id; lines[count].line.employee_id = emp_id; count++;
      }
      if (accrual) {
        double amount = payroll_value(row, payout ? "КВыплате" : "Результат");
        lines[i].line.amount += amount;
        if (payout) lines[i].line.net += amount;
      }
      if (tax) for (size_t j = 0; j < ARRAY_LEN(tax_fields); j++)
        lines[i].line.withheld += payroll_value(row, payout ? payout_tax_fields[j] : tax_fields[j]);
      if (contributions) {
        for (xmlNodePtr v = row->children; v; v = v->next) {
          if (!is_el(v, "v")) continue;
          char field[128], value[64], value_type[32];
          attr(v, "n", field, sizeof(field)); attr(v, "t", value_type, sizeof(value_type));
          if (strcmp(value_type, "decimal")) continue;
          v_text(row, field, value, sizeof(value));
          int64_t minor = 0;
          if (!money_parse_decimal(value, 2, &minor) || !minor) continue;
          bool known = false;
          for (size_t j = 0; j < ARRAY_LEN(contribution_fields); j++)
            if (!strcmp(field, contribution_fields[j])) { known = true; break; }
          if (known) {
            if (!money_add(lines[i].contributions_minor, minor, &lines[i].contributions_minor)) { ok = false; break; }
          }
          else note(st, "%s: contribution field %s is retained as source detail only", type, field);
        }
      }
      if (!ok) break;
    }
  }
  for (size_t i = 0; i < count && ok; i++) {
    lines[i].line.contributions = (double)lines[i].contributions_minor / 100.0;
    ok = send_db_message(st->ib, dbInsert, TABLE_PAYROLL_LINES, &lines[i].line) != 0;
  }
  free(lines);
  return ok;
}

static bool import_payroll_lines(stage_t *st, int doc_id, xmlNodePtr doc, const char *type) {
  if (!strcmp(type, "Документ.НачислениеЗарплаты") || !strcmp(type, "Документ.ВедомостьНаВыплатуЗарплатыВКассу"))
    return import_1c_payroll(st, doc_id, doc, type);
  for (xmlNodePtr t = doc->children; t; t = t->next) {
    if (!is_el(t, "table")) continue;
    bool employees = false;
    for (xmlNodePtr row = t->children; row && !employees; row = row->next)
      employees = is_el(row, "row") && v_node(row, "ФизическоеЛицо");
    if (!employees) continue;
    for (xmlNodePtr row = t->children; row; row = row->next) {
      if (!is_el(row, "row")) continue;
      int emp_id = ref_id(st, row, "ФизическоеЛицо");
      if (!emp_id) { note(st, "%s: a payroll line names an employee that was not imported", type, ""); continue; }
      db_payroll_line_t l = {0};
      send_db_message(st->ib, dbInitRecord, TABLE_PAYROLL_LINES, &l);
      l.document_id = doc_id;
      l.employee_id = emp_id;
      char buf[64];
      v_text(row, "Начислено", buf, sizeof(buf));     if (buf[0]) l.amount = strtod(buf, NULL);
      v_text(row, "НДФЛ", buf, sizeof(buf));          if (buf[0]) l.withheld = strtod(buf, NULL);
      v_text(row, "Взносы", buf, sizeof(buf));        if (buf[0]) l.contributions = strtod(buf, NULL);
      v_text(row, "КВыплате", buf, sizeof(buf));      if (buf[0]) l.net = strtod(buf, NULL);
      if (!l.net && l.amount > 0) l.net = l.amount - l.withheld;
      if (!send_db_message(st->ib, dbInsert, TABLE_PAYROLL_LINES, &l)) return false;
    }
  }
  return true;
}

// The lines and entries a re-import replaces: deleting the document cascades
// them away, but a rewrite keeps the document itself and its identity.
static bool delete_document_body(database_t *ib, int doc_id) {
  bool ok = true;
  int field = ledger_field(ib, TABLE_DOC_LINES, "document_id");
  result_node_t *lines = (result_node_t *)send_db_message(ib, dbFetch, MAKEDWORD(TABLE_DOC_LINES, field), (void *)(intptr_t)doc_id);
  EACH_RECORD(db_doc_line_t, l, lines) if (!send_db_message(ib, dbDelete, TABLE_DOC_LINES, (void *)(intptr_t)l->id)) { ok = false; break; }
  free_result_list(lines);
  if (!ok) return false;
  field = ledger_field(ib, TABLE_ASSET_LINES, "document_id");
  result_node_t *alines = (result_node_t *)send_db_message(ib, dbFetch, MAKEDWORD(TABLE_ASSET_LINES, field), (void *)(intptr_t)doc_id);
  EACH_RECORD(db_asset_line_t, al, alines) if (!send_db_message(ib, dbDelete, TABLE_ASSET_LINES, (void *)(intptr_t)al->id)) { ok = false; break; }
  free_result_list(alines);
  if (!ok) return false;
  field = ledger_field(ib, TABLE_PAYROLL_LINES, "document_id");
  result_node_t *plines = (result_node_t *)send_db_message(ib, dbFetch, MAKEDWORD(TABLE_PAYROLL_LINES, field), (void *)(intptr_t)doc_id);
  EACH_RECORD(db_payroll_line_t, pl, plines) if (!send_db_message(ib, dbDelete, TABLE_PAYROLL_LINES, (void *)(intptr_t)pl->id)) { ok = false; break; }
  free_result_list(plines);
  if (!ok) return false;
  field = ledger_field(ib, TABLE_IMPORT_DOCUMENT_DETAILS, "document_id");
  result_node_t *details = (result_node_t *)send_db_message(ib, dbFetch, MAKEDWORD(TABLE_IMPORT_DOCUMENT_DETAILS, field), (void *)(intptr_t)doc_id);
  EACH_RECORD(db_import_document_detail_t, d, details)
    if (!send_db_message(ib, dbDelete, TABLE_IMPORT_DOCUMENT_DETAILS, (void *)(intptr_t)d->id)) { ok = false; break; }
  free_result_list(details);
  if (!ok) return false;
  field = ledger_field(ib, TABLE_ENTRIES, "document_id");
  result_node_t *entries = (result_node_t *)send_db_message(ib, dbFetch, MAKEDWORD(TABLE_ENTRIES, field), (void *)(intptr_t)doc_id);
  EACH_RECORD(db_entry_t, e, entries) if (!send_db_message(ib, dbDelete, TABLE_ENTRIES, (void *)(intptr_t)e->id)) { ok = false; break; }
  free_result_list(entries);
  return ok;
}

// Why a document found again may not be touched: a closed period or a linked
// reversal keep it as it is; new_date is the date the file would give it
// (NULL when the document is only being removed).
static bool document_is_protected(stage_t *st, const db_document_t *old, const char *type, const char *number, const char *new_date) {
  if (ledger_period_closed(st->ib, old->date) || (new_date && new_date[0] && ledger_period_closed(st->ib, new_date))) {
    note(st, "%s: the document %s was kept: its period is closed", type, number);
    return true;
  }
  if (ledger_reversal_linked(st->ib, old->id)) {
    note(st, "%s: the document %s was kept: a reversal is linked to it", type, number);
    return true;
  }
  return false;
}

static bool finish_imported_document(stage_t *st, int doc_id, xmlNodePtr source, bool posted) {
  const db_document_t *stored = (const db_document_t *)send_db_message(st->ib, dbFind, MAKEDWORD(TABLE_DOCUMENTS, 0), (void *)(intptr_t)doc_id);
  if (!stored) return false;
  db_document_t doc = *stored;
  const db_doc_type_t *type = (const void *)send_db_message(st->cfg, dbFind, MAKEDWORD(TABLE_DOC_TYPES, 0), (void *)(intptr_t)doc.doc_type_id);
  double amount = 0, vat = 0;
  int field = ledger_field(st->ib, TABLE_DOC_LINES, "document_id");
  result_node_t *lines = (result_node_t *)send_db_message(st->ib, dbFetch, MAKEDWORD(TABLE_DOC_LINES, field), (void *)(intptr_t)doc_id);
  bool have_goods = lines && type && !strcmp(type->form, "document");
  if (have_goods) EACH_RECORD(db_doc_line_t, line, lines) { amount += line->amount; vat += line->vat; }
  free_result_list(lines);
  char text[64];
  v_text(source, "СуммаДокумента", text, sizeof(text));
  int64_t source_total;
  if (text[0] && money_parse_decimal(text, 2, &source_total)) amount = (double)source_total / 100.0 - vat;
  else if (!have_goods) {
    field = ledger_field(st->ib, TABLE_ENTRIES, "document_id");
    result_node_t *entries = (result_node_t *)send_db_message(st->ib, dbFetch, MAKEDWORD(TABLE_ENTRIES, field), (void *)(intptr_t)doc_id);
    EACH_RECORD(db_entry_t, entry, entries) if (!entry->source_inactive) amount += entry->amount;
    free_result_list(entries);
  }
  doc.amount = amount; doc.vat = vat; doc.total = amount + vat;
  doc.posted = posted;
  return send_db_message(st->ib, dbUpdate, TABLE_DOCUMENTS, &doc) != 0;
}

static void import_document(stage_t *st, xmlNodePtr doc) {
  char type[128], guid[40], number[40], date[40], posted[8], deleted[8], key[80];
  attr(doc, "type", type, sizeof(type)); attr(doc, "id", guid, sizeof(guid)); attr(doc, "number", number, sizeof(number));
  attr(doc, "date", date, sizeof(date)); attr(doc, "posted", posted, sizeof(posted)); attr(doc, "deleted", deleted, sizeof(deleted));
  db_import_source_t *src = source_find(st, guid);
  const db_document_t *old = NULL;
  if (src && !strcmp(src->table_name, "documents") && src->record_id > 0)
    old = (const db_document_t *)send_db_message(st->ib, dbFind, MAKEDWORD(TABLE_DOCUMENTS, 0), (void *)(intptr_t)src->record_id);
  if (!strcmp(deleted, "true")) {
    if (!old) { tally(st, type, 0, 0, 1); return; }  // never imported: nothing to remove
    if (document_is_protected(st, old, type, number, NULL)) { tally(st, type, 0, 0, 1); return; }
    bool gone = send_db_message(st->ib, dbDelete, TABLE_DOCUMENTS, (void *)(intptr_t)old->id) != 0;
    if (gone) gone = send_db_message(st->ib, dbDelete, TABLE_IMPORT_SOURCES, (void *)(intptr_t)src->id) != 0;
    if (gone) tally_removed(st, type);
    else {
      note(st, "%s: the document %s was kept: it is still referenced", type, number);
      tally(st, type, 0, 0, 1);
    }
    return;
  }
  int first = st->buckets[guid_bucket(guid)], moves = 0, active_moves = 0;
  for (int i = first; i; i = st->movements[i - 1].next) {
    if (st->movements[i - 1].depreciation) continue;
    char recorder[40], active[8];
    attr(st->movements[i - 1].node, "recorder", recorder, sizeof(recorder));
    if (strcmp(recorder, guid)) continue;
    attr(st->movements[i - 1].node, "active", active, sizeof(active));
    moves++;
    active_moves += strcmp(active, "false") != 0;
  }
  bool is_posted = !strcmp(posted, "true");
  if (!is_posted && active_moves) note(st, "%s: not posted in 1C but with register movements; imported posted", type, "");
  int type_id = document_type_for(st, type);
  if (!type_id) { snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("The configuration has no operation document type.")); return; }
  if (old && document_is_protected(st, old, type, number, date)) { tally(st, type, 0, 0, 1); return; }
  db_document_t d = {0};
  if (old) d = *old;  // the document keeps its identity; the file overwrites what it describes
  else send_db_message(st->ib, dbInitRecord, TABLE_DOCUMENTS, &d);
  d.doc_type_id = type_id;
  ledger_copy_text(d.source_posted_state, sizeof(d.source_posted_state), is_posted ? "posted" :
                   !strcmp(posted, "false") ? "unposted" : "");
  ledger_copy_text(d.number, sizeof(d.number), number);
  if (strlen(date) >= 19) ledger_copy_text(d.time, sizeof(d.time), date + 11);
  else d.time[0] = '\0';
  ledger_copy_text(d.date, sizeof(d.date), date[0] ? (date[10] = '\0', date) : "");
  d.counterparty_id = ref_id(st, doc, "Контрагент");
  d.contract_id = ref_id(st, doc, "ДоговорКонтрагента");
  d.warehouse_id = ref_id(st, doc, "Склад");
  d.bank_account_id = ref_id(st, doc, "СчетОрганизации") ? ref_id(st, doc, "СчетОрганизации") : ref_id(st, doc, "БанковскийСчет");
  d.currency_id = ref_id(st, doc, "ВалютаДокумента");
  if (old && d.currency_id != old->currency_id) d.rate = 0;  // recalculate fills the new currency's rate
  char text[512];
  v_text(doc, "Комментарий", text, sizeof(text));
  ledger_copy_text(d.comment, sizeof(d.comment), text);
  v_text(doc, "НазначениеПлатежа", text, sizeof(text));
  if (text[0]) ledger_copy_text(d.payment_reference, sizeof(d.payment_reference), text);
  v_text(doc, "Основание", text, sizeof(text));
  if (text[0] && !d.comment[0]) ledger_copy_text(d.comment, sizeof(d.comment), text);
  v_text(doc, "Приложение", text, sizeof(text));
  if (text[0] && !d.legal_note[0]) ledger_copy_text(d.legal_note, sizeof(d.legal_note), text);
  snprintf(key, sizeof(key), "1cd:%s", guid);
  ledger_copy_text(d.import_source, sizeof(d.import_source), key);
  int doc_id = 0;
  if (old) {
    d.posted = false;  // the rewrite posts it again below
    if (!send_db_message(st->ib, dbUpdate, TABLE_DOCUMENTS, &d)) {
      note(st, "%s: the document %s was kept: it could not be updated", type, number);
      tally(st, type, 0, 0, 1);
      return;
    }
    if (!delete_document_body(st->ib, d.id)) {
      snprintf(st->out->error, sizeof(st->out->error), ui_tr("Cannot store the imported document %s."), number);
      return;
    }
    doc_id = d.id;
  } else {
    const db_document_t *added = (const db_document_t *)send_db_message(st->ib, dbInsert, TABLE_DOCUMENTS, &d);
    if (!added) { snprintf(st->out->error, sizeof(st->out->error), ui_tr("Cannot store the imported document %s."), number); return; }
    doc_id = added->id;
    if (src && !strcmp(src->table_name, "documents")) {  // its record was deleted in Ledger: the identity follows the new one
      src->record_id = doc_id;
      if (!send_db_message(st->ib, dbUpdate, TABLE_IMPORT_SOURCES, src)) {
        snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("Cannot record the source of an imported record."));
        return;
      }
    }
  }
  char included[8];
  v_text(doc, "СуммаВключаетНДС", included, sizeof(included));
  if (!import_lines(st, doc_id, doc, type, d.date, !strcmp(included, "true")))
    snprintf(st->out->error, sizeof(st->out->error), ui_tr("Cannot store a line of the imported document %s."), number);
  const db_doc_type_t *doc_type = st->out->error[0] ? NULL :
    (const db_doc_type_t *)send_db_message(st->cfg, dbFind, MAKEDWORD(TABLE_DOC_TYPES, 0), (void *)(intptr_t)type_id);
  if (!st->out->error[0] && doc_type && !strcmp(doc_type->form, "assets") &&
      !import_asset_lines(st, doc_id, doc, type))
    snprintf(st->out->error, sizeof(st->out->error), ui_tr("Cannot store a line of the imported document %s."), number);
  if (!st->out->error[0] && doc_type && !strcmp(doc_type->form, "payroll") &&
      !import_payroll_lines(st, doc_id, doc, type))
    snprintf(st->out->error, sizeof(st->out->error), ui_tr("Cannot store a line of the imported document %s."), number);
  for (int i = first; i && !st->out->error[0]; i = st->movements[i - 1].next) {
    char recorder[40];
    xmlNodePtr movement = st->movements[i - 1].node;
    attr(movement, "recorder", recorder, sizeof(recorder));
    if (strcmp(recorder, guid)) continue;
    if (st->movements[i - 1].depreciation) {
      if (!import_detail_row(st, doc_id, movement, "РасчетАмортизации", type))
        snprintf(st->out->error, sizeof(st->out->error), ui_tr("Cannot store a line of the imported document %s."), number);
    } else if (!import_movement(st, doc_id, movement))
      snprintf(st->out->error, sizeof(st->out->error), ui_tr("Cannot store an entry of the imported document %s."), number);
  }
  if (!st->out->error[0] && !finish_imported_document(st, doc_id, doc, is_posted || active_moves))
    snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("Cannot finish the imported document."));
  if (!st->out->error[0] && !source_remember(st, type, guid, "documents", doc_id))
    snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("Cannot record the source of an imported record."));
  if (!st->out->error[0]) { tally(st, type, old ? 0 : 1, old ? 1 : 0, 0); st->out->entries += moves; }
}

// The OData handoff is intentionally narrower than a full 1CD stage.
static bool unf_catalog_stage(xmlNodePtr root) {
  xmlNodePtr records = NULL;
  for (xmlNodePtr n = root->children; n; n = n->next) {
    if (n->type != XML_ELEMENT_NODE) continue;
    if (!is_el(n, "records") || records) return false;
    records = n;
  }
  if (!records) return false;
  for (xmlNodePtr n = records->children; n; n = n->next) {
    if (n->type != XML_ELEMENT_NODE) continue;
    char type[96], guid[64], name[512];
    attr(n, "type", type, sizeof(type));
    attr(n, "id", guid, sizeof(guid));
    attr(n, "description", name, sizeof(name));
    bool item = !strcmp(type, "Справочник.Номенклатура");
    bool counterparty = !strcmp(type, "Справочник.Контрагенты");
    bool warehouse = !strcmp(type, "Справочник.Склады");
    if (!is_el(n, "record") || !(item || counterparty || warehouse) || !name[0] || strlen(guid) != 36)
      return false;
    for (int i = 0; i < 36; ++i) {
      char ch = guid[i];
      if ((i == 8 || i == 13 || i == 18 || i == 23) ? ch != '-'
          : !((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    }
  }
  return true;
}

// ── Exchange rates ───────────────────────────────────────────────────────────
static void import_rates(stage_t *st, xmlNodePtr root) {
  xmlNodePtr rates = child(root, "rates");
  for (xmlNodePtr r = rates ? rates->children : NULL; r && !st->out->error[0]; r = r->next) {
    if (!is_el(r, "rate")) continue;
    char currency_guid[40], period[40], text[64];
    attr(r, "currency", currency_guid, sizeof(currency_guid));
    attr(r, "period", period, sizeof(period));
    if (!currency_guid[0] || !period[0]) continue;
    db_import_source_t *cs = source_find(st, currency_guid);
    if (!cs || !cs->record_id) continue;
    v_text(r, "Курс", text, sizeof(text));
    double rate_val = strtod(text, NULL);
    v_text(r, "Кратность", text, sizeof(text));
    int mult = text[0] ? atoi(text) : 1;
    if (!rate_val || mult <= 0) continue;
    char date[12];
    ledger_copy_text(date, sizeof(date), period);
    if (strlen(date) > 10) date[10] = '\0';
    int fld = ledger_field(st->ib, TABLE_RATES, "currency_id");
    result_node_t *existing = (result_node_t *)send_db_message(st->ib, dbFetch, MAKEDWORD(TABLE_RATES, fld), (void *)(intptr_t)cs->record_id);
    db_rate_t *found = NULL;
    for (result_node_t *n = existing; n && !found; n = n->next) { db_rate_t *rv = *(db_rate_t **)n->data; if (!strcmp(rv->date, date)) found = rv; }
    if (found) {
      found->rate = rate_val / mult;
      if (!send_db_message(st->ib, dbUpdate, TABLE_RATES, found))
        snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("Cannot store an imported exchange rate."));
      else tally(st, "РегистрСведений.КурсыВалют", 0, 1, 0);
    } else {
      db_rate_t rate = { .currency_id = cs->record_id, .rate = rate_val / mult };
      ledger_copy_text(rate.date, sizeof(rate.date), date);
      const int *added = (const int *)send_db_message(st->ib, dbInsert, TABLE_RATES, &rate);
      if (!added)
        snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("Cannot store an imported exchange rate."));
      else tally(st, "РегистрСведений.КурсыВалют", 1, 0, 0);
    }
    free_result_list(existing);
  }
}

static int b64_digit(unsigned char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

// The XML serializer writes at most 2047 characters per field. Base64 needs groups of four.
enum { ATTACHMENT_CHUNK_CHARS = 2044, ATTACHMENT_CHUNK_BYTES = 1533, ATTACHMENT_MAX_BYTES = 8 * 1024 * 1024 };

static bool attachment_decode(const char *s, size_t n, unsigned char *data, size_t capacity, size_t *bytes) {
  if (n % 4) return false;
  *bytes = 0;
  for (size_t i = 0; i < n; i += 4) {
    int a = b64_digit(s[i]), b = b64_digit(s[i + 1]);
    int c = s[i + 2] == '=' ? -2 : b64_digit(s[i + 2]);
    int d = s[i + 3] == '=' ? -2 : b64_digit(s[i + 3]);
    if (a < 0 || b < 0 || c == -1 || d == -1 || (c == -2 && d != -2) ||
        ((c == -2 || d == -2) && i + 4 != n) ||
        (c == -2 && (b & 15)) || (d == -2 && c >= 0 && (c & 3))) return false;
    size_t count = 1 + (c >= 0) + (d >= 0);
    if (count > capacity - *bytes) return false;
    data[(*bytes)++] = (unsigned char)((a << 2) | (b >> 4));
    if (c >= 0) data[(*bytes)++] = (unsigned char)((b << 4) | (c >> 2));
    if (d >= 0) data[(*bytes)++] = (unsigned char)((c << 6) | d);
  }
  return true;
}

static bool attachment_hash_matches(const unsigned char *data, size_t size, const char *sha256) {
  char hash[65];
  ledger_sha256(data, size, hash);
  return !strcmp(hash, sha256);
}

static bool attachment_attr(xmlNodePtr n, const char *key, char *out, size_t size) {
  xmlChar *v = xmlGetProp(n, (const xmlChar *)key);
  if (v && strlen((const char *)v) >= size) { xmlFree(v); return false; }
  ledger_copy_text(out, size, v ? (const char *)v : "");
  if (v) xmlFree(v);
  return true;
}

static bool attachment_guid(const char *s) {
  if (strlen(s) != 36) return false;
  for (int i = 0; i < 36; i++)
    if ((i == 8 || i == 13 || i == 18 || i == 23) ? s[i] != '-' : !isxdigit((unsigned char)s[i])) return false;
  return true;
}

static bool import_attachment(stage_t *st, xmlNodePtr n) {
  db_import_attachment_t a = {0};
  char size_text[32], *end = NULL;
  if (!attachment_attr(n, "id", a.guid, sizeof(a.guid)) ||
      !attachment_attr(n, "type", a.metadata, sizeof(a.metadata)) ||
      !attachment_attr(n, "owner-id", a.owner_guid, sizeof(a.owner_guid)) ||
      !attachment_attr(n, "owner-type", a.owner_type, sizeof(a.owner_type)) ||
      !attachment_attr(n, "name", a.name, sizeof(a.name)) ||
      !attachment_attr(n, "extension", a.extension, sizeof(a.extension)) ||
      !attachment_attr(n, "size", size_text, sizeof(size_text)) ||
      !attachment_attr(n, "sha256", a.sha256, sizeof(a.sha256)) ||
      !attachment_attr(n, "unavailable", a.unavailable, sizeof(a.unavailable)) ||
      !attachment_attr(n, "volume-id", a.volume_guid, sizeof(a.volume_guid)) ||
      !attachment_guid(a.guid) || !a.metadata[0] || !size_text[0] ||
      (a.owner_guid[0] && !attachment_guid(a.owner_guid)) || (a.volume_guid[0] && !attachment_guid(a.volume_guid))) return false;
  for (const char *p = size_text; *p; p++) if (!isdigit((unsigned char)*p)) return false;
  for (const char *p = a.sha256; *p; p++) if (!isxdigit((unsigned char)*p)) return false;
  for (xmlNodePtr c = n->children; c; c = c->next) if (c->type != XML_TEXT_NODE) return false;
  errno = 0;
  long long size = strtoll(size_text, &end, 10);
  if (errno == ERANGE || *end || size < 0 || (!a.unavailable[0] && size > ATTACHMENT_MAX_BYTES)) return false;
  a.size = size;
  xmlChar *content = xmlNodeGetContent(n);
  size_t encoded = content ? strlen((const char *)content) : 0, decoded = 0;
  bool valid = a.unavailable[0] && encoded == 0;
  if (!a.unavailable[0] && strlen(a.sha256) == 64 && encoded == ((size_t)size + 2) / 3 * 4) {
    unsigned char *bytes = malloc(size ? (size_t)size : 1);
    valid = bytes && attachment_decode((const char *)content, encoded, bytes, (size_t)size, &decoded) &&
      decoded == (size_t)size && attachment_hash_matches(bytes, decoded, a.sha256);
    free(bytes);
  }
  if (!valid) { if (content) xmlFree(content); return false; }
  db_import_attachment_t *old = find_by_text(st->ib, TABLE_IMPORT_ATTACHMENTS, "guid", a.guid);
  if (old && strcmp(old->metadata, a.metadata)) { if (content) xmlFree(content); return false; }
  if (old && a.unavailable[0] && !old->unavailable[0]) {
    tally(st, a.metadata, 0, 0, 1); if (content) xmlFree(content); return true;
  }
  if (old) {
    a.id = old->id;
    int field = ledger_field(st->ib, TABLE_IMPORT_ATTACHMENT_CHUNKS, "attachment_id");
    result_node_t *chunks = (result_node_t *)send_db_message(st->ib, dbFetch, MAKEDWORD(TABLE_IMPORT_ATTACHMENT_CHUNKS, field), (void *)(intptr_t)a.id);
    bool ok = true;
    EACH_RECORD(db_import_attachment_chunk_t, c, chunks)
      if (!send_db_message(st->ib, dbDelete, TABLE_IMPORT_ATTACHMENT_CHUNKS, (void *)(intptr_t)c->id)) { ok = false; break; }
    free_result_list(chunks);
    if (!ok || !send_db_message(st->ib, dbUpdate, TABLE_IMPORT_ATTACHMENTS, &a)) { if (content) xmlFree(content); return false; }
  } else {
    const int *id = (const int *)send_db_message(st->ib, dbInsert, TABLE_IMPORT_ATTACHMENTS, &a);
    if (!id) { if (content) xmlFree(content); return false; }
    a.id = *id;
  }
  for (size_t i = 0; i < encoded; i += ATTACHMENT_CHUNK_CHARS) {
    db_import_attachment_chunk_t chunk = { .attachment_id = a.id, .ordinal = (int)(i / ATTACHMENT_CHUNK_CHARS) };
    size_t count = encoded - i < ATTACHMENT_CHUNK_CHARS ? encoded - i : ATTACHMENT_CHUNK_CHARS;
    memcpy(chunk.data, content + i, count); chunk.data[count] = 0;
    if (!send_db_message(st->ib, dbInsert, TABLE_IMPORT_ATTACHMENT_CHUNKS, &chunk)) { xmlFree(content); return false; }
  }
  tally(st, a.metadata, old ? 0 : 1, old ? 1 : 0, 0);
  if (content) xmlFree(content);
  return true;
}

bool ledger_attachment_export(database_t *ib, int attachment_id, const char *path) {
  if (!ib || !path || !path[0] || attachment_id <= 0) return false;
  db_import_attachment_t *a = (db_import_attachment_t *)send_db_message(ib, dbFind, MAKEDWORD(TABLE_IMPORT_ATTACHMENTS, 0), (void *)(intptr_t)attachment_id);
  if (!a || a->unavailable[0] || a->size < 0 || a->size > ATTACHMENT_MAX_BYTES) return false;
  size_t size = (size_t)a->size, count = (size + ATTACHMENT_CHUNK_BYTES - 1) / ATTACHMENT_CHUNK_BYTES;
  unsigned char *all = malloc(size ? size : 1);
  const db_import_attachment_chunk_t **ordered = calloc(count ? count : 1, sizeof(*ordered));
  if (!all || !ordered) { free(all); free(ordered); return false; }
  int field = ledger_field(ib, TABLE_IMPORT_ATTACHMENT_CHUNKS, "attachment_id");
  result_node_t *chunks = (result_node_t *)send_db_message(ib, dbFetch, MAKEDWORD(TABLE_IMPORT_ATTACHMENT_CHUNKS, field), (void *)(intptr_t)attachment_id);
  bool ok = true;
  EACH_RECORD(db_import_attachment_chunk_t, part, chunks) {
    if (part->ordinal < 0 || (size_t)part->ordinal >= count || ordered[part->ordinal]) { ok = false; break; }
    ordered[part->ordinal] = part;
  }
  for (size_t i = 0; i < count && ok; i++) {
    size_t offset = i * ATTACHMENT_CHUNK_BYTES;
    size_t capacity = size - offset, decoded = 0;
    if (capacity > ATTACHMENT_CHUNK_BYTES) capacity = ATTACHMENT_CHUNK_BYTES;
    ok = ordered[i] && attachment_decode(ordered[i]->data, strlen(ordered[i]->data), all + offset, capacity, &decoded) &&
      decoded == capacity;
  }
  free(ordered);
  free_result_list(chunks);
  if (ok && attachment_hash_matches(all, size, a->sha256)) {
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0600);
    FILE *out = fd >= 0 ? fdopen(fd, "wb") : NULL;
    ok = out && fwrite(all, 1, size, out) == size;
    if (out) ok = fclose(out) == 0 && ok;
    else if (fd >= 0) close(fd);
    if (!ok && fd >= 0) unlink(path);
  } else {
    ok = false;
  }
  free(all);
  return ok;
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
  char source[32];
  attr(root, "source", source, sizeof(source));
  const bool unf_catalogs = !strcmp(source, "unf-odata");
  if (xml->intSubset || xml->extSubset || (unf_catalogs && !unf_catalog_stage(root))) {
    const char *error = unf_catalogs ? ui_tr("The 1C UNF stage must contain catalogs only.") :
      ui_tr("The file is not a 1C staging file (orion-1cd-stage-v1).");
    snprintf(out->error, sizeof(out->error), "%s", error);
    xmlFreeDoc(xml);
    return false;
  }
  stage_t st = { .ib = ib, .cfg = cfg, .out = out, .unf_catalogs = unf_catalogs };
  if (!send_db_message(ib, dbBeginChanges, 0, NULL)) {
    snprintf(out->error, sizeof(out->error), "%s", ui_tr("Cannot begin the import transaction."));
    xmlFreeDoc(xml);
    return false;
  }
  // Kinds first: accounts name them.
  xmlNodePtr kinds = child(root, "subkonto-kinds"), chart = child(root, "chart-of-accounts");
  st.records = child(root, "records");
  st.documents = child(root, "documents");
  st.registers = child(root, "registers");
  for (xmlNodePtr r = st.records ? st.records->children : NULL; r && !out->error[0]; r = r->next) {
    char type[96];
    attr(r, "type", type, sizeof(type));
    if (is_el(r, "record") && !strcmp(type, "Справочник.КлассификаторЕдиницИзмерения")) import_unit(&st, r);
  }
  for (xmlNodePtr r = kinds ? kinds->children : NULL; r && !out->error[0]; r = r->next) if (is_el(r, "record")) import_kind(&st, r);
  for (xmlNodePtr r = chart ? chart->children : NULL; r && !out->error[0]; r = r->next) if (is_el(r, "account")) import_account(&st, r);
  for (xmlNodePtr r = st.records ? st.records->children : NULL; r && !out->error[0]; r = r->next) {
    char type[96];
    attr(r, "type", type, sizeof(type));
    if (is_el(r, "record") && !strcmp(type, "Справочник.Организации")) import_organization(&st, r);
  }
  if (!unf_catalogs && !out->error[0] && !ledger_apply_extensions(ib, cfg))  // movements name the new accounts
    snprintf(out->error, sizeof(out->error), "%s", ui_tr("The imported accounts could not join the configuration."));
  for (size_t i = 0; i < ARRAY_LEN(kStageCatalogs) && !out->error[0]; i++)
    for (xmlNodePtr r = st.records ? st.records->children : NULL; r && !out->error[0]; r = r->next) {
      char type[96];
      attr(r, "type", type, sizeof(type));
      if (is_el(r, "record") && !strcmp(type, kStageCatalogs[i].type)) import_catalog_record(&st, &kStageCatalogs[i], r);
    }
  if (!out->error[0]) import_analytic_records(&st);
  if (!unf_catalogs && !out->error[0]) import_catalog_folders(&st);
  if (!out->error[0]) report_unimported(&st);
  if (!out->error[0]) import_rates(&st, root);
  xmlNodePtr constants = child(root, "constants");
  if (constants) tally(&st, "Константа", 0, 0, (int)xmlChildElementCount(constants));
  st.documents = child(root, "documents");
  if (!out->error[0] && !index_movements(&st, root)) snprintf(out->error, sizeof(out->error), "%s", ui_tr("Out of memory."));
  for (xmlNodePtr d = st.documents ? st.documents->children : NULL; d && !out->error[0]; d = d->next) if (is_el(d, "document")) import_document(&st, d);
  xmlNodePtr attachments = child(root, "attachments");
  for (xmlNodePtr a = attachments ? attachments->children : NULL; a && !out->error[0]; a = a->next) {
    if (!is_el(a, "attachment")) continue;
    char guid[64]; attr(a, "id", guid, sizeof(guid));
    bool duplicate = false;
    for (xmlNodePtr prev = attachments->children; prev != a; prev = prev->next) {
      char previous[64]; if (!is_el(prev, "attachment")) continue;
      attr(prev, "id", previous, sizeof(previous));
      if (!strcmp(guid, previous)) { duplicate = true; break; }
    }
    if (duplicate || !import_attachment(&st, a))
      snprintf(out->error, sizeof(out->error), "%s", ui_tr("Cannot import an invalid attachment."));
  }
  if (!out->error[0]) {
    xmlNodePtr hdr = child(root, "header");
    db_import_snapshot_t snap = {0};
    attr(child(hdr, "source"), "file", snap.source_file, sizeof(snap.source_file));
    attr(child(hdr, "source"), "size", snap.source_size, sizeof(snap.source_size));
    attr(child(hdr, "source"), "sha256", snap.source_sha256, sizeof(snap.source_sha256));
    attr(child(hdr, "source"), "format-version", snap.format_version, sizeof(snap.format_version));
    attr(child(hdr, "source"), "locale", snap.locale, sizeof(snap.locale));
    attr(child(hdr, "configuration"), "name", snap.config_name, sizeof(snap.config_name));
    attr(child(hdr, "configuration"), "version", snap.config_version, sizeof(snap.config_version));
    time_t now = time(NULL); struct tm *today = localtime(&now);
    if (today) strftime(snap.imported_date, sizeof(snap.imported_date), "%Y-%m-%d", today);
    if (!send_db_message(ib, dbInsert, TABLE_IMPORT_SNAPSHOTS, &snap))
      snprintf(out->error, sizeof(out->error), "%s", ui_tr("Cannot store the import snapshot."));
  }
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

bool ledger_stage_header(const char *path, ledger_stage_header_t *out) {
  memset(out, 0, sizeof(*out));
  xmlDocPtr xml = path ? xmlReadFile(path, NULL, XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_HUGE) : NULL;
  xmlNodePtr root = xml ? xmlDocGetRootElement(xml) : NULL;
  char format[64];
  attr(root, "format", format, sizeof(format));
  if (!is_el(root, "stage") || strcmp(format, STAGE_FORMAT)) { if (xml) xmlFreeDoc(xml); return false; }
  xmlNodePtr hdr = child(root, "header");
  attr(child(hdr, "source"), "file",           out->source_file,    sizeof(out->source_file));
  attr(child(hdr, "source"), "size",           out->source_size,    sizeof(out->source_size));
  attr(child(hdr, "source"), "sha256",         out->source_sha256,  sizeof(out->source_sha256));
  attr(child(hdr, "source"), "format-version", out->format_version, sizeof(out->format_version));
  attr(child(hdr, "source"), "locale",         out->locale,         sizeof(out->locale));
  attr(child(hdr, "configuration"), "name",    out->config_name,    sizeof(out->config_name));
  attr(child(hdr, "configuration"), "version", out->config_version, sizeof(out->config_version));
  xmlNodePtr chart = child(root, "chart-of-accounts");
  for (xmlNodePtr n = chart ? chart->children : NULL; n; n = n->next) out->accounts += is_el(n, "account");
  xmlNodePtr records = child(root, "records"), docs = child(root, "documents"), registers = child(root, "registers");
  for (xmlNodePtr n = records ? records->children : NULL; n; n = n->next) out->records += is_el(n, "record");
  for (xmlNodePtr n = docs ? docs->children : NULL; n; n = n->next) out->documents += is_el(n, "document");
  for (xmlNodePtr reg = registers ? registers->children : NULL; reg; reg = reg->next)
    if (is_el(reg, "register"))
      for (xmlNodePtr n = reg->children; n; n = n->next) out->movements += is_el(n, "movement");
  xmlNodePtr attachments = child(root, "attachments");
  for (xmlNodePtr n = attachments ? attachments->children : NULL; n; n = n->next) {
    char volume[40], unavailable[8];
    if (!is_el(n, "attachment")) continue;
    attr(n, "volume-id", volume, sizeof(volume));
    attr(n, "unavailable", unavailable, sizeof(unavailable));
    out->external_attachments += volume[0] && unavailable[0];
  }
  char skipped[24];
  attr(child(root, "skipped"), "tables", skipped, sizeof(skipped));
  out->skipped = atoi(skipped);
  xmlFreeDoc(xml);
  return true;
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
  ADD(ui_tr("Source detail fields written: %d\n"), r->detail_fields);
  if (summary) { ADD(ui_tr("Held back or not imported: %d"), held); return n; }
  ADD("\n");
  for (int i = 0; i < r->count_types; i++)
    ADD(ui_tr("%s: added %d, updated %d, removed %d, held back %d\n"), r->counts[i].type, r->counts[i].added, r->counts[i].updated, r->counts[i].removed, r->counts[i].held);
  if (r->notes[0]) ADD("\n%s\n%s", ui_tr("Held back or not imported:"), r->notes);
#undef ADD
  return n;
}

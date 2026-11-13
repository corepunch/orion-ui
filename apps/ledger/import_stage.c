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
} stage_t;

// Copies into a record field, cutting at a character boundary: the stores
// write UTF-8, and a name cut inside a Cyrillic letter would not load again.
static void copy_text(char *dst, size_t sz, const char *src) {
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
  snprintf(buf, sz, "%s", v ? (const char *)v : "");
  if (v) xmlFree(v);
}

static void note(stage_t *st, const char *fmt, const char *a, const char *b) {
  size_t used = strlen(st->out->notes);
  if (used + 2 >= sizeof(st->out->notes)) return;
  snprintf(st->out->notes + used, sizeof(st->out->notes) - used, fmt, a, b);
  used = strlen(st->out->notes);
  if (used + 1 < sizeof(st->out->notes)) { st->out->notes[used] = '\n'; st->out->notes[used + 1] = '\0'; }
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
  copy_text(s.metadata, sizeof(s.metadata), metadata);
  copy_text(s.guid, sizeof(s.guid), guid);
  copy_text(s.table_name, sizeof(s.table_name), table);
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

static void import_kind(stage_t *st, xmlNodePtr rec) {
  char metadata[256], guid[40], code[64], name[512];
  attr(rec, "type", metadata, sizeof(metadata)); attr(rec, "id", guid, sizeof(guid));
  attr(rec, "code", code, sizeof(code)); attr(rec, "description", name, sizeof(name));
  if (source_find(st, guid)) { st->out->updated++; return; }
  const char *table = kind_table_for(rec);
  db_subconto_kind_t *known = table ? (db_subconto_kind_t *)find_by_text(st->cfg, TABLE_SUBCONTO_KINDS, "table", table) : NULL;
  if (known) {
    st->out->kinds_mapped++;
    if (!source_remember(st, metadata, guid, "subconto_kinds", known->id)) snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("Cannot record the source of an imported record."));
    return;
  }
  db_ext_subconto_kind_t k = { .id = next_ext_id(st->ib, TABLE_EXT_SUBCONTO_KINDS) };
  copy_text(k.code, sizeof(k.code), code);
  copy_text(k.name, sizeof(k.name), name);
  snprintf(k.table, sizeof(k.table), "analytic_items");
  if (!send_db_message(st->ib, dbInsert, TABLE_EXT_SUBCONTO_KINDS, &k) || !source_remember(st, metadata, guid, "ext_subconto_kinds", k.id)) {
    snprintf(st->out->error, sizeof(st->out->error), "%s", ui_tr("Cannot store an imported analytics kind."));
    return;
  }
  st->out->kinds_added++;
}

static int ledger_kind_for(stage_t *st, const char *guid) {
  db_import_source_t *s = source_find(st, guid);
  return s ? s->record_id : 0;
}

// ── Accounts ─────────────────────────────────────────────────────────────────
static const char *ledger_account_kind(const char *kind) {
  return !strcmp(kind, "active") ? "А" : !strcmp(kind, "passive") ? "П" : "АП";
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
    return;
  }
  db_ext_account_t a = { .id = next_ext_id(st->ib, TABLE_EXT_ACCOUNTS), .currency = !strcmp(currency, "true") };
  copy_text(a.code, sizeof(a.code), code);
  copy_text(a.name, sizeof(a.name), name);
  copy_text(a.kind, sizeof(a.kind), ledger_account_kind(kind));
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
  for (xmlNodePtr r = kinds ? kinds->children : NULL; r && !out->error[0]; r = r->next) if (is_el(r, "record")) import_kind(&st, r);
  for (xmlNodePtr r = chart ? chart->children : NULL; r && !out->error[0]; r = r->next) if (is_el(r, "account")) import_account(&st, r);
  if (!out->error[0] && !send_db_message(ib, dbCommitChanges, 0, NULL))
    snprintf(out->error, sizeof(out->error), "%s", ui_tr("Cannot commit the import transaction."));
  if (out->error[0]) {
    send_db_message(ib, dbRevertChanges, 0, NULL);
    char error[sizeof(out->error)];
    snprintf(error, sizeof(error), "%s", out->error);
    memset(out, 0, sizeof(*out));
    snprintf(out->error, sizeof(out->error), "%s", error);
  } else if (!ledger_apply_extensions(ib, cfg)) {
    snprintf(out->error, sizeof(out->error), "%s", ui_tr("The imported accounts could not join the configuration."));
  }
  xmlFreeDoc(xml);
  return !out->error[0];
}

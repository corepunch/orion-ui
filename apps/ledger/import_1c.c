// import_1c.c — reads a 1C:Enterprise 8 exchange file in the EnterpriseData
// format (Администрирование → Обмен данными → Выгрузка в файл) into the
// infobase: counterparties, items, employees, fixed assets and currencies,
// and the opening balances of its Остатки... documents (ОстаткиСобственныхТМЦ,
// ОстаткиРасчетовСПоставщикамиДолги, ...) as one opening operation against
// the configuration's opening account (000). The documents name what they
// hold, not an account: the configuration's balance rules give the account.
//
// Objects are recognized by their element names and properties are read by
// name, inside КлючевыеСвойства or on the object, so the format's versions all
// read. A reference inside a document carries the referenced object's key
// properties, so it is imported like the catalog object it names. Records
// already in the infobase are matched by tax ID, code or name instead of
// being added again. The import is one editing context: a file that fails
// leaves the infobase as it was.

#include "ledger.h"
#include <libxml/parser.h>
#include <libxml/tree.h>

typedef struct { char ref[48]; int table, id; } ed_ref_t;

typedef struct {
  database_t *ib, *cfg;
  ledger_import_t *out;
  ed_ref_t *refs;
  int nrefs, cap;
  int opening_doc;     // the opening operation, created by the first balance row
} ed_import_t;

// ── XML ──────────────────────────────────────────────────────────────────────

static bool ed_is(xmlNodePtr n, const char *name) {
  return n && n->type == XML_ELEMENT_NODE && !strcmp((const char *)n->name, name);
}

static xmlNodePtr ed_child(xmlNodePtr n, const char *name) {
  for (xmlNodePtr c = n ? n->children : NULL; c; c = c->next) if (ed_is(c, name)) return c;
  return NULL;
}

static xmlNodePtr ed_descendant(xmlNodePtr n, const char *name) {
  for (xmlNodePtr c = n ? n->children : NULL; c; c = c->next) {
    if (c->type != XML_ELEMENT_NODE) continue;
    if (ed_is(c, name)) return c;
    xmlNodePtr d = ed_descendant(c, name);
    if (d) return d;
  }
  return NULL;
}

static bool ed_has_elements(xmlNodePtr n) {
  for (xmlNodePtr c = n ? n->children : NULL; c; c = c->next) if (c->type == XML_ELEMENT_NODE) return true;
  return false;
}

static void ed_text(xmlNodePtr n, char *buf, size_t sz) {
  buf[0] = '\0';
  xmlChar *v = n ? xmlNodeGetContent(n) : NULL;
  if (!v) return;
  const char *s = (const char *)v;
  while (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') s++;
  snprintf(buf, sz, "%s", s);
  for (size_t len = strlen(buf); len && strchr(" \n\r\t", buf[len - 1]); len--) buf[len - 1] = '\0';
  xmlFree(v);
}

// A part of an object: in its КлючевыеСвойства or on the object itself.
static xmlNodePtr ed_part(xmlNodePtr obj, const char *name) {
  xmlNodePtr n = ed_child(ed_child(obj, "КлючевыеСвойства"), name);
  return n ? n : ed_child(obj, name);
}

static bool ed_prop(xmlNodePtr obj, const char *name, char *buf, size_t sz) {
  xmlNodePtr n = ed_part(obj, name);
  ed_text(n && !ed_has_elements(n) ? n : NULL, buf, sz);
  return buf[0] != '\0';
}

static bool ed_number(xmlNodePtr n, const char *name, double *value) {
  char buf[64];
  ed_text(ed_child(n, name), buf, sizeof(buf));
  if (!buf[0]) return false;
  char clean[64], *o = clean;
  for (const char *p = buf; *p && o < clean + sizeof(clean) - 1; p++)
    if (*p == ',') *o++ = '.'; else if (*p != ' ') *o++ = *p;
  *o = '\0';
  *value = atof(clean);
  return true;
}

// ── Infobase ─────────────────────────────────────────────────────────────────

static void *ed_find(database_t *db, int table, const char *field, const char *value) {
  return value && value[0] ? (void *)send_db_message(db, dbFind, MAKEDWORD(table, ledger_field(db, table, field)), (void *)value) : NULL;
}

static void *ed_first(database_t *db, int table) {
  result_node_t *rows = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(table, 0), NULL);
  void *record = rows ? *(void **)rows->data : NULL;
  free_result_list(rows);
  return record;
}

// Every record struct starts with its integer key.
static int ed_insert(ed_import_t *im, int table, void *record) {
  const void *row = (const void *)send_db_message(im->ib, dbInsert, (uint32_t)table, record);
  return row ? *(const int *)row : 0;
}

static int ed_ref(ed_import_t *im, int table, const char *ref) {
  for (int i = 0; ref[0] && i < im->nrefs; i++)
    if (im->refs[i].table == table && !strcmp(im->refs[i].ref, ref)) return im->refs[i].id;
  return 0;
}

static int ed_remember(ed_import_t *im, int table, const char *ref, int id) {
  if (!ref[0] || !id) return id;
  if (im->nrefs == im->cap) {
    int cap = im->cap ? im->cap * 2 : 64;
    ed_ref_t *grown = realloc(im->refs, (size_t)cap * sizeof(*grown));
    if (!grown) return id;  // the reference is then matched by name again
    im->refs = grown; im->cap = cap;
  }
  im->refs[im->nrefs++] = (ed_ref_t){ .table = table, .id = id };
  snprintf(im->refs[im->nrefs - 1].ref, sizeof(im->refs[0].ref), "%s", ref);
  return id;
}

// Contact information rows name their kind (ЮридическийАдрес, Телефон) and
// carry the text as Представление or Значение.
static void ed_contact(xmlNodePtr n, const char *kind, char *buf, size_t sz) {
  for (xmlNodePtr c = n ? n->children : NULL; c && !buf[0]; c = c->next) {
    if (c->type != XML_ELEMENT_NODE) continue;
    char k[128];
    ed_text(ed_child(c, "ВидКонтактнойИнформации"), k, sizeof(k));
    if (k[0] && strstr(k, kind)) {
      ed_text(ed_child(c, "Представление"), buf, sz);
      if (!buf[0]) ed_text(ed_child(c, "Значение"), buf, sz);
    }
    if (!buf[0]) ed_contact(c, kind, buf, sz);
  }
}

// ── Catalogs ─────────────────────────────────────────────────────────────────

static int ed_counterparty(ed_import_t *im, xmlNodePtr obj) {
  char ref[48] = "", name[160], tax_id[16] = "", kpp[16] = "";
  ed_prop(obj, "Ссылка", ref, sizeof(ref));
  int id = ed_ref(im, TABLE_COUNTERPARTIES, ref);
  if (id) return id;
  if (!ed_prop(obj, "Наименование", name, sizeof(name)) && !ed_prop(obj, "НаименованиеПолное", name, sizeof(name))) return 0;
  ed_prop(obj, "ИНН", tax_id, sizeof(tax_id));
  ed_prop(obj, "КПП", kpp, sizeof(kpp));
  db_counterparty_t *old = ed_find(im->ib, TABLE_COUNTERPARTIES, "tax_id", tax_id);
  if (!old) old = ed_find(im->ib, TABLE_COUNTERPARTIES, "name", name);
  if (old) { im->out->matched++; return ed_remember(im, TABLE_COUNTERPARTIES, ref, old->id); }
  db_counterparty_t c = {0};
  send_db_message(im->ib, dbInitRecord, TABLE_COUNTERPARTIES, &c);
  snprintf(c.name, sizeof(c.name), "%s", name);
  snprintf(c.tax_id, sizeof(c.tax_id), "%s", tax_id);
  snprintf(c.kpp, sizeof(c.kpp), "%s", kpp);
  ed_contact(obj, "Адрес", c.address, sizeof(c.address));
  ed_contact(obj, "Телефон", c.phone, sizeof(c.phone));
  if ((id = ed_insert(im, TABLE_COUNTERPARTIES, &c))) im->out->counterparties++;
  return ed_remember(im, TABLE_COUNTERPARTIES, ref, id);
}

// БезНДС, НДС0, НДС10, НДС20 or "20%", given as the value or as its
// ВидСтавки: the configuration's rate of that value.
static int ed_vat_rate(database_t *cfg, xmlNodePtr n) {
  char text[32];
  ed_text(ed_has_elements(n) ? ed_child(n, "ВидСтавки") : n, text, sizeof(text));
  if (!text[0]) return 0;
  bool exempt = strstr(text, "Без") != NULL;
  int value = 0;
  for (const char *p = text; *p; p++) if (*p >= '0' && *p <= '9') value = value * 10 + (*p - '0');
  int id = 0;
  result_node_t *rates = (result_node_t *)send_db_message(cfg, dbFetch, MAKEDWORD(TABLE_VAT_RATES, 0), NULL);
  for (result_node_t *n = rates; n && !id; n = n->next) {
    db_vat_rate_t *r = *(db_vat_rate_t **)n->data;
    bool rate_exempt = strstr(r->name, "Без") != NULL;
    if (exempt ? rate_exempt : (!rate_exempt && (int)(r->rate + 0.5) == value)) id = r->id;
  }
  free_result_list(rates);
  return id;
}

// The unit and VAT rate are the item's, or those of the balance row naming it
// (ДанныеНоменклатуры/Номенклатура).
static int ed_item(ed_import_t *im, xmlNodePtr obj) {
  char ref[48] = "", name[160], sku[16] = "", unit[32] = "", unit_code[8] = "";
  ed_prop(obj, "Ссылка", ref, sizeof(ref));
  int id = ed_ref(im, TABLE_ITEMS, ref);
  if (id) return id;
  if (!ed_prop(obj, "Наименование", name, sizeof(name)) && !ed_prop(obj, "НаименованиеПолное", name, sizeof(name))) return 0;
  if (!ed_prop(obj, "КодВПрограмме", sku, sizeof(sku)) && !ed_prop(obj, "Артикул", sku, sizeof(sku))) ed_prop(obj, "Код", sku, sizeof(sku));
  db_item_t *old = ed_find(im->ib, TABLE_ITEMS, "sku", sku);
  if (!old) old = ed_find(im->ib, TABLE_ITEMS, "name", name);
  if (old) { im->out->matched++; return ed_remember(im, TABLE_ITEMS, ref, old->id); }
  db_item_t it = {0};
  send_db_message(im->ib, dbInitRecord, TABLE_ITEMS, &it);
  snprintf(it.name, sizeof(it.name), "%s", name);
  snprintf(it.sku, sizeof(it.sku), "%s", sku);
  xmlNodePtr row = ed_is(obj->parent, "ДанныеНоменклатуры") ? obj->parent->parent : NULL;
  xmlNodePtr u = ed_part(obj, "ЕдиницаИзмерения"), vat = ed_part(obj, "СтавкаНДС");
  if (!u) u = ed_child(row, "ЕдиницаИзмерения");
  if (!vat) vat = ed_child(row, "СтавкаНДС");
  xmlNodePtr classifier = ed_child(u, "ДанныеКлассификатора");  // its code (ОКЕИ) and short name
  ed_prop(classifier ? classifier : u, "Код", unit_code, sizeof(unit_code));
  ed_prop(classifier ? classifier : u, "Наименование", unit, sizeof(unit));
  db_unit_t *un = ed_find(im->cfg, TABLE_UNITS, "code", unit_code);
  if (!un) un = ed_find(im->cfg, TABLE_UNITS, "name", unit);
  it.unit_id = un ? un->id : 0;
  if (vat) it.vat_rate_id = ed_vat_rate(im->cfg, vat);
  if ((id = ed_insert(im, TABLE_ITEMS, &it))) im->out->items++;
  return ed_remember(im, TABLE_ITEMS, ref, id);
}

// ФизическиеЛица and Сотрудники become employees; the expense account follows
// the employees already kept.
static int ed_employee(ed_import_t *im, xmlNodePtr obj) {
  char ref[48] = "", name[128] = "", tax_id[16] = "";
  ed_prop(obj, "Ссылка", ref, sizeof(ref));
  int id = ed_ref(im, TABLE_EMPLOYEES, ref);
  if (id) return id;
  if (!ed_prop(obj, "Наименование", name, sizeof(name))) {
    char last[40] = "", first[40] = "", middle[40] = "";
    xmlNodePtr fio = ed_part(obj, "ФИО");
    if (!fio) fio = obj;
    ed_prop(fio, "Фамилия", last, sizeof(last)); ed_prop(fio, "Имя", first, sizeof(first)); ed_prop(fio, "Отчество", middle, sizeof(middle));
    snprintf(name, sizeof(name), "%s%s%s%s%s", last, first[0] ? " " : "", first, middle[0] ? " " : "", middle);
  }
  if (!name[0]) return 0;
  ed_prop(obj, "ИНН", tax_id, sizeof(tax_id));
  db_employee_t *old = ed_find(im->ib, TABLE_EMPLOYEES, "tax_id", tax_id);
  if (!old) old = ed_find(im->ib, TABLE_EMPLOYEES, "name", name);
  if (old) { im->out->matched++; return ed_remember(im, TABLE_EMPLOYEES, ref, old->id); }
  db_employee_t e = {0}, *like = ed_first(im->ib, TABLE_EMPLOYEES);
  send_db_message(im->ib, dbInitRecord, TABLE_EMPLOYEES, &e);
  e.account_id = like ? like->account_id : 0;
  snprintf(e.name, sizeof(e.name), "%s", name);
  snprintf(e.tax_id, sizeof(e.tax_id), "%s", tax_id);
  if ((id = ed_insert(im, TABLE_EMPLOYEES, &e))) im->out->employees++;
  return ed_remember(im, TABLE_EMPLOYEES, ref, id);
}

// A fixed asset takes the accounts and useful life of the assets already kept.
static int ed_asset(ed_import_t *im, xmlNodePtr obj) {
  char ref[48] = "", name[160], inventory[16] = "";
  ed_prop(obj, "Ссылка", ref, sizeof(ref));
  int id = ed_ref(im, TABLE_ASSETS, ref);
  if (id) return id;
  if (!ed_prop(obj, "Наименование", name, sizeof(name)) && !ed_prop(obj, "НаименованиеПолное", name, sizeof(name))) return 0;
  if (!ed_prop(obj, "ИнвентарныйНомер", inventory, sizeof(inventory))) ed_prop(obj, "КодВПрограмме", inventory, sizeof(inventory));
  db_asset_t *old = ed_find(im->ib, TABLE_ASSETS, "inventory", inventory);
  if (!old) old = ed_find(im->ib, TABLE_ASSETS, "name", name);
  if (old) { im->out->matched++; return ed_remember(im, TABLE_ASSETS, ref, old->id); }
  db_asset_t a = {0}, *like = ed_first(im->ib, TABLE_ASSETS);
  send_db_message(im->ib, dbInitRecord, TABLE_ASSETS, &a);
  if (like) { a.account_id = like->account_id; a.depreciation_id = like->depreciation_id; a.expense_id = like->expense_id; a.life = like->life; }
  snprintf(a.name, sizeof(a.name), "%s", name);
  snprintf(a.inventory, sizeof(a.inventory), "%s", inventory);
  if ((id = ed_insert(im, TABLE_ASSETS, &a))) im->out->assets++;
  return ed_remember(im, TABLE_ASSETS, ref, id);
}

static bool ed_alpha_code(const char *s) {
  return strlen(s) == 3 && s[0] >= 'A' && s[0] <= 'Z' && s[1] >= 'A' && s[1] <= 'Z' && s[2] >= 'A' && s[2] <= 'Z';
}

// Currencies are kept by their ISO letter code; the national one is not a
// catalog entry (documents in it have no currency).
static int ed_currency(ed_import_t *im, xmlNodePtr obj) {
  char ref[48] = "", code[16] = "", name[64] = "";
  ed_prop(obj, "Ссылка", ref, sizeof(ref));
  int id = ed_ref(im, TABLE_CURRENCIES, ref);
  if (id) return id;
  xmlNodePtr classifier = ed_descendant(obj, "ДанныеКлассификатора");
  if (!ed_prop(obj, "БуквенныйКод", code, sizeof(code)) || !ed_alpha_code(code)) ed_prop(classifier, "Наименование", code, sizeof(code));
  if (!ed_alpha_code(code)) ed_prop(obj, "Наименование", code, sizeof(code));
  if (!ed_alpha_code(code)) return 0;
  const db_setting_t *s = (const db_setting_t *)send_db_message(im->cfg, dbFind, MAKEDWORD(TABLE_SETTINGS, 0), (void *)(intptr_t)1);
  if (s && (!strcmp(s->currency_code, code) || (!strcmp(code, "RUR") && !strcmp(s->currency_code, "RUB")))) return 0;
  db_currency_t *old = ed_find(im->ib, TABLE_CURRENCIES, "code", code);
  if (old) { im->out->matched++; return ed_remember(im, TABLE_CURRENCIES, ref, old->id); }
  db_currency_t c = {0};
  send_db_message(im->ib, dbInitRecord, TABLE_CURRENCIES, &c);
  snprintf(c.code, sizeof(c.code), "%.3s", code);
  if (!ed_prop(obj, "НаименованиеПолное", name, sizeof(name))) snprintf(name, sizeof(name), "%s", code);
  snprintf(c.name, sizeof(c.name), "%s", name);
  if ((id = ed_insert(im, TABLE_CURRENCIES, &c))) im->out->currencies++;
  return ed_remember(im, TABLE_CURRENCIES, ref, id);
}

static const struct { const char *element; int (*import)(ed_import_t *, xmlNodePtr); } kCatalogs[] = {
  { "Справочник.Контрагенты",      ed_counterparty }, { "Справочник.Номенклатура", ed_item },
  { "Справочник.ФизическиеЛица",   ed_employee },     { "Справочник.Сотрудники",   ed_employee },
  { "Справочник.ОсновныеСредства", ed_asset },        { "Справочник.Валюты",       ed_currency },
};

// ── Opening balances ─────────────────────────────────────────────────────────

// The analytics object a balance row names for the account's analytics kind:
// on the row, or inside its ДанныеНоменклатуры / ДанныеВзаиморасчетов.
static int ed_subconto(ed_import_t *im, xmlNodePtr row, const char *table) {
  static const struct { const char *table, *element; int (*import)(ed_import_t *, xmlNodePtr); } kinds[] = {
    { "counterparties", "Контрагент",       ed_counterparty }, { "items",     "Номенклатура",   ed_item },
    { "employees",      "ПодотчетноеЛицо",  ed_employee },     { "employees", "ФизическоеЛицо", ed_employee },
    { "employees",      "Сотрудник",        ed_employee },     { "assets",    "ОсновноеСредство", ed_asset },
  };
  for (size_t i = 0; i < ARRAY_LEN(kinds); i++) {
    xmlNodePtr ref = strcmp(kinds[i].table, table) ? NULL : ed_descendant(row, kinds[i].element);
    if (ref) return kinds[i].import(im, ref);
  }
  return 0;
}

static void ed_skip(ed_import_t *im, const char *what) {
  char *list = im->out->skipped;
  size_t len = strlen(list), n = strlen(what);
  for (const char *p = list; (p = strstr(p, what)); p += n)
    if ((p == list || p[-1] == ' ') && (p[n] == ',' || !p[n])) return;
  if (len + n + 3 < sizeof(im->out->skipped)) snprintf(list + len, sizeof(im->out->skipped) - len, "%s%s", len ? ", " : "", what);
}

// One opening operation holds every balance of the file. Balances of a date
// already imported from 1C are not entered twice.
static int ed_opening_document(ed_import_t *im, xmlNodePtr doc) {
  if (im->opening_doc || im->out->balances_present) return im->opening_doc;
  const db_doc_type_t *type = ed_find(im->cfg, TABLE_DOC_TYPES, "code", "opening");
  if (!type) { snprintf(im->out->error, sizeof(im->out->error), "%s", ui_tr("The configuration has no opening balances document type.")); return 0; }
  char date[32] = "";
  ed_prop(doc, "Дата", date, sizeof(date));
  db_document_t d = {0};
  send_db_message(im->ib, dbInitRecord, TABLE_DOCUMENTS, &d);
  d.doc_type_id = type->id;
  if (date[0]) snprintf(d.date, sizeof(d.date), "%.10s", date);
  snprintf(d.comment, sizeof(d.comment), "%s", ui_tr("Opening balances imported from 1C"));
  result_node_t *docs = (result_node_t *)send_db_message(im->ib, dbFetch, MAKEDWORD(TABLE_DOCUMENTS, 0), NULL);
  for (result_node_t *n = docs; n; n = n->next) {
    const db_document_t *old = *(db_document_t **)n->data;
    if (old->doc_type_id == d.doc_type_id && !strcmp(old->date, d.date) && !strcmp(old->comment, d.comment)) im->out->balances_present = true;
  }
  free_result_list(docs);
  return im->out->balances_present ? 0 : (im->opening_doc = ed_insert(im, TABLE_DOCUMENTS, &d));
}

static const db_account_t *ed_account(ed_import_t *im, int id) {
  return id ? (const db_account_t *)send_db_message(im->cfg, dbFind, MAKEDWORD(TABLE_ACCOUNTS, 0), (void *)(intptr_t)id) : NULL;
}

// A row's Сумма is in the settlement currency and СуммаРегл in the national
// one; a row of goods may give only their Количество.
static void ed_balance_row(ed_import_t *im, xmlNodePtr doc, xmlNodePtr row, const db_balance_rule_t *rule, int opening_id) {
  double amount = 0, national = 0, quantity = 0;
  bool has_amount = ed_number(row, "Сумма", &amount), has_national = ed_number(row, "СуммаРегл", &national);
  ed_number(row, "Количество", &quantity);
  if (!has_national) national = amount;
  if (national == 0 && quantity == 0) return;
  xmlNodePtr currency = ed_descendant(row, "ВалютаВзаиморасчетов");
  if (!currency) currency = ed_descendant(row, "Валюта");
  int currency_id = currency && has_amount ? ed_currency(im, currency) : 0;
  const db_account_t *account = ed_account(im, currency_id && rule->currency_account_id ? rule->currency_account_id : rule->account_id);
  if (!account) return;
  int doc_id = ed_opening_document(im, doc);
  if (!doc_id) return;
  bool credit = rule->credit;
  if (national < 0) { national = -national; amount = -amount; credit = !credit; }
  db_entry_t e = {0};
  send_db_message(im->ib, dbInitRecord, TABLE_ENTRIES, &e);
  const db_document_t *d = (const db_document_t *)send_db_message(im->ib, dbFind, MAKEDWORD(TABLE_DOCUMENTS, 0), (void *)(intptr_t)doc_id);
  e.document_id = doc_id;
  snprintf(e.date, sizeof(e.date), "%s", d ? d->date : "");
  e.debit_id = credit ? opening_id : account->id;
  e.credit_id = credit ? account->id : opening_id;
  int subconto = ed_subconto(im, row, ledger_subconto_table(im->cfg, account->id));
  if (credit) e.credit_subconto_id = subconto; else e.debit_subconto_id = subconto;
  e.quantity = quantity;
  e.amount = national;
  if (currency_id && account->currency) { e.currency_id = currency_id; e.currency_amount = amount; }
  snprintf(e.content, sizeof(e.content), "%s", account->name);
  if (!ed_insert(im, TABLE_ENTRIES, &e)) return;
  im->out->entries++;
  if (credit) im->out->credit += national; else im->out->debit += national;
}

// The rows of an Остатки... document are the Строка elements of its tables
// (ТМЦ, Расчеты, ДенежныеСредства, ...). A document the configuration has no
// rule for is named in the summary.
static void ed_balances(ed_import_t *im, xmlNodePtr doc) {
  const char *kind = (const char *)doc->name + strlen("Документ.");
  const db_balance_rule_t *rule = ed_find(im->cfg, TABLE_BALANCE_RULES, "document", kind);
  const db_setting_t *s = (const db_setting_t *)send_db_message(im->cfg, dbFind, MAKEDWORD(TABLE_SETTINGS, 0), (void *)(intptr_t)1);
  if (!rule || !s || !s->opening_account_id) { ed_skip(im, kind); return; }
  for (xmlNodePtr t = doc->children; t; t = t->next)
    for (xmlNodePtr row = ed_child(t, "Строка"); row; row = row->next)
      if (ed_is(row, "Строка")) ed_balance_row(im, doc, row, rule, s->opening_account_id);
}

// ── Entry point ──────────────────────────────────────────────────────────────

bool ledger_import_enterprisedata(database_t *ib, database_t *cfg, const char *path, ledger_import_t *out) {
  memset(out, 0, sizeof(*out));
  FILE *f = path ? fopen(path, "rb") : NULL;
  if (!f) {
    snprintf(out->error, sizeof(out->error), ui_tr("Cannot open the file %s."), path ? path : "");
    return false;
  }
  fclose(f);
  xmlDocPtr xml = xmlReadFile(path, NULL, XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_HUGE);
  xmlNodePtr root = xml ? xmlDocGetRootElement(xml) : NULL, body = ed_child(root, "Body");
  char format[160] = "";
  ed_text(ed_descendant(root, "Format"), format, sizeof(format));
  if (!body || !strstr(format, "EnterpriseData")) {
    snprintf(out->error, sizeof(out->error), "%s", ui_tr("The file is not a 1C exchange file in the EnterpriseData format."));
    if (xml) xmlFreeDoc(xml);
    return false;
  }
  ed_import_t im = { .ib = ib, .cfg = cfg, .out = out };
  bool scoped = send_db_message(ib, dbBeginChanges, 0, NULL) != 0;
  for (xmlNodePtr obj = body->children; obj && !out->error[0]; obj = obj->next) {
    if (obj->type != XML_ELEMENT_NODE) continue;
    const char *name = (const char *)obj->name;
    bool known = false;
    for (size_t i = 0; i < ARRAY_LEN(kCatalogs); i++)
      if (!strcmp(name, kCatalogs[i].element)) { kCatalogs[i].import(&im, obj); known = true; }
    if (!strncmp(name, "Документ.Остатки", strlen("Документ.Остатки"))) { ed_balances(&im, obj); known = true; }
    if (!known) out->ignored++;
  }
  if (!out->error[0] && im.opening_doc) ledger_post(ib, cfg, im.opening_doc);
  if (scoped) send_db_message(ib, out->error[0] ? dbRevertChanges : dbCommitChanges, 0, NULL);
  free(im.refs);
  xmlFreeDoc(xml);
  return !out->error[0];
}

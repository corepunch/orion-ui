// ledger_ib — the infobase datasource: db_schema_xml with Ledger's business
// rules, the way EOF enterprise objects add behavior to stored data. It
// subclasses the schema class like a WinAPI window subclass: whatever it does
// not handle goes to db_schema_xml.
//
//   documents   numbered by their type's prefix when inserted; recalculated
//               (lines, tax, totals, rate) whenever they are saved
//   lines       doc_lines, payroll_lines, asset_lines and a manual
//               operation's entries recalculate their document on any change
//
// Defaults, cascades and refusals are declared in ledger.orion (default=,
// on-delete=) and applied by db_schema_xml.

#include "ledger.h"

static int g_recalculating;  // the rules write through this proc too

static database_t *cfg(void) { return get_database_by_name("cfg"); }

static int document_of(database_t *db, uint32_t table_id, const void *record) {
  const db_table_schema_t *t = db_schema_table(db->schema, (int)table_id);
  int field = db_schema_field_index(t, "document_id");  // tables without one belong to no document
  const db_field_meta_t *meta = field > 0 ? &t->meta[field] : NULL;
  return meta && record ? *(const int *)((const char *)record + meta->offset) : 0;
}

// A manual operation's entries are its lines; posted documents' entries are
// written by posting and change nothing.
static bool is_line_table(database_t *db, uint32_t table_id, int doc_id) {
  if (table_id == TABLE_DOC_LINES || table_id == TABLE_PAYROLL_LINES || table_id == TABLE_ASSET_LINES) return true;
  if (table_id != TABLE_ENTRIES || !doc_id) return false;
  const db_document_t *doc = (const db_document_t *)db_schema_xml(db, dbFind, MAKEDWORD(TABLE_DOCUMENTS, 0), (void *)(intptr_t)doc_id);
  const db_doc_type_t *type = doc && cfg() ? (const db_doc_type_t *)send_db_message(cfg(), dbFind,
    MAKEDWORD(TABLE_DOC_TYPES, 0), (void *)(intptr_t)doc->doc_type_id) : NULL;
  return type && !strcmp(type->form, "operation");
}

static bool recalculate(database_t *db, int doc_id, bool number) {
  if (!doc_id || !cfg()) return true;
  g_recalculating++;
  ledger_recalc_document(db, cfg(), doc_id);
  bool ok = !number || ledger_number_document(db, cfg(), doc_id);
  g_recalculating--;
  return ok;
}

lresult_t ledger_ib(database_t *db, uint32_t msg, uint32_t wparam, void *lparam) {
  if (g_recalculating || ledger_script_in_executor() || (msg != dbInsert && msg != dbUpdate && msg != dbDelete))
    return db_schema_xml(db, msg, wparam, lparam);
  uint32_t table_id = LOWORD(wparam);
  int doc_id = 0;
  if (msg == dbDelete && table_id != TABLE_DOCUMENTS) {
    const void *record = (const void *)db_schema_xml(db, dbFind, MAKEDWORD(table_id, 0), lparam);
    doc_id = document_of(db, table_id, record);
  }
  const db_document_t *input = table_id == TABLE_DOCUMENTS && msg != dbDelete ? lparam : NULL;
  bool scope = input && !input->number[0];
  if (scope && !db_schema_xml(db, dbBeginChanges, 0, NULL)) return 0;
  lresult_t result = db_schema_xml(db, msg, wparam, lparam);
  if (!result) {
    if (scope) db_schema_xml(db, dbRevertChanges, 0, NULL);
    return 0;
  }
  if (table_id == TABLE_DOCUMENTS && msg != dbDelete) {
    const db_document_t *doc = msg == dbInsert ? (const db_document_t *)result : (const db_document_t *)lparam;
    if (!recalculate(db, doc->id, true)) {
      if (scope) db_schema_xml(db, dbRevertChanges, 0, NULL);
      return 0;
    }
  } else if (table_id != TABLE_DOCUMENTS) {
    if (msg != dbDelete) doc_id = document_of(db, table_id, msg == dbInsert ? (const void *)result : lparam);
    if (is_line_table(db, table_id, doc_id)) recalculate(db, doc_id, false);
  }
  if (scope && !db_schema_xml(db, dbCommitChanges, 0, NULL)) {
    db_schema_xml(db, dbRevertChanges, 0, NULL);
    return 0;
  }
  return result;
}

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
#include <time.h>
#include <math.h>
#include <orion/user/money.h>

static int g_recalculating;  // the rules write through this proc too

static database_t *cfg(void) { return get_database_by_name("cfg"); }

static bool exact_trade_type(int id) {
  const db_setting_t *settings = cfg() ? (const void *)send_db_message(cfg(), dbFind, TABLE_SETTINGS, (void *)(intptr_t)1) : NULL;
  if (!settings || (strcmp(settings->code, "ru-2026") && strcmp(settings->code, "de-2026"))) return false;
  const db_doc_type_t *type = cfg() ? (const void *)send_db_message(cfg(), dbFind, TABLE_DOC_TYPES, (void *)(intptr_t)id) : NULL;
  return type && !strcmp(type->form, "document") && (!strcmp(type->code, "receipt") || !strcmp(type->code, "sale") ||
    !strcmp(type->code, "incoming_invoice") || !strcmp(type->code, "outgoing_invoice"));
}

static bool exact_line_inputs(db_doc_line_t *line) {
  if (!isfinite(line->quantity) || !isfinite(line->price)) return false;
  char q[64], p[64];
  snprintf(q, sizeof(q), "%.3f", line->quantity); snprintf(p, sizeof(p), "%.2f", line->price);
  int64_t quantity, price;
  if (!money_parse_decimal(q, 3, &quantity) || !money_parse_decimal(p, 2, &price) ||
      quantity < 0 || quantity > 10000000 || price < 0 || price > 10000000 ||
      fabs(line->quantity - (double)quantity / 1000.0) > 1e-9 ||
      fabs(line->price - (double)price / 100.0) > 1e-9) return false;
  line->quantity_milli = quantity; line->price_minor = price;
  return true;
}

bool ledger_valid_date(const char *date) {
  int y, m, d; char tail;
  if (!date || strlen(date) != 10 || sscanf(date, "%4d-%2d-%2d%c", &y, &m, &d, &tail) != 3 ||
      y < 1 || m < 1 || m > 12 || d < 1 || date[4] != '-' || date[7] != '-') return false;
  for (int i = 0; i < 10; i++) if (i != 4 && i != 7 && (date[i] < '0' || date[i] > '9')) return false;
  static const int days[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
  return d <= days[m - 1] + (m == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)));
}

bool ledger_period_boundary(database_t *db, char *boundary, size_t size) {
  boundary[0] = '\0';
  if (!db) {
    fprintf(stderr, "[ledger] period lock rejected: missing database\n"); fflush(stderr); return false;
  }
  result_node_t *rows = (void *)db_schema_xml(db, dbFetch, MAKEDWORD(TABLE_PERIOD_LOCKS, 0), NULL);
  bool ok = rows || !db->error[0];
  for (result_node_t *n = rows; n; n = n->next) {
    const db_period_lock_t *lock = *(db_period_lock_t **)n->data;
    if (!lock || lock->id != 1 || boundary[0] || !ledger_valid_date(lock->closed_through)) {
      fprintf(stderr, "[ledger] period lock rejected: invalid or duplicate boundary id=%d date=%s\n",
        lock ? lock->id : 0, lock ? lock->closed_through : "(null)");
      fflush(stderr); ok = false; break;
    }
    snprintf(boundary, size, "%s", lock->closed_through);
  }
  free_result_list(rows);
  return ok;
}

bool ledger_period_closed(database_t *db, const char *date) {
  char boundary[11];
  if (!ledger_period_boundary(db, boundary, sizeof(boundary))) return true;
  return boundary[0] && (!ledger_valid_date(date) || strcmp(date, boundary) <= 0);
}

bool ledger_close_document_blocks(const db_document_t *doc, const char *date) {
  return !doc || !ledger_valid_date(doc->date) || (!doc->posted && strcmp(doc->date, date) <= 0);
}

bool ledger_close_through(database_t *db, const char *date) {
  if (!ledger_valid_date(date)) {
    fprintf(stderr, "[ledger] close rejected: invalid date=%s\n", date ? date : "(null)"); fflush(stderr); return false;
  }
  char boundary[11];
  if (!ledger_period_boundary(db, boundary, sizeof(boundary))) return false;
  if (boundary[0] && strcmp(date, boundary) <= 0) {
    fprintf(stderr, "[ledger] close rejected: date=%s current=%s\n", date, boundary); fflush(stderr); return false;
  }
  result_node_t *documents = (void *)db_schema_xml(db, dbFetch, MAKEDWORD(TABLE_DOCUMENTS, 0), NULL);
  if (!documents && db->error[0]) {
    fprintf(stderr, "[ledger] close rejected: cannot read documents through %s\n", date); fflush(stderr); return false;
  }
  for (result_node_t *node = documents; node; node = node->next) {
    const db_document_t *doc = *(db_document_t **)node->data;
    if (ledger_close_document_blocks(doc, date)) {
      fprintf(stderr, "[ledger] close rejected: document=%d date=%s posted=%d blocks close through %s\n",
        doc->id, doc->date, doc->posted, date);
      fflush(stderr); free_result_list(documents); return false;
    }
  }
  free_result_list(documents);
  db_period_lock_t lock = { .id = 1 };
  db_period_closure_t closure = {0};
  snprintf(lock.closed_through, sizeof(lock.closed_through), "%s", date);
  snprintf(closure.previous_through, sizeof(closure.previous_through), "%s", boundary);
  snprintf(closure.closed_through, sizeof(closure.closed_through), "%s", date);
  time_t now = time(NULL);
  const struct tm *utc = gmtime(&now);
  if (!utc || !strftime(closure.recorded_utc, sizeof(closure.recorded_utc), "%Y-%m-%dT%H:%M:%SZ", utc)) {
    fprintf(stderr, "[ledger] close rejected: cannot read clock for date=%s\n", date); fflush(stderr); return false;
  }
  if (db->access_enabled) snprintf(closure.actor, sizeof(closure.actor), "%s", db->access_actor);
  if (!db_schema_xml(db, dbBeginChanges, 0, NULL)) return false;
  bool ok = db_schema_xml(db, boundary[0] ? dbUpdate : dbInsert, TABLE_PERIOD_LOCKS, &lock) &&
            db_schema_xml(db, dbInsert, TABLE_PERIOD_CLOSURES, &closure);
  if (ok && db_schema_xml(db, dbCommitChanges, 0, NULL)) return true;
  db_schema_xml(db, dbRevertChanges, 0, NULL);
  fprintf(stderr, "[ledger] close rolled back: date=%s\n", date); fflush(stderr);
  return false;
}

static bool locked_document(database_t *db, int id) {
  const db_document_t *doc = (const db_document_t *)db_schema_xml(db, dbFind, MAKEDWORD(TABLE_DOCUMENTS, 0), (void *)(intptr_t)id);
  return doc && ledger_period_closed(db, doc->date);
}

bool ledger_record_revision(database_t *db, const db_document_t *doc, const char *action) {
  db_posting_revision_t revision = { .document_id = doc->id, .doc_type_id = doc->doc_type_id,
    .amount = doc->amount, .vat = doc->vat, .total = doc->total, .exact_money = doc->exact_money, .lines_captured = doc->exact_money,
    .amount_minor = doc->amount_minor, .vat_minor = doc->vat_minor, .total_minor = doc->total_minor };
  snprintf(revision.action, sizeof(revision.action), "%s", action);
  time_t now = time(NULL);
  const struct tm *utc = gmtime(&now);
  if (!utc || !strftime(revision.recorded_utc, sizeof(revision.recorded_utc), "%Y-%m-%dT%H:%M:%SZ", utc)) {
    fprintf(stderr, "[ledger] revision rejected: document=%d cannot read clock\n", doc->id); fflush(stderr); return false;
  }
  if (db->access_enabled) snprintf(revision.actor, sizeof(revision.actor), "%s", db->access_actor);
  snprintf(revision.number, sizeof(revision.number), "%s", doc->number);
  snprintf(revision.date, sizeof(revision.date), "%s", doc->date);
  const db_posting_revision_t *saved = (const db_posting_revision_t *)db_schema_xml(db, dbInsert, TABLE_POSTING_REVISIONS, &revision);
  if (!saved) {
    fprintf(stderr, "[ledger] revision rejected: document=%d action=%s\n", doc->id, action); fflush(stderr); return false;
  }
  int revision_id = saved->id;
  int field = db_schema_field_index(db_schema_table(db->schema, TABLE_ENTRIES), "document_id");
  result_node_t *rows = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(TABLE_ENTRIES, field), (void *)(intptr_t)doc->id);
  bool ok = rows || !db->error[0];
  for (result_node_t *node = rows; node; node = node->next) {
    const db_entry_t *e = *(db_entry_t **)node->data;
    db_revision_entry_t copy = { .revision_id = revision_id, .debit_id = e->debit_id, .credit_id = e->credit_id,
      .debit_subconto_id = e->debit_subconto_id, .credit_subconto_id = e->credit_subconto_id,
      .debit_subconto2_id = e->debit_subconto2_id, .debit_subconto3_id = e->debit_subconto3_id,
      .credit_subconto2_id = e->credit_subconto2_id, .credit_subconto3_id = e->credit_subconto3_id,
      .debit_tax_set = e->debit_tax_set, .debit_tax_minor = e->debit_tax_minor,
      .debit_permanent_minor = e->debit_permanent_minor, .debit_temporary_minor = e->debit_temporary_minor,
      .credit_tax_set = e->credit_tax_set, .credit_tax_minor = e->credit_tax_minor,
      .credit_permanent_minor = e->credit_permanent_minor, .credit_temporary_minor = e->credit_temporary_minor,
      .currency_id = e->currency_id, .quantity = e->quantity, .currency_amount = e->currency_amount, .amount = e->amount,
      .exact_money = e->exact_money, .amount_minor = e->amount_minor, .quantity_milli = e->quantity_milli };
    snprintf(copy.date, sizeof(copy.date), "%s", e->date);
    snprintf(copy.content, sizeof(copy.content), "%s", e->content);
    if (!db_schema_xml(db, dbInsert, TABLE_REVISION_ENTRIES, &copy)) { ok = false; break; }
  }
  free_result_list(rows);
  if (ok && revision.lines_captured) {
    field = db_schema_field_index(db_schema_table(db->schema, TABLE_DOC_LINES), "document_id");
    rows = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(TABLE_DOC_LINES, field), (void *)(intptr_t)doc->id);
    if (!rows && db->error[0]) ok = false;
    for (result_node_t *node = rows; ok && node; node = node->next) {
      const db_doc_line_t *line = *(db_doc_line_t **)node->data;
      db_revision_line_t copy = { .revision_id = revision_id, .source_line_id = line->id, .item_id = line->item_id,
        .vat_rate_id = line->vat_rate_id, .quantity = line->quantity, .price = line->price, .amount = line->amount,
        .vat = line->vat, .total = line->total, .quantity_milli = line->quantity_milli, .price_minor = line->price_minor,
        .amount_minor = line->amount_minor, .vat_minor = line->vat_minor, .total_minor = line->total_minor,
        .tracking_quantity = line->tracking_quantity, .tracking_value = line->tracking_value, .is_traceable = line->is_traceable };
      snprintf(copy.commodity_code, sizeof(copy.commodity_code), "%s", line->commodity_code);
      snprintf(copy.origin_code, sizeof(copy.origin_code), "%s", line->origin_code);
      snprintf(copy.origin_name, sizeof(copy.origin_name), "%s", line->origin_name);
      snprintf(copy.customs_reference, sizeof(copy.customs_reference), "%s", line->customs_reference);
      snprintf(copy.tracking_unit_code, sizeof(copy.tracking_unit_code), "%s", line->tracking_unit_code);
      snprintf(copy.tracking_unit_name, sizeof(copy.tracking_unit_name), "%s", line->tracking_unit_name);
      if (!db_schema_xml(db, dbInsert, TABLE_REVISION_LINES, &copy)) { ok = false; break; }
    }
    free_result_list(rows);
  }
  if (!ok) { fprintf(stderr, "[ledger] revision rejected: document=%d action=%s\n", doc->id, action); fflush(stderr); }
  return ok;
}

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
  const db_document_t *doc = (const void *)db_schema_xml(db, dbFind, TABLE_DOCUMENTS, (void *)(intptr_t)doc_id);
  bool ok = doc && (!doc->exact_money || (isfinite(doc->amount) && isfinite(doc->vat) && isfinite(doc->total))) &&
            (!number || ledger_number_document(db, cfg(), doc_id));
  g_recalculating--;
  return ok;
}

bool ledger_exact_posted(database_t *db, int id) {
  const db_document_t *doc = (const void *)db_schema_xml(db, dbFind, TABLE_DOCUMENTS, (void *)(intptr_t)id);
  if (!doc) return false;
  if (doc->exact_money && doc->posted) return true;
  int field = db_schema_field_index(db_schema_table(db->schema, TABLE_POSTING_REVISIONS), "document_id");
  result_node_t *rows = (void *)send_db_message(db, dbFetch, MAKEDWORD(TABLE_POSTING_REVISIONS, field), (void *)(intptr_t)id);
  bool posted = !rows && db->error[0];
  for (result_node_t *n = rows; n; n = n->next) {
    const db_posting_revision_t *revision = *(db_posting_revision_t **)n->data;
    if (revision->exact_money && !strcmp(revision->action, "post")) { posted = true; break; }
  }
  free_result_list(rows);
  return posted;
}

bool ledger_reversal_linked(database_t *db, int id) {
  if (!db || id <= 0) return false;
  const db_table_schema_t *table = db_schema_table(db->schema, TABLE_REVERSAL_LINKS);
  const char *fields[] = { "original_id", "reversal_id" };
  for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
    int field = db_schema_field_index(table, fields[i]);
    result_node_t *rows = (void *)send_db_message(db, dbFetch, MAKEDWORD(TABLE_REVERSAL_LINKS, field), (void *)(intptr_t)id);
    bool found = rows != NULL || db->error[0];
    free_result_list(rows);
    if (found) return true;
  }
  return false;
}

static bool document_id_has_history(database_t *db, int id) {
  int field = db_schema_field_index(db_schema_table(db->schema, TABLE_POSTING_REVISIONS), "document_id");
  result_node_t *rows = (void *)send_db_message(db, dbFetch, MAKEDWORD(TABLE_POSTING_REVISIONS, field), (void *)(intptr_t)id);
  bool found = rows != NULL || db->error[0];
  free_result_list(rows);
  return found;
}

static int reserve_document_id(database_t *db) {
  int id; bool history;
  do {
    id = (int)db_schema_xml(db, dbReserveKey, TABLE_DOCUMENTS, NULL);
    history = id > 0 && document_id_has_history(db, id);
    if (id > 0 && db->error[0]) { id = 0; break; }
  } while (id > 0 && history);
  if (id <= 0) { fprintf(stderr, "[ledger] document key reservation failed id=%d\n", id); fflush(stderr); }
  return id;
}

lresult_t ledger_ib(database_t *db, uint32_t msg, uint32_t wparam, void *lparam) {
  db_document_t reserved_doc;
  if (msg == dbReserveKey && LOWORD(wparam) == TABLE_DOCUMENTS) return reserve_document_id(db);
  if (msg == dbInsert && LOWORD(wparam) == TABLE_DOCUMENTS && lparam &&
      ((const db_document_t *)lparam)->id > 0 && document_id_has_history(db, ((const db_document_t *)lparam)->id)) {
    fprintf(stderr, "[ledger] document insert rejected: id=%d belongs to retained posting history\n", ((const db_document_t *)lparam)->id);
    fflush(stderr); return 0;
  }
  if (msg == dbInsert && LOWORD(wparam) == TABLE_DOCUMENTS && lparam &&
      ((const db_document_t *)lparam)->id <= 0 && !db->edit_parent) {
    reserved_doc = *(const db_document_t *)lparam;
    reserved_doc.id = reserve_document_id(db);
    if (reserved_doc.id <= 0) return 0;
    lparam = &reserved_doc;
  }

  if (msg == dbInsert || msg == dbUpdate || msg == dbDelete) {
    uint32_t table = LOWORD(wparam);
    bool locked = table == TABLE_PERIOD_LOCKS || table == TABLE_PERIOD_CLOSURES || table == TABLE_POSTING_REVISIONS || table == TABLE_REVISION_ENTRIES || table == TABLE_REVISION_LINES || table == TABLE_REVERSAL_LINKS;
    if (table == TABLE_DOCUMENTS) {
      const db_document_t *doc = msg == dbDelete ? NULL : lparam;
      int id = msg == dbDelete ? (int)(intptr_t)lparam : doc ? doc->id : 0;
      locked = (doc && ledger_period_closed(db, doc->date)) || (msg != dbInsert && locked_document(db, id));
      if (!ledger_posting_active() && msg != dbInsert && (ledger_exact_posted(db, id) || ledger_reversal_linked(db, id))) locked = true;
    } else if (table == TABLE_DOC_LINES || table == TABLE_PAYROLL_LINES || table == TABLE_ASSET_LINES || table == TABLE_ENTRIES) {
      const void *old = msg == dbInsert || !lparam ? NULL : (const void *)db_schema_xml(db, dbFind, MAKEDWORD(table, 0),
        msg == dbDelete ? lparam : (void *)(intptr_t)*(const int *)lparam);
      locked = (old && locked_document(db, document_of(db, table, old))) ||
               (msg != dbDelete && lparam && locked_document(db, document_of(db, table, lparam)));
      if (!ledger_posting_active() && ((old && (ledger_exact_posted(db, document_of(db, table, old)) || ledger_reversal_linked(db, document_of(db, table, old)))) ||
          (msg != dbDelete && lparam && (ledger_exact_posted(db, document_of(db, table, lparam)) || ledger_reversal_linked(db, document_of(db, table, lparam)))))) locked = true;
    }
    if (locked) {
      fprintf(stderr, "[ledger] write rejected: closed period, protected posting or history table=%u message=%u\n", table, msg);
      fflush(stderr); return 0;
    }
  }
  if (g_recalculating || ledger_script_in_executor() || (msg != dbInsert && msg != dbUpdate && msg != dbDelete))
    return db_schema_xml(db, msg, wparam, lparam);
  uint32_t table_id = LOWORD(wparam);
  db_document_t staged_doc;
  db_doc_line_t staged_line;
  if (table_id == TABLE_DOCUMENTS && msg != dbDelete && lparam) {
    staged_doc = *(const db_document_t *)lparam;
    const db_document_t *old = msg == dbUpdate ? (const void *)db_schema_xml(db, dbFind, TABLE_DOCUMENTS, (void *)(intptr_t)staged_doc.id) : NULL;
    if (msg == dbInsert && !staged_doc.currency_id && exact_trade_type(staged_doc.doc_type_id)) staged_doc.exact_money = true;
    if (!ledger_posting_active() && staged_doc.exact_money && staged_doc.posted) {
      fprintf(stderr, "[ledger] document rejected: exact document=%d must be posted by the posting API\n", staged_doc.id); fflush(stderr); return 0;
    }
    if (old && old->exact_money && (!staged_doc.exact_money || staged_doc.currency_id ||
        (!old->correction_of && !exact_trade_type(staged_doc.doc_type_id)))) {
      fprintf(stderr, "[ledger] document rejected: exact document=%d cannot change representation or currency\n", staged_doc.id); fflush(stderr); return 0;
    }
    lparam = &staged_doc;
  } else if (table_id == TABLE_DOC_LINES && msg != dbDelete && lparam) {
    staged_line = *(const db_doc_line_t *)lparam;
    const db_doc_line_t *old = msg == dbUpdate ? (const void *)db_schema_xml(db, dbFind, TABLE_DOC_LINES, (void *)(intptr_t)staged_line.id) : NULL;
    const db_document_t *old_doc = old ? (const void *)db_schema_xml(db, dbFind, TABLE_DOCUMENTS, (void *)(intptr_t)old->document_id) : NULL;
    if (old_doc && old_doc->exact_money && old->document_id != staged_line.document_id) {
      fprintf(stderr, "[ledger] line rejected: cannot move exact line=%d\n", staged_line.id); fflush(stderr); return 0;
    }
    const db_document_t *doc = (const void *)db_schema_xml(db, dbFind, TABLE_DOCUMENTS, (void *)(intptr_t)staged_line.document_id);
    if (doc && doc->exact_money && !exact_line_inputs(&staged_line)) {
      fprintf(stderr, "[ledger] line rejected: document=%d quantity=%g price=%g\n", staged_line.document_id, staged_line.quantity, staged_line.price); fflush(stderr); return 0;
    }
    lparam = &staged_line;
  } else if (table_id == TABLE_ENTRIES && msg != dbDelete && lparam) {
    const db_entry_t *entry = lparam;
    const db_document_t *doc = (const void *)db_schema_xml(db, dbFind, TABLE_DOCUMENTS, (void *)(intptr_t)entry->document_id);
    if (doc && doc->exact_money && (!entry->exact_money || entry->currency_id || !isfinite(entry->amount) ||
        !isfinite(entry->quantity) || entry->amount != (double)entry->amount_minor / 100.0 ||
        entry->quantity != (double)entry->quantity_milli / 1000.0)) {
      fprintf(stderr, "[ledger] entry rejected: document=%d exact values do not match\n", entry->document_id); fflush(stderr); return 0;
    }
  }
  int doc_id = 0;
  if (msg == dbDelete && table_id != TABLE_DOCUMENTS) {
    const void *record = (const void *)db_schema_xml(db, dbFind, MAKEDWORD(table_id, 0), lparam);
    doc_id = document_of(db, table_id, record);
  }
  const db_document_t *input = table_id == TABLE_DOCUMENTS && msg != dbDelete ? lparam : NULL;
  int owner = table_id == TABLE_DOCUMENTS ? 0 : msg == dbDelete ? doc_id : document_of(db, table_id, lparam);
  const db_document_t *owner_doc = owner ? (const void *)db_schema_xml(db, dbFind, TABLE_DOCUMENTS, (void *)(intptr_t)owner) : NULL;
  bool scope = (input && (!input->number[0] || input->exact_money)) ||
               (owner_doc && owner_doc->exact_money && is_line_table(db, table_id, owner));
  if (scope && !db_schema_xml(db, dbBeginChanges, 0, NULL)) return 0;
  lresult_t result = db_schema_xml(db, msg, wparam, lparam);
  if (!result) {
    if (scope) db_schema_xml(db, dbRevertChanges, 0, NULL);
    return 0;
  }
  if (table_id == TABLE_DOCUMENTS && msg != dbDelete) {
    const db_document_t *doc = msg == dbInsert ? (const db_document_t *)result : (const db_document_t *)lparam;
    if (!recalculate(db, doc->id, !db->edit_parent)) {
      if (scope) db_schema_xml(db, dbRevertChanges, 0, NULL);
      return 0;
    }
  } else if (table_id != TABLE_DOCUMENTS) {
    if (msg != dbDelete) doc_id = document_of(db, table_id, msg == dbInsert ? (const void *)result : lparam);
    if (is_line_table(db, table_id, doc_id) && !recalculate(db, doc_id, false)) {
      if (scope) db_schema_xml(db, dbRevertChanges, 0, NULL);
      return 0;
    }
  }
  if (scope && !db_schema_xml(db, dbCommitChanges, 0, NULL)) {
    db_schema_xml(db, dbRevertChanges, 0, NULL);
    return 0;
  }
  return result;
}

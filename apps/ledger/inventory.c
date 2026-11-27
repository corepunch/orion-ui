// Physical stock is the quantity recorded on goods accounts, by warehouse.
// Transfers allocate physical stock without changing Ledger's pooled monetary
// valuation. Reservations constrain availability, not accounting entries.
#include "ledger.h"
#include <math.h>
#include <orion/user/money.h>

enum stock_action { STOCK_RESERVE = 1, STOCK_RELEASE, STOCK_TRANSFER, STOCK_REVERSE, STOCK_FULFILL };

#define STOCK_RECORD(type, node) (*(const type **)(node)->data)
static const void *inventory_record(database_t *db, int table, int id) {
  return id > 0 ? (const void *)send_db_message(db, dbFind, table, (void *)(intptr_t)id) : NULL;
}

static result_node_t *inventory_rows(database_t *db, int table, const char *field, int id) {
  return (void *)send_db_message(db, dbFetch, MAKEDWORD(table, field ? ledger_field(db, table, field) : 0), (void *)(intptr_t)id);
}

typedef struct { int item, warehouse; bool valid; } stock_side_t;

static stock_side_t stock_side(database_t *cfg, const result_node_t *types, const db_entry_t *entry,
                               const db_document_t *doc, bool debit) {
  int account = debit ? entry->debit_id : entry->credit_id;
  bool goods = false;
  for (const result_node_t *n = types; n && !goods; n = n->next) {
    const db_doc_type_t *type = STOCK_RECORD(db_doc_type_t, n);
    if (type->cost_credit_id) goods = ledger_account_within(cfg, account, type->cost_credit_id);
  }
  if (!goods) return (stock_side_t){0};
  int ids[] = {
    debit ? entry->debit_subconto_id : entry->credit_subconto_id,
    debit ? entry->debit_subconto2_id : entry->credit_subconto2_id,
    debit ? entry->debit_subconto3_id : entry->credit_subconto3_id
  };
  stock_side_t side = { .warehouse = doc->warehouse_id };
  for (int i = 0; i < 3; i++) {
    const char *kind = ledger_subconto_slot_table(cfg, account, i + 1);
    if (kind && !strcmp(kind, "items")) side.item = ids[i];
    else if (kind && !strcmp(kind, "warehouses") && ids[i] > 0) side.warehouse = ids[i];
  }
  side.valid = side.item > 0;
  return side;
}

static bool stock_units(const db_entry_t *e, int64_t *out) {
  if (e->exact_money) {
    *out = e->quantity_milli;
    return *out != INT64_MIN;
  }
  long double raw = (long double)e->quantity * 1000;
  long double units = roundl(raw);
  if (!isfinite(raw) || fabsl(raw - units) > 0.000001L || fabsl(units) >= 0x1p63L) return false;
  *out = (int64_t)units;
  return true;
}

// 1C import preserves the quantity on each side independently; an absent
// imported quantity is not evidence of a physical movement.
static bool stock_side_units(const db_entry_t *entry, bool debit, int64_t *out) {
  if (!entry->source_period[0]) return stock_units(entry, out);
  bool set = debit ? entry->debit_quantity_set : entry->credit_quantity_set;
  *out = !set ? 0 : debit ? entry->debit_quantity_milli : entry->credit_quantity_milli;
  return *out != INT64_MIN;
}

static bool inventory_balance(database_t *ib, database_t *cfg, int item, int warehouse, const db_document_t *pending,
                    bool omit, ledger_stock_t *out) {
  if (!ib || !cfg || !out || item <= 0 || warehouse < 0) return false;
  *out = (ledger_stock_t){0};
  result_node_t *types = inventory_rows(cfg, TABLE_DOC_TYPES, NULL, 0);
  result_node_t *entries = inventory_rows(ib, TABLE_ENTRIES, NULL, 0);
  bool ok = !cfg->error[0] && !ib->error[0];
  for (const result_node_t *n = entries; ok && n; n = n->next) {
    const db_entry_t *entry = STOCK_RECORD(db_entry_t, n);
    if (entry->source_inactive || (!entry->source_period[0] &&
        ((!entry->exact_money && !entry->quantity) || (entry->exact_money && !entry->quantity_milli)))) continue;
    const db_document_t *doc = inventory_record(ib, TABLE_DOCUMENTS, entry->document_id);
    if (pending && entry->document_id == pending->id) {
      if (omit) continue;
      doc = pending;
    } else {
      if (!doc || !doc->posted) continue;
    }
    int64_t dr = 0, cr = 0, delta = 0;
    stock_side_t debit = stock_side(cfg, types, entry, doc, true);
    stock_side_t credit = stock_side(cfg, types, entry, doc, false);
    if (debit.valid && debit.item == item && debit.warehouse == warehouse) ok = stock_side_units(entry, true, &dr);
    if (ok && credit.valid && credit.item == item && credit.warehouse == warehouse) ok = stock_side_units(entry, false, &cr);
    if (ok) ok = money_add(dr, -cr, &delta) && money_add(out->on_hand, delta, &out->on_hand);
  }
  free_result_list(entries);
  free_result_list(types);
  if (!ok) return false;
  result_node_t *operations = inventory_rows(ib, TABLE_STOCK_OPERATIONS, "item_id", item);
  ok = !ib->error[0];
  for (const result_node_t *n = operations; ok && n; n = n->next) {
    const db_stock_operation_t *op = STOCK_RECORD(db_stock_operation_t, n);
    if (op->quantity_milli <= 0 || op->action < STOCK_RESERVE || op->action > STOCK_FULFILL) { ok = false; break; }
    if ((op->action == STOCK_RESERVE || op->action == STOCK_RELEASE || op->action == STOCK_FULFILL) &&
        warehouse == op->warehouse_id) {
      ok = money_add(out->reserved, op->action == STOCK_RESERVE ? op->quantity_milli : -op->quantity_milli, &out->reserved);
    } else if (op->action == STOCK_TRANSFER || op->action == STOCK_REVERSE) {
      if (op->warehouse_id == warehouse) ok = money_add(out->on_hand, -op->quantity_milli, &out->on_hand);
      if (ok && op->destination_id == warehouse) ok = money_add(out->on_hand, op->quantity_milli, &out->on_hand);
    }
  }
  free_result_list(operations);
  return ok && out->reserved >= 0 && money_add(out->on_hand, -out->reserved, &out->available);
}

bool ledger_stock_balance(database_t *ib, database_t *cfg, int item, int warehouse, ledger_stock_t *out) {
  return inventory_record(ib, TABLE_ITEMS, item) && (!warehouse || inventory_record(ib, TABLE_WAREHOUSES, warehouse)) &&
         inventory_balance(ib, cfg, item, warehouse, NULL, false, out);
}

// A named warehouse must not go below its current active holds. Preview leaves
// the operational ledger untouched. Bucket zero is legacy/unassigned stock.
bool ledger_stock_validate_posting(database_t *ib, database_t *cfg, const db_document_t *doc, bool remove) {
  result_node_t *types = inventory_rows(cfg, TABLE_DOC_TYPES, NULL, 0);
  result_node_t *own = inventory_rows(ib, TABLE_ENTRIES, "document_id", doc->id);
  bool ok = !ib->error[0] && !cfg->error[0];
  for (const result_node_t *n = own; ok && n; n = n->next) {
    const db_entry_t *entry = STOCK_RECORD(db_entry_t, n);
    if (entry->source_inactive || (!entry->source_period[0] &&
        ((!entry->exact_money && !entry->quantity) || (entry->exact_money && !entry->quantity_milli)))) continue;
    for (int debit = 0; debit <= 1 && ok; debit++) {
      stock_side_t side = stock_side(cfg, types, entry, doc, debit != 0);
      if (!side.valid || side.warehouse <= 0) continue;
      ledger_stock_t current;
      ok = inventory_balance(ib, cfg, side.item, side.warehouse, doc, remove, &current) && current.available >= 0;
      if (!ok) fprintf(stderr, "[ledger] stock at warehouse %d would be insufficient: document=%d item=%d\n",
                       side.warehouse, doc->id, side.item);
    }
  }
  free_result_list(own);
  free_result_list(types);
  return ok;
}

bool ledger_stock_document_fulfilled(database_t *ib, int document) {
  if (document <= 0) return false;
  result_node_t *linked = inventory_rows(ib, TABLE_STOCK_OPERATIONS, "document_id", document);
  bool present = linked != NULL || ib->error[0];
  free_result_list(linked);
  return present;
}

static bool inventory_same_payload(const db_stock_operation_t *a, const db_stock_operation_t *b) {
  return a->action == b->action && a->item_id == b->item_id && a->warehouse_id == b->warehouse_id &&
    a->destination_id == b->destination_id && a->quantity_milli == b->quantity_milli &&
    a->original_id == b->original_id && a->document_id == b->document_id;
}

// Idempotency key scopes are set by callers. Only explicit, writable commands
// may append operations; the datasource denies ordinary insert/update/delete.
static int inventory_apply(database_t *ib, database_t *cfg, const char *key, db_stock_operation_t op) {
  if (!ib || !cfg || !key || !key[0] || strlen(key) >= sizeof(op.source_key) ||
      ledger_posting_active()) return 0;
  for (const unsigned char *p = (const unsigned char *)key; *p; p++)
    if (*p <= 32 || *p > 126) return 0;
  if (!send_db_message(ib, dbBeginChanges, 0, NULL)) return 0;
  memcpy(op.source_key, key, strlen(key) + 1);
  ib->error[0] = 0;
  const db_stock_operation_t *stored = (const void *)send_db_message(ib, dbFind,
    MAKEDWORD(TABLE_STOCK_OPERATIONS, ledger_field(ib, TABLE_STOCK_OPERATIONS, "source_key")), op.source_key);
  int id = 0;
  if (ib->error[0]) goto done;
  if (stored) { id = inventory_same_payload(stored, &op) ? stored->id : 0; goto done; }
  if (!inventory_record(ib, TABLE_ITEMS, op.item_id) || !inventory_record(ib, TABLE_WAREHOUSES, op.warehouse_id) ||
      op.quantity_milli <= 0) goto done;
  if (op.destination_id && (op.destination_id == op.warehouse_id ||
      !inventory_record(ib, TABLE_WAREHOUSES, op.destination_id))) goto done;
  if (op.original_id) {
    result_node_t *cancel = inventory_rows(ib, TABLE_STOCK_OPERATIONS, "original_id", op.original_id);
    bool consumed = cancel != NULL || ib->error[0];
    free_result_list(cancel);
    if (consumed) goto done;
  }

  ledger_stock_t old;
  if (!ledger_stock_balance(ib, cfg, op.item_id, op.warehouse_id, &old)) goto done;
  if ((op.action == STOCK_RESERVE || op.action == STOCK_TRANSFER || op.action == STOCK_REVERSE) &&
      old.available < op.quantity_milli) goto done;
  if (op.destination_id) {
    ledger_stock_t destination;
    int64_t ignored;
    if (!ledger_stock_balance(ib, cfg, op.item_id, op.destination_id, &destination) ||
        !money_add(destination.on_hand, op.quantity_milli, &ignored)) goto done;
  }
  if (op.action == STOCK_FULFILL) {
    const db_document_t *doc = inventory_record(ib, TABLE_DOCUMENTS, op.document_id);
    const db_doc_type_t *type = doc ? inventory_record(cfg, TABLE_DOC_TYPES, doc->doc_type_id) : NULL;
    if (!type || !type->cost_credit_id || doc->posted || doc->warehouse_id != op.warehouse_id) goto done;
    result_node_t *lines = inventory_rows(ib, TABLE_DOC_LINES, "document_id", doc->id);
    bool matches = !ib->error[0];
    int64_t quantity = 0;
    for (const result_node_t *n = lines; matches && n; n = n->next) {
      const db_doc_line_t *l = STOCK_RECORD(db_doc_line_t, n);
      int64_t units;
      db_entry_t x = { .exact_money = doc->exact_money, .quantity_milli = l->quantity_milli, .quantity = l->quantity };
      matches = l->item_id == op.item_id && stock_units(&x, &units) && units > 0 && money_add(quantity, units, &quantity);
    }
    free_result_list(lines);
    if (!matches || quantity != op.quantity_milli) goto done;
  }

  stored = (const void *)db_schema_xml(ib, dbInsert, TABLE_STOCK_OPERATIONS, &op);
  id = stored ? stored->id : 0;
  if (id && op.action == STOCK_FULFILL) {
    ledger_stock_t after;
    int64_t expected;
    if (!ledger_post(ib, cfg, op.document_id) ||
        !ledger_stock_balance(ib, cfg, op.item_id, op.warehouse_id, &after) ||
        !money_add(old.on_hand, -op.quantity_milli, &expected) || after.on_hand != expected) id = 0;
  }
done:
  if (id && send_db_message(ib, dbCommitChanges, 0, NULL)) return id;
  send_db_message(ib, dbRevertChanges, 0, NULL);
  return 0;
}

int ledger_stock_reserve(database_t *ib, database_t *cfg, const char *key, int item, int warehouse, int64_t quantity) {
  return inventory_apply(ib, cfg, key, (db_stock_operation_t){ .action = STOCK_RESERVE, .item_id = item,
    .warehouse_id = warehouse, .quantity_milli = quantity });
}

int ledger_stock_transfer(database_t *ib, database_t *cfg, const char *key, int item, int warehouse, int destination, int64_t quantity) {
  if (destination <= 0) return 0;
  return inventory_apply(ib, cfg, key, (db_stock_operation_t){ .action = STOCK_TRANSFER, .item_id = item,
    .warehouse_id = warehouse, .destination_id = destination, .quantity_milli = quantity });
}

static int inventory_complete(database_t *ib, database_t *cfg, const char *key, int original, int document, int action) {
  const db_stock_operation_t *op = inventory_record(ib, TABLE_STOCK_OPERATIONS, original);
  if (!op || op->action != (action == STOCK_REVERSE ? STOCK_TRANSFER : STOCK_RESERVE)) return 0;
  return inventory_apply(ib, cfg, key, (db_stock_operation_t){ .action = action, .item_id = op->item_id,
    .warehouse_id = action == STOCK_REVERSE ? op->destination_id : op->warehouse_id,
    .destination_id = action == STOCK_REVERSE ? op->warehouse_id : 0,
    .quantity_milli = op->quantity_milli, .original_id = original, .document_id = document });
}

int ledger_stock_release(database_t *ib, database_t *cfg, const char *key, int reservation) {
  return inventory_complete(ib, cfg, key, reservation, 0, STOCK_RELEASE);
}

int ledger_stock_reverse_transfer(database_t *ib, database_t *cfg, const char *key, int transfer) {
  return inventory_complete(ib, cfg, key, transfer, 0, STOCK_REVERSE);
}

int ledger_stock_fulfill(database_t *ib, database_t *cfg, const char *key, int reservation, int document) {
  if (document <= 0) return 0;
  return inventory_complete(ib, cfg, key, reservation, document, STOCK_FULFILL);
}

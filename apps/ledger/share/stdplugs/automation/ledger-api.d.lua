-- Generated; edit apps/ledger/ledger-api.xml
---@meta

---Read one record by its integer ID. Returns nil if it does not exist.
---@param db string Registered database name, normally ib or cfg.
---@param table string Schema table name.
---@param id integer Record ID.
---@return table|nil
function ledger.find(db, table, id) end

---Read records, optionally filtered by one field's exact value. The filter value is required when field is supplied; integer and string filters are supported.
---@param db string Registered database.
---@param table string Schema table.
---@param field? string Exact-match field.
---@param value? string|integer Exact-match value.
---@return table
function ledger.fetch(db, table, field, value) end

---Read formatted text of a stored field or a join path. Returns an empty string for a missing record.
---@param db string Database.
---@param table string Table.
---@param id integer Record ID.
---@param path string Field or path such as counterparty.name.
---@return string
function ledger.field(db, table, id, path) end

---Find an account in the active configuration by its code.
---@param code string Country-specific account code.
---@return table|nil
function ledger.account(code) end

---Add an entry to the document currently being posted. Only available inside posting, closing and post-event hooks. Subject fields (auto-derive analytics from the account's kind): counterparty, item, employee, asset, warehouse, contract. Per-side slot-2/3 explicit overrides (bypass auto-derive when non-zero): debit_subconto2, debit_subconto3, credit_subconto2, credit_subconto3. Other fields: debit, credit (code or ID), amount, quantity, content, currency, currency_amount.
---@param entry table Entry fields.
function ledger.entry(entry) end

---Read cumulative turnovers before the current document plus its entries so far. Only available inside posting context.
---@param account string|integer
---@param subconto? integer Analytics ID; omitted means all.
---@param currency? integer Currency ID; omitted means all.
---@param slot? integer Analytics slot 1–3. Defaults to 1.
---@return table
function ledger.turnover(account, subconto, currency, slot) end

---Read the exchange rate on a date. Zero means no rate was found.
---@param currency_id integer
---@param date string ISO date YYYY-MM-DD.
---@return number
function ledger.rate(currency_id, date) end

---Round to two decimal places, halves away from zero.
---@param value number
---@return number
function ledger.round2(value) end

---Write a diagnostic to stderr with the Ledger script prefix.
---@param text string
function ledger.log(text) end

---Register an event subscription in the active configuration's Lua state. Currently the platform emits post; callbacks receive a posting context.
---@param event string
---@param callback function
function ledger.on(event, callback) end

---Inspect a table's fields, types, lengths, relations and whether it is an automation write target.
---@param db string
---@param table string
---@return table
function ledger.schema(db, table) end

---Create a catalog record through the Ledger datasource lifecycle. Requires a writable script and an active ledger.transaction. Unknown fields, explicit IDs, invalid types, overlong strings, invalid dates and missing foreign references are rejected.
---@param table string items, counterparties or rates.
---@param fields table Typed stored field values, excluding id.
---@return table
function ledger.insert(table, fields) end

---Patch an existing catalog record through its datasource. Same write restrictions and field validation as insert. An ID cannot be changed. Does not repost historical documents.
---@param table string items, counterparties or rates.
---@param id integer
---@param fields table Changed field values.
---@return table
function ledger.update(table, id, fields) end

---Run a callback atomically in the infobase editing context. A thrown Lua error reverts every change in the scope and is rethrown. Returning false is an ordinary successful return, not a rollback request. Nested transactions are supported. Requires an explicitly writable script run; unavailable during posting hooks.
---@param callback function
---@return any
function ledger.transaction(callback) end

---Advance the inclusive accounting close date. Requires a writable script inside ledger.transaction; invalid dates and dates that do not advance the boundary are rejected. Closing also rejects any unposted document on or before the requested date and any document with an invalid date; resolve these before closing. A malformed or unexpected stored period lock blocks document changes and further closing until the infobase is recovered. Each successful close records the previous and new boundaries, UTC time, and authenticated actor when available, in the same transaction. Earlier closes have no reconstructed history. The caller must save the infobase after the transaction. There is no reopening API. This history is not tamper-proof and native code can mutate exposed record pointers.
---@param date string Inclusive ISO date YYYY-MM-DD.
function ledger.close_through(date) end

---Create and post one linked manual accounting reversal from the latest matching posting snapshot, in an open period. Requires a writable script inside ledger.transaction. The reason must contain a non-whitespace character. The source remains posted; duplicate reversals, changed entries and missing snapshots are rejected. This is not a corrected invoice or a statutory tax-return correction.
---@param document_id integer Posted source document ID.
---@param date string Reversal date in YYYY-MM-DD, no earlier than the source.
---@param reason string Nonempty reason, at most 255 bytes.
---@return integer
function ledger.reverse(document_id, date, reason) end

---Validate a catalog insert or patch without writing. Applies the same schema, value, reference and table restrictions as insert/update, and fills datasource defaults. Supplying id validates an update; omitting it validates an insert. Available in read-only runs.
---@param table string Writable catalog table.
---@param fields table Typed values excluding id.
---@param id? integer Existing record ID for an update.
---@return table
function ledger.validate(table, fields, id) end

---Read physical on-hand, active reserved and available quantities as exact thousandths. Warehouse zero reads unassigned historical stock. The balance is current over posted entries; marketplace inventory is not a source.
---@param item_id integer
---@param warehouse_id integer
---@return table
function ledger.stock_balance(item_id, warehouse_id) end

---Reserve available quantity for a source order or work order. Requires a writable ledger.transaction. All stock mutation keys are unique per infobase and accept a byte-for-byte identical retry; conflicting requests fail.
---@param source_key string
---@param item_id integer
---@param warehouse_id integer
---@param quantity_milli integer
---@return integer
function ledger.stock_reserve(source_key, item_id, warehouse_id, quantity_milli) end

---Release a whole reservation once, without adjusting physical inventory. Requires a writable ledger.transaction.
---@param source_key string
---@param reservation_id integer
---@return integer
function ledger.stock_release(source_key, reservation_id) end

---Move available physical goods between two named warehouses of the infobase. This does not change pooled monetary inventory valuation. Requires a writable ledger.transaction.
---@param source_key string
---@param item_id integer
---@param warehouse_id integer
---@param destination_id integer
---@param quantity_milli integer
---@return integer
function ledger.stock_transfer(source_key, item_id, warehouse_id, destination_id, quantity_milli) end

---Reverse one transfer if its destination still has sufficient available stock. Requires a writable ledger.transaction.
---@param source_key string
---@param transfer_id integer
---@return integer
function ledger.stock_reverse_transfer(source_key, transfer_id) end

---Consume one reservation by posting a matching draft goods sale in the same transaction. It must use the same item, complete reserved quantity and warehouse. The reserved quantity is not restored when the accounting sale is separately reversed. Requires a writable ledger.transaction.
---@param source_key string
---@param reservation_id integer
---@param document_id integer
---@return integer
function ledger.stock_fulfill(source_key, reservation_id, document_id) end

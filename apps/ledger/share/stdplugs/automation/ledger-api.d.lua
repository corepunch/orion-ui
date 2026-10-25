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

---Add an entry to the document currently being posted. Only available inside posting, closing and post-event hooks. Fields: debit, credit (code or ID), amount, quantity, content, counterparty, item, employee, asset, currency, currency_amount.
---@param entry table Entry fields.
function ledger.entry(entry) end

---Read cumulative turnovers before the current document plus its entries so far. Only available inside posting context.
---@param account string|integer
---@param subconto? integer Analytics ID; omitted means all.
---@param currency? integer Currency ID; omitted means all.
---@return table
function ledger.turnover(account, subconto, currency) end

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

---Validate a catalog insert or patch without writing. Applies the same schema, value, reference and table restrictions as insert/update, and fills datasource defaults. Supplying id validates an update; omitting it validates an insert. Available in read-only runs.
---@param table string Writable catalog table.
---@param fields table Typed values excluding id.
---@param id? integer Existing record ID for an update.
---@return table
function ledger.validate(table, fields, id) end

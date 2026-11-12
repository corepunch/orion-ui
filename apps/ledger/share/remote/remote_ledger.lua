local orion = require("orion")
local Remote = require("orion.remote")
local json = require("orion.json")

local Ledger = {form = "remote_ledger", title = orion.tr("Ledger — Shared Company")}
local controls = {"post", "update", "replace_lines", "reverse", "create", "add_line", "remove_line",
  "save_counterparty", "save_item", "prepare_close", "close_period", "type_id", "draft_party_id", "draft_date",
  "item_id", "vat_id", "quantity", "price", "document_date", "counterparty_id", "reversal_date", "reason",
  "name", "sku", "tax_id", "unit_id", "catalog_vat_id", "close_date"}
local inputs = {"type_id", "draft_party_id", "draft_date", "item_id", "vat_id", "quantity", "price",
  "document_date", "counterparty_id", "reversal_date", "reason", "name", "sku", "tax_id", "unit_id", "catalog_vat_id", "close_date", "history_document_id"}

local function decimal_separator() return orion.formatNumber(1.5, 1):match("1(.-)5") or "." end
local function minor(value, fallback)
  if type(value) ~= "string" or not value:match("^-?%d+$") then return "~" .. orion.formatNumber(fallback or 0, 2) end
  local sign, digits = value:match("^(-?)(%d+)$")
  while #digits < 3 do digits = "0" .. digits end
  local separator = decimal_separator()
  return sign .. digits:sub(1, -3) .. separator .. digits:sub(-2)
end
local function milli(value, fallback)
  if type(value) ~= "string" or not value:match("^-?%d+$") then return "~" .. orion.formatNumber(fallback or 0, 3) end
  local sign, digits = value:match("^(-?)(%d+)$")
  while #digits < 4 do digits = "0" .. digits end
  return sign .. digits:sub(1, -4) .. decimal_separator() .. digits:sub(-3)
end
local function integer(value, message)
  return orion.assert(type(value) == "number" and math.type(value) == "integer" and value > 0 and value <= 2147483647 and value, message)
end
local function selected(value)
  return orion.assert(value, orion.tr("Select a document."))
end
local function decimal(value, message)
  orion.assert(type(value) == "string", message)
  local separator = decimal_separator()
  local whole, fraction
  if value:match("^%d+$") then return value end
  local position = value:find(separator, 1, true)
  if position then whole, fraction = value:sub(1, position - 1), value:sub(position + #separator) end
  orion.assert(whole and whole:match("^%d+$") and fraction:match("^%d+$"), message)
  return whole .. "." .. fraction
end
local function projection(source, rows, fields) return {source = "ledger_remote_cache." .. source, rows = rows, fields = fields} end

function Ledger:init()
  self.remote = Remote.new(self.app.handle, "ledger")
  self.fields = {draft_date = orion.today(), document_date = orion.today(), reversal_date = orion.today(),
                 close_date = orion.today(), quantity = "1" .. decimal_separator() .. "000", price = "0" .. decimal_separator() .. "00"}
  self.lines, self.doc_offset, self.catalog_offset, self.journal_offset = {}, 0, 0, 0
  self.history_offset, self.posting_history_offset = 0, 0
  self.posting_history_document, self.revision_entries_revision = nil, nil
  self.revision_entries_offset, self.revision_entries_count = 0, 0
  self.next_line, self.role, self.connected, self.catalog_kind = 1, "", false, "items"
  self.notice, self.error = "", ""
end

function Ledger:remember(values)
  for _, name in ipairs(inputs) do if values[name] ~= nil then self.fields[name] = values[name] end end
end

function Ledger:complete(result, err)
  local callback = self.pending_callback
  local state = self.remote:state()
  if err then
    self.error = err
    self.notice = state.conflict and orion.tr("Data changed. Refresh before continuing.") or
      state.pending and orion.tr("The outcome is unknown. Retry the same request before making changes.") or ""
    if not state.pending then self.pending_callback = nil end
    return
  end
  self.pending_callback, self.error = nil, ""
  if callback then callback(result) elseif self.connected then self:load_documents(0) end
end

function Ledger:request(command, args, callback)
  orion.assert(self.connected, orion.tr("Connect to the shared company first."))
  local state = self.remote:state()
  orion.assert(not state.busy and not state.pending and not state.conflict, orion.tr("Resolve the pending request before changing the draft."))
  self.error = ""
  self.pending_callback = callback
  local ok, err = self.remote:submit(command, args, function(result, failure) self:complete(result, failure) end)
  if not ok then
    self.error = err or orion.tr("The request could not be started.")
    if not self.remote:state().pending then self.pending_callback = nil end
  end
end

function Ledger:connect(values)
  self:remember(values)
  local ok, err = self.remote:refresh(function(status, failure)
    if failure then self.error = failure; return end
    if status.application ~= "ledger" or (status.country ~= "RU" and status.country ~= "DE") then
      self.connected, self.role = false, ""
      self.error = orion.tr("This server is not a supported Ledger company.")
      return
    end
    self.connected, self.role, self.country, self.error = true, status.role or "", status.country, ""
    self.posting_history_document, self.posting_history_offset, self.posting_history_count = nil, 0, 0
    self.revision_entries_revision, self.revision_entries_offset, self.revision_entries_count = nil, 0, 0
    Remote.replace_many({projection("posting_history", {}, {"id", "recorded_utc", "actor", "action", "date", "number", "total"}),
      projection("revision_entries", {}, {"id", "date", "debit_id", "credit_id", "quantity", "amount", "content"})})
    if self.remote:state().pending then
      self.notice = orion.tr("The outcome is unknown. Retry the same request before making changes.")
    else
      self.notice = orion.tr("Connected. Amounts marked approximately use legacy storage.")
      self:load_documents(0)
    end
  end)
  if not ok then self.error = err end
end

function Ledger:retry(values)
  self:remember(values)
  local ok, err = self.remote:retry(function(result, failure) self:complete(result, failure) end)
  if not ok then self.error = err end
end

function Ledger:load_documents(offset)
  self:request("documents.list", {offset = offset, limit = 200}, function(rows)
    local view = {}
    for _, row in ipairs(rows) do
      view[#view + 1] = {id = row.id, date = row.date, number = row.number, correction_of = row.correction_of,
        kind = tostring(row.doc_type_id), party = tostring(row.counterparty_id), amount = minor(row.exact_money and row.amount_minor or nil, row.amount),
        posted = row.posted, state = row.posted and orion.tr("Posted") or orion.tr("Draft")}
    end
    Remote.replace_many({projection("documents", view, {"id", "date", "number", "correction_of", "kind", "party", "amount", "posted", "state"})})
    self.doc_offset, self.doc_count = offset, #rows
  end)
end
function Ledger:previous_documents(v) self:remember(v); self:load_documents(math.max(0, self.doc_offset - 200)) end
function Ledger:next_documents(v) self:remember(v); self:load_documents(self.doc_offset + 200) end

function Ledger:load_catalog(kind, offset)
  self:request("catalogs.list", {table = kind, offset = offset, limit = 200}, function(rows)
    local view = {}
    for _, row in ipairs(rows) do
      view[#view + 1] = {id = row.id, name = row.name or row.code or "", detail = tostring(row.code or row.sku or row.rate or row.tax_id or "")}
    end
    Remote.replace_many({projection("catalog", view, {"id", "name", "detail"})})
    self.catalog_kind, self.catalog_offset, self.catalog_count = kind, offset, #rows
  end)
end
for _, kind in ipairs({"doc_types", "vat_rates", "units", "items", "counterparties"}) do
  local action = kind == "doc_types" and "document_types" or kind
  Ledger[action] = function(self, values) self:remember(values); self:load_catalog(kind, 0) end
end
function Ledger:previous_catalog(v) self:remember(v); self:load_catalog(self.catalog_kind, math.max(0, self.catalog_offset - 200)) end
function Ledger:next_catalog(v) self:remember(v); self:load_catalog(self.catalog_kind, self.catalog_offset + 200) end

function Ledger:load_journal(offset)
  self:request("journal.list", {offset = offset, limit = 200}, function(rows)
    local view = {}
    for _, row in ipairs(rows) do
      view[#view + 1] = {id = row.id, date = row.date, document_id = row.document_id,
        debit_id = row.debit_id, credit_id = row.credit_id, amount = minor(row.exact_money and row.amount_minor or nil, row.amount), content = row.content}
    end
    Remote.replace_many({projection("journal", view, {"id", "date", "document_id", "debit_id", "credit_id", "amount", "content"})})
    self.journal_offset, self.journal_count = offset, #rows
  end)
end
function Ledger:show_journal(v) self:remember(v); self:load_journal(0) end
function Ledger:previous_journal(v) self:remember(v); self:load_journal(math.max(0, self.journal_offset - 200)) end
function Ledger:next_journal(v) self:remember(v); self:load_journal(self.journal_offset + 200) end

function Ledger:load_posting_history(document_id, offset)
  self:request("documents.history", {document_id = document_id, offset = offset, limit = 200}, function(rows)
    local view = {}
    for _, row in ipairs(rows) do
      view[#view + 1] = {id = row.id, recorded_utc = row.recorded_utc, actor = row.actor, action = row.action,
        date = row.date, number = row.number, total = minor(row.exact_money and row.total_minor or nil, row.total)}
    end
    Remote.replace_many({projection("posting_history", view, {"id", "recorded_utc", "actor", "action", "date", "number", "total"}),
      projection("revision_entries", {}, {"id", "date", "debit_id", "credit_id", "quantity", "amount", "content"})})
    self.posting_history_document, self.posting_history_offset, self.posting_history_count = document_id, offset, #rows
    self.revision_entries_revision, self.revision_entries_offset, self.revision_entries_count = nil, 0, 0
  end)
end
function Ledger:show_posting_history(v)
  self:remember(v)
  self:load_posting_history(selected(v.documents).id, 0)
end
function Ledger:find_posting_history(v)
  self:remember(v)
  self:load_posting_history(integer(v.history_document_id, orion.tr("Enter a document ID.")), 0)
end
function Ledger:previous_posting_history(v) self:remember(v); self:load_posting_history(self.posting_history_document, math.max(0, self.posting_history_offset - 200)) end
function Ledger:next_posting_history(v) self:remember(v); self:load_posting_history(self.posting_history_document, self.posting_history_offset + 200) end

function Ledger:load_revision_entries(revision_id, offset)
  self:request("documents.history_entries", {revision_id = revision_id, offset = offset, limit = 200}, function(rows)
    local view = {}
    for _, row in ipairs(rows) do
      view[#view + 1] = {id = row.id, date = row.date, debit_id = row.debit_id, credit_id = row.credit_id,
        quantity = milli(row.exact_money and row.quantity_milli or nil, row.quantity),
        amount = minor(row.exact_money and row.amount_minor or nil, row.amount), content = row.content}
    end
    Remote.replace_many({projection("revision_entries", view, {"id", "date", "debit_id", "credit_id", "quantity", "amount", "content"})})
    self.revision_entries_revision, self.revision_entries_offset, self.revision_entries_count = revision_id, offset, #rows
  end)
end
function Ledger:show_revision_entries(v)
  self:remember(v)
  local revision = orion.assert(v.posting_history, orion.tr("Select a posting snapshot."))
  self:load_revision_entries(revision.id, 0)
end
function Ledger:previous_revision_entries(v) self:remember(v); self:load_revision_entries(self.revision_entries_revision, math.max(0, self.revision_entries_offset - 200)) end
function Ledger:next_revision_entries(v) self:remember(v); self:load_revision_entries(self.revision_entries_revision, self.revision_entries_offset + 200) end

function Ledger:load_history(offset)
  self:request("period.history", {offset = offset, limit = 200}, function(rows)
    Remote.replace_many({projection("period_history", rows, {"id", "previous_through", "closed_through", "recorded_utc", "actor"})})
    self.history_offset, self.history_count = offset, #rows
  end)
end
function Ledger:show_history(v) self:remember(v); self:load_history(0) end
function Ledger:previous_history(v) self:remember(v); self:load_history(math.max(0, self.history_offset - 200)) end
function Ledger:next_history(v) self:remember(v); self:load_history(self.history_offset + 200) end

function Ledger:publish_lines()
  local rows, separator = {}, decimal_separator()
  for _, line in ipairs(self.lines) do
    rows[#rows + 1] = {id = line.id, item_id = line.item_id, vat_rate_id = line.vat_rate_id,
      quantity = line.quantity:gsub("%.", separator), price = line.price:gsub("%.", separator)}
  end
  Remote.replace_many({projection("lines", rows, {"id", "item_id", "vat_rate_id", "quantity", "price"})})
end
function Ledger:add_line(v)
  local state = self.remote:state()
  orion.assert(not state.busy and not state.pending and not state.conflict, orion.tr("Resolve the pending request before changing the draft."))
  self:remember(v)
  orion.assert(#self.lines < 100, orion.tr("A draft can contain at most 100 lines."))
  self.lines[#self.lines + 1] = {id = self.next_line, item_id = integer(v.item_id, orion.tr("Enter an item ID.")),
    vat_rate_id = integer(v.vat_id, orion.tr("Enter a VAT rate ID.")), quantity = decimal(v.quantity, orion.tr("Enter a quantity.")),
    price = decimal(v.price, orion.tr("Enter a unit price."))}
  self.next_line = self.next_line + 1
  self:publish_lines()
end
function Ledger:remove_line(v)
  local state = self.remote:state()
  orion.assert(not state.busy and not state.pending and not state.conflict, orion.tr("Resolve the pending request before changing the draft."))
  self:remember(v)
  local row = orion.assert(v.lines, orion.tr("Select a draft line."))
  for i, line in ipairs(self.lines) do if line.id == row.id then table.remove(self.lines, i); break end end
  self:publish_lines()
end
function Ledger:line_arguments()
  orion.assert(#self.lines > 0, orion.tr("Add at least one line."))
  local lines = json.array({})
  for _, row in ipairs(self.lines) do lines[#lines + 1] = {item_id = row.item_id, vat_rate_id = row.vat_rate_id, quantity = row.quantity, price = row.price} end
  return lines
end
function Ledger:create(v)
  self:remember(v)
  local args = {doc_type_id = integer(v.type_id, orion.tr("Enter a document type ID.")),
    counterparty_id = integer(v.draft_party_id, orion.tr("Enter a counterparty ID.")),
    date = orion.assert(v.draft_date, orion.tr("Enter a valid document date.")), lines = self:line_arguments()}
  self:request("documents.create", args, function(result)
    self.notice = string.format(orion.tr("Draft %d created."), result.document_id)
    self.lines = {}; self:publish_lines(); self:load_documents(0)
  end)
end
function Ledger:update(v)
  self:remember(v); local row = selected(v.documents)
  self:request("documents.update", {document_id = row.id, counterparty_id = integer(v.counterparty_id, orion.tr("Enter a counterparty ID.")),
    date = orion.assert(v.document_date, orion.tr("Enter a valid document date."))}, function() self:load_documents(self.doc_offset) end)
end
function Ledger:replace_lines(v)
  self:remember(v); local row = selected(v.documents)
  self:request("documents.replace_lines", {document_id = row.id, lines = self:line_arguments()}, function()
    self.notice = orion.tr("Draft lines replaced."); self:load_documents(self.doc_offset)
  end)
end
function Ledger:post(v)
  self:remember(v); local row = selected(v.documents)
  self:request("documents.post", {document_id = row.id}, function() self.notice = orion.tr("Document posted."); self:load_documents(self.doc_offset) end)
end
function Ledger:reverse(v)
  self:remember(v); local row = selected(v.documents)
  self:request("documents.reverse", {document_id = row.id, date = orion.assert(v.reversal_date, orion.tr("Enter a valid reversal date.")),
    reason = orion.assert(type(v.reason) == "string" and v.reason:match("%S") and v.reason, orion.tr("Enter a reason."))},
    function(result) self.notice = string.format(orion.tr("Reversal %d posted."), result.document_id); self:load_documents(0) end)
end
function Ledger:save_counterparty(v)
  self:remember(v)
  self:request("counterparties.save", {fields = {name = orion.assert(v.name and v.name:match("%S") and v.name, orion.tr("Enter a name.")),
    tax_id = v.tax_id or "", country_code = self.country}}, function(result)
      self.notice = string.format(orion.tr("Counterparty %d created."), result.id); self:load_catalog("counterparties", 0)
    end)
end
function Ledger:save_item(v)
  self:remember(v)
  self:request("items.save", {fields = {name = orion.assert(v.name and v.name:match("%S") and v.name, orion.tr("Enter a name.")),
    sku = orion.assert(v.sku and v.sku:match("%S") and v.sku, orion.tr("Enter an SKU.")),
    unit_id = integer(v.unit_id, orion.tr("Enter a unit ID.")), vat_rate_id = integer(v.catalog_vat_id, orion.tr("Enter a VAT rate ID."))}},
    function(result) self.notice = string.format(orion.tr("Item %d created."), result.id); self:load_catalog("items", 0) end)
end
function Ledger:prepare_close(v)
  self:remember(v); self.closing_date = orion.assert(v.close_date, orion.tr("Enter a valid close date."))
  self.notice = string.format(orion.tr("Review: close through %s. Confirm to continue."), orion.formatDate(self.closing_date))
end
function Ledger:close_period(v)
  self:remember(v)
  orion.assert(self.closing_date and self.closing_date == v.close_date, orion.tr("Review the close date first."))
  self:request("period.close", {date = self.closing_date}, function()
    self.notice = orion.tr("Accounting period closed."); self.closing_date = nil; self:load_documents(0)
  end)
end
function Ledger:data()
  local state = self.remote:state()
  local blocked = state.busy or state.pending or state.conflict or not self.connected
  local readonly = self.role ~= "admin" and self.role ~= "accountant"
  local disabled = {connect = state.busy or (state.pending and not state.conflict), retry = state.busy or not state.pending}
  for _, name in ipairs(controls) do disabled[name] = blocked or readonly end
  disabled.close_period = blocked or self.role ~= "admin" or not self.closing_date
  disabled.prepare_close = blocked or self.role ~= "admin"
  for _, name in ipairs({"previous_documents", "next_documents", "previous_catalog", "next_catalog", "previous_journal", "next_journal",
                         "document_types", "vat_rates", "units", "items", "counterparties", "show_journal", "show_history", "show_posting_history", "show_revision_entries", "find_posting_history", "history_document_id"}) do disabled[name] = blocked end
  disabled.previous_documents = blocked or self.doc_offset == 0
  disabled.previous_catalog = blocked or self.catalog_offset == 0
  disabled.previous_journal = blocked or self.journal_offset == 0
  disabled.next_documents = blocked or self.doc_count ~= 200
  disabled.next_catalog = blocked or self.catalog_count ~= 200
  disabled.next_journal = blocked or self.journal_count ~= 200
  disabled.previous_history = blocked or self.history_offset == 0
  disabled.next_history = blocked or self.history_count ~= 200
  disabled.previous_posting_history = blocked or not self.posting_history_document or self.posting_history_offset == 0
  disabled.next_posting_history = blocked or not self.posting_history_document or self.posting_history_count ~= 200
  disabled.previous_revision_entries = blocked or not self.revision_entries_revision or self.revision_entries_offset == 0
  disabled.next_revision_entries = blocked or not self.revision_entries_revision or self.revision_entries_count ~= 200
  local status = self.connected and string.format(orion.tr("Country: %s · Role: %s · Revision: %d"), self.country, self.role, state.revision or 0) or orion.tr("Not connected")
  local titles = {doc_types = "Document types", vat_rates = "VAT rates", units = "Units", items = "Items", counterparties = "Counterparties"}
  local values = {connection = status, notice = self.notice, error = self.error, catalog_title = orion.tr(titles[self.catalog_kind]),
    posting_history_title = self.posting_history_document and string.format(orion.tr("Posting history for document ID %d"), self.posting_history_document) or orion.tr("Select a document and show its posting history."),
    revision_entries_title = self.revision_entries_revision and string.format(orion.tr("Entries for posting snapshot ID %d"), self.revision_entries_revision) or orion.tr("Select a posting snapshot to show its entries.")}
  for key, value in pairs(self.fields) do values[key] = value end
  return {values = values, disabled = disabled}
end
return Ledger

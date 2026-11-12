local json = require("orion.json")
local request_file = ...

local function safe_rows(rows)
  for _, row in ipairs(rows) do
    for key, value in pairs(row) do
      if type(value) == "number" and math.type(value) == "integer" and
          (key:match("_minor$") or key:match("_milli$")) then row[key] = tostring(value) end
    end
  end
  return json.array(rows)
end

local function page(rows, args)
  local offset, limit = args.offset, args.limit
  if offset == nil then offset = 0 end
  if limit == nil then limit = 200 end
  assert(type(offset) == "number" and math.type(offset) == "integer" and offset >= 0 and offset <= 1000000000, "invalid offset")
  assert(type(limit) == "number" and math.type(limit) == "integer" and limit >= 1 and limit <= 500, "invalid limit")
  table.sort(rows, function(a, b) return a.id < b.id end)
  local result = json.array({})
  for i = offset + 1, math.min(#rows, offset + limit) do result[#result + 1] = rows[i] end
  return safe_rows(result)
end

local function dispatch(request)
  assert(type(request) == "table" and not getmetatable(request) and type(request.args) == "table" and not getmetatable(request.args), "request and args must be objects")
  assert(type(request.actor) == "string" and #request.actor > 0 and #request.actor < 128, "invalid actor")
  local role = request.role
  assert(role == "admin" or role == "accountant" or role == "auditor", "invalid role")
  ledger_service_native.access(request.actor, role)
  local command, args = request.command, request.args
  if command == "catalogs.list" then
    local tables = {doc_types = "cfg", vat_rates = "cfg", units = "cfg", items = "ib", counterparties = "ib"}
    assert(type(args.table) == "string" and tables[args.table], "unsupported catalog")
    local rows = ledger.fetch(tables[args.table], args.table)
    return page(rows, args)
  elseif command == "counterparties.save" or command == "items.save" then
    assert(role == "admin" or role == "accountant", "role cannot edit catalogs")
    local items = command == "items.save"
    local allowed = items and {name=true, sku=true, unit_id=true, vat_rate_id=true} or
      {name=true, tax_id=true, kpp=true, address=true, bank=true, bank_account=true, bik=true, phone=true, country_code=true}
    assert(type(args.fields) == "table" and not getmetatable(args.fields), "fields must be an object")
    for key in pairs(args.fields) do assert(allowed[key], "unsupported catalog field") end
    assert(type(args.fields.name) == "string" and args.fields.name:match("%S"), "name is required")
    if items then assert(type(args.fields.sku) == "string" and args.fields.sku:match("%S"), "SKU is required") end
    local name = items and "items" or "counterparties"
    local record = ledger.transaction(function()
      if args.id ~= nil then
        assert(type(args.id) == "number" and math.type(args.id) == "integer" and args.id > 0, "invalid id")
        return ledger.update(name, args.id, args.fields)
      end
      return ledger.insert(name, args.fields)
    end)
    return {id = record.id}
  elseif command == "documents.list" then
    local rows = ledger.fetch("ib", "documents")
    return page(rows, args)
  elseif command == "documents.lines" then
    assert(type(args.document_id) == "number" and math.type(args.document_id) == "integer" and args.document_id > 0, "invalid document id")
    local exact, rows = false, {}
    for _, doc in ipairs(ledger.fetch("ib", "documents")) do
      if doc.id == args.document_id then exact = doc.exact_money; break end
    end
    for _, row in ipairs(ledger.fetch("ib", "doc_lines")) do
      if row.document_id == args.document_id then row.exact_money = exact; rows[#rows + 1] = row end
    end
    return page(rows, args)
  elseif command == "documents.history" then
    assert(type(args.document_id) == "number" and math.type(args.document_id) == "integer" and args.document_id > 0, "invalid document id")
    local rows = {}
    for _, row in ipairs(ledger.fetch("ib", "posting_revisions")) do
      if row.document_id == args.document_id then rows[#rows + 1] = row end
    end
    return page(rows, args)
  elseif command == "documents.history_entries" then
    assert(type(args.revision_id) == "number" and math.type(args.revision_id) == "integer" and args.revision_id > 0, "invalid revision id")
    local rows = {}
    for _, row in ipairs(ledger.fetch("ib", "revision_entries")) do
      if row.revision_id == args.revision_id then rows[#rows + 1] = row end
    end
    return page(rows, args)
  elseif command == "documents.history_lines" then
    assert(type(args.revision_id) == "number" and math.type(args.revision_id) == "integer" and args.revision_id > 0, "invalid revision id")
    local revision
    for _, row in ipairs(ledger.fetch("ib", "posting_revisions")) do if row.id == args.revision_id then revision = row; break end end
    assert(revision, "unknown revision")
    local rows = {}
    if revision.lines_captured then
      for _, row in ipairs(ledger.fetch("ib", "revision_lines")) do if row.revision_id == revision.id then rows[#rows + 1] = row end end
    end
    return {available = revision.lines_captured, lines = page(rows, args)}
  elseif command == "journal.list" then
    local posted, result = {}, json.array({})
    for _, doc in ipairs(ledger.fetch("ib", "documents")) do if doc.posted then posted[doc.id] = true end end
    for _, entry in ipairs(ledger.fetch("ib", "entries")) do
      if posted[entry.document_id] then result[#result + 1] = entry end
    end
    return page(result, args)
  elseif command == "period.history" then
    return page(ledger.fetch("ib", "period_closures"), args)
  elseif command == "documents.create" then
    assert(role == "admin" or role == "accountant", "role cannot create documents")
    return {document_id = ledger_service_native.create(args)}
  elseif command == "documents.delete" then
    assert(role == "admin" or role == "accountant", "role cannot delete documents")
    assert(type(args.document_id) == "number" and math.type(args.document_id) == "integer", "invalid document id")
    return {document_id = ledger_service_native.delete(args.document_id)}
  elseif command == "documents.post" then
    assert(role == "admin" or role == "accountant", "role cannot post documents")
    assert(type(args.document_id) == "number" and math.type(args.document_id) == "integer", "invalid document id")
    return {document_id = ledger_service_native.post(args.document_id)}
  elseif command == "documents.update" then
    assert(role == "admin" or role == "accountant", "role cannot edit documents")
    return {document_id = ledger_service_native.update(args)}
  elseif command == "documents.replace_lines" then
    assert(role == "admin" or role == "accountant", "role cannot edit documents")
    return {document_id = ledger_service_native.replace_lines(args)}
  elseif command == "documents.reverse" then
    assert(role == "admin" or role == "accountant", "role cannot reverse documents")
    assert(type(args.document_id) == "number" and math.type(args.document_id) == "integer", "invalid document id")
    assert(type(args.date) == "string" and type(args.reason) == "string", "invalid reversal details")
    return {document_id = ledger_service_native.reverse(args.document_id, args.date, args.reason)}
  elseif command == "period.preview_close" then
    assert(role == "admin", "role cannot close periods")
    assert(type(args.date) == "string", "invalid date")
    return ledger_service_native.close_preview(args.date)
  elseif command == "period.close" then
    assert(role == "admin", "role cannot close periods")
    assert(type(args.date) == "string", "invalid date")
    ledger.transaction(function() ledger.close_through(args.date) end)
    return {closed_through = args.date}
  end
  error("unsupported command", 0)
end

local ok, result = pcall(function()
  local file = assert(io.open(request_file, "rb"), "cannot open request")
  local body = file:read(1048577); file:close()
  assert(body and #body <= 1048576, "request is too large")
  return dispatch(json.decode(body))
end)
if ok then return json.encode({ok = true, result = result}), true end
return json.encode({ok = false, error = tostring(result):sub(1, 512)}), false

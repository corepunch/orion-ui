local json = require("orion.json")
local request_file = ...
local directory = assert(debug.getinfo(1, "S").source:match("^@(.*[/\\])"))
local function module(name)
  return assert(loadfile(directory .. "business/" .. name .. ".lua"))()
end

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

-- The local host selects the shipped profile, never the incoming JSON.
local ctx = {
  ledger=ledger, native=ledger_service_native, page=page, catalog_write=module("catalog_write"),
  view={"admin", "accountant", "auditor"}, edit={"admin", "accountant"}, manage={"admin"},
}
local manifests = {}
for _, source in ipairs({"counterparties", "items", "catalogs", "documents", "journal", "period", "stock"}) do
  manifests[#manifests + 1] = module(source)(ctx)
end
local registry = module("registry").assemble({
  id="ledger.accounting", api=1, modules={"ledger.catalogs", "ledger.documents", "ledger.journal", "ledger.period", "ledger.stock"},
}, manifests)

local function dispatch(request)
  assert(type(request) == "table" and not getmetatable(request) and type(request.args) == "table" and not getmetatable(request.args), "request and args must be objects")
  assert(type(request.actor) == "string" and #request.actor > 0 and #request.actor < 128, "invalid actor")
  local role = request.role
  assert(role == "admin" or role == "accountant" or role == "auditor", "invalid role")
  ledger_service_native.access(request.actor, role)
  if request.command == "capabilities.list" then
    local contract = registry.describe(role)
    contract.modules = json.array(contract.modules)
    contract.commands = json.array(contract.commands)
    for _, entry in ipairs(contract.modules) do entry.resources = json.array(entry.resources) end
    return contract
  end
  return registry.invoke(role, request.command, request.args)
end

local ok, result = pcall(function()
  local file = assert(io.open(request_file, "rb"), "cannot open request")
  local body = file:read(1048577); file:close()
  assert(body and #body <= 1048576, "request is too large")
  return dispatch(json.decode(body))
end)
if ok then return json.encode({ok = true, result = result}), true end
return json.encode({ok = false, error = tostring(result):sub(1, 512)}), false

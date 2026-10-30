-- Trusted local automation. Metadata is generated from automation.xml.
local directory = assert(debug.getinfo(1, "S").source:match("^@(.*/)"))
local N = assert(loadfile(directory .. "nodes.lua"))()
local catalog = assert(loadfile(directory .. "catalog.lua"))()
local A = {nodes = {}, recipes = {}, plugins = {}, version = 1}
local I, handlers, recipe_handlers = {}, {}, {}
ledger.automation = A

local function same(a, b)
  if type(a) ~= type(b) then return false end
  if type(a) ~= "table" then return a == b end
  for k, v in pairs(a) do if not same(v, b[k]) then return false end end
  for k in pairs(b) do if a[k] == nil then return false end end
  return true
end

function I.register(descriptor, node_impl, recipe_impl)
  assert(type(descriptor.id) == "string" and descriptor.version == "1", "unsupported plugin descriptor")
  assert(not A.plugins[descriptor.id], "duplicate plugin ID")
  local pending_nodes, pending_recipes = descriptor.bind_nodes(node_impl), descriptor.bind_recipes(recipe_impl)
  for _, group in ipairs({{rows=descriptor.nodes, registered=A.nodes, impl=pending_nodes}, {rows=descriptor.recipes, registered=A.recipes, impl=pending_recipes}}) do
    local seen = {}
    for _, item in ipairs(group.rows) do
      assert(not seen[item.id] and type(group.impl[item.id]) == "function", "invalid plugin implementation")
      seen[item.id] = true
      for _, existing in ipairs(group.registered) do assert(existing.id ~= item.id, "duplicate contribution ID: " .. item.id) end
    end
  end
  for _, item in ipairs(descriptor.nodes) do A.nodes[#A.nodes + 1] = item; handlers[item.id] = pending_nodes[item.id] end
  for _, item in ipairs(descriptor.recipes) do A.recipes[#A.recipes + 1] = item; recipe_handlers[item.id] = pending_recipes[item.id] end
  A.plugins[descriptor.id] = descriptor.version
end

function I.search(query, tags, kind)
  local out, wanted = {}, {}
  for _, tag in ipairs(tags or {}) do wanted[tag] = true end
  query = N.normalized(query or "")
  for _, collection in ipairs({{name = "recipe", rows = A.recipes}, {name = "node", rows = A.nodes}}) do
    if not kind or kind == collection.name then
      for _, item in ipairs(collection.rows) do
        local haystack = N.normalized(item.title .. " " .. item.description .. " " .. table.concat(item.tags, " ") .. " " .. table.concat(item.aliases, " "))
        local matches = true
        for word in query:gmatch("%S+") do if not haystack:find(word, 1, true) then matches = false end end
        for tag in pairs(wanted) do
          local found = false
          for _, existing in ipairs(item.tags) do if existing == tag then found = true end end
          if not found then matches = false end
        end
        if matches then out[#out + 1] = item end
      end
    end
  end
  return out
end

local function validate_params(spec, params)
  local declared = {}
  for _, param in ipairs(spec.params) do
    declared[param.name] = true
    local value = params[param.name]
    assert(param.optional or value ~= nil, "missing parameter: " .. param.name)
    if value ~= nil then
      local valid = param.type == "any" or type(value) == param.type or (param.type == "integer" and math.type(value) == "integer")
      assert(valid, "invalid parameter: " .. param.name)
    end
  end
  for name in pairs(params) do assert(declared[name], "unknown parameter: " .. tostring(name)) end
end

-- Graphs are data: IDs, node types, single typed inputs and parameter tables.
-- Validate the entire graph before executing any node; nodes never apply writes.
function I.execute(graph)
  assert(type(graph) == "table" and type(graph.nodes) == "table", "graph.nodes required")
  local definitions, states, outputs, metadata = {}, {}, {}, {}
  for _, node in ipairs(A.nodes) do metadata[node.id] = node end
  for _, node in ipairs(graph.nodes) do
    assert(type(node.id) == "string" and node.id ~= "" and not definitions[node.id], "invalid or duplicate graph ID")
    assert(metadata[node.type] and handlers[node.type], "unknown node type: " .. tostring(node.type))
    assert(node.params == nil or type(node.params) == "table", "node params must be a table")
    definitions[node.id] = node
  end
  local function validate(id)
    assert(definitions[id], "missing graph node: " .. tostring(id))
    assert(states[id] ~= "visiting", "graph contains a cycle")
    if states[id] == "done" then return end
    states[id] = "visiting"
    local node, spec = definitions[id], metadata[definitions[id].type]
    if node.input then
      validate(node.input)
      assert(metadata[definitions[node.input].type].output == spec.input, "incompatible node ports")
    else assert(spec.input == "none", "node requires an input: " .. id) end
    validate_params(spec, node.params or {})
    states[id] = "done"
  end
  for _, node in ipairs(graph.nodes) do validate(node.id) end
  assert(definitions[graph.output], "graph.output must name a node")
  local function evaluate(id)
    if outputs[id] ~= nil then return outputs[id] end
    local node = definitions[id]
    outputs[id] = handlers[node.type](node.input and evaluate(node.input) or nil, node.params or {})
    return outputs[id]
  end
  return evaluate(graph.output)
end

local R = {}
local function input_rows(p, defaults)
  local rows = p.rows or (p.csv and N.csv(p.csv, p))
  assert(rows, "rows or csv required")
  return N.map(rows, {columns = p.columns or defaults.columns, types = defaults.types})
end
local function import(p, defaults)
  local rows = input_rows(p, defaults)
  local issues = N.required(rows, {fields = defaults.required})
  if #issues > 0 then return {changes = {}, unchanged = 0, errors = issues} end
  return N.upsert(rows, {table = defaults.table, keys = defaults.keys, existing_only = defaults.existing_only})
end
R.import_items = function(p)
  return import(p, {table = "items", keys = {"sku"}, required = {"sku", "name"},
    columns = {sku = "sku", name = "name", price = "price"}, types = {price = "number", unit_id = "integer", vat_rate_id = "integer"}})
end
R.import_counterparties = function(p)
  return import(p, {table = "counterparties", keys = {"tax_id", "kpp"}, required = {"name", "tax_id"},
    columns = {name = "name", tax_id = "tax_id", kpp = "kpp"}, types = {}})
end
R.import_rates = function(p)
  return import(p, {table = "rates", keys = {"currency_id", "date"}, required = {"currency_id", "date", "rate"},
    columns = {currency_id = "currency_id", date = "date", rate = "rate"}, types = {currency_id = "integer", rate = "number"}})
end
R.prices_update = function(p)
  return import(p, {table = "items", keys = {"sku"}, required = {"sku", "price"}, existing_only = true,
    columns = {sku = "sku", price = "price"}, types = {price = "number"}})
end
R.duplicates_items = function(p) return N.duplicates(ledger.fetch("ib", "items"), {keys = p.keys or {"sku"}}) end
R.duplicates_counterparties = function(p) return N.duplicates(ledger.fetch("ib", "counterparties"), {keys = p.keys or {"tax_id", "kpp"}}) end
R.duplicates_documents = function(p) return N.duplicates(ledger.fetch("ib", "documents"), {keys = p.keys or {"doc_type_id", "counterparty_id", "number", "date", "total"}}) end
R.check_required = function(p) return N.required(ledger.fetch("ib", assert(p.table)), p) end
R.fields_replace = function(p) return N.patch(N.filter(ledger.fetch("ib", assert(p.table)), p), p) end
R.report_stock = function(p) return N.stock(ledger.fetch("ib", "entries"), p) end
R.report_settlements = function(p) return N.settlements(ledger.fetch("ib", "entries"), p) end
R.check_month = function(p) return N.month(ledger.fetch("ib", "documents"), p) end

function I.preview(id, params)
  local run = assert(recipe_handlers[id], "unknown recipe: " .. tostring(id))
  params = params or {}
  for _, spec in ipairs(A.recipes) do if spec.id == id then validate_params(spec, params) end end
  return run(params)
end
function I.apply(plan)
  assert(type(plan) == "table" and type(plan.changes) == "table" and type(plan.errors) == "table", "a change plan is required")
  assert(#plan.errors == 0, "plan contains errors")
  return ledger.transaction(function()
    local seen = {}
    for _, change in ipairs(plan.changes) do
      assert(type(change.after) == "table", "change.after required")
      if change.id then
        local identity = change.table .. ":" .. change.id
        assert(not seen[identity], "duplicate change target")
        seen[identity] = true
        assert(change.before and same(ledger.find("ib", change.table, change.id), change.before), "stale change plan")
        assert(change.after.id == change.id, "record ID cannot change")
      else assert(change.before == nil and change.after.id == nil, "invalid insert plan") end
      if change.keys then
        local identity = "key:" .. change.table .. ":" .. assert(N.key(change.after, change.keys), "empty match key")
        assert(not seen[identity], "duplicate change key")
        seen[identity] = true
        local matches = N.match({change.after}, {table = change.table, keys = change.keys})
        assert((matches[1].existing and matches[1].existing.id) == change.id, "stale match key")
      end
    end
    local result = {}
    for _, change in ipairs(plan.changes) do
      local values = N.copy(change.after)
      values.id = nil
      result[#result + 1] = change.id and ledger.update(change.table, change.id, values) or ledger.insert(change.table, values)
    end
    return result
  end)
end
catalog.bind_api(I, A)
A.register(catalog, N, R)
return A

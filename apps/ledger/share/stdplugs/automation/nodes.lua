-- Pure Lua operations. All database access goes through the generated ledger API.
local N = {}
function N.source_text(_, p) return assert(p.text, "text required") end
function N.source_rows(_, p) return assert(p.rows, "rows required") end
function N.source_table(_, p) return ledger.fetch(p.db or "ib", assert(p.table)) end
local function copy(row)
  local out = {}
  for k, v in pairs(row) do out[k] = v end
  return out
end
local function blank(v) return v == nil or v == "" end
local function normalized(v)
  if v == nil then return "" end
  local text, out = tostring(v):match("^%s*(.-)%s*$"):lower(), {}
  for _, cp in utf8.codes(text) do
    if cp >= 0x410 and cp <= 0x42f then cp = cp + 32
    elseif cp == 0x401 then cp = 0x451 end
    out[#out + 1] = utf8.char(cp)
  end
  return table.concat(out)
end
local function key(row, fields)
  local parts = {}
  for _, field in ipairs(fields) do
    local v = normalized(row[field])
    if v == "" and field ~= "kpp" then return nil end
    parts[#parts + 1] = #v .. ":" .. v
  end
  return table.concat(parts, "|")
end
local function index(rows, fields)
  local out = {}
  for _, row in ipairs(rows) do
    local k = key(row, fields)
    if k then
      if out[k] ~= nil then out[k] = false else out[k] = row end
    end
  end
  return out
end
N.copy, N.key, N.normalized = copy, key, normalized

function N.csv(text, options)
  options = options or {}
  local delimiter = options.delimiter or ";"
  assert(#delimiter == 1 and delimiter ~= '"' and delimiter ~= "\r" and delimiter ~= "\n", "invalid CSV delimiter")
  text = text:gsub("^\239\187\191", "")
  local records, row, field, quoted, closed = {}, {}, {}, false, false
  local function cell() row[#row + 1] = table.concat(field); field, closed = {}, false end
  local function line() cell(); records[#records + 1] = row; row = {} end
  local i = 1
  while i <= #text do
    local c = text:sub(i, i)
    if quoted then
      if c == '"' then
        if text:sub(i + 1, i + 1) == '"' then field[#field + 1] = '"'; i = i + 1
        else quoted, closed = false, true end
      else field[#field + 1] = c end
    elseif c == '"' then
      assert(#field == 0 and not closed, "unexpected quote in CSV")
      quoted = true
    elseif c == delimiter then cell()
    elseif c == "\n" or c == "\r" then
      line()
      if c == "\r" and text:sub(i + 1, i + 1) == "\n" then i = i + 1 end
    else
      assert(not closed, "characters after closing CSV quote")
      field[#field + 1] = c
    end
    i = i + 1
  end
  assert(not quoted, "unterminated CSV quote")
  if #field > 0 or #row > 0 or closed then line() end
  if #records == 0 then return {} end
  local headers, seen, out = records[1], {}, {}
  for _, header in ipairs(headers) do
    assert(header ~= "" and not seen[header], "empty or duplicate CSV header")
    seen[header] = true
  end
  for r = 2, #records do
    assert(#records[r] == #headers, "CSV column count mismatch at row " .. r)
    local obj = {}
    for c, header in ipairs(headers) do obj[header] = records[r][c] end
    out[#out + 1] = obj
  end
  return out
end

function N.map(rows, options)
  local out = {}
  for _, row in ipairs(rows) do
    local mapped = {}
    for source, target in pairs(assert(options.columns, "columns required")) do
      assert(row[source] ~= nil, "missing input column: " .. source)
      local v = row[source]
      local convert = (options.types or {})[target]
      if convert == "number" or convert == "integer" then
        if type(v) == "string" then v = v:gsub("%s", ""):gsub(",", ".") end
        v = tonumber(v)
        assert(v and v == v and math.abs(v) < math.huge, "invalid numeric column: " .. source)
        if convert == "integer" then v = assert(math.tointeger(v), "integer required: " .. source) end
      elseif type(v) == "string" then v = v:match("^%s*(.-)%s*$") end
      mapped[target] = v
    end
    out[#out + 1] = mapped
  end
  return out
end

function N.required(rows, options)
  local issues = {}
  for i, row in ipairs(rows) do
    for _, field in ipairs(assert(options.fields, "fields required")) do
      if blank(row[field]) or (type(row[field]) == "string" and normalized(row[field]) == "") then
        issues[#issues + 1] = {row = i, id = row.id, field = field, code = "required"}
      end
    end
  end
  return issues
end

function N.filter(rows, options)
  local out = {}
  for _, row in ipairs(rows) do
    local keep = true
    for field, value in pairs(options.equals or {}) do if row[field] ~= value then keep = false end end
    if keep then out[#out + 1] = copy(row) end
  end
  return out
end

function N.group(rows, options)
  local groups, order = {}, {}
  for _, row in ipairs(rows) do
    local k = assert(key(row, options.keys), "group key is empty")
    if not groups[k] then groups[k] = {key = k, count = 0, total = 0}; order[#order + 1] = groups[k] end
    local g = groups[k]
    g.count = g.count + 1
    if options.sum then g.total = g.total + assert(tonumber(row[options.sum]), "sum requires numbers") end
  end
  return order
end

function N.match(rows, options)
  local by = index(ledger.fetch("ib", assert(options.table)), assert(options.keys))
  local out = {}
  for _, row in ipairs(rows) do
    local k = key(row, options.keys)
    assert(k, "empty match key")
    assert(by[k] ~= false, "ambiguous match key: " .. k)
    out[#out + 1] = {input = copy(row), existing = by[k] and copy(by[k]) or nil}
  end
  return out
end

function N.upsert(rows, options)
  local matched = N.match(rows, options)
  local plan = {changes = {}, unchanged = 0, errors = {}}
  local seen = {}
  for i, pair in ipairs(matched) do
    local k = key(pair.input, options.keys)
    assert(not seen[k], "duplicate input key at row " .. i)
    seen[k] = true
    if not pair.existing and options.existing_only then
      plan.errors[#plan.errors + 1] = {row = i, code = "not_found", key = k}
    else
      local after = pair.existing and copy(pair.existing) or {}
      local changed = not pair.existing
      for field, value in pairs(pair.input) do
        assert(field ~= "id", "input cannot supply IDs")
        if after[field] ~= value then changed = true end
        after[field] = value
      end
      if changed then
        local fields = copy(after)
        fields.id = nil
        local valid, checked = pcall(ledger.validate, options.table, fields, pair.existing and pair.existing.id)
        if not valid then
          plan.errors[#plan.errors + 1] = {row = i, code = "invalid_record", message = tostring(checked)}
        else
        after = checked
        if not pair.existing then after.id = nil end
        plan.changes[#plan.changes + 1] = {table = options.table, id = pair.existing and pair.existing.id,
          before = pair.existing, after = after, keys = options.keys, key = k}
        end
      else plan.unchanged = plan.unchanged + 1 end
    end
  end
  return plan
end

function N.patch(rows, options)
  local plan = {changes = {}, unchanged = 0, errors = {}}
  assert(options.field ~= "id" and options.value ~= nil, "field/value required; id is protected")
  local schema = ledger.schema("ib", options.table)
  assert(schema.writable, "table is read-only for automation")
  local found = false
  for _, f in ipairs(schema.fields) do if f.name == options.field then found = true end end
  assert(found, "unknown patch field")
  for _, row in ipairs(rows) do
    local before = assert(ledger.find("ib", options.table, row.id), "record not found")
    local after = copy(before)
    after[options.field] = options.value
    local fields = copy(after)
    fields.id = nil
    after = ledger.validate(options.table, fields, row.id)
    if before[options.field] ~= options.value then
      plan.changes[#plan.changes + 1] = {table = options.table, id = row.id, before = before, after = after}
    else plan.unchanged = plan.unchanged + 1 end
  end
  return plan
end

function N.duplicates(rows, options)
  local groups, out = {}, {}
  for _, row in ipairs(rows) do
    local k = key(row, options.keys)
    if k then
      if not groups[k] then groups[k] = {key = k, ids = {}} end
      local g = groups[k]
      g.ids[#g.ids + 1] = row.id
      if #g.ids == 2 then out[#out + 1] = g end
    end
  end
  return out
end

local function within(date, options)
  return (not options.date_from or date >= options.date_from) and (not options.date_to or date <= options.date_to)
end
local function account_set(options)
  local codes, out, kinds = {}, {}, {}
  for _, kind in ipairs(ledger.fetch("cfg", "subconto_kinds")) do kinds[kind.id] = kind.table end
  for _, code in ipairs(assert(options.accounts, "accounts required")) do codes[code] = true end
  for _, account in ipairs(ledger.fetch("cfg", "accounts")) do
    if codes[account.code] then
      assert(not options.analytics or kinds[account.subconto_kind_id] == options.analytics, "account has incompatible analytics: " .. account.code)
      out[account.id] = true; codes[account.code] = nil
    end
  end
  assert(next(codes) == nil, "unknown account code")
  return out
end
function N.stock(entries, options)
  local selected = copy(options)
  selected.analytics = "items"
  local accounts, by, out = account_set(selected), {}, {}
  local function add(id, qty, amount)
    if id == 0 then return end
    if not by[id] then by[id] = {item_id = id, quantity = 0, amount = 0}; out[#out + 1] = by[id] end
    by[id].quantity = by[id].quantity + qty
    by[id].amount = by[id].amount + amount
  end
  for _, e in ipairs(entries) do
    if not options.date_to or e.date <= options.date_to then
      if accounts[e.debit_id] then add(e.debit_subconto_id, e.quantity, e.amount) end
      if accounts[e.credit_id] then add(e.credit_subconto_id, -e.quantity, -e.amount) end
    end
  end
  for _, row in ipairs(out) do row.name = ledger.field("ib", "items", row.item_id, "name"); row.negative = row.quantity < -0.000001 end
  table.sort(out, function(a, b) return a.item_id < b.item_id end)
  return out
end
function N.settlements(entries, options)
  local selected = copy(options)
  selected.analytics = "counterparties"
  local accounts, by, out = account_set(selected), {}, {}
  local function add(id, amount, date, debit)
    if id == 0 or (options.counterparty_id and options.counterparty_id ~= id) then return end
    if not by[id] then
      by[id] = {counterparty_id = id, opening = 0, debit = 0, credit = 0}; out[#out + 1] = by[id]
    end
    local r = by[id]
    if options.date_from and date < options.date_from then r.opening = r.opening + amount
    elseif debit then r.debit = r.debit + amount else r.credit = r.credit - amount end
  end
  for _, e in ipairs(entries) do
    if not options.date_to or e.date <= options.date_to then
      if accounts[e.debit_id] then add(e.debit_subconto_id, e.amount, e.date, true) end
      if accounts[e.credit_id] then add(e.credit_subconto_id, -e.amount, e.date, false) end
    end
  end
  for _, r in ipairs(out) do
    r.closing = r.opening + r.debit - r.credit
    r.name = ledger.field("ib", "counterparties", r.counterparty_id, "name")
  end
  table.sort(out, function(a, b) return a.counterparty_id < b.counterparty_id end)
  return out
end
function N.month(documents, options)
  local issues = {}
  for _, doc in ipairs(documents) do
    if within(doc.date, options) then
      if not doc.posted then issues[#issues + 1] = {id = doc.id, code = "unposted"} end
      if doc.currency_id ~= 0 and doc.rate <= 0 and ledger.rate(doc.currency_id, doc.date) <= 0 then
        issues[#issues + 1] = {id = doc.id, code = "missing_rate"}
      end
    end
  end
  return issues
end
function N.export_csv(rows, options)
  local columns = assert(options.columns, "columns required")
  local delimiter = options.delimiter or ";"
  assert(#delimiter == 1 and delimiter ~= '"' and delimiter ~= "\r" and delimiter ~= "\n", "invalid delimiter")
  local function quote(value) return '"' .. tostring(value == nil and "" or value):gsub('"', '""') .. '"' end
  local lines, header = {}, {}
  for _, field in ipairs(columns) do header[#header + 1] = quote(field) end
  lines[1] = table.concat(header, delimiter)
  for _, row in ipairs(rows) do
    local cells = {}
    for _, field in ipairs(columns) do cells[#cells + 1] = quote(row[field]) end
    lines[#lines + 1] = table.concat(cells, delimiter)
  end
  return table.concat(lines, "\r\n") .. "\r\n"
end
return N

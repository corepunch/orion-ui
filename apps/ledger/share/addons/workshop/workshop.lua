-- Operational workshop commands. This module owns no Ledger balances or payroll data.
local Workshop = {}

local function check(ok, why)
  if not ok then error(why, 0) end
end

local function object(value, fields)
  check(type(value) == "table" and getmetatable(value) == nil, "expected object")
  for key in pairs(value) do check(fields[key] == true, "unexpected field: " .. tostring(key)) end
end

local function fields(names)
  local keys = {}
  for name in names:gmatch("%S+") do keys[name] = true end
  return keys
end

local function text(value, name, limit)
  check(type(value) == "string" and #value <= (limit or 160) and utf8.len(value)
    and value:find("%S") and not value:find("%c"), "invalid " .. name)
  return value
end

local function id(value, name)
  value = text(value, name, 64)
  check(value:match("^[%w][%w_.:%-]*$"), "invalid " .. name)
  return value
end

local function int(value, lo, hi, name)
  check(math.type(value) == "integer" and value >= lo and value <= hi, "invalid " .. name)
  return value
end

-- Units are UTC epoch minutes (minute boundaries, no host time-zone conversion).
local MAX_TIME = 68374080 -- 2100-01-01 UTC
local MAX_TOTAL = 9000000000000
local MAX_HISTORY = 1000
local MAX_JOURNAL_BYTES = 1048575

local function interval(first, last, earliest)
  int(first, earliest or 0, MAX_TIME, "start_min")
  int(last, first + 1, math.min(first + 44640, MAX_TIME), "end_min")
end

local function list(value, min, max, name)
  check(type(value) == "table" and #value >= min and #value <= max, "invalid " .. name)
  local n = 0
  for k in pairs(value) do int(k, 1, #value, name); n = n + 1 end
  check(n == #value, "invalid " .. name)
end

local function lookup(table_, value, name)
  local key = id(value, name)
  check(table_[key] ~= nil, "unknown " .. name)
  return table_[key]
end

local function new_key(table_, value, name)
  local key = id(value, name)
  check(table_[key] == nil, "duplicate " .. name)
  return key
end

local function overlap(a, b)
  return a.start_min < b.end_min and b.start_min < a.end_min
end

local function normalize_vin(vin)
  vin = text(vin, "vin", 17):upper()
  check(#vin == 17 and not vin:find("[^A-HJ-NPR-Z0-9]"), "invalid vin")
  return vin
end

local function registration(a)
  if a.plate == nil and a.jurisdiction == nil then return nil end
  local jurisdiction = text(a.jurisdiction, "jurisdiction", 16):upper()
  check(jurisdiction:match("^[A-Z][A-Z0-9%-]+$"), "invalid jurisdiction")
  local plate = text(a.plate, "plate", 40)
  local normalized = plate:upper():gsub("[%s%-]", "")
  check(#normalized > 0, "invalid plate")
  return { plate = plate, jurisdiction = jurisdiction, normalized = normalized }
end

local commands = {}
local function command(name, keys, perform)
  commands[name] = { keys = fields(keys), perform = perform }
end

command("vehicle.register", "id owner_ref vin plate jurisdiction make model year odometer_km", function(s, a, req)
  local vehicle_id = new_key(s.vehicles, a.id, "vehicle id")
  local vin = a.vin ~= nil and normalize_vin(a.vin) or nil
  check(not vin or not s.vins[vin], "vin already registered")
  local current = registration(a)
  local vehicle = {
    id = vehicle_id,
    owner_ref = id(a.owner_ref, "owner reference"),
    vin = vin, registration = current,
    make = text(a.make, "make"), model = text(a.model, "model"),
    year = int(a.year, 1886, 2100, "year"),
    registration_history = s.array(), odometer_history = s.array(),
  }
  if a.odometer_km ~= nil then
    vehicle.odometer_km = int(a.odometer_km, 0, 10000000, "odometer_km")
    vehicle.odometer_history[1] = { km = vehicle.odometer_km, at_min = req.at_min, actor = req.actor }
  end
  if current then
    vehicle.registration_history[1] = { registration = current, at_min = req.at_min, actor = req.actor }
  end
  s.vehicles[vehicle_id] = vehicle
  if vin then s.vins[vin] = vehicle_id end
  return { id = vehicle_id }
end)

command("vehicle.registration", "id plate jurisdiction reason", function(s, a, req)
  local vehicle = lookup(s.vehicles, a.id, "vehicle")
  local next_registration = registration(a)
  local event = { registration = next_registration, at_min = req.at_min, actor = req.actor,
    reason = text(a.reason, "reason", 400) }
  vehicle.registration = next_registration
  vehicle.registration_history[#vehicle.registration_history + 1] = event
  return { id = vehicle.id }
end)

command("vehicle.odometer", "id km reason", function(s, a, req)
  local vehicle = lookup(s.vehicles, a.id, "vehicle")
  local km = int(a.km, 0, 10000000, "km")
  if vehicle.odometer_km and km < vehicle.odometer_km then text(a.reason, "correction reason", 400) end
  local event = { km = km, at_min = req.at_min, actor = req.actor,
    reason = a.reason ~= nil and text(a.reason, "reason", 400) or nil }
  vehicle.odometer_km = km
  vehicle.odometer_history[#vehicle.odometer_history + 1] = event
  return { id = vehicle.id, km = km }
end)

command("resource.register", "id name kind", function(s, a)
  local resource = new_key(s.resources, a.id, "resource id")
  check(a.kind == "bay" or a.kind == "worker" or a.kind == "equipment", "invalid resource kind")
  s.resources[resource] = { id = resource, kind = a.kind, name = text(a.name, "resource name") }
  return { id = resource }
end)

local function conflicts(state, slot, except_slot)
  for key, existing in pairs(state.slots) do
    if key ~= except_slot and existing.active and overlap(slot, existing) then
      for _, resource in ipairs(existing.resources) do
        check(not slot.resource_set[resource], "resource unavailable: " .. resource)
      end
    end
  end
end

command("resource.block", "id resource start_min end_min reason", function(s, a, req)
  local key = "block:" .. id(a.id, "block id")
  check(not s.slots[key], "duplicate block id")
  local resource = lookup(s.resources, a.resource, "resource")
  interval(a.start_min, a.end_min, req.at_min)
  local slot = { id = key, start_min = a.start_min, end_min = a.end_min, active = true, kind = "unavailable",
    resources = s.array({ resource.id }), resource_set = { [resource.id] = true },
    reason = text(a.reason, "reason", 400) }
  conflicts(s, slot)
  s.slots[key] = slot
  return { id = key }
end)

command("resource.unblock", "id reason", function(s, a)
  local slot = s.slots["block:" .. id(a.id, "block id")]
  check(slot and slot.active, "slot is not an active block")
  slot.released_reason = text(a.reason, "reason", 400)
  slot.active = false
  return { id = slot.id }
end)

command("order.open", "id vehicle complaint", function(s, a, req)
  local key = new_key(s.orders, a.id, "work order id")
  local vehicle = lookup(s.vehicles, a.vehicle, "vehicle")
  s.orders[key] = { id = key, vehicle = vehicle.id, owner_ref = vehicle.owner_ref,
    status = "intake", opened_min = req.at_min, complaint = text(a.complaint, "complaint", 1000),
    estimates = s.array(), time = s.array(), actual_parts = {} }
  return { id = key, status = "intake" }
end)

local line_fields = fields("kind reference description quantity unit_minor")

command("order.quote", "id currency lines", function(s, a)
  local order = lookup(s.orders, a.id, "work order")
  check(order.status == "intake" or order.status == "quoted", "work order cannot be quoted")
  list(a.lines, 1, 100, "estimate lines")
  local currency = text(a.currency, "currency", 3)
  check(currency:match("^[A-Z][A-Z][A-Z]$"), "currency must be three uppercase letters")
  local lines, total, seen = s.array(), 0, {}
  for _, line in ipairs(a.lines) do
    object(line, line_fields)
    check(line.kind == "part" or line.kind == "labor", "invalid estimate line kind")
    local reference = id(line.reference, "line reference")
    local key = line.kind .. ":" .. reference
    check(not seen[key], "duplicate estimate line reference")
    seen[key] = true
    local qty = int(line.quantity, 1, 100000, "quantity")
    local unit = int(line.unit_minor, 0, MAX_TOTAL // qty, "unit_minor")
    local subtotal = unit * qty
    check(total <= MAX_TOTAL - subtotal, "estimate total limit")
    total = total + subtotal
    lines[#lines + 1] = { kind = line.kind, reference = reference,
      description = text(line.description, "line description", 400), quantity = qty,
      unit_minor = unit, subtotal_minor = subtotal }
  end
  local num = #order.estimates + 1
  order.estimates[num] = { version = num, currency = currency, lines = lines, quoted_minor = total }
  order.status = "quoted"
  return { id = order.id, estimate_version = num, quoted_minor = total, currency = currency }
end)

command("order.approve", "id estimate_version", function(s, a)
  local order = lookup(s.orders, a.id, "work order")
  check(order.status == "quoted" and int(a.estimate_version, 1, MAX_HISTORY, "estimate_version")
    == #order.estimates, "quote is not current")
  order.approved = a.estimate_version
  order.status = "approved"
  return { id = order.id, status = order.status }
end)

command("order.schedule", "id start_min end_min resources", function(s, a, req)
  local order = lookup(s.orders, a.id, "work order")
  check(order.status == "approved" or order.status == "scheduled", "work order cannot be scheduled")
  interval(a.start_min, a.end_min, req.at_min)
  list(a.resources, 1, 16, "resources")
  local resources, resource_set, worker = s.array(), {}, false
  for _, rid in ipairs(a.resources) do
    local resource = lookup(s.resources, rid, "resource")
    check(not resource_set[rid], "duplicate resource")
    resources[#resources + 1], resource_set[rid] = rid, true
    worker = worker or resource.kind == "worker"
  end
  check(worker, "at least one worker is required")
  local slot_id = "order:" .. order.id
  local slot = { id = slot_id, start_min = a.start_min, end_min = a.end_min, active = true,
    kind = "appointment", resources = resources, resource_set = resource_set }
  conflicts(s, slot, slot_id)
  order.status, order.appointment = "scheduled", slot_id
  s.slots[slot_id] = slot
  return { id = order.id, status = order.status, appointment = slot_id }
end)

command("order.start", "id", function(s, a, req)
  local order = lookup(s.orders, a.id, "work order")
  check(order.status == "scheduled", "work order is not scheduled")
  order.status, order.started_min = "in_progress", req.at_min
  return { id = order.id, status = order.status }
end)

local function time_entry(s, a, req, existing)
  local order = lookup(s.orders, a.id, "work order")
  check(order.status == "in_progress", "work order is not in progress")
  local worker = lookup(s.resources, a.worker, "worker")
  local slot = s.slots[order.appointment]
  check(worker.kind == "worker" and slot.resource_set[worker.id], "worker is not assigned to this work order")
  interval(a.start_min, a.end_min, order.started_min)
  check(a.end_min <= req.at_min, "time entry ends in the future")
  local recorded = a.end_min - a.start_min
  local billable = int(a.billable_min, 0, recorded, "billable_min")
  for _, item in pairs(s.time) do
    if item ~= existing and item.worker == worker.id and item.current then
      check(not overlap(a, item), "overlapping worker time")
    end
  end
  return order, { start_min = a.start_min, end_min = a.end_min, recorded_min = recorded,
    billable_min = billable, worker = worker.id, order = order.id, current = true }
end

command("time.log", "id entry worker start_min end_min billable_min", function(s, a, req)
  local key = new_key(s.time, a.entry, "time entry id")
  local order, row = time_entry(s, a, req)
  row.entry = key
  s.time[key] = row
  order.time[#order.time + 1] = key
  return { id = order.id, entry = key, recorded_min = row.recorded_min, billable_min = row.billable_min }
end)

command("time.correct", "id entry replacement worker start_min end_min billable_min reason", function(s, a, req)
  local old = lookup(s.time, a.entry, "time entry")
  check(old.current and old.order == a.id, "time entry not current in this work order")
  local key = new_key(s.time, a.replacement, "replacement time entry id")
  local order, row = time_entry(s, a, req, old)
  row.reason = text(a.reason, "correction reason", 400)
  row.entry, row.corrects = key, old.entry
  old.current, old.replaced_by = false, key
  s.time[key] = row
  order.time[#order.time + 1] = key
  return { id = order.id, entry = key, corrects = old.entry }
end)

command("parts.record", "id reference quantity reason", function(s, a, req)
  local order = lookup(s.orders, a.id, "work order")
  check(order.status == "in_progress", "work order is not in progress")
  local reference = id(a.reference, "part reference")
  local approved
  for _, line in ipairs(order.estimates[order.approved].lines) do
    if line.kind == "part" and line.reference == reference then approved = line.quantity; break end
  end
  check(approved ~= nil, "part is not in the approved estimate")
  local quantity = int(a.quantity, 0, approved, "actual part quantity")
  local reason = text(a.reason, "reason", 400)
  order.actual_parts[reference] = { quantity = quantity, reason = reason, at_min = req.at_min, actor = req.actor }
  return { id = order.id, reference = reference, quantity = quantity }
end)

command("order.cancel", "id reason", function(s, a)
  local order = lookup(s.orders, a.id, "work order")
  check(order.status ~= "completed" and order.status ~= "cancelled", "work order is closed")
  order.cancel_reason = text(a.reason, "reason", 400)
  order.status = "cancelled"
  if order.appointment then s.slots[order.appointment].active = false end
  return { id = order.id, status = order.status }
end)

command("order.complete", "id inspection", function(s, a)
  local order = lookup(s.orders, a.id, "work order")
  check(order.status == "in_progress", "work order is not in progress")
  local quote = order.estimates[order.approved]
  local inspection = text(a.inspection, "inspection", 1000)
  local intent = { id = "workshop:" .. order.id, source = order.id, status = "review_required",
    currency = quote.currency, quoted_minor = quote.quoted_minor, estimate_version = quote.version,
    owner_ref = order.owner_ref, lines = quote.lines, actual_parts = order.actual_parts }
  order.inspection, order.intent, order.status = inspection, intent, "completed"
  return { id = order.id, status = order.status, invoice_intent = intent.id }
end)

local request_fields = fields("request_id expected_revision actor at_min command args")

function Workshop.open(json, entries)
  local function copy(value)
    if type(value) ~= "table" then return value end
    local clone = setmetatable({}, getmetatable(value))
    for key, entry in pairs(value) do clone[key] = copy(entry) end
    return clone
  end
  local s = { vehicles = {}, vins = {}, orders = {}, resources = {}, slots = {}, time = {}, array = json.array }
  local journal, requests = json.array(), {}
  local journal_bytes = 2
  local api = {}

  function api.apply(req)
    local fingerprint, handler, rid
    local ok, result = pcall(function()
      object(req, request_fields)
      rid = id(req.request_id, "request_id")
      local action = id(req.command, "command")
      handler = commands[action]
      check(handler ~= nil, "unknown command")
      object(req.args, handler.keys)
      int(req.expected_revision, 0, MAX_HISTORY, "expected_revision")
      id(req.actor, "actor reference")
      int(req.at_min, 0, MAX_TIME, "at_min")
      fingerprint = json.encode(req)
      if requests[rid] then
        check(requests[rid].fingerprint == fingerprint, "request_id already used with different data")
        return copy(requests[rid].receipt)
      end
      check(req.expected_revision == #journal, "stale revision")
      check(#journal == 0 or req.at_min >= journal[#journal].at_min, "command time precedes the previous command")
      check(#journal < MAX_HISTORY and journal_bytes + #fingerprint + 1 <= MAX_JOURNAL_BYTES,
        "journal limit reached")
      local stable = json.decode(fingerprint)
      local change = handler.perform(s, stable.args, stable)
      journal[#journal + 1] = stable
      journal_bytes = journal_bytes + #fingerprint + (#journal == 1 and 0 or 1)
      change.revision = #journal
      requests[rid] = { fingerprint = fingerprint, receipt = change }
      return copy(change)
    end)
    if not ok then return nil, tostring(result) end
    return result
  end

  function api.journal() return copy(journal) end
  function api.snapshot()
    local slots = {}
    for key, slot in pairs(s.slots) do
      slots[key] = { id = slot.id, start_min = slot.start_min, end_min = slot.end_min, active = slot.active,
        kind = slot.kind, resources = slot.resources, reason = slot.reason, released_reason = slot.released_reason }
    end
    return copy({ revision = #journal, vehicles = s.vehicles, resources = s.resources,
      orders = s.orders, slots = slots, time = s.time })
  end

  function api.find_vehicle(query)
    object(query, fields("owner_ref vin plate jurisdiction"))
    check(query.owner_ref or query.vin or query.plate or query.jurisdiction, "missing search criteria")
    local vin = query.vin ~= nil and normalize_vin(query.vin)
    local owner = query.owner_ref ~= nil and id(query.owner_ref, "owner reference")
    local reg = registration(query)
    local result = json.array()
    for _, vehicle in pairs(s.vehicles) do
      local vreg = vehicle.registration
      if (not vin or vehicle.vin == vin) and (not owner or vehicle.owner_ref == owner)
        and (not reg or (vreg and reg.normalized == vreg.normalized and reg.jurisdiction == vreg.jurisdiction)) then
        result[#result + 1] = vehicle
      end
    end
    table.sort(result, function(a, b) return a.id < b.id end)
    return copy(result)
  end

  function api.history(vehicle_id)
    local vehicle = lookup(s.vehicles, vehicle_id, "vehicle")
    local orders = json.array()
    for _, order in pairs(s.orders) do
      if order.vehicle == vehicle.id then orders[#orders + 1] = order end
    end
    table.sort(orders, function(a, b)
      if a.opened_min == b.opened_min then return a.id < b.id end
      return a.opened_min < b.opened_min
    end)
    return copy({ vehicle = vehicle, orders = orders })
  end

  function api.activity(first, last)
    interval(first, last)
    local function minutes(entry)
      return math.max(0, math.min(last, entry.end_min) - math.max(first, entry.start_min))
    end
    local resources, open_orders = {}, 0
    for key, resource in pairs(s.resources) do
      resources[key] = { scheduled_min = 0, unavailable_min = 0 }
      if resource.kind == "worker" then resources[key].actual_min = 0 end
    end
    for _, slot in pairs(s.slots) do
      if slot.active then
        for _, rid in ipairs(slot.resources) do
          local data = resources[rid]
          local column = slot.kind == "appointment" and "scheduled_min" or "unavailable_min"
          data[column] = data[column] + minutes(slot)
        end
      end
    end
    for _, entry in pairs(s.time) do
      if entry.current then resources[entry.worker].actual_min = resources[entry.worker].actual_min + minutes(entry) end
    end
    for _, order in pairs(s.orders) do
      if order.status ~= "completed" and order.status ~= "cancelled" then open_orders = open_orders + 1 end
    end
    return { start_min = first, end_min = last, resources = resources, open_orders_now = open_orders }
  end

  if entries ~= nil then
    list(entries, 0, MAX_HISTORY, "journal")
    check(entries ~= json.null and json.encode(entries):sub(1, 1) == "[",
      "expected journal array")
    for _, entry in ipairs(entries) do
      local count = #journal
      local receipt, err = api.apply(entry)
      check(receipt and #journal == count + 1, "invalid journal entry " .. (count + 1) .. ": " .. (err or "repeated request"))
    end
  end
  return api
end

return Workshop

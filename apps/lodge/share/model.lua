local Hotel = require("hotel")
local orion = require("orion")
local dbmodel = require("orion.model")
local history = dbmodel:extend("lodge.reservation_history")
local payments = dbmodel:extend("lodge.payment_events")
local db = require("orion.db")
local money = require("orion.plugins").use("money", 1)
local labels = {reserved = "Reserved", checked_in = "Checked in", checked_out = "Checked out", cancelled = "Cancelled"}
local cleaning_labels = {inspection_needed = "Inspection needed", dirty = "Dirty", clean = "Clean"}
local Model = {}
local writing_history = false
local fields = {"room_id", "guest_id", "group_id", "arrival", "departure", "persons", "rate", "extras", "paid", "status", "notes"}
local function snapshot(row)
  if not row then return "" end
  local values = {}
  for _, field in ipairs(fields) do
    local value = tostring(row[field] or "")
    values[#values + 1] = field .. "=" .. #value .. ":" .. value
  end
  return table.concat(values, ";")
end
function Model:validate(table, row, previous)
  if table == "hotel_settings" then return Hotel.hotel_settings.validate(row, previous) end
  if (table == "reservations" or table == "night_rates" or table == "reservation_groups" or table == "day_closes") and #Hotel.hotel_settings.model:all() == 0 then
    return nil, orion.tr("Configure the hotel country and currency before recording reservations or payments.")
  end
  if table == "day_closes" then return Hotel.day_closes.validate(row, previous) end
  if table == "group_settlements" then return Hotel.group_settlements.validate(row, previous) end
  if (table == "reservations" or table == "night_rates" or table == "reservation_groups") and Hotel.day_closes.model:find({day = orion.today()}) then
    return nil, orion.tr("Today is closed; reservation and payment changes resume tomorrow.")
  end
  if table == "reservation_history" or table == "payment_events" then
    if previous or not writing_history then return nil, orion.tr("History records cannot be changed directly.") end
    return true
  end
  if table == "reservations" and row.paid ~= (previous and previous.paid or 0) and not Hotel.payment_request then
    return nil, orion.tr("Record payments and refunds using the payment action.")
  end
  local records = Hotel[table]
  if records then return records.validate(row, previous) end
  return true
end
function Model:validate_delete(table, previous)
  if table == "hotel_settings" then return nil, orion.tr("Hotel currency settings cannot be changed directly.") end
  if table == "day_closes" then return nil, orion.tr("Day close records cannot be changed directly.") end
  if table == "group_settlements" then return nil, orion.tr("Group settlement records cannot be changed directly.") end
  if table == "night_rates" then
    if #Hotel.hotel_settings.model:all() == 0 then return nil, orion.tr("Configure the hotel country and currency before recording reservations or payments.") end
    if Hotel.day_closes.model:find({day = orion.today()}) then return nil, orion.tr("Today is closed; reservation and payment changes resume tomorrow.") end
    local reservation = previous and Hotel.reservations.model:find(previous.reservation_id)
    if not reservation then return nil, orion.tr("Choose a reservation.") end
    if reservation.status == "checked_out" or reservation.status == "cancelled" then return nil, orion.tr("This reservation is closed.") end
    return true
  end
  if table == "reservation_history" or table == "payment_events" or table == "reservations" then
    return nil, orion.tr("History records and reservations cannot be deleted.")
  end
  return true
end
function Model:initialize(database)
  if database ~= "lodge" then return true end
  writing_history = true
  local ok, result, message = pcall(function()
    for _, row in ipairs(Hotel.reservations.model:all()) do
      local tracked = false
      for _, item in ipairs(history:select({reservation_id = row.id})) do
        if item.operation == "insert" or item.operation == "opening_balance" then tracked = true; break end
      end
      if not tracked then
        if not Hotel.valid_amount(row.paid) then return nil, orion.tr("An existing paid amount has fractional cents or is out of range; reconcile it before upgrading.") end
        local saved, refusal = payments:create({reservation_id = row.id, amount_minor = money.minor(row.paid), kind = "opening_balance", actor = "migration", request_key = ""})
        if not saved then return nil, refusal end
        saved, refusal = history:create({reservation_id = row.id, operation = "opening_balance", actor = "migration", before = "", after = snapshot(row)})
        if not saved then return nil, refusal end
      end
    end
    return true
  end)
  writing_history = false
  if not ok then error(result, 0) end
  return result, message
end
function Model:after_write(table, operation, current, previous)
  if table == "night_rates" then
    local night = current or previous
    local function detail(value)
      return value and (value.night .. ":" .. value.amount_minor) or ""
    end
    writing_history = true
    local ok, saved, refusal = pcall(function()
      return history:create({reservation_id = night.reservation_id, operation = "night_" .. operation, actor = db.actor("lodge"),
        before = detail(previous), after = detail(current)})
    end)
    writing_history = false
    if not ok then error(saved, 0) end
    if not saved then return nil, refusal end
    return true
  end
  if table ~= "reservations" then return true end
  local row = current or previous
  writing_history = true
  local ok, result, message = pcall(function()
    local saved, refusal = history:create({reservation_id = row.id, operation = operation, actor = db.actor("lodge"),
      before = snapshot(previous), after = snapshot(current)})
    if not saved then return nil, refusal end
    local delta = money.minor(current and current.paid or 0) - money.minor(previous and previous.paid or 0)
    if delta ~= 0 then
      saved, refusal = payments:create({reservation_id = row.id, amount_minor = delta, kind = delta > 0 and "payment" or "refund", actor = db.actor("lodge"), posted_on = orion.today(),
        request_key = Hotel.payment_request or ""})
      if not saved then return nil, refusal end
    end
    if previous and current and ((previous.status ~= "checked_out" and current.status == "checked_out") or
      (previous.status == "checked_in" and current.room_id ~= previous.room_id)) then
      saved, refusal = Hotel.rooms:save(previous.room_id, {cleaning_state = "dirty"})
      if not saved then return nil, refusal end
    end
    return true
  end)
  writing_history = false
  if not ok then error(result, 0) end
  return result, message
end
function Model:compute(table, row, property)
  if table == "rooms" and property == "cleaning_label" then return orion.tr(cleaning_labels[row.cleaning_state] or row.cleaning_state) end
  if table == "rooms" and property == "occupancy_label" then return orion.tr(Hotel.room_occupied(row.id) and "Guest checked in" or "No guest checked in") end
  if table == "day_closes" and ({net_display = true, outstanding_display = true, cancelled_paid_display = true, cancelled_count_display = true})[property] then
    return require("remote.close_display")(row, property)
  end
  if table ~= "reservations" then return nil end
  if property == "total" then return Hotel.total(row) end
  if property == "balance" then return Hotel.balance(row) end
  if property == "status_label" then return orion.tr(labels[row.status] or row.status) end
end
return Model

-- Hotel rules stay in Lodge. All reusable operations come from small plugins.
local orion = require("orion")
local Plugins = require("orion.plugins")
local plugins = Plugins.load({records = 1, calendar = 1, intervals = 1, money = 1, template = 1})
local records, calendar, intervals, money = plugins.records, plugins.calendar, plugins.intervals, plugins.money
local Hotel = {}

local function valid_amount(value)
  return type(value) == "number" and value == value and value >= 0 and value <= 1000000 and
    math.abs(value * 100 - math.floor(value * 100 + 0.5)) < 1e-7
end
Hotel.valid_amount = valid_amount

local function named(row)
  if not row.name or not row.name:find("%S") then return nil, orion.tr("Enter a name.") end
  return true
end
Hotel.rooms = records.bind("lodge.rooms", function(row, previous)
  local ok, message = named(row)
  if not ok then return nil, message end
  for _, other in ipairs(Hotel.rooms.model:all()) do
    if other.id ~= row.id and other.name == row.name then return nil, orion.tr("A room with this name already exists.") end
  end
  if not row.capacity or row.capacity < 1 or row.capacity % 1 ~= 0 then return nil, orion.tr("Capacity must be a positive integer.") end
  if not valid_amount(row.rate) then return nil, orion.tr("Enter an amount from 0 to 1000000 with at most two decimal places.") end
  if previous and row.capacity < previous.capacity then
    for _, reservation in ipairs(Hotel.reservations.model:select({room_id = row.id})) do
      if (reservation.status == "reserved" or reservation.status == "checked_in") and reservation.persons > row.capacity then
        return nil, orion.tr("The guest count exceeds the room capacity.")
      end
    end
  end
  if not ((not previous and row.cleaning_state == nil) or ({inspection_needed = true, dirty = true, clean = true})[row.cleaning_state]) then
    return nil, orion.tr("Choose a valid room cleaning status.")
  end
  return true
end)
Hotel.guests = records.bind("lodge.guests", named)
function Hotel.room_occupied(id)
  for _, row in ipairs(Hotel.reservations.model:select({room_id = id})) do
    if row.status == "checked_in" then return true end
  end
  return false
end
Hotel.hotel_settings = records.bind("lodge.hotel_settings", function(row, previous)
  if previous or not Hotel.configuring then return nil, orion.tr("Hotel currency settings cannot be changed directly.") end
  if not ((row.country == "DE" and row.currency == "EUR") or (row.country == "RU" and row.currency == "RUB")) then
    return nil, orion.tr("Choose Germany with EUR or Russia with RUB.")
  end
  if #Hotel.hotel_settings.model:all() ~= 0 then return nil, orion.tr("Hotel currency is already configured.") end
  return true
end)
function Hotel.configure_hotel(country, currency)
  local current = Hotel.hotel_settings.model:all()[1]
  if current then
    if current.country == country and current.currency == currency then return current end
    return nil, orion.tr("Hotel currency is already configured.")
  end
  Hotel.configuring = true
  local ok, result, message = pcall(function()
    return Hotel.hotel_settings.model:transaction(function()
      local row = orion.assert(Hotel.hotel_settings:save(nil, {country = country, currency = currency, actor = require("orion.db").actor("lodge")}))
      orion.assert(require("orion.application").save("lodge"))
      return row
    end)
  end)
  Hotel.configuring = false
  if not ok then error(result, 0) end
  return result, message
end
Hotel.reservation_groups = records.bind("lodge.reservation_groups", named)
Hotel.reservations = records.bind("lodge.reservations", function(row, previous)
  local room = Hotel.rooms.model:find(row.room_id)
  if not room or not Hotel.guests.model:find(row.guest_id) then return nil, orion.tr("Choose a room and a guest.") end
  if row.group_id and row.group_id ~= 0 and not Hotel.reservation_groups.model:find(row.group_id) then return nil, orion.tr("Choose a valid reservation group.") end
  if previous and previous.group_id ~= row.group_id then return nil, orion.tr("A reservation cannot be moved to another group.") end
  if not intervals.valid(row.arrival, row.departure) then return nil, orion.tr("Departure must follow a valid arrival date.") end
  if row.id and row.id ~= 0 then
    for _, night in ipairs(Hotel.night_rates.model:select({reservation_id = row.id})) do
      if night.night < row.arrival or night.night >= row.departure then
        return nil, orion.tr("Remove nightly prices outside the new stay before changing the dates.")
      end
    end
  end
  if not row.persons or row.persons < 1 or row.persons % 1 ~= 0 or row.persons > room.capacity then return nil, orion.tr("The guest count exceeds the room capacity.") end
  for _, field in ipairs({"rate", "extras", "paid"}) do
    local value = row[field]
    if not valid_amount(value) then return nil, orion.tr("Enter an amount from 0 to 1000000 with at most two decimal places.") end
  end
  if Hotel.total(row) >= 1e12 then
    return nil, orion.tr("The reservation total is too large.")
  end
  if not ({reserved = true, checked_in = true, checked_out = true, cancelled = true})[row.status] then return nil, orion.tr("Unknown reservation status.") end
  if previous and (previous.status == "checked_out" or previous.status == "cancelled") then
    for _, field in ipairs({"room_id", "guest_id", "group_id", "arrival", "departure", "persons", "rate", "extras", "status", "notes"}) do
      if row[field] ~= previous[field] then return nil, orion.tr("This reservation is closed.") end
    end
    if money.minor(row.paid) == money.minor(previous.paid) then return nil, orion.tr("This reservation is closed.") end
  end
  if previous and previous.status == "checked_in" and row.room_id ~= previous.room_id and room.cleaning_state ~= "clean" then
    return nil, orion.tr("Mark the room clean before checking in.")
  end
  if row.status == "checked_in" then
    for _, other in ipairs(Hotel.reservations.model:select({room_id = row.room_id})) do
      if other.id ~= row.id and other.status == "checked_in" then
        return nil, orion.tr("Another guest is still checked in to this room.")
      end
    end
  end
  if previous and previous.status ~= row.status then
    if money.minor(previous.paid) ~= money.minor(row.paid) then return nil, orion.tr("Record payments separately from status changes.") end
    local allowed = {reserved = {checked_in = true, cancelled = true}, checked_in = {checked_out = true}}
    if not (allowed[previous.status] and allowed[previous.status][row.status]) then return nil, orion.tr("This reservation cannot make that transition.") end
    if row.status == "checked_in" and room.cleaning_state ~= "clean" then return nil, orion.tr("Mark the room clean before checking in.") end
    if row.status == "checked_out" and money.minor(Hotel.balance(row)) ~= 0 then return nil, orion.tr("Settle the balance before checking out.") end
  elseif not previous and row.status ~= "reserved" then return nil, orion.tr("Unknown reservation status.") end
  if row.status ~= "cancelled" then
    for _, other in ipairs(Hotel.reservations.model:all()) do
      if other.id ~= row.id and other.room_id == row.room_id and other.status ~= "cancelled" and
        intervals.overlap(row.arrival, row.departure, other.arrival, other.departure) then
        return nil, orion.tr("The room is already booked for these dates.")
      end
    end
  end
  return true
end)
Hotel.night_rates = records.bind("lodge.night_rates", function(row, previous)
  local reservation = Hotel.reservations.model:find(row.reservation_id)
  if not reservation then return nil, orion.tr("Choose a reservation.") end
  if reservation.status == "checked_out" or reservation.status == "cancelled" then return nil, orion.tr("This reservation is closed.") end
  if previous and (previous.reservation_id ~= row.reservation_id or previous.night ~= row.night) then
    return nil, orion.tr("A nightly price cannot be moved to another date or reservation.")
  end
  local valid_date = pcall(calendar.day, row.night)
  if not valid_date or row.night < reservation.arrival or row.night >= reservation.departure then
    return nil, orion.tr("Choose a night within the reservation dates.")
  end
  if type(row.amount_minor) ~= "number" or row.amount_minor % 1 ~= 0 or row.amount_minor < 0 or row.amount_minor > 100000000 then
    return nil, orion.tr("Enter a valid nightly amount in minor units.")
  end
  for _, other in ipairs(Hotel.night_rates.model:select({reservation_id = row.reservation_id})) do
    if other.id ~= row.id and other.night == row.night then return nil, orion.tr("A nightly price already exists for this date.") end
  end
  local prior = previous and previous.amount_minor or money.minor(reservation.rate)
  if money.minor(Hotel.total(reservation)) - prior + row.amount_minor >= 100000000000000 then
    return nil, orion.tr("The reservation total is too large.")
  end
  return true
end)
Hotel.day_closes = records.bind("lodge.day_closes", function(row, previous)
  if previous or not Hotel.closing_day then return nil, orion.tr("Day close records cannot be changed directly.") end
  if row.day ~= orion.today() or Hotel.day_closes.model:find({day = row.day}) then return nil, orion.tr("Today is already closed or is not the current day.") end
  return true
end)
function Hotel.day_report()
  local day, net, outstanding, count, pending = orion.today(), 0, 0, 0, 0
  local cancelled_paid, cancelled_count = 0, 0
  local events = require("orion.model"):extend("lodge.payment_events")
  for _, event in ipairs(events:select({posted_on = day})) do net = net + event.amount_minor end
  for _, row in ipairs(Hotel.reservations.model:all()) do
    if row.status ~= "cancelled" then outstanding = outstanding + money.minor(Hotel.balance(row))
    else
      local paid = money.minor(Hotel.paid(row))
      if paid > 0 then cancelled_paid, cancelled_count = cancelled_paid + paid, cancelled_count + 1 end
    end
    if (row.status == "reserved" and row.arrival <= day) or (row.status == "checked_in" and row.departure <= day) then pending = pending + 1 end
    count = count + 1
  end
  orion.assert(math.abs(net) <= 9007199254740991 and math.abs(outstanding) <= 9007199254740991 and cancelled_paid <= 9007199254740991, orion.tr("The day totals are too large."))
  return {day = day, net_minor = net, outstanding_minor = outstanding, reservations = count, pending = pending,
    cancelled_paid_minor = cancelled_paid, cancelled_paid_count = cancelled_count, closed = Hotel.day_closes.model:find({day = day}) ~= nil}
end
function Hotel.close_today()
  local report = Hotel.day_report()
  if report.closed then return nil, orion.tr("Today is already closed or is not the current day.") end
  if report.pending > 0 then return nil, orion.tr("Resolve overdue arrivals and departures before closing the day.") end
  Hotel.closing_day = true
  local ok, result, message = pcall(function()
    return Hotel.day_closes.model:transaction(function()
      local row = orion.assert(Hotel.day_closes:save(nil, {day = report.day, actor = require("orion.db").actor("lodge"),
        net_minor = string.format("%.0f", report.net_minor), outstanding_minor = string.format("%.0f", report.outstanding_minor), reservations = report.reservations,
        cancelled_paid_minor = string.format("%.0f", report.cancelled_paid_minor), cancelled_paid_count = report.cancelled_paid_count}))
      orion.assert(require("orion.application").save("lodge"))
      return row
    end)
  end)
  Hotel.closing_day = false
  if not ok then error(result, 0) end
  return result, message
end

function Hotel.total(row)
  local nights = calendar.between(row.arrival, row.departure)
  local total = money.minor(row.rate) * nights + money.minor(row.extras)
  if row.id and row.id ~= 0 then
    for _, night in ipairs(Hotel.night_rates.model:select({reservation_id = row.id})) do
      total = total + night.amount_minor - money.minor(row.rate)
    end
  end
  return total / 100
end
function Hotel.paid(row)
  if not row.id or row.id == 0 then return row.paid end
  local Model = require("orion.model")
  local history = Model:extend("lodge.reservation_history")
  local tracked = false
  for _, item in ipairs(history:select({reservation_id = row.id})) do
    if item.operation == "insert" or item.operation == "opening_balance" then tracked = true; break end
  end
  if not tracked then return row.paid end
  local total = 0
  for _, item in ipairs(Model:extend("lodge.payment_events"):select({reservation_id = row.id})) do
    total = total + item.amount_minor
  end
  return total / 100
end
function Hotel.balance(row) return (money.minor(Hotel.total(row)) - money.minor(Hotel.paid(row))) / 100 end
function Hotel.save(id, fields)
  local previous = id and Hotel.reservations.model:find(id)
  if id and not previous then return nil, orion.tr("Choose a reservation.") end
  fields.status = previous and previous.status or "reserved"
  return Hotel.reservations:save(id, fields)
end
function Hotel.create_group(name, stays)
  if type(stays) ~= "table" or #stays < 2 or #stays > 100 then return nil, orion.tr("A group needs between 2 and 100 room reservations.") end
  return Hotel.reservations.model:transaction(function()
    local group = orion.assert(Hotel.reservation_groups:save(nil, {name = name}))
    local ids = {}
    for _, stay in ipairs(stays) do
      local fields = {}
      for _, key in ipairs({"room_id", "guest_id", "arrival", "departure", "persons", "rate", "extras", "notes"}) do fields[key] = stay[key] end
      fields.group_id = group.id
      local reservation = orion.assert(Hotel.save(nil, fields))
      ids[#ids + 1] = reservation.id
    end
    orion.assert(require("orion.application").save("lodge"))
    return {id = group.id, reservations = ids}
  end)
end
function Hotel.transition(id, target)
  local row = Hotel.reservations.model:find(id)
  if not row then return nil, orion.tr("Choose a reservation.") end
  local allowed = {reserved = {checked_in = true, cancelled = true}, checked_in = {checked_out = true}}
  if not (allowed[row.status] and allowed[row.status][target]) then return nil, orion.tr("This reservation cannot make that transition.") end
  if target == "checked_out" and money.minor(Hotel.balance(row)) ~= 0 then return nil, orion.tr("Settle the balance before checking out.") end
  return Hotel.reservations:save(id, {status = target})
end
local payment_sequence = 0
function Hotel.next_payment_key()
  local events = require("orion.model"):extend("lodge.payment_events")
  local key
  repeat
    payment_sequence = payment_sequence + 1
    key = "reception:" .. payment_sequence
  until not events:find({request_key = key})
  return key
end
-- The caller supplies a stable key for each payment or refund request.
function Hotel.payment(id, amount_minor, request_key)
  if type(request_key) ~= "string" or not request_key:find("%S") or #request_key > 100 or
    type(amount_minor) ~= "number" or amount_minor % 1 ~= 0 or amount_minor == 0 or math.abs(amount_minor) > 100000000 then
    return nil, orion.tr("Enter a valid payment amount and request key.")
  end
  local events = require("orion.model"):extend("lodge.payment_events")
  local existing = events:find({request_key = request_key})
  if existing then
    if existing.reservation_id ~= id or existing.amount_minor ~= amount_minor then
      return nil, orion.tr("The payment request key was already used for a different payment.")
    end
    return Hotel.reservations.model:find(id)
  end
  local row = Hotel.reservations.model:find(id)
  if not row then return nil, orion.tr("Choose a reservation.") end
  local new_paid = money.minor(row.paid) + amount_minor
  if new_paid < 0 or new_paid > 100000000 then return nil, orion.tr("The resulting paid amount is out of range.") end
  Hotel.payment_request = request_key
  local ok, result, message = pcall(function()
    return Hotel.reservations.model:transaction(function()
      local saved = orion.assert(Hotel.reservations:save(id, {paid = new_paid / 100}))
      orion.assert(require("orion.application").save("lodge"))
      return saved
    end)
  end)
  Hotel.payment_request = nil
  if not ok then error(result, 0) end
  return result, message
end
return Hotel

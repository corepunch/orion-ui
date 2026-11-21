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
local function unique_name(binding)
  return function(row)
    local ok, message = named(row)
    if not ok then return nil, message end
    for _, other in ipairs(binding().model:all()) do
      if other.id ~= row.id and other.name == row.name then return nil, orion.tr("This name is already used.") end
    end
    return true
  end
end
-- Where bookings come from and how money moves: catalogs the hotel extends.
Hotel.booking_sources = records.bind("lodge.booking_sources", unique_name(function() return Hotel.booking_sources end))
Hotel.payment_methods = records.bind("lodge.payment_methods", unique_name(function() return Hotel.payment_methods end))
function Hotel.seed_catalogs()
  local seeds = {{Hotel.booking_sources, {"Direct", "Phone", "Email", "Website", "Booking portal", "Walk-in"}},
    {Hotel.payment_methods, {"Cash", "Card", "Bank transfer"}}}
  for _, seed in ipairs(seeds) do
    if #seed[1].model:all() == 0 then
      for _, name in ipairs(seed[2]) do orion.assert(seed[1]:save(nil, {name = orion.tr(name)})) end
    end
  end
end
Hotel.guests = records.bind("lodge.guests", function(row)
  local ok, message = named(row)
  if not ok then return nil, message end
  if row.email and row.email ~= "" and not row.email:match("^[^%s@]+@[^%s@]+%.[^%s@]+$") then
    return nil, orion.tr("Enter a valid email address or leave it empty.")
  end
  if row.birth_date and row.birth_date ~= "" then
    if not pcall(calendar.day, row.birth_date) or row.birth_date > orion.today() then return nil, orion.tr("Enter a birth date in the past.") end
  end
  return true
end)
-- A room out of service (repair, renovation) is not sold for its dates; the
-- block is half-open like a stay.
Hotel.room_blocks = records.bind("lodge.room_blocks", function(row)
  if not Hotel.rooms.model:find(row.room_id) then return nil, orion.tr("Choose a room.") end
  if not intervals.valid(row.blocked_from, row.blocked_until) then return nil, orion.tr("The block must end after it starts.") end
  if not row.reason or not row.reason:find("%S") then return nil, orion.tr("Enter the reason the room is out of service.") end
  for _, other in ipairs(Hotel.room_blocks.model:select({room_id = row.room_id})) do
    if other.id ~= row.id and intervals.overlap(row.blocked_from, row.blocked_until, other.blocked_from, other.blocked_until) then
      return nil, orion.tr("The room is already out of service for these dates.")
    end
  end
  for _, stay in ipairs(Hotel.reservations.model:select({room_id = row.room_id})) do
    if (stay.status == "reserved" or stay.status == "checked_in") and
      intervals.overlap(row.blocked_from, row.blocked_until, stay.arrival, stay.departure) then
      return nil, orion.tr("Move the reservations in these dates to another room first.")
    end
  end
  return true
end)
function Hotel.room_blocked(room_id, arrival, departure)
  for _, block in ipairs(Hotel.room_blocks.model:select({room_id = room_id})) do
    if intervals.overlap(arrival, departure, block.blocked_from, block.blocked_until) then return block end
  end
end
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
    for _, field in ipairs({"room_id", "guest_id", "group_id", "arrival", "departure", "persons", "rate", "extras", "status", "notes", "source_id"}) do
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
  if row.source_id and row.source_id ~= 0 and not Hotel.booking_sources.model:find(row.source_id) then return nil, orion.tr("Choose where the booking came from.") end
  local moved = not previous or previous.room_id ~= row.room_id or previous.arrival ~= row.arrival or previous.departure ~= row.departure
  if (row.status == "reserved" or row.status == "checked_in") and moved and Hotel.room_blocked(row.room_id, row.arrival, row.departure) then
    return nil, orion.tr("The room is out of service for these dates.")
  end
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
  local history_revision = 0
  for _, entry in ipairs(require("orion.model"):extend("lodge.reservation_history"):all()) do
    history_revision = math.max(history_revision, entry.id)
  end
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
    cancelled_paid_minor = cancelled_paid, cancelled_paid_count = cancelled_count, history_revision = history_revision,
    closed = Hotel.day_closes.model:find({day = day}) ~= nil}
end
function Hotel.close_today(expected)
  local report = Hotel.day_report()
  if expected then
    for _, key in ipairs({"day", "net_minor", "outstanding_minor", "reservations", "pending", "cancelled_paid_minor", "cancelled_paid_count", "history_revision"}) do
      if expected[key] == nil or expected[key] ~= report[key] then return nil, orion.tr("The day has changed. Preview it again before closing.") end
    end
  end
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
      for _, key in ipairs({"room_id", "guest_id", "arrival", "departure", "persons", "rate", "extras", "notes", "source_id"}) do fields[key] = stay[key] end
      fields.group_id = group.id
      local reservation = orion.assert(Hotel.save(nil, fields))
      ids[#ids + 1] = reservation.id
    end
    orion.assert(require("orion.application").save("lodge"))
    return {id = group.id, reservations = ids}
  end)
end
Hotel.group_settlements = records.bind("lodge.group_settlements", function(row, previous)
  if previous or not Hotel.settling_group then return nil, orion.tr("Group settlement records cannot be changed directly.") end
  if Hotel.group_settlements.model:find({request_key = row.request_key}) then return nil, orion.tr("The group settlement request key was already used.") end
  return true
end)
local group_payment_sequence = 0
function Hotel.next_group_settlement_key()
  local key
  repeat
    group_payment_sequence = group_payment_sequence + 1
    key = "local-" .. group_payment_sequence
  until not Hotel.group_settlements.model:find({request_key = key})
  return key
end
-- The explicit allocation is part of the retry identity and must still match
-- every member's current balance before any payment can be recorded.
function Hotel.settle_group(id, allocation, request_key)
  if type(id) ~= "number" or id % 1 ~= 0 or id < 1 or id > 2147483647 or
     type(request_key) ~= "string" or not request_key:match("^[%w_.%-]+$") or #request_key > 70 or
     type(allocation) ~= "table" or #allocation < 1 or #allocation > 100 then
    return nil, orion.tr("Enter a valid group settlement request.")
  end
  local copy, seen = {}, {}
  for _, item in ipairs(allocation) do
    if type(item) ~= "table" or type(item.id) ~= "number" or item.id % 1 ~= 0 or item.id < 1 or item.id > 2147483647 or
       type(item.amount_minor) ~= "number" or item.amount_minor % 1 ~= 0 or item.amount_minor < 1 or
       item.amount_minor > 100000000 or seen[item.id] then return nil, orion.tr("Enter a valid group settlement request.") end
    for key in pairs(item) do if key ~= "id" and key ~= "amount_minor" then return nil, orion.tr("Enter a valid group settlement request.") end end
    seen[item.id] = item.amount_minor; copy[#copy + 1] = {id = item.id, amount_minor = item.amount_minor}
  end
  table.sort(copy, function(a, b) return a.id < b.id end)
  local parts = {}
  for _, item in ipairs(copy) do parts[#parts + 1] = item.id .. ":" .. item.amount_minor end
  local encoded = table.concat(parts, ";")
  local existing = Hotel.group_settlements.model:find({request_key = request_key})
  if existing then
    if existing.group_id ~= id or existing.allocation ~= encoded then return nil, orion.tr("The group settlement request key was already used.") end
    return {id = existing.id}
  end
  if not Hotel.reservation_groups.model:find(id) then return nil, orion.tr("Choose a reservation group.") end
  local members = Hotel.reservations.model:select({group_id = id})
  local count = 0
  for _, row in ipairs(members) do
    local balance = money.minor(Hotel.balance(row))
    if row.status == "cancelled" or balance < 0 then return nil, orion.tr("Settle cancelled reservations and credits individually before paying the group.") end
    if balance > 0 then
      count = count + 1
      if seen[row.id] ~= balance or money.minor(row.paid) + balance > 100000000 then
        return nil, orion.tr("Group balances changed; preview the settlement again.")
      end
    elseif seen[row.id] then return nil, orion.tr("Group balances changed; preview the settlement again.") end
  end
  if count ~= #copy then return nil, orion.tr("Group balances changed; preview the settlement again.") end
  local events = require("orion.model"):extend("lodge.payment_events")
  for _, item in ipairs(copy) do
    if events:find({request_key = "group:" .. request_key .. ":" .. item.id}) then return nil, orion.tr("The group settlement request key was already used.") end
  end
  Hotel.settling_group = true
  local ok, result, message = pcall(function()
    return Hotel.reservations.model:transaction(function()
      local header = orion.assert(Hotel.group_settlements:save(nil, {group_id = id, request_key = request_key, allocation = encoded,
        actor = require("orion.db").actor("lodge")}))
      for _, item in ipairs(copy) do
        local row = orion.assert(Hotel.reservations.model:find(item.id))
        Hotel.payment_request = "group:" .. request_key .. ":" .. item.id
        orion.assert(Hotel.reservations:save(item.id, {paid = (money.minor(row.paid) + item.amount_minor) / 100}))
      end
      orion.assert(require("orion.application").save("lodge"))
      return {id = header.id}
    end)
  end)
  Hotel.payment_request, Hotel.settling_group = nil, false
  if not ok then error(result, 0) end
  return result, message
end
function Hotel.cancel_group(id)
  local group = Hotel.reservation_groups.model:find(id)
  if not group then return nil, orion.tr("Choose a reservation group.") end
  local members = Hotel.reservations.model:select({group_id = id})
  if #members == 0 then return nil, orion.tr("This group has no reservations.") end
  for _, row in ipairs(members) do
    if row.status ~= "reserved" then return nil, orion.tr("All group reservations must be reserved before cancelling the group.") end
  end
  return Hotel.reservations.model:transaction(function()
    for _, row in ipairs(members) do orion.assert(Hotel.transition(row.id, "cancelled")) end
    orion.assert(require("orion.application").save("lodge"))
    return {id = id}
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
-- The caller supplies a stable key for each payment or refund request, and
-- optionally how the money moved (a payment_methods id).
function Hotel.payment(id, amount_minor, request_key, method_id)
  if method_id ~= nil and (math.type(method_id) ~= "integer" or not Hotel.payment_methods.model:find(method_id)) then return nil, orion.tr("Choose a payment method.") end
  if type(request_key) ~= "string" or not request_key:find("%S") or #request_key > 100 or
    type(amount_minor) ~= "number" or amount_minor % 1 ~= 0 or amount_minor == 0 or math.abs(amount_minor) > 100000000 then
    return nil, orion.tr("Enter a valid payment amount and request key.")
  end
  local events = require("orion.model"):extend("lodge.payment_events")
  local existing = events:find({request_key = request_key})
  local group_key = request_key:match("^group:([%w_.%-]+):%d+$")
  if request_key:sub(1, 6) == "group:" then
    if not existing then return nil, orion.tr("Enter a valid payment amount and request key.") end
    local group = group_key and Hotel.group_settlements.model:find({request_key = group_key})
    if group then
      for member, amount in group.allocation:gmatch("(%d+):(%d+)") do
        if tonumber(member) == existing.reservation_id and tonumber(amount) == existing.amount_minor then
          return nil, orion.tr("Enter a valid payment amount and request key.")
        end
      end
    end
  end
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
  Hotel.payment_request, Hotel.payment_method = request_key, method_id
  local ok, result, message = pcall(function()
    return Hotel.reservations.model:transaction(function()
      local saved = orion.assert(Hotel.reservations:save(id, {paid = new_paid / 100}))
      orion.assert(require("orion.application").save("lodge"))
      return saved
    end)
  end)
  Hotel.payment_request, Hotel.payment_method = nil, nil
  if not ok then error(result, 0) end
  return result, message
end
-- ── Front-desk shortcuts ────────────────────────────────────────────────────
-- What a small desk does many times a day, each as one transaction.

-- Runs fn in one transaction and saves; a refusal comes back as nil, message.
local function write_all(fn)
  local ok, result = pcall(Hotel.reservations.model.transaction, Hotel.reservations.model, function()
    local value = orion.assert(fn())
    orion.assert(require("orion.application").save("lodge"))
    return value
  end)
  if ok then return result end
  if type(result) == "table" and type(result.message) == "string" then return nil, result.message end
  error(result, 0)
end
Hotel.write_all = write_all

-- The guest named exactly so (ignoring case and surrounding spaces), else a
-- new guest with that name and phone.
function Hotel.guest_named(name, phone)
  name = type(name) == "string" and name:match("^%s*(.-)%s*$") or ""
  if name == "" then return nil, orion.tr("Enter the guest's name.") end
  local lower = name:lower()
  for _, guest in ipairs(Hotel.guests.model:all()) do
    if guest.name:lower() == lower then return guest end
  end
  return Hotel.guests:save(nil, {name = name, phone = phone or ""})
end

-- A guest who walks in: the guest, a stay from today, check-in and an
-- optional payment, all or nothing.
function Hotel.walk_in(args)
  local today = orion.today()
  local nights = args.nights
  if math.type(nights) ~= "integer" or nights < 1 or nights > 90 then return nil, orion.tr("Enter from 1 to 90 nights.") end
  local room = Hotel.rooms.model:find(args.room_id or 0)
  if not room then return nil, orion.tr("Choose a room.") end
  if room.cleaning_state ~= "clean" then return nil, orion.tr("Mark the room clean before checking in.") end
  local paid = args.pay or 0
  if not valid_amount(paid) then return nil, orion.tr("Enter an amount from 0 to 1000000 with at most two decimal places.") end
  return write_all(function()
    local guest = orion.assert(Hotel.guest_named(args.name, args.phone))
    local row = orion.assert(Hotel.save(nil, {room_id = room.id, guest_id = guest.id, arrival = today, departure = calendar.add(today, nights),
      persons = args.persons or 1, rate = room.rate, extras = 0, notes = args.notes or "", source_id = args.source_id or 0}))
    orion.assert(Hotel.reservations:save(row.id, {status = "checked_in"}))
    if money.minor(paid) > 0 then
      Hotel.payment_request, Hotel.payment_method = Hotel.next_payment_key(), args.method_id
      local ok, saved, refusal = pcall(Hotel.reservations.save, Hotel.reservations, row.id, {paid = paid})
      Hotel.payment_request, Hotel.payment_method = nil, nil
      if not ok then error(saved, 0) end
      orion.assert(saved, refusal)
    end
    return Hotel.reservations.model:find(row.id)
  end)
end

-- One more night (or `nights`) on a stay that is reserved or in house.
function Hotel.extend(id, nights)
  nights = nights or 1
  local row = Hotel.reservations.model:find(id)
  if not row then return nil, orion.tr("Choose a reservation.") end
  if row.status ~= "reserved" and row.status ~= "checked_in" then return nil, orion.tr("This reservation is closed.") end
  if math.type(nights) ~= "integer" or nights == 0 or nights > 90 or nights < -90 then return nil, orion.tr("Enter from 1 to 90 nights.") end
  local departure = calendar.add(row.departure, nights)
  if departure <= row.arrival then return nil, orion.tr("A stay needs at least one night.") end
  return write_all(function() return Hotel.reservations:save(id, {departure = departure}) end)
end

-- A booked guest who did not come: the stay is cancelled and noted as a no-show.
function Hotel.no_show(id)
  local row = Hotel.reservations.model:find(id)
  if not row then return nil, orion.tr("Choose a reservation.") end
  if row.status ~= "reserved" then return nil, orion.tr("Only reserved bookings can be cancelled.") end
  if row.arrival > orion.today() then return nil, orion.tr("A guest can be marked as a no-show from the arrival day.") end
  local note = orion.tr("No-show")
  local notes = row.notes and row.notes ~= "" and (note .. ". " .. row.notes) or note
  return write_all(function() return Hotel.reservations:save(id, {status = "cancelled", notes = notes:sub(1, 255)}) end)
end

-- The reservations that match a guest, room, phone or reservation number,
-- active first, then by arrival.
function Hotel.search(text, limit)
  text = type(text) == "string" and text:lower():match("^%s*(.-)%s*$") or ""
  if text == "" then return {} end
  local number = tonumber(text:match("^#?(%d+)$") or "")
  local rows = {}
  for _, row in ipairs(Hotel.reservations.model:select(nil, {order = "arrival, id"})) do
    local guest, room = row.guest or {}, row.room or {}
    if row.id == number or (guest.name or ""):lower():find(text, 1, true) or (room.name or ""):lower() == text or
       ((guest.phone or ""):gsub("%D", "") ~= "" and text:gsub("%D", "") ~= "" and (guest.phone or ""):gsub("%D", ""):find(text:gsub("%D", ""), 1, true)) then
      rows[#rows + 1] = row
    end
  end
  -- The reservation with that number, then the room of that name, then by status.
  local rank = {checked_in = 1, reserved = 2, checked_out = 3, cancelled = 4}
  local function order(row)
    if row.id == number then return 0 end
    if row.room and row.room.name:lower() == text then return 0.5 end
    return rank[row.status] or 9
  end
  table.sort(rows, function(a, b)
    if order(a) ~= order(b) then return order(a) < order(b) end
    if a.arrival ~= b.arrival then return a.arrival < b.arrival end
    return a.id < b.id
  end)
  while limit and #rows > limit do table.remove(rows) end
  return rows
end
-- The hotel's own details for printed documents: one editable record.
Hotel.hotel_profile = records.bind("lodge.hotel_profile", function(row)
  for _, key in ipairs({"name", "address", "phone", "email", "check_in_time", "check_out_time"}) do
    if row[key] ~= nil and type(row[key]) ~= "string" then return nil, orion.tr("Enter the hotel details as text.") end
  end
  if row.email and row.email ~= "" and not row.email:match("^[^%s@]+@[^%s@]+%.[^%s@]+$") then
    return nil, orion.tr("Enter a valid email address or leave it empty.")
  end
  for _, key in ipairs({"check_in_time", "check_out_time"}) do
    local value = row[key]
    if value and value ~= "" then
      local h, m = value:match("^(%d%d?):(%d%d)$")
      if not h or tonumber(h) > 23 or tonumber(m) > 59 then return nil, orion.tr("Enter times as hours and minutes, such as 14:00.") end
    end
  end
  return true
end)
function Hotel.profile() return Hotel.hotel_profile.model:all()[1] or {} end
function Hotel.save_profile(fields)
  local current = Hotel.hotel_profile.model:all()[1]
  return Hotel.hotel_profile:save(current and current.id, fields)
end
return Hotel

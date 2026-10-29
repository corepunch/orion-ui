-- Hotel rules stay in Lodge. All reusable operations come from small plugins.
local orion = require("orion")
local Plugins = require("orion.plugins")
local records = Plugins.use("records", 1)
local calendar = Plugins.use("calendar", 1)
local intervals = Plugins.use("intervals", 1)
local money = Plugins.use("money", 1)
local Hotel = {}

local function named(row)
  if not row.name or not row.name:find("%S") then return nil, orion.tr("Enter a name.") end
  return true
end
Hotel.rooms = records.bind("lodge.rooms", function(row)
  local ok, message = named(row)
  if not ok then return nil, message end
  for _, other in ipairs(Hotel.rooms.model:all()) do
    if other.id ~= row.id and other.name == row.name then return nil, orion.tr("A room with this name already exists.") end
  end
  if not row.capacity or row.capacity < 1 or row.capacity % 1 ~= 0 then return nil, orion.tr("Capacity must be a positive integer.") end
  if not row.rate or row.rate < 0 or row.rate > 1000000 then return nil, orion.tr("Enter a nightly rate between 0 and 1000000.") end
  return true
end)
Hotel.guests = records.bind("lodge.guests", named)
Hotel.reservations = records.bind("lodge.reservations", function(row, previous)
  local room = Hotel.rooms.model:find(row.room_id)
  if not room or not Hotel.guests.model:find(row.guest_id) then return nil, orion.tr("Choose a room and a guest.") end
  if not intervals.valid(row.arrival, row.departure) then return nil, orion.tr("Departure must follow a valid arrival date.") end
  if not row.persons or row.persons < 1 or row.persons % 1 ~= 0 or row.persons > room.capacity then return nil, orion.tr("The guest count exceeds the room capacity.") end
  for _, field in ipairs({"rate", "extras", "paid"}) do
    local value = row[field]
    if type(value) ~= "number" or value ~= value or value < 0 or value > 1000000 then return nil, orion.tr("Enter amounts between 0 and 1000000.") end
  end
  if row.rate * calendar.between(row.arrival, row.departure) + row.extras >= 1e12 then
    return nil, orion.tr("The reservation total is too large.")
  end
  if not ({reserved = true, checked_in = true, checked_out = true, cancelled = true})[row.status] then return nil, orion.tr("Unknown reservation status.") end
  if previous and (previous.status == "checked_out" or previous.status == "cancelled") then return nil, orion.tr("This reservation is closed.") end
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

function Hotel.total(row)
  local nights = calendar.between(row.arrival, row.departure)
  return (money.minor(row.rate) * nights + money.minor(row.extras)) / 100
end
function Hotel.balance(row) return (money.minor(Hotel.total(row)) - money.minor(row.paid)) / 100 end
function Hotel.save(id, fields)
  local previous = id and Hotel.reservations.model:find(id)
  if id and not previous then return nil, orion.tr("Choose a reservation.") end
  fields.status = previous and previous.status or "reserved"
  return Hotel.reservations:save(id, fields)
end
function Hotel.transition(id, target)
  local row = Hotel.reservations.model:find(id)
  if not row then return nil, orion.tr("Choose a reservation.") end
  local allowed = {reserved = {checked_in = true, cancelled = true}, checked_in = {checked_out = true}}
  if not (allowed[row.status] and allowed[row.status][target]) then return nil, orion.tr("This reservation cannot make that transition.") end
  if target == "checked_out" and money.minor(Hotel.balance(row)) ~= 0 then return nil, orion.tr("Settle the balance before checking out.") end
  return Hotel.reservations:save(id, {status = target})
end
return Hotel

-- The host authenticates the caller and supplies actor and role.
local json = require("orion.json")
local orion = require("orion")
local Hotel = require("hotel")
local application = require("orion.application")
local service = {}
local roles = {
  snapshot = {admin = true, reception = true, housekeeping = true, auditor = true},
  save_room = {admin = true, reception = true}, save_guest = {admin = true, reception = true},
  reserve = {admin = true, reception = true}, check_in = {admin = true, reception = true},
  check_out = {admin = true, reception = true}, cancel = {admin = true, reception = true},
  payment = {admin = true, reception = true}, set_night = {admin = true, reception = true},
  reset_night = {admin = true, reception = true}, set_room_cleaning = {admin = true, housekeeping = true},
  group_reserve = {admin = true, reception = true}, group_cancel = {admin = true, reception = true}, group_settle = {admin = true, reception = true},
  day_preview = {admin = true, reception = true, auditor = true}, close_day = {admin = true},
  configure_hotel = {admin = true},
}
local function positive_id(value)
  orion.assert(type(value) == "number" and value % 1 == 0 and value > 0, orion.tr("Choose a valid record."))
  return value
end
local function fields(args, allowed)
  orion.assert(type(args) == "table", orion.tr("Invalid request."))
  local copy = {}
  for key, value in pairs(args) do
    orion.assert(allowed[key], orion.tr("Invalid request field."))
    copy[key] = value
  end
  return copy
end
local function write(fn)
  return Hotel.reservations.model:transaction(function()
    local row = orion.assert(fn())
    orion.assert(application.save("lodge"))
    return {id = row.id}
  end)
end
local function snapshot(role)
  local result = {today = orion.today(), rooms = json.array({})}
  for _, room in ipairs(Hotel.rooms.model:all()) do
    local item = {id = room.id, name = room.name, cleaning_state = room.cleaning_state, occupied = Hotel.room_occupied(room.id)}
    if role ~= "housekeeping" then item.category, item.capacity, item.rate = room.category, room.capacity, room.rate end
    result.rooms[#result.rooms + 1] = item
  end
  if role == "housekeeping" then return result end
  local settings = Hotel.hotel_settings.model:all()[1]
  if settings then result.hotel = {country = settings.country, currency = settings.currency} end
  result.guests, result.reservations, result.payment_events, result.reservation_history, result.night_rates, result.reservation_groups, result.day_closes =
    json.array({}), json.array({}), json.array({}), json.array({}), json.array({}), json.array({}), json.array({})
  for _, guest in ipairs(Hotel.guests.model:all()) do
    result.guests[#result.guests + 1] = {id = guest.id, name = guest.name, phone = guest.phone}
  end
  for _, row in ipairs(Hotel.reservations.model:all()) do
    result.reservations[#result.reservations + 1] = {id = row.id, room_id = row.room_id, guest_id = row.guest_id,
      group_id = row.group_id, arrival = row.arrival, departure = row.departure, persons = row.persons, rate = row.rate, extras = row.extras,
      paid = Hotel.paid(row), total = Hotel.total(row), balance = Hotel.balance(row), status = row.status, notes = row.notes}
  end
  local Model = require("orion.model")
  for _, row in ipairs(Model:extend("lodge.payment_events"):all()) do
    result.payment_events[#result.payment_events + 1] = {id = row.id, reservation_id = row.reservation_id,
      amount_minor = row.amount_minor, kind = row.kind, request_key = row.request_key, actor = row.actor, posted_on = row.posted_on}
  end
  for _, row in ipairs(Model:extend("lodge.reservation_history"):all()) do
    result.reservation_history[#result.reservation_history + 1] = {id = row.id, reservation_id = row.reservation_id,
      operation = row.operation, actor = row.actor, before = row.before, after = row.after}
  end
  for _, row in ipairs(Hotel.night_rates.model:all()) do
    result.night_rates[#result.night_rates + 1] = {id = row.id, reservation_id = row.reservation_id, night = row.night, amount_minor = row.amount_minor}
  end
  for _, row in ipairs(Hotel.reservation_groups.model:all()) do result.reservation_groups[#result.reservation_groups + 1] = {id = row.id, name = row.name} end
  for _, row in ipairs(Hotel.day_closes.model:all()) do
    result.day_closes[#result.day_closes + 1] = {id = row.id, day = row.day, actor = row.actor,
      net_minor = row.net_minor, outstanding_minor = row.outstanding_minor, reservations = row.reservations,
      cancelled_paid_minor = row.cancelled_paid_minor, cancelled_paid_count = row.cancelled_paid_count}
  end
  return result
end
local function run(command, args, role)
  if command == "snapshot" then fields(args, {}); return snapshot(role) end
  if command == "configure_hotel" then
    fields(args, {country = true, currency = true})
    local row = orion.assert(Hotel.configure_hotel(args.country, args.currency)); return {id = row.id}
  end
  if command == "day_preview" then fields(args, {}); return Hotel.day_report() end
  if command == "close_day" then fields(args, {}); local row = orion.assert(Hotel.close_today()); return {id = row.id} end
  if command == "save_room" or command == "save_guest" then
    local allowed = command == "save_room" and {id = true, name = true, category = true, capacity = true, rate = true} or {id = true, name = true, phone = true}
    local values = fields(args, allowed); local id = values.id and positive_id(values.id); values.id = nil
    local records = command == "save_room" and Hotel.rooms or Hotel.guests
    return write(function() return records:save(id, values) end)
  end
  if command == "reserve" then
    local values = fields(args, {id = true, room_id = true, guest_id = true, arrival = true, departure = true,
      persons = true, rate = true, extras = true, notes = true})
    local id = values.id and positive_id(values.id); values.id = nil
    return write(function() return Hotel.save(id, values) end)
  end
  if command == "group_settle" then
    fields(args, {id = true, allocation = true, request_key = true}); return orion.assert(Hotel.settle_group(positive_id(args.id), args.allocation, args.request_key))
  end
  if command == "group_cancel" then
    fields(args, {id = true}); return orion.assert(Hotel.cancel_group(positive_id(args.id)))
  end
  if command == "group_reserve" then
    fields(args, {name = true, stays = true})
    orion.assert(type(args.stays) == "table" and #args.stays >= 2 and #args.stays <= 100, orion.tr("A group needs between 2 and 100 room reservations."))
    local stays = {}
    for _, stay in ipairs(args.stays) do
      stays[#stays + 1] = fields(stay, {room_id = true, guest_id = true, arrival = true, departure = true,
        persons = true, rate = true, extras = true, notes = true})
    end
    return orion.assert(Hotel.create_group(args.name, stays))
  end
  if command == "check_in" or command == "check_out" or command == "cancel" then
    fields(args, {id = true}); local id = positive_id(args.id)
    local target = ({check_in = "checked_in", check_out = "checked_out", cancel = "cancelled"})[command]
    return write(function() return Hotel.transition(id, target) end)
  end
  if command == "payment" then
    fields(args, {id = true, amount_minor = true, request_key = true})
    local row = orion.assert(Hotel.payment(positive_id(args.id), args.amount_minor, args.request_key))
    return {id = row.id}
  end
  if command == "set_room_cleaning" then
    fields(args, {id = true, state = true}); local id = positive_id(args.id)
    return write(function() return Hotel.rooms:save(id, {cleaning_state = args.state}) end)
  end
  if command == "set_night" or command == "reset_night" then
    fields(args, command == "set_night" and {id = true, night = true, amount_minor = true} or {id = true, night = true})
    local id = positive_id(args.id)
    orion.assert(type(args.night) == "string" and args.night ~= "", orion.tr("Choose a night within the reservation dates."))
    if command == "set_night" then orion.assert(type(args.amount_minor) == "number", orion.tr("Enter a valid nightly amount in minor units.")) end
    local existing = Hotel.night_rates.model:find({reservation_id = id, night = args.night})
    if command == "set_night" then
      return write(function() return Hotel.night_rates:save(existing and existing.id, {reservation_id = id, night = args.night, amount_minor = args.amount_minor}) end)
    end
    orion.assert(existing, orion.tr("This night already uses the base rate."))
    return Hotel.reservations.model:transaction(function()
      orion.assert(Hotel.night_rates:delete(existing.id)); orion.assert(application.save("lodge")); return {id = id}
    end)
  end
  orion.fail(orion.tr("Unknown service command."))
end
function service.handle(input)
  local ok, result = pcall(function()
    local request = json.decode(input)
    orion.assert(type(request) == "table" and type(request.actor) == "string" and request.actor:find("%S") and
      type(request.role) == "string" and type(request.command) == "string", orion.tr("Invalid request."))
    orion.assert(roles[request.command] and roles[request.command][request.role], orion.tr("This role cannot perform that action."))
    return run(request.command, request.args or {}, request.role)
  end)
  if not ok then return json.encode({ok = false, error = tostring(result)}) end
  return json.encode({ok = true, result = result})
end
return service

local orion = require("orion")
local Hotel = require("hotel")
local application = require("orion.application")
local Plugins = require("orion.plugins")
local calendar = Plugins.use("calendar", 1)
local template = Plugins.use("template", 1)
local money = Plugins.use("money", 1)
local Reception = {form = "reception", title = orion.tr("Orion Lodge")}

function Reception:init() self:new_booking() end
function Reception:new_booking()
  local today = orion.today()
  self.editing = nil
  self.fields = {room = 0, guest = 0, arrival = today, departure = calendar.add(today, 1), persons = 1, rate = 0, extras = 0, paid = 0, notes = ""}
  self.notice = ""
end
function Reception:save_data() orion.assert(application.save("lodge")) end
function Reception:remember(v)
  for _, key in ipairs({"room", "guest", "arrival", "departure", "persons", "rate", "extras", "paid", "notes"}) do self.fields[key] = v[key] end
end
function Reception:write(model, action)
  return model:transaction(function()
    local result = orion.assert(action())
    self:save_data()
    return result
  end)
end
function Reception:edit(v)
  local selected = orion.assert(v.bookings, orion.tr("Choose a reservation."))
  local row = orion.assert(Hotel.reservations.model:find(selected.id))
  self.editing = row.id
  self.fields = {room = row.room_id, guest = row.guest_id, arrival = row.arrival, departure = row.departure,
    persons = row.persons, rate = row.rate, extras = row.extras, paid = row.paid, notes = row.notes}
end
function Reception:room_rate(v)
  local room = orion.assert(Hotel.rooms.model:find(v.room), orion.tr("Choose a room."))
  for k in pairs(self.fields) do if v[k] ~= nil then self.fields[k] = v[k] end end
  self.fields.room, self.fields.guest, self.fields.rate = v.room, v.guest, room.rate
end
function Reception:reserve(v)
  local row = self:write(Hotel.reservations.model, function() return Hotel.save(self.editing, {room_id = v.room or 0, guest_id = v.guest or 0,
    arrival = v.arrival or "", departure = v.departure or "", persons = v.persons or 0, rate = v.rate or 0,
    extras = v.extras or 0, paid = v.paid or 0, notes = v.notes or ""}) end)
  self.editing = row.id
  self.fields = {room = row.room_id, guest = row.guest_id, arrival = row.arrival, departure = row.departure,
    persons = row.persons, rate = row.rate, extras = row.extras, paid = row.paid, notes = row.notes}
end
function Reception:transition(v, target)
  local selected = orion.assert(v.bookings, orion.tr("Choose a reservation."))
  self:remember(v)
  self:write(Hotel.reservations.model, function() return Hotel.transition(selected.id, target) end)
end
function Reception:check_in(v) self:transition(v, "checked_in") end
function Reception:check_out(v) self:transition(v, "checked_out") end
function Reception:cancel_booking(v) self:transition(v, "cancelled") end
function Reception:confirmation(v)
  self:remember(v)
  local selected = orion.assert(v.bookings, orion.tr("Choose a reservation."))
  local row = orion.assert(Hotel.reservations.model:find(selected.id))
  local html = template.render([[<!doctype html><html><meta charset="utf-8"><title>{{title}}</title>
<style>body{font:16px sans-serif;max-width:700px;margin:40px auto} @page{size:A4;margin:20mm}</style>
<h1>{{title}}</h1><p>{{guest_label}} {{guest}}</p><p>{{room_label}} {{room}}</p>
<p>{{arrival_label}} {{arrival}} — {{departure_label}} {{departure}}</p>
<p>{{total_label}} {{total}} · {{paid_label}} {{paid}} · {{balance_label}} {{balance}}</p></html>]], {
    title = orion.tr("Booking confirmation"), guest_label = orion.tr("Guest:"), guest = row.guest.name,
    room_label = orion.tr("Room:"), room = row.room.name, arrival_label = orion.tr("Arrival:"), arrival = orion.formatDate(row.arrival),
    departure_label = orion.tr("Departure:"), departure = orion.formatDate(row.departure),
    total_label = orion.tr("Total:"), total = orion.formatNumber(Hotel.total(row), 2),
    paid_label = orion.tr("Paid:"), paid = orion.formatNumber(row.paid, 2), balance_label = orion.tr("Balance:"), balance = orion.formatNumber(Hotel.balance(row), 2),
  })
  self.notice = string.format(orion.tr("Confirmation saved: %s"), orion.assert(application.write("confirmation.html", html)))
end
function Reception:data()
  local count, outstanding = 0, 0
  for _, row in ipairs(Hotel.reservations.model:select(nil, {order = "arrival, id"})) do
    local balance = Hotel.balance(row)
    if row.status ~= "cancelled" then outstanding = outstanding + money.minor(balance) end
    count = count + 1
  end
  local values = {editor = self.editing and string.format(orion.tr("Edit reservation %d"), self.editing) or orion.tr("New reservation"),
    summary = string.format(orion.tr("%d reservations · Outstanding: %s"), count, orion.formatNumber(outstanding / 100, 2)), notice = self.notice}
  for k, v in pairs(self.fields) do values[k] = v end
  return {values = values}
end
return Reception

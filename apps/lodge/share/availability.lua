local orion = require("orion")
local Hotel = require("hotel")
local FrontDesk = require("front_desk")
local application = require("orion.application")
local calendar = require("orion.plugins").use("calendar", 1)
local Availability = {form = "availability", title = orion.tr("Find a room")}

function Availability:init()
  local today = orion.today()
  self.values = {arrival = today, departure = calendar.add(today, 1), persons = 1, guest = 0, source = 0}
  self.rows, self.searched, self.notice = {}, false, ""
end
function Availability:keep(values) for key in pairs(self.values) do if values[key] ~= nil then self.values[key] = values[key] end end end
function Availability:search(values)
  self:keep(values)
  self.rows = FrontDesk.free_rooms(values.arrival, values.departure, values.persons)
  self.searched, self.notice = true, ""
end
function Availability:reserve(values)
  self:keep(values)
  local free = orion.assert(values.free, orion.tr("Show free rooms, then choose one."))
  orion.assert(values.guest, orion.tr("Choose a guest."))
  local room = orion.assert(Hotel.rooms.model:find(free.room_id), orion.tr("Choose a room."))
  local row = Hotel.reservations.model:transaction(function()
    local saved = orion.assert(Hotel.save(nil, {room_id = room.id, guest_id = values.guest, arrival = values.arrival, departure = values.departure,
      persons = values.persons or 1, rate = room.rate, extras = 0, notes = "", source_id = values.source or 0}))
    orion.assert(application.save("lodge"))
    return saved
  end)
  self.rows = FrontDesk.free_rooms(values.arrival, values.departure, values.persons)
  self.notice = string.format(orion.tr("Reservation %d saved: room %s for %s."), row.id, room.name, row.guest.name)
end
function Availability:data()
  local values = {notice = self.notice, summary = self.searched and string.format(orion.tr("%d rooms free"), #self.rows) or ""}
  for key, value in pairs(self.values) do values[key] = value end
  return {values = values, lists = {free = self.rows}, disabled = {reserve = #self.rows == 0}}
end
return Availability

local orion = require("orion")
local Hotel = require("hotel")
local calendar = require("orion.plugins").use("calendar", 1)
local Group = {form = "group_reservation", title = orion.tr("Group reservation")}
function Group:init()
  local today = orion.today()
  self.values = {name = "", guest = 0, arrival = today, departure = calendar.add(today, 1), room1 = 0, persons1 = 1, room2 = 0, persons2 = 1}
  self.notice, self.completed = "", false
end
function Group:create_group(values)
  orion.assert(not self.completed, orion.tr("These group reservations were already created."))
  for key in pairs(self.values) do self.values[key] = values[key] end
  local first = orion.assert(Hotel.rooms.model:find(values.room1 or 0), orion.tr("Choose a room."))
  local second = orion.assert(Hotel.rooms.model:find(values.room2 or 0), orion.tr("Choose a room."))
  orion.assert(Hotel.guests.model:find(values.guest or 0), orion.tr("Choose a guest."))
  local stays = {}
  for i, room in ipairs({first, second}) do
    stays[i] = {room_id = room.id, guest_id = values.guest, arrival = values.arrival, departure = values.departure,
      persons = values["persons" .. i], rate = room.rate, extras = 0, notes = ""}
  end
  local group = orion.assert(Hotel.create_group(values.name, stays))
  self.completed, self.notice = true, string.format(orion.tr("Created group %d with two reservations."), group.id)
end
function Group:data()
  local values = {notice = self.notice}
  for key, value in pairs(self.values) do values[key] = value end
  return {values = values, disabled = {create_group = self.completed}}
end
return Group

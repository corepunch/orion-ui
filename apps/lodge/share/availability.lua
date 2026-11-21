-- The room calendar and finding a free room: who sleeps where over the next
-- two weeks, and the rooms free for a stay, cheapest first.
local orion = require("orion")
local Hotel = require("hotel")
local FrontDesk = require("front_desk")
local application = require("orion.application")
local calendar = require("orion.plugins").use("calendar", 1)
local Availability = {form = "availability", title = orion.tr("Calendar")}

function Availability:init()
  local today = orion.today()
  self.start = today
  self.values = {arrival = today, departure = calendar.add(today, 1), persons = 1, guest = 0, new_guest = "", source = 0}
  self.rows, self.searched, self.notice = {}, false, ""
end
function Availability:keep(values)
  for key in pairs(self.values) do if values[key] ~= nil then self.values[key] = values[key] end end
  if values.start then self.start = values.start end
end
function Availability:previous_week(values) self:keep(values); self.start = calendar.add(self.start, -7) end
function Availability:next_week(values) self:keep(values); self.start = calendar.add(self.start, 7) end
function Availability:go_today(values) self:keep(values); self.start = orion.today() end
function Availability:show_dates(values) self:keep(values) end
-- A room in the calendar opens a new reservation for it from its first free
-- night shown, or the booking that occupies that night.
function Availability:book(values)
  self:keep(values)
  local row = orion.assert(values.calendar, orion.tr("Choose a room in the calendar."))
  local first
  for i = 1, FrontDesk.CALENDAR_DAYS do if not first and not (row["d" .. i] and row["d" .. i] ~= "") then first = i end end
  orion.assert(first, orion.tr("The room has no free night in these two weeks."))
  local arrival = calendar.add(self.start, first - 1)
  Hotel.prefill = {room = row.room_id, arrival = arrival, departure = calendar.add(arrival, 1), persons = 1}
  orion.assert(application.open("reception"))
end
function Availability:search(values)
  self:keep(values)
  self.rows = FrontDesk.free_rooms(values.arrival, values.departure, values.persons)
  self.searched, self.notice = true, ""
end
function Availability:reserve(values)
  self:keep(values)
  local free = orion.assert(values.free, orion.tr("Show free rooms, then choose one."))
  local new_guest = values.new_guest and values.new_guest:find("%S") and values.new_guest
  orion.assert(values.guest or new_guest, orion.tr("Choose a guest."))
  local room = orion.assert(Hotel.rooms.model:find(free.room_id), orion.tr("Choose a room."))
  local row = orion.assert(Hotel.write_all(function()
    local guest = new_guest and orion.assert(Hotel.guest_named(new_guest)).id or values.guest
    return Hotel.save(nil, {room_id = room.id, guest_id = guest, arrival = values.arrival, departure = values.departure,
      persons = values.persons or 1, rate = room.rate, extras = 0, notes = "", source_id = values.source or 0})
  end))
  self.values.new_guest = ""
  self.rows = FrontDesk.free_rooms(values.arrival, values.departure, values.persons)
  self.notice = string.format(orion.tr("Reservation %d saved: room %s for %s."), row.id, room.name, row.guest.name)
end
function Availability:data()
  local rows = FrontDesk.calendar(self.start)
  local titles = FrontDesk.calendar_titles(self.start)
  local last = calendar.add(self.start, FrontDesk.CALENDAR_DAYS - 1)
  local values = {notice = self.notice, start = self.start, summary = self.searched and string.format(orion.tr("%d rooms free"), #self.rows) or "",
    range = string.format(orion.tr("%s to %s · tonight %d of %d rooms free"), orion.formatDate(self.start), orion.formatDate(last),
      FrontDesk.calendar_free(rows, 1), #rows)}
  for key, value in pairs(self.values) do values[key] = value end
  return {values = values, titles = {calendar = titles}, lists = {free = self.rows, calendar = rows},
    disabled = {reserve = #self.rows == 0}, commands = {book = #rows > 0}}
end
return Availability

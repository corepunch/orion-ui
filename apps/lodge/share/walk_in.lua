-- A guest at the desk without a booking: name, nights and a ready room, then
-- one click books, checks in and records what they pay now.
local orion = require("orion")
local Hotel = require("hotel")
local FrontDesk = require("front_desk")
local calendar = require("orion.plugins").use("calendar", 1)
local WalkIn = {form = "walk_in", title = orion.tr("Walk-in guest")}

function WalkIn:init()
  self.values = {name = "", phone = "", nights = 1, persons = 1, pay = 0, method = 0, source = 0}
  self.notice, self.completed = "", false
  local walk_in = Hotel.booking_sources.model:find({name = orion.tr("Walk-in")})
  if walk_in then self.values.source = walk_in.id end
  self:rooms()
end
function WalkIn:keep(values) for key in pairs(self.values) do if values[key] ~= nil then self.values[key] = values[key] end end end
-- Rooms free from tonight for the stay, ready ones first.
function WalkIn:rooms()
  local today = orion.today()
  local nights = math.type(self.values.nights) == "integer" and self.values.nights >= 1 and self.values.nights or 1
  local ok, rows = pcall(FrontDesk.free_rooms, today, calendar.add(today, nights), self.values.persons or 1)
  rows = ok and rows or {}
  -- A room whose guest has not checked out yet cannot take a walk-in now.
  for i = #rows, 1, -1 do if Hotel.room_occupied(rows[i].room_id) then table.remove(rows, i) end end
  table.sort(rows, function(a, b)
    local ra, rb = a.cleaning == orion.tr("Clean"), b.cleaning == orion.tr("Clean")
    if ra ~= rb then return ra end
    if a.rate ~= b.rate then return a.rate < b.rate end
    return a.name < b.name
  end)
  self.free = rows
end
function WalkIn:search(values) self:keep(values); self:rooms() end
function WalkIn:check_in(values)
  orion.assert(not self.completed, orion.tr("This guest is already checked in. Close the window to start another walk-in."))
  self:keep(values)
  local room = orion.assert(values.free, orion.tr("Choose a room."))
  local row = orion.assert(Hotel.walk_in({name = values.name, phone = values.phone, nights = values.nights, persons = values.persons,
    room_id = room.room_id, pay = values.pay or 0, method_id = values.method, source_id = values.source}))
  self.completed = true
  self.notice = string.format(orion.tr("%s is checked in to room %s until %s. Balance: %s."), row.guest.name, row.room.name,
    orion.formatDate(row.departure), orion.formatNumber(Hotel.balance(row), 2))
  self:rooms()
end
function WalkIn:data()
  local values = {notice = self.notice}
  for key, value in pairs(self.values) do values[key] = value end
  local first = self.free[1]
  values.hint = first and string.format(orion.tr("%d rooms free from tonight; %s at %s a night is first."), #self.free, first.name, orion.formatNumber(first.rate, 2))
    or orion.tr("No room is free from tonight for this stay.")
  return {values = values, lists = {free = self.free}, disabled = {check_in = self.completed or #self.free == 0}}
end
return WalkIn

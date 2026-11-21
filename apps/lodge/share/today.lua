local orion = require("orion")
local Hotel = require("hotel")
local FrontDesk = require("front_desk")
local Documents = require("documents")
local application = require("orion.application")
local Today = {form = "today", title = orion.tr("Today")}

function Today:init() self.notice = "" end
local function chosen(row, message) return orion.assert(row and Hotel.reservations.model:find(row.reservation_id), message) end
function Today:move(row, target)
  orion.assert(Hotel.write_all(function() return Hotel.transition(row.id, target) end))
end
function Today:check_in(values)
  local row = chosen(values.arrivals, orion.tr("Choose an arrival."))
  self:move(row, "checked_in")
  self.notice = string.format(orion.tr("%s checked in to room %s."), row.guest.name, row.room.name)
end
function Today:check_out(values)
  local row = chosen(values.departures, orion.tr("Choose a departure."))
  self:move(row, "checked_out")
  self.notice = string.format(orion.tr("%s checked out of room %s."), row.guest.name, row.room.name)
end
function Today:open_payment(row)
  Hotel.pending_payment_id = row.id
  orion.assert(application.open("payment"))
end
-- The selected guest: the departure when one is chosen, else the guest in house.
function Today:guest(values, message)
  return chosen(values.departures or values.in_house or values.arrivals, message or orion.tr("Choose a guest."))
end
function Today:settle(values) self:open_payment(chosen(values.departures, orion.tr("Choose a departure."))) end
function Today:payment(values) self:open_payment(self:guest(values)) end
function Today:registration(values)
  local row = chosen(values.arrivals or values.in_house, orion.tr("Choose an arrival."))
  self.notice = string.format(orion.tr("Registration form saved: %s"), orion.assert(application.write("registration.html", Documents.registration(row))))
end
function Today:invoice(values)
  local row = chosen(values.departures or values.in_house, orion.tr("Choose a departure."))
  self.notice = string.format(orion.tr("Guest bill saved: %s"), orion.assert(application.write("bill.html", Documents.bill(row))))
end
function Today:extend(values)
  local row = chosen(values.in_house or values.departures, orion.tr("Choose a guest in house."))
  local saved = orion.assert(Hotel.extend(row.id, 1))
  self.notice = string.format(orion.tr("%s now stays until %s."), row.guest.name, orion.formatDate(saved.departure))
end
function Today:no_show(values)
  local row = chosen(values.arrivals, orion.tr("Choose an arrival."))
  orion.assert(Hotel.no_show(row.id))
  self.notice = string.format(orion.tr("%s marked as a no-show; room %s is free again."), row.guest.name, row.room.name)
end
function Today:open_reservation(values)
  local row = self:guest(values)
  Hotel.focus_reservation = row.id
  orion.assert(application.open("reception"))
end
function Today:arrivals_list()
  self.notice = string.format(orion.tr("Arrivals and departures list saved: %s"),
    orion.assert(application.write("front-desk-list.html", Documents.front_desk_list(orion.today()))))
end
function Today:refresh() self.notice = "" end
-- A row of Needs attention opens the place that settles it.
function Today:attend(values)
  local row = orion.assert(values.attention, orion.tr("Choose a task."))
  local page = row.target ~= "" and row.target or nil
  if page and page ~= "today" then orion.assert(application.open(page)) end
end
function Today:data()
  local day = orion.today()
  local arrivals, departures, in_house = FrontDesk.movements(day)
  local attention = FrontDesk.attention(day)
  local rooms, occupied = #Hotel.rooms.model:all(), #in_house
  local due = 0
  for _, row in ipairs(departures) do if Hotel.valid_amount(row.balance) and row.balance > 0 then due = due + 1 end end
  return {values = {
    day = string.format(orion.tr("Today, %s"), orion.formatDate(day)),
    occupancy = rooms > 0 and string.format(orion.tr("%d of %d rooms occupied · %d arriving · %d leaving"), occupied, rooms, #arrivals, #departures) or "",
    attention_count = #attention > 0 and tostring(#attention) or orion.tr("All clear"),
    arrivals_count = tostring(#arrivals), departures_count = tostring(#departures), in_house_count = tostring(#in_house),
    notice = self.notice,
  }, lists = {attention = attention, arrivals = arrivals, departures = departures, in_house = in_house},
  disabled = {check_in = #arrivals == 0, registration = #arrivals == 0, settle = due == 0 and #departures == 0, check_out = #departures == 0, invoice = #departures == 0 and #in_house == 0},
  commands = {extend = #in_house > 0, no_show = #arrivals > 0, payment = #in_house + #departures > 0, open_reservation = #arrivals + #departures + #in_house > 0}}
end
return Today

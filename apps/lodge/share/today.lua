local orion = require("orion")
local Hotel = require("hotel")
local FrontDesk = require("front_desk")
local Documents = require("documents")
local application = require("orion.application")
local Today = {form = "today", title = orion.tr("Today")}

function Today:init() self.notice = "" end
local function chosen(row, message) return orion.assert(row and Hotel.reservations.model:find(row.reservation_id), message) end
function Today:move(row, target)
  Hotel.reservations.model:transaction(function()
    orion.assert(Hotel.transition(row.id, target))
    orion.assert(application.save("lodge"))
  end)
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
function Today:settle(values) self:open_payment(chosen(values.departures, orion.tr("Choose a departure."))) end
function Today:payment(values) self:open_payment(chosen(values.in_house, orion.tr("Choose a guest in house."))) end
function Today:registration(values)
  local row = chosen(values.arrivals, orion.tr("Choose an arrival."))
  self.notice = string.format(orion.tr("Registration form saved: %s"), orion.assert(application.write("registration.html", Documents.registration(row))))
end
function Today:invoice(values)
  local row = chosen(values.departures or values.in_house, orion.tr("Choose a departure."))
  self.notice = string.format(orion.tr("Guest bill saved: %s"), orion.assert(application.write("bill.html", Documents.bill(row))))
end
function Today:data()
  local day = orion.today()
  local arrivals, departures, in_house = FrontDesk.movements(day)
  local attention = FrontDesk.attention(day)
  local rooms, occupied = #Hotel.rooms.model:all(), #in_house
  return {values = {
    day = string.format(orion.tr("Today, %s"), orion.formatDate(day)),
    occupancy = rooms > 0 and string.format(orion.tr("%d of %d rooms occupied"), occupied, rooms) or "",
    attention_count = #attention > 0 and tostring(#attention) or orion.tr("All clear"),
    arrivals_count = tostring(#arrivals), departures_count = tostring(#departures), in_house_count = tostring(#in_house),
    notice = self.notice,
  }, lists = {attention = attention, arrivals = arrivals, departures = departures, in_house = in_house},
  disabled = {check_in = #arrivals == 0, registration = #arrivals == 0, settle = #departures == 0, check_out = #departures == 0, invoice = #departures == 0}}
end
return Today

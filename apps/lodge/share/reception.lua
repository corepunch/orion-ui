local orion = require("orion")
local Hotel = require("hotel")
local application = require("orion.application")
local Plugins = require("orion.plugins")
local calendar = Plugins.use("calendar", 1)
local confirmation = require("remote.confirmation")
local group_statement = require("remote.group_statement")
local money = Plugins.use("money", 1)
local Documents = require("documents")
local Reception = {form = "reception", title = orion.tr("Orion Lodge")}

function Reception:init() self:new_booking() end
function Reception:new_booking()
  local today = orion.today()
  self.editing = nil
  self.fields = {room = 0, guest = 0, new_guest = "", arrival = today, departure = calendar.add(today, 1), persons = 1, rate = 0, extras = 0, paid = 0, notes = "", source = 0, search = self.fields and self.fields.search or ""}
  self.notice = ""
end
function Reception:save_data() orion.assert(application.save("lodge")) end
function Reception:remember(v)
  for _, key in ipairs({"room", "guest", "new_guest", "arrival", "departure", "persons", "rate", "extras", "notes", "source", "search"}) do self.fields[key] = v[key] end
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
  self:load(row)
end
function Reception:load(row)
  self.editing = row.id
  self.fields = {room = row.room_id, guest = row.guest_id, new_guest = "", arrival = row.arrival, departure = row.departure,
    persons = row.persons, rate = row.rate, extras = row.extras, paid = row.paid, notes = row.notes, source = row.source_id,
    search = self.fields and self.fields.search or ""}
end
-- Opens the next active reservation whose guest or room matches the text.
function Reception:find(v)
  self:remember(v)
  local text = orion.assert(v.search and v.search:find("%S") and v.search:lower(), orion.tr("Enter part of a guest or room name."))
  local matches = {}
  for _, row in ipairs(Hotel.reservations.model:select(nil, {order = "arrival, id"})) do
    local guest, room = row.guest and row.guest.name:lower() or "", row.room and row.room.name:lower() or ""
    if (guest:find(text, 1, true) or room:find(text, 1, true)) and row.status ~= "cancelled" then matches[#matches + 1] = row end
  end
  orion.assert(#matches > 0, orion.tr("No active reservation matches."))
  local index = 1
  for i, row in ipairs(matches) do if row.id == self.editing then index = i % #matches + 1 end end
  self:load(matches[index])
  self.notice = string.format(orion.tr("Match %d of %d: reservation %d, %s, room %s, %s to %s."), index, #matches, matches[index].id,
    matches[index].guest.name, matches[index].room.name, orion.formatDate(matches[index].arrival), orion.formatDate(matches[index].departure))
end
function Reception:room_rate(v)
  local room = orion.assert(Hotel.rooms.model:find(v.room), orion.tr("Choose a room."))
  for k in pairs(self.fields) do if v[k] ~= nil then self.fields[k] = v[k] end end
  self.fields.room, self.fields.guest, self.fields.rate = v.room, v.guest, room.rate
end
-- The guest chosen in the list, or a new guest typed by name (found again
-- when a guest of that name is already on file).
function Reception:reserve(v)
  local new_guest = v.new_guest and v.new_guest:find("%S") and v.new_guest
  local creating = not self.editing
  local row = self:write(Hotel.reservations.model, function()
    local guest = v.guest or 0
    if new_guest and (guest == 0 or not self.editing) then guest = orion.assert(Hotel.guest_named(new_guest)).id end
    return Hotel.save(self.editing, {room_id = v.room or 0, guest_id = guest,
      arrival = v.arrival or "", departure = v.departure or "", persons = v.persons or 0, rate = v.rate or 0,
      extras = v.extras or 0, notes = v.notes or "", source_id = v.source or 0})
  end)
  self:load(row)
  self.notice = string.format(creating and orion.tr("Reservation %d saved: room %s for %s, %s to %s.") or orion.tr("Reservation %d updated: room %s for %s, %s to %s."),
    row.id, row.room.name, row.guest.name, orion.formatDate(row.arrival), orion.formatDate(row.departure))
end
function Reception:extend(v)
  local row = self:selected(v)
  local saved = orion.assert(Hotel.extend(row.id, 1))
  if self.editing == row.id then self:load(saved) end
  self.notice = string.format(orion.tr("%s now stays until %s."), row.guest.name, orion.formatDate(saved.departure))
end
function Reception:shorten(v)
  local row = self:selected(v)
  local saved = orion.assert(Hotel.extend(row.id, -1))
  if self.editing == row.id then self:load(saved) end
  self.notice = string.format(orion.tr("%s now stays until %s."), row.guest.name, orion.formatDate(saved.departure))
end
function Reception:no_show(v)
  local row = self:selected(v)
  orion.assert(Hotel.no_show(row.id))
  self.notice = string.format(orion.tr("%s marked as a no-show; room %s is free again."), row.guest.name, row.room.name)
end
function Reception:transition(v, target)
  local selected = orion.assert(v.bookings, orion.tr("Choose a reservation."))
  self:remember(v)
  self:write(Hotel.reservations.model, function() return Hotel.transition(selected.id, target) end)
end
function Reception:check_in(v) self:transition(v, "checked_in") end
function Reception:check_out(v) self:transition(v, "checked_out") end
function Reception:preview_cancel(v)
  self.cancel_booking, self.cancel_notice = nil, ""
  self:remember(v)
  local selected = orion.assert(v.bookings, orion.tr("Choose a reservation."))
  local row = orion.assert(Hotel.reservations.model:find(selected.id))
  orion.assert(row.status == "reserved", orion.tr("Only reserved bookings can be cancelled."))
  self.cancel_booking = {id = row.id, room_id = row.room_id, guest_id = row.guest_id, arrival = row.arrival,
    departure = row.departure, paid = money.minor(Hotel.paid(row))}
  self.cancel_notice = string.format(orion.tr("Cancel reservation %d · %s · %s to %s · Payments held: %s %s. Refunds are separate."),
    row.id, row.guest.name, orion.formatDate(row.arrival), orion.formatDate(row.departure),
    orion.formatNumber(self.cancel_booking.paid / 100, 2), orion.assert(Hotel.hotel_settings.model:all()[1]).currency)
end
function Reception:confirm_cancel(v)
  local selected = orion.assert(v.bookings, orion.tr("Choose a reservation."))
  local preview = orion.assert(self.cancel_booking, orion.tr("Preview the cancellation again."))
  local row = Hotel.reservations.model:find(selected.id)
  orion.assert(row and row.id == preview.id and row.room_id == preview.room_id and row.guest_id == preview.guest_id and
    row.arrival == preview.arrival and row.departure == preview.departure and row.status == "reserved" and
    money.minor(Hotel.paid(row)) == preview.paid, orion.tr("Preview the cancellation again."))
  self:transition(v, "cancelled")
  self.cancel_booking, self.cancel_notice = nil, ""
end
function Reception:payment(v)
  local selected = orion.assert(v.bookings, orion.tr("Choose a reservation."))
  self:remember(v)
  Hotel.pending_payment_id = selected.id
  orion.assert(application.open("payment"))
end
function Reception:night_prices(v)
  local selected = orion.assert(v.bookings, orion.tr("Choose a reservation."))
  self:remember(v)
  Hotel.pending_night_id = selected.id
  orion.assert(application.open("night_prices"))
end
function Reception:confirmation(v)
  self:remember(v)
  local selected = orion.assert(v.bookings, orion.tr("Choose a reservation."))
  local row = orion.assert(Hotel.reservations.model:find(selected.id))
  local html = confirmation({guest = row.guest.name, room = row.room.name, arrival = row.arrival, departure = row.departure,
    status = row.status, currency = orion.assert(Hotel.hotel_settings.model:all()[1]).currency,
    total = Hotel.total(row), paid = Hotel.paid(row), balance = Hotel.balance(row)})
  self.notice = string.format(orion.tr("Confirmation saved: %s"), orion.assert(application.write("confirmation.html", html)))
end
function Reception:selected(v)
  self:remember(v)
  local selected = orion.assert(v.bookings, orion.tr("Choose a reservation."))
  return orion.assert(Hotel.reservations.model:find(selected.id))
end
function Reception:registration(v)
  local row = self:selected(v)
  self.notice = string.format(orion.tr("Registration form saved: %s"), orion.assert(application.write("registration.html", Documents.registration(row))))
end
function Reception:invoice(v)
  local row = self:selected(v)
  self.notice = string.format(orion.tr("Guest bill saved: %s"), orion.assert(application.write("bill.html", Documents.bill(row))))
end
function Reception:group_statement(v)
  self:remember(v)
  local selected = orion.assert(v.bookings, orion.tr("Choose a reservation."))
  local row = orion.assert(Hotel.reservations.model:find(selected.id))
  local group = orion.assert(row.group_id and row.group_id ~= 0 and Hotel.reservation_groups.model:find(row.group_id), orion.tr("Choose a reservation in a group."))
  local lines = {}
  for _, member in ipairs(Hotel.reservations.model:select({group_id = group.id})) do
    lines[#lines + 1] = {id = member.id, room = member.room.name, guest = member.guest.name, arrival = member.arrival, departure = member.departure,
      status = member.status, total = Hotel.total(member), paid = Hotel.paid(member), balance = Hotel.balance(member)}
  end
  local settings = orion.assert(Hotel.hotel_settings.model:all()[1])
  local html = group_statement(group.name, lines, settings.currency)
  self.notice = string.format(orion.tr("Group statement saved: %s"), orion.assert(application.write("group-statement.html", html)))
end
function Reception:preview_group_cancel(v)
  self.cancel_group_id, self.cancel_group_notice = nil, ""
  self:remember(v)
  local selected = orion.assert(v.bookings, orion.tr("Choose a reservation."))
  local row = orion.assert(Hotel.reservations.model:find(selected.id))
  local group = orion.assert(row.group_id and row.group_id ~= 0 and Hotel.reservation_groups.model:find(row.group_id), orion.tr("Choose a reservation in a group."))
  local members, paid = Hotel.reservations.model:select({group_id = group.id}), 0
  for _, member in ipairs(members) do
    orion.assert(member.status == "reserved", orion.tr("All group reservations must be reserved before cancelling the group."))
    paid = paid + money.minor(Hotel.paid(member))
  end
  self.cancel_group_id, self.cancel_group_count, self.cancel_group_paid = group.id, #members, paid
  self.cancel_group_notice = string.format(orion.tr("Cancel group %s: %d reservations; payments held: %s. Refunds are separate."), group.name, #members, orion.formatNumber(paid / 100, 2))
end
function Reception:confirm_group_cancel(v)
  local selected = orion.assert(v.bookings, orion.tr("Choose a reservation."))
  local row = orion.assert(Hotel.reservations.model:find(selected.id))
  orion.assert(self.cancel_group_id and row.group_id == self.cancel_group_id, orion.tr("Preview the group cancellation again."))
  local members, paid = Hotel.reservations.model:select({group_id = row.group_id}), 0
  for _, member in ipairs(members) do paid = paid + money.minor(Hotel.paid(member)) end
  orion.assert(#members == self.cancel_group_count and paid == self.cancel_group_paid, orion.tr("Preview the group cancellation again."))
  orion.assert(Hotel.cancel_group(row.group_id))
  self.cancel_group_id, self.cancel_group_notice = nil, ""
  self.notice = orion.tr("Group reservations cancelled. Check payments for refunds.")
end
function Reception:preview_group_settlement(v)
  self.settlement, self.settlement_notice = nil, ""
  self:group_statement(v)
  local selected = orion.assert(v.bookings, orion.tr("Choose a reservation."))
  local row = orion.assert(Hotel.reservations.model:find(selected.id))
  local allocation, total = {}, 0
  for _, member in ipairs(Hotel.reservations.model:select({group_id = row.group_id})) do
    local balance = money.minor(Hotel.balance(member))
    orion.assert(member.status ~= "cancelled" and balance >= 0, orion.tr("Settle cancelled reservations and credits individually before paying the group."))
    orion.assert(balance <= 100000000 and money.minor(Hotel.paid(member)) + balance <= 100000000, orion.tr("The resulting paid amount is out of range."))
    if balance > 0 then allocation[#allocation + 1] = {id = member.id, amount_minor = balance}; total = total + balance end
  end
  orion.assert(#allocation > 0, orion.tr("This group has no outstanding balance."))
  self.settlement = {id = row.group_id, allocation = allocation, key = Hotel.next_group_settlement_key()}
  self.settlement_notice = string.format(orion.tr("Pay group %s: %s %s across %d reservations. Review the exported group statement before confirming."), orion.assert(Hotel.reservation_groups.model:find(row.group_id)).name, orion.formatNumber(total / 100, 2), orion.assert(Hotel.hotel_settings.model:all()[1]).currency, #allocation)
end
function Reception:confirm_group_settlement(v)
  local selected = orion.assert(v.bookings, orion.tr("Choose a reservation."))
  local row = orion.assert(Hotel.reservations.model:find(selected.id))
  local settlement = orion.assert(self.settlement and self.settlement.id == row.group_id and self.settlement, orion.tr("Preview the group settlement again."))
  orion.assert(Hotel.settle_group(settlement.id, settlement.allocation, settlement.key))
  self.settlement, self.settlement_notice = nil, ""
  self.notice = orion.tr("Group settlement recorded.")
end
function Reception:data()
  -- Another page (the calendar, Today, the assistant) asked to show a booking
  -- or to start one with a room and dates.
  if Hotel.focus_reservation then
    local row = Hotel.reservations.model:find(Hotel.focus_reservation)
    Hotel.focus_reservation = nil
    if row then self:load(row); self.notice = "" end
  elseif Hotel.prefill then
    local prefill = Hotel.prefill
    Hotel.prefill = nil
    self:new_booking()
    for key, value in pairs(prefill) do self.fields[key] = value end
    local room = Hotel.rooms.model:find(self.fields.room or 0)
    if room and (self.fields.rate or 0) == 0 then self.fields.rate = room.rate end
  end
  local count, outstanding = 0, 0
  for _, row in ipairs(Hotel.reservations.model:select(nil, {order = "arrival, id"})) do
    local balance = Hotel.balance(row)
    if row.status ~= "cancelled" then outstanding = outstanding + money.minor(balance) end
    count = count + 1
  end
  local values = {editor = self.editing and string.format(orion.tr("Edit reservation %d"), self.editing) or orion.tr("New reservation"),
    summary = string.format(orion.tr("%d reservations · Outstanding: %s"), count, orion.formatNumber(outstanding / 100, 2)), notice = self.notice, cancel_notice = self.cancel_notice or "", group_cancel_notice = self.cancel_group_notice or "", settlement_notice = self.settlement_notice or ""}
  local settings = Hotel.hotel_settings.model:all()[1]
  values.currency = settings and string.format(orion.tr("Country: %s · Currency: %s"), settings.country, settings.currency) or orion.tr("Hotel country and currency are not configured.")
  for k, v in pairs(self.fields) do values[k] = v end
  local edited = self.editing and Hotel.reservations.model:find(self.editing)
  values.paid = orion.formatNumber(edited and Hotel.paid(edited) or 0, 2)
  local any = count > 0
  return {values = values, commands = {confirm_cancel = self.cancel_booking ~= nil, confirm_group_cancel = self.cancel_group_id ~= nil,
    confirm_group_settlement = self.settlement ~= nil, edit = any, check_in = any, check_out = any, payment = any, preview_cancel = any,
    confirmation = any, registration = any, invoice = any, night_prices = any, extend = any, shorten = any, no_show = any,
    group_statement = any, preview_group_cancel = any, preview_group_settlement = any}}
end
return Reception

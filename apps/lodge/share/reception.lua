local orion = require("orion")
local Hotel = require("hotel")
local application = require("orion.application")
local Plugins = require("orion.plugins")
local calendar = Plugins.use("calendar", 1)
local confirmation = require("remote.confirmation")
local group_statement = require("remote.group_statement")
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
  for _, key in ipairs({"room", "guest", "arrival", "departure", "persons", "rate", "extras", "notes"}) do self.fields[key] = v[key] end
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
    extras = v.extras or 0, notes = v.notes or ""}) end)
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
  local count, outstanding = 0, 0
  for _, row in ipairs(Hotel.reservations.model:select(nil, {order = "arrival, id"})) do
    local balance = Hotel.balance(row)
    if row.status ~= "cancelled" then outstanding = outstanding + money.minor(balance) end
    count = count + 1
  end
  local values = {editor = self.editing and string.format(orion.tr("Edit reservation %d"), self.editing) or orion.tr("New reservation"),
    summary = string.format(orion.tr("%d reservations · Outstanding: %s"), count, orion.formatNumber(outstanding / 100, 2)), notice = self.notice, group_cancel_notice = self.cancel_group_notice or "", settlement_notice = self.settlement_notice or ""}
  local settings = Hotel.hotel_settings.model:all()[1]
  values.currency = settings and string.format(orion.tr("Country: %s · Currency: %s"), settings.country, settings.currency) or orion.tr("Hotel country and currency are not configured.")
  for k, v in pairs(self.fields) do values[k] = v end
  local edited = self.editing and Hotel.reservations.model:find(self.editing)
  values.paid = orion.formatNumber(edited and Hotel.paid(edited) or 0, 2)
  return {values = values, disabled = {confirm_group_cancel = not self.cancel_group_id, confirm_group_settlement = not self.settlement}}
end
return Reception

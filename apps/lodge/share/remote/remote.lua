local orion = require("orion")
local Remote = require("orion.remote")
local Plugins = require("orion.plugins")
local application, confirmation = require("orion.application"), require("confirmation")
local group_statement = require("group_statement")
local close_display = require("close_display")
local calendar, money = Plugins.use("calendar", 1), Plugins.use("money", 1)
local Page = {form = "remote", title = orion.tr("Lodge · Shared server")}
local status_labels = {reserved = "Reserved", checked_in = "Checked in", checked_out = "Checked out", cancelled = "Cancelled"}
local cleaning_labels = {inspection_needed = "Inspection needed", dirty = "Dirty", clean = "Clean"}
local inputs = {"room_name", "room_category", "capacity", "room_rate", "edit_room_choice", "guest_name", "phone", "edit_guest_choice", "clean_room", "room", "guest", "arrival", "departure", "persons", "rate", "extras", "notes", "amount", "group_name", "group_room1", "group_room2", "group_guest", "group_arrival", "group_departure", "group_persons1", "group_persons2", "group_rate1", "group_rate2", "statement_group", "night", "night_amount", "night_booking"}
function Page:init()
  self.client = Remote.new(self.app.handle, "lodge")
  self.values = {room_name = "", room_category = "", capacity = 2, room_rate = 0, edit_room_choice = 0, guest_name = "", phone = "", edit_guest_choice = 0, clean_room = 0,
    room = 0, guest = 0, arrival = "", departure = "", persons = 1, rate = 0, extras = 0, notes = "", amount = 0}
  self.values.group_name, self.values.group_room1, self.values.group_room2, self.values.group_guest = "", 0, 0, 0
  self.values.group_arrival, self.values.group_departure, self.values.group_persons1, self.values.group_persons2 = "", "", 1, 1
  self.values.group_rate1, self.values.group_rate2, self.values.night, self.values.night_amount, self.values.night_booking = 0, 0, "", 0, 0
  self.values.statement_group = 0
  self.day_report, self.cancelled_paid_report = "", ""
  self.allowed, self.connected, self.connection, self.notice = {}, false, orion.tr("Not connected."), ""
end
function Page:remember(values)
  for _, key in ipairs(inputs) do if values[key] ~= nil then self.values[key] = values[key] end end
end
function Page:load_snapshot(after)
  self.cancel_group_id, self.cancel_group_notice = nil, ""
  self.day_report, self.cancelled_paid_report = "", ""
  self.snapshot_ready = false
  local ok, err = self.client:submit("snapshot", {}, function(result, failure)
    if not result then self.notice = failure; return end
    if type(result.today) ~= "string" or not pcall(calendar.day, result.today) then
      self.notice = orion.tr("The server did not provide a valid hotel day."); return
    end
    self.server_day = result.today
    if not self.dates_initialized then
      self.values.arrival, self.values.departure = result.today, calendar.add(result.today, 1)
      self.values.group_arrival, self.values.group_departure, self.values.night = result.today, calendar.add(result.today, 1), result.today
      self.dates_initialized = true
    end
    local rooms, guests, groups, reservations, day_closes, room_names, guest_names = {}, {}, {}, {}, {}, {}, {}
    self.reservations, self.guests, self.rooms, self.groups = {}, {}, {}, {}
    self.currency = result.hotel and result.hotel.currency
    for _, item in ipairs(result.rooms or {}) do
      self.rooms[item.id] = item
      rooms[#rooms + 1] = {id = item.id, name = item.name, cleaning_state = orion.tr(cleaning_labels[item.cleaning_state] or item.cleaning_state),
        occupancy_label = orion.tr(item.occupied == nil and "Occupancy unknown" or (item.occupied and "Guest checked in" or "No guest checked in"))}
      room_names[item.id] = item.name
    end
    for _, item in ipairs(result.guests or {}) do
      self.guests[item.id] = item
      guests[#guests + 1] = {id = item.id, name = item.name}; guest_names[item.id] = item.name
    end
    for _, item in ipairs(result.reservation_groups or {}) do
      self.groups[item.id] = item; groups[#groups + 1] = {id = item.id, name = item.name}
    end
    for _, item in ipairs(result.day_closes or {}) do
      day_closes[#day_closes + 1] = {id = item.id, day = item.day, actor = item.actor,
        net_display = close_display(item, "net_display"), outstanding_display = close_display(item, "outstanding_display"),
        cancelled_count_display = close_display(item, "cancelled_count_display"), cancelled_paid_display = close_display(item, "cancelled_paid_display")}
    end
    for _, item in ipairs(result.reservations or {}) do
      self.reservations[item.id] = item
      reservations[#reservations + 1] = {id = item.id, choice = string.format("#%d · %s · %s", item.id, room_names[item.room_id] or "", item.arrival), room_name = room_names[item.room_id] or "", guest_name = guest_names[item.guest_id] or "",
        arrival = item.arrival, departure = item.departure, status = orion.tr(status_labels[item.status] or item.status), balance = item.balance}
    end
    Remote.replace_many({
      {source = "lodge_remote_cache.rooms", rows = rooms, fields = {"id", "name", "cleaning_state", "occupancy_label"}},
      {source = "lodge_remote_cache.guests", rows = guests, fields = {"id", "name"}},
      {source = "lodge_remote_cache.groups", rows = groups, fields = {"id", "name"}},
      {source = "lodge_remote_cache.day_closes", rows = day_closes, fields = {"id", "day", "actor", "net_display", "outstanding_display", "cancelled_count_display", "cancelled_paid_display"}},
      {source = "lodge_remote_cache.reservations", rows = reservations, fields = {"id", "choice", "room_name", "guest_name", "arrival", "departure", "status", "balance"}},
    })
    self.snapshot_ready = true
    self.notice = self.needs_reload and orion.tr("Reload the selected reservation before saving, or start a new reservation.") or orion.tr("Server data refreshed.")
    if after then after() end
  end)
  if not ok then self.notice = err end
end
function Page:connect(values)
  self:remember(values)
  if self.editing then self.needs_reload = true end
  if self.editing_guest then self.guest_needs_reload = true end
  if self.editing_room then self.room_needs_reload = true end
  local ok, err = self.client:refresh(function(status, failure)
    if not status then self.connected = false; self.notice = failure; return end
    if status.application ~= "lodge" then self.connected = false; self.notice = orion.tr("This server does not host Lodge."); return end
    self.connected, self.allowed, self.country, self.role = true, {}, status.country, status.role
    for _, command in ipairs(status.commands or {}) do self.allowed[command] = true end
    self.connection = string.format(orion.tr("Country: %s · Role: %s · Revision: %d"), status.country, status.role, status.revision)
    if self.client:state().pending then self.notice = orion.tr("A saved request needs retry before new work."); return end
    self:load_snapshot()
  end)
  if not ok then self.notice = err end
end
function Page:submit(values, command, args)
  self.day_report, self.cancelled_paid_report = "", ""
  self:remember(values)
  local ok, err = self.client:submit(command, args, function(result, failure)
    if not result then self.notice = failure; return end
    self.notice = orion.tr("Request completed.")
    self:load_snapshot()
  end)
  if not ok then self.notice = err end
end
function Page:retry(values)
  self:remember(values)
  local payment = self.client:state().pending_command == "payment"
  local ok, err = self.client:retry(function(result, failure)
    if not result then self.notice = failure; return end
    if payment then self.payment_done = true end
    self.notice = orion.tr("Request completed."); self:load_snapshot()
  end)
  if not ok then self.notice = err end
end
function Page:add_room(v) self:submit(v, "save_room", {name = v.room_name, category = v.room_category, capacity = v.capacity, rate = v.room_rate}) end
function Page:load_room(v)
  self:remember(v)
  local row = orion.assert(self.rooms and self.rooms[v.edit_room_choice], orion.tr("Choose a room."))
  self.editing_room, self.room_needs_reload = row.id, false
  self.values.room_name, self.values.room_category, self.values.capacity, self.values.room_rate = row.name, row.category or "", row.capacity, row.rate
end
function Page:save_room(v)
  orion.assert(self.editing_room and self.editing_room == v.edit_room_choice and not self.room_needs_reload, orion.tr("Load the room again before saving."))
  self:submit(v, "save_room", {id = self.editing_room, name = v.room_name, category = v.room_category, capacity = v.capacity, rate = v.room_rate})
end
function Page:add_guest(v) self:submit(v, "save_guest", {name = v.guest_name, phone = v.phone}) end
function Page:load_guest(v)
  self:remember(v)
  local row = orion.assert(self.guests and self.guests[v.edit_guest_choice], orion.tr("Choose a guest."))
  self.editing_guest, self.guest_needs_reload = row.id, false
  self.values.guest_name, self.values.phone = row.name, row.phone or ""
end
function Page:save_guest(v)
  orion.assert(self.editing_guest and self.editing_guest == v.edit_guest_choice and not self.guest_needs_reload, orion.tr("Load the guest again before saving."))
  self:submit(v, "save_guest", {id = self.editing_guest, name = v.guest_name, phone = v.phone})
end
function Page:mark_clean(v) self:submit(v, "set_room_cleaning", {id = v.clean_room, state = "clean"}) end
function Page:mark_dirty(v) self:submit(v, "set_room_cleaning", {id = v.clean_room, state = "dirty"}) end
function Page:mark_inspection(v) self:submit(v, "set_room_cleaning", {id = v.clean_room, state = "inspection_needed"}) end
function Page:use_room_rate(v, room_field, rate_field)
  self:remember(v)
  local room = orion.assert(self.rooms and self.rooms[v[room_field]], orion.tr("Choose a room."))
  self.values[rate_field] = room.rate
end
function Page:room_rate(v) self:use_room_rate(v, "room", "rate") end
function Page:group_rate_first(v) self:use_room_rate(v, "group_room1", "group_rate1") end
function Page:group_rate_second(v) self:use_room_rate(v, "group_room2", "group_rate2") end
function Page:reserve(v)
  orion.assert(not self.needs_reload, orion.tr("Reload the selected reservation before saving, or start a new reservation."))
  self:submit(v, "reserve", {id = self.editing, room_id = v.room, guest_id = v.guest, arrival = v.arrival, departure = v.departure,
    persons = v.persons, rate = v.rate, extras = v.extras, notes = v.notes})
end
function Page:new_booking(v)
  self:remember(v)
  self:load_snapshot(function()
    local day = self.server_day
    self.editing, self.needs_reload = nil, false
    self.values.room, self.values.guest, self.values.arrival, self.values.departure = 0, 0, day, calendar.add(day, 1)
    self.values.persons, self.values.rate, self.values.extras, self.values.notes = 1, 0, 0, ""
  end)
end
function Page:new_group(v)
  self:remember(v)
  self:load_snapshot(function()
    local day = self.server_day
    self.values.group_name, self.values.group_guest, self.values.group_room1, self.values.group_room2 = "", 0, 0, 0
    self.values.group_arrival, self.values.group_departure = day, calendar.add(day, 1)
    self.values.group_persons1, self.values.group_persons2, self.values.group_rate1, self.values.group_rate2 = 1, 1, 0, 0
  end)
end
function Page:edit_booking(v)
  self:remember(v)
  local selected = orion.assert(v.bookings, orion.tr("Choose a reservation."))
  local row = orion.assert(self.reservations and self.reservations[selected.id], orion.tr("Refresh the server before starting a request."))
  self.editing, self.needs_reload = row.id, false
  self.values.room, self.values.guest, self.values.arrival, self.values.departure = row.room_id, row.guest_id, row.arrival, row.departure
  self.values.persons, self.values.rate, self.values.extras, self.values.notes = row.persons, row.rate, row.extras, row.notes or ""
end
function Page:transition(v, command)
  local selected = orion.assert(v.bookings, orion.tr("Choose a reservation."))
  self:submit(v, command, {id = selected.id})
end
function Page:check_in(v) self:transition(v, "check_in") end
function Page:check_out(v) self:transition(v, "check_out") end
function Page:cancel(v) self:transition(v, "cancel") end
function Page:confirmation(v)
  self:remember(v)
  local selected = orion.assert(v.bookings, orion.tr("Choose a reservation."))
  local id = selected.id
  self:load_snapshot(function()
    local row = orion.assert(self.reservations[id], orion.tr("Choose a reservation."))
    local room, guest = orion.assert(self.rooms[row.room_id]), orion.assert(self.guests[row.guest_id])
    local html = confirmation({guest = guest.name, room = room.name, arrival = row.arrival, departure = row.departure,
      status = row.status, currency = orion.assert(self.currency), total = row.total, paid = row.paid, balance = row.balance})
    local written, path = pcall(application.write, "confirmation.html", html)
    self.notice = written and string.format(orion.tr("Confirmation saved: %s"), path) or tostring(path)
  end)
end
function Page:payment(v, sign)
  orion.assert(not self.payment_done, orion.tr("Start a new payment or refund before recording another."))
  local selected = orion.assert(v.bookings, orion.tr("Choose a reservation."))
  local amount = v.amount
  orion.assert(type(amount) == "number" and amount > 0 and amount <= 1000000 and math.abs(amount * 100 - math.floor(amount * 100 + 0.5)) < 1e-7,
    orion.tr("Enter a positive amount with at most two decimal places."))
  self:remember(v)
  local ok, err = self.client:submit("payment", {id = selected.id, amount_minor = sign * money.minor(amount), request_key = Remote.new_key()}, function(result, failure)
    if not result then self.notice = failure; return end
    self.payment_done = true
    self:load_snapshot()
  end)
  if not ok then self.notice = err end
end
function Page:pay(v) self:payment(v, 1) end
function Page:refund(v) self:payment(v, -1) end
function Page:new_payment(v)
  local state = self.client:state()
  orion.assert(self.payment_done and not state.pending and not state.busy and not state.conflict, orion.tr("Start a new payment or refund before recording another."))
  self:remember(v)
  self.payment_done, self.values.amount = false, 0
end
function Page:group_reserve(v)
  local stays = {}
  for i = 1, 2 do
    stays[i] = {room_id = v["group_room" .. i], guest_id = v.group_guest, arrival = v.group_arrival,
      departure = v.group_departure, persons = v["group_persons" .. i], rate = v["group_rate" .. i], extras = 0, notes = ""}
  end
  self:submit(v, "group_reserve", {name = v.group_name, stays = stays})
end
function Page:group_statement(v)
  self:remember(v)
  local id = orion.assert(v.statement_group and v.statement_group > 0 and v.statement_group, orion.tr("Choose a reservation group."))
  self:load_snapshot(function()
    local group = orion.assert(self.groups[id], orion.tr("Choose a reservation group."))
    local lines = {}
    for _, row in pairs(self.reservations) do
      if row.group_id == id then
        local room, guest = orion.assert(self.rooms[row.room_id]), orion.assert(self.guests[row.guest_id])
        lines[#lines + 1] = {id = row.id, room = room.name, guest = guest.name, arrival = row.arrival, departure = row.departure,
          status = row.status, total = row.total, paid = row.paid, balance = row.balance}
      end
    end
    local html = group_statement(group.name, lines, orion.assert(self.currency))
    local written, path = pcall(application.write, "group-statement.html", html)
    self.notice = written and string.format(orion.tr("Group statement saved: %s"), path) or tostring(path)
  end)
end
function Page:preview_group_cancel(v)
  self.cancel_group_id, self.cancel_group_notice = nil, ""
  self:remember(v)
  local group = orion.assert(self.groups and self.groups[v.statement_group], orion.tr("Choose a reservation group."))
  local count, paid = 0, 0
  for _, row in pairs(self.reservations) do
    if row.group_id == group.id then
      orion.assert(row.status == "reserved", orion.tr("All group reservations must be reserved before cancelling the group."))
      count, paid = count + 1, paid + money.minor(row.paid)
    end
  end
  orion.assert(count > 0, orion.tr("This group has no reservations."))
  self.cancel_group_id, self.cancel_group_revision = group.id, self.client:state().revision
  self.cancel_group_notice = string.format(orion.tr("Cancel group %s: %d reservations; payments held: %s. Refunds are separate."), group.name, count, orion.formatNumber(paid / 100, 2))
end
function Page:confirm_group_cancel(v)
  orion.assert(self.cancel_group_id and self.cancel_group_id == v.statement_group and self.cancel_group_revision == self.client:state().revision,
    orion.tr("Preview the group cancellation again."))
  local id = self.cancel_group_id
  self.cancel_group_id, self.cancel_group_notice = nil, ""
  self:submit(v, "group_cancel", {id = id})
end
function Page:night_price(v, command)
  local id = orion.assert(v.night_booking and v.night_booking > 0 and v.night_booking, orion.tr("Choose a reservation."))
  local args = {id = id, night = v.night}
  if command == "set_night" then args.amount_minor = money.minor(v.night_amount) end
  self:submit(v, command, args)
end
function Page:set_night(v) self:night_price(v, "set_night") end
function Page:reset_night(v) self:night_price(v, "reset_night") end
function Page:preview_day(v)
  self:remember(v)
  local ok, err = self.client:submit("day_preview", {}, function(report, failure)
    if not report then self.notice = failure; return end
    self.day_report = string.format(orion.tr("Day: %s · Net payments: %s · Outstanding: %s · Overdue: %d"),
      orion.formatDate(report.day), orion.formatNumber(report.net_minor / 100, 2), orion.formatNumber(report.outstanding_minor / 100, 2), report.pending)
    self.cancelled_paid_report = string.format(orion.tr("Cancelled reservations with payments: %d · Paid: %s"), report.cancelled_paid_count, orion.formatNumber(report.cancelled_paid_minor / 100, 2))
    self.notice = report.closed and orion.tr("Today is closed.") or orion.tr("Day preview refreshed.")
  end)
  if not ok then self.notice = err end
end
function Page:close_day(v) self:submit(v, "close_day", {}) end
function Page:data()
  local state = self.client:state()
  local blocked = not self.connected or not self.snapshot_ready or state.busy or state.pending or state.conflict
  local disabled = {connect = state.busy or (state.pending and not state.conflict), retry = state.busy or not state.pending}
  local map = {add_room = "save_room", add_guest = "save_guest", mark_clean = "set_room_cleaning", mark_dirty = "set_room_cleaning", mark_inspection = "set_room_cleaning", reserve = "reserve",
    check_in = "check_in", check_out = "check_out", cancel = "cancel", confirmation = "snapshot", pay = "payment", refund = "payment",
    group_reserve = "group_reserve", group_statement = "snapshot", preview_group_cancel = "group_cancel", confirm_group_cancel = "group_cancel", set_night = "set_night", reset_night = "reset_night", preview_day = "day_preview", close_day = "close_day"}
  for control, command in pairs(map) do disabled[control] = blocked or not self.allowed[command] end
  disabled.confirm_group_cancel = disabled.confirm_group_cancel or not self.cancel_group_id
  disabled.new_booking = blocked or not self.allowed.reserve
  disabled.new_group = blocked or not self.allowed.group_reserve
  disabled.room_rate = blocked or not self.allowed.reserve
  disabled.group_rate_first = blocked or not self.allowed.group_reserve
  disabled.group_rate_second = blocked or not self.allowed.group_reserve
  disabled.reserve = disabled.reserve or self.needs_reload == true
  disabled.pay, disabled.refund = disabled.pay or self.payment_done == true, disabled.refund or self.payment_done == true
  disabled.new_payment = blocked or not self.allowed.payment or not self.payment_done
  disabled.edit_booking = blocked or not self.allowed.reserve
  disabled.load_guest = blocked or not self.allowed.save_guest
  disabled.save_guest = disabled.load_guest or not self.editing_guest or self.guest_needs_reload
  disabled.load_room = blocked or not self.allowed.save_room
  disabled.save_room = disabled.load_room or not self.editing_room or self.room_needs_reload
  local connection = self.connected and string.format(orion.tr("Country: %s · Role: %s · Revision: %d"), self.country, self.role, state.revision) or self.connection
  local values = {connection = connection, hotel_day = self.server_day and string.format(orion.tr("Hotel day at refresh: %s"), orion.formatDate(self.server_day)) or "", notice = self.notice, group_cancel_notice = self.cancel_group_notice or "", day_report = self.day_report, cancelled_paid_report = self.cancelled_paid_report,
    editor = self.editing and string.format(orion.tr("Edit reservation %d"), self.editing) or orion.tr("New reservation"),
    payment_status = self.payment_done and orion.tr("Start a new payment or refund before recording another.") or "",
    room_editor = self.room_needs_reload and orion.tr("Load the room again before saving.") or
      (self.editing_room and string.format(orion.tr("Editing room %d"), self.editing_room) or orion.tr("No room loaded")),
    guest_editor = self.guest_needs_reload and orion.tr("Load the guest again before saving.") or
      (self.editing_guest and string.format(orion.tr("Editing guest %d"), self.editing_guest) or orion.tr("No guest loaded"))}
  for key, value in pairs(self.values) do values[key] = value end
  return {values = values, disabled = disabled}
end
return Page

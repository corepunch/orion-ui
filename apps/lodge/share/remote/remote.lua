local orion = require("orion")
local Remote = require("orion.remote")
local Plugins = require("orion.plugins")
local application, confirmation = require("orion.application"), require("confirmation")
local calendar, money = Plugins.use("calendar", 1), Plugins.use("money", 1)
local Page = {form = "remote", title = orion.tr("Lodge · Shared server")}
local status_labels = {reserved = "Reserved", checked_in = "Checked in", checked_out = "Checked out", cancelled = "Cancelled"}
local cleaning_labels = {inspection_needed = "Inspection needed", dirty = "Dirty", clean = "Clean"}
local inputs = {"room_name", "room_category", "capacity", "room_rate", "edit_room_choice", "guest_name", "phone", "edit_guest_choice", "clean_room", "room", "guest", "arrival", "departure", "persons", "rate", "extras", "notes", "amount", "group_name", "group_room1", "group_room2", "group_guest", "group_arrival", "group_departure", "group_persons1", "group_persons2", "group_rate1", "group_rate2", "night", "night_amount", "night_booking"}
function Page:init()
  local day = orion.today()
  self.client = Remote.new(self.app.handle, "lodge")
  self.values = {room_name = "", room_category = "", capacity = 2, room_rate = 0, edit_room_choice = 0, guest_name = "", phone = "", edit_guest_choice = 0, clean_room = 0,
    room = 0, guest = 0, arrival = day, departure = calendar.add(day, 1), persons = 1, rate = 0, extras = 0, notes = "", amount = 0}
  self.values.group_name, self.values.group_room1, self.values.group_room2, self.values.group_guest = "", 0, 0, 0
  self.values.group_arrival, self.values.group_departure, self.values.group_persons1, self.values.group_persons2 = day, calendar.add(day, 1), 1, 1
  self.values.group_rate1, self.values.group_rate2, self.values.night, self.values.night_amount, self.values.night_booking = 0, 0, day, 0, 0
  self.day_report = ""
  self.allowed, self.connected, self.connection, self.notice = {}, false, orion.tr("Not connected."), ""
end
function Page:remember(values)
  for _, key in ipairs(inputs) do if values[key] ~= nil then self.values[key] = values[key] end end
end
function Page:load_snapshot(after)
  self.snapshot_ready = false
  local ok, err = self.client:submit("snapshot", {}, function(result, failure)
    if not result then self.notice = failure; return end
    local rooms, guests, reservations, room_names, guest_names = {}, {}, {}, {}, {}
    self.reservations, self.guests, self.rooms = {}, {}, {}
    for _, item in ipairs(result.rooms or {}) do
      self.rooms[item.id] = item
      rooms[#rooms + 1] = {id = item.id, name = item.name, cleaning_state = orion.tr(cleaning_labels[item.cleaning_state] or item.cleaning_state)}
      room_names[item.id] = item.name
    end
    for _, item in ipairs(result.guests or {}) do
      self.guests[item.id] = item
      guests[#guests + 1] = {id = item.id, name = item.name}; guest_names[item.id] = item.name
    end
    for _, item in ipairs(result.reservations or {}) do
      self.reservations[item.id] = item
      reservations[#reservations + 1] = {id = item.id, choice = string.format("#%d · %s · %s", item.id, room_names[item.room_id] or "", item.arrival), room_name = room_names[item.room_id] or "", guest_name = guest_names[item.guest_id] or "",
        arrival = item.arrival, departure = item.departure, status = orion.tr(status_labels[item.status] or item.status), balance = item.balance}
    end
    Remote.replace_many({
      {source = "lodge_remote_cache.rooms", rows = rooms, fields = {"id", "name", "cleaning_state"}},
      {source = "lodge_remote_cache.guests", rows = guests, fields = {"id", "name"}},
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
  local ok, err = self.client:retry(function(result, failure)
    if not result then self.notice = failure; return end
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
function Page:reserve(v)
  orion.assert(not self.needs_reload, orion.tr("Reload the selected reservation before saving, or start a new reservation."))
  self:submit(v, "reserve", {id = self.editing, room_id = v.room, guest_id = v.guest, arrival = v.arrival, departure = v.departure,
    persons = v.persons, rate = v.rate, extras = v.extras, notes = v.notes})
end
function Page:new_booking(v)
  self:remember(v)
  local day = orion.today()
  self.editing, self.needs_reload = nil, false
  self.values.room, self.values.guest, self.values.arrival, self.values.departure = 0, 0, day, calendar.add(day, 1)
  self.values.persons, self.values.rate, self.values.extras, self.values.notes = 1, 0, 0, ""
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
      total = row.total, paid = row.paid, balance = row.balance})
    local written, path = pcall(application.write, "confirmation.html", html)
    self.notice = written and string.format(orion.tr("Confirmation saved: %s"), path) or tostring(path)
  end)
end
function Page:payment(v, sign)
  local selected = orion.assert(v.bookings, orion.tr("Choose a reservation."))
  local amount = v.amount
  orion.assert(type(amount) == "number" and amount > 0 and amount <= 1000000 and math.abs(amount * 100 - math.floor(amount * 100 + 0.5)) < 1e-7,
    orion.tr("Enter a positive amount with at most two decimal places."))
  self:submit(v, "payment", {id = selected.id, amount_minor = sign * money.minor(amount), request_key = Remote.new_key()})
end
function Page:pay(v) self:payment(v, 1) end
function Page:refund(v) self:payment(v, -1) end
function Page:group_reserve(v)
  local stays = {}
  for i = 1, 2 do
    stays[i] = {room_id = v["group_room" .. i], guest_id = v.group_guest, arrival = v.group_arrival,
      departure = v.group_departure, persons = v["group_persons" .. i], rate = v["group_rate" .. i], extras = 0, notes = ""}
  end
  self:submit(v, "group_reserve", {name = v.group_name, stays = stays})
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
    group_reserve = "group_reserve", set_night = "set_night", reset_night = "reset_night", preview_day = "day_preview", close_day = "close_day"}
  for control, command in pairs(map) do disabled[control] = blocked or not self.allowed[command] end
  disabled.reserve = disabled.reserve or self.needs_reload
  disabled.edit_booking = blocked or not self.allowed.reserve
  disabled.load_guest = blocked or not self.allowed.save_guest
  disabled.save_guest = disabled.load_guest or not self.editing_guest or self.guest_needs_reload
  disabled.load_room = blocked or not self.allowed.save_room
  disabled.save_room = disabled.load_room or not self.editing_room or self.room_needs_reload
  local connection = self.connected and string.format(orion.tr("Country: %s · Role: %s · Revision: %d"), self.country, self.role, state.revision) or self.connection
  local values = {connection = connection, notice = self.notice, day_report = self.day_report,
    editor = self.editing and string.format(orion.tr("Edit reservation %d"), self.editing) or orion.tr("New reservation"),
    room_editor = self.room_needs_reload and orion.tr("Load the room again before saving.") or
      (self.editing_room and string.format(orion.tr("Editing room %d"), self.editing_room) or orion.tr("No room loaded")),
    guest_editor = self.guest_needs_reload and orion.tr("Load the guest again before saving.") or
      (self.editing_guest and string.format(orion.tr("Editing guest %d"), self.editing_guest) or orion.tr("No guest loaded"))}
  for key, value in pairs(self.values) do values[key] = value end
  return {values = values, disabled = disabled}
end
return Page

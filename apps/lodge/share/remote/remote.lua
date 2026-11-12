local orion = require("orion")
local Remote = require("orion.remote")
local Plugins = require("orion.plugins")
local calendar, money = Plugins.use("calendar", 1), Plugins.use("money", 1)
local Page = {form = "remote", title = orion.tr("Lodge · Shared server")}
local status_labels = {reserved = "Reserved", checked_in = "Checked in", checked_out = "Checked out", cancelled = "Cancelled"}
local cleaning_labels = {inspection_needed = "Inspection needed", dirty = "Dirty", clean = "Clean"}
local inputs = {"room_name", "capacity", "room_rate", "guest_name", "phone", "clean_room", "room", "guest", "arrival", "departure", "persons", "rate", "extras", "amount"}
function Page:init()
  local day = orion.today()
  self.client = Remote.new(self.app.handle, "lodge")
  self.values = {room_name = "", capacity = 2, room_rate = 0, guest_name = "", phone = "", clean_room = 0,
    room = 0, guest = 0, arrival = day, departure = calendar.add(day, 1), persons = 1, rate = 0, extras = 0, amount = 0}
  self.allowed, self.connected, self.connection, self.notice = {}, false, orion.tr("Not connected."), ""
end
function Page:remember(values)
  for _, key in ipairs(inputs) do if values[key] ~= nil then self.values[key] = values[key] end end
end
function Page:load_snapshot()
  local ok, err = self.client:submit("snapshot", {}, function(result, failure)
    if not result then self.notice = failure; return end
    local rooms, guests, reservations, room_names, guest_names = {}, {}, {}, {}, {}
    for _, item in ipairs(result.rooms or {}) do
      rooms[#rooms + 1] = {id = item.id, name = item.name, cleaning_state = orion.tr(cleaning_labels[item.cleaning_state] or item.cleaning_state)}
      room_names[item.id] = item.name
    end
    for _, item in ipairs(result.guests or {}) do
      guests[#guests + 1] = {id = item.id, name = item.name}; guest_names[item.id] = item.name
    end
    for _, item in ipairs(result.reservations or {}) do
      reservations[#reservations + 1] = {id = item.id, room_name = room_names[item.room_id] or "", guest_name = guest_names[item.guest_id] or "",
        arrival = item.arrival, departure = item.departure, status = orion.tr(status_labels[item.status] or item.status), balance = item.balance}
    end
    Remote.replace_many({
      {source = "lodge_remote_cache.rooms", rows = rooms, fields = {"id", "name", "cleaning_state"}},
      {source = "lodge_remote_cache.guests", rows = guests, fields = {"id", "name"}},
      {source = "lodge_remote_cache.reservations", rows = reservations, fields = {"id", "room_name", "guest_name", "arrival", "departure", "status", "balance"}},
    })
    self.notice = orion.tr("Server data refreshed.")
  end)
  if not ok then self.notice = err end
end
function Page:connect(values)
  self:remember(values)
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
function Page:add_room(v) self:submit(v, "save_room", {name = v.room_name, category = "", capacity = v.capacity, rate = v.room_rate}) end
function Page:add_guest(v) self:submit(v, "save_guest", {name = v.guest_name, phone = v.phone}) end
function Page:mark_clean(v) self:submit(v, "set_room_cleaning", {id = v.clean_room, state = "clean"}) end
function Page:reserve(v)
  self:submit(v, "reserve", {room_id = v.room, guest_id = v.guest, arrival = v.arrival, departure = v.departure,
    persons = v.persons, rate = v.rate, extras = v.extras, notes = ""})
end
function Page:transition(v, command)
  local selected = orion.assert(v.bookings, orion.tr("Choose a reservation."))
  self:submit(v, command, {id = selected.id})
end
function Page:check_in(v) self:transition(v, "check_in") end
function Page:check_out(v) self:transition(v, "check_out") end
function Page:cancel(v) self:transition(v, "cancel") end
function Page:payment(v, sign)
  local selected = orion.assert(v.bookings, orion.tr("Choose a reservation."))
  local amount = v.amount
  orion.assert(type(amount) == "number" and amount > 0 and amount <= 1000000 and math.abs(amount * 100 - math.floor(amount * 100 + 0.5)) < 1e-7,
    orion.tr("Enter a positive amount with at most two decimal places."))
  self:submit(v, "payment", {id = selected.id, amount_minor = sign * money.minor(amount), request_key = Remote.new_key()})
end
function Page:pay(v) self:payment(v, 1) end
function Page:refund(v) self:payment(v, -1) end
function Page:data()
  local state = self.client:state()
  local blocked = not self.connected or state.busy or state.pending or state.conflict
  local disabled = {connect = state.busy or (state.pending and not state.conflict), retry = state.busy or not state.pending}
  local map = {add_room = "save_room", add_guest = "save_guest", mark_clean = "set_room_cleaning", reserve = "reserve",
    check_in = "check_in", check_out = "check_out", cancel = "cancel", pay = "payment", refund = "payment"}
  for control, command in pairs(map) do disabled[control] = blocked or not self.allowed[command] end
  local connection = self.connected and string.format(orion.tr("Country: %s · Role: %s · Revision: %d"), self.country, self.role, state.revision) or self.connection
  local values = {connection = connection, notice = self.notice}
  for key, value in pairs(self.values) do values[key] = value end
  return {values = values, disabled = disabled}
end
return Page

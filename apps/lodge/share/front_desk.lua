-- What the front desk reads: today's movements, what needs attention, free
-- rooms for a stay and the figures of a period. Nothing here writes.
local orion = require("orion")
local Hotel = require("hotel")
local Plugins = require("orion.plugins")
local calendar, intervals, money = Plugins.use("calendar", 1), Plugins.use("intervals", 1), Plugins.use("money", 1)
local FrontDesk = {}

-- detail is the translated status a person reads; state the same, untranslated, for icons.
local function stay(row, detail, state)
  return {reservation_id = row.id, room = row.room and row.room.name or "", guest = row.guest and row.guest.name or "",
    arrival = row.arrival, departure = row.departure, detail = detail or "", state = state or "", balance = Hotel.balance(row)}
end

-- Arrivals still to check in (today's and overdue), departures due (today's
-- and overdue) and the guests in house, each as rows of one list.
function FrontDesk.movements(day)
  local arrivals, departures, in_house = {}, {}, {}
  for _, row in ipairs(Hotel.reservations.model:select(nil, {order = "arrival, id"})) do
    if row.status == "reserved" and row.arrival <= day then
      local room = row.room
      local state = row.arrival < day and "overdue" or (room and room.cleaning_state ~= "clean" and "not_ready" or "ready")
      local detail = ({overdue = orion.tr("Overdue arrival"), not_ready = orion.tr("Room not ready"), ready = orion.tr("Room ready")})[state]
      arrivals[#arrivals + 1] = stay(row, detail, state)
    elseif row.status == "checked_in" then
      if row.departure <= day then
        local balance = money.minor(Hotel.balance(row))
        local state = row.departure < day and "overdue" or (balance ~= 0 and "due" or "settled")
        local detail = ({overdue = orion.tr("Overdue departure"), due = orion.tr("Balance to settle"), settled = orion.tr("Settled")})[state]
        departures[#departures + 1] = stay(row, detail, state)
      end
      in_house[#in_house + 1] = stay(row, string.format(orion.tr("%d guests"), row.persons))
    end
  end
  return arrivals, departures, in_house
end

-- The tasks a shift must settle, most urgent first. `target` is the page
-- that settles a task.
function FrontDesk.attention(day)
  local rows = {}
  local function add(kind, title, detail, target) rows[#rows + 1] = {kind = kind, title = title, detail = detail or "", target = target or ""} end
  local configured = #Hotel.hotel_settings.model:all() > 0
  if not configured then
    add("error", orion.tr("Choose the hotel's country and currency"), orion.tr("Setup, before the first reservation"), "hotel_setup")
  end
  if #Hotel.rooms.model:all() == 0 then
    add("tip", orion.tr("Add the hotel's rooms"), configured and orion.tr("Rooms page, or try the sample hotel in Setup") or orion.tr("Rooms page"), "rooms")
  end
  local arrivals, departures = FrontDesk.movements(day)
  for _, row in ipairs(arrivals) do
    if row.arrival < day then add("error", string.format(orion.tr("%s did not arrive"), row.guest), string.format(orion.tr("Room %s · check in or mark as a no-show"), row.room), "today")
    elseif row.state == "not_ready" then add("warning", string.format(orion.tr("Prepare room %s"), row.room), string.format(orion.tr("%s arrives today"), row.guest), "rooms") end
  end
  for _, row in ipairs(departures) do
    if row.departure < day then add("error", string.format(orion.tr("%s is past departure"), row.guest), string.format(orion.tr("Room %s · check out or extend"), row.room), "today")
    elseif money.minor(row.balance) ~= 0 then add("warning", string.format(orion.tr("Settle %s before departure"), row.guest), string.format(orion.tr("Balance %s"), orion.formatNumber(row.balance, 2)), "today") end
  end
  local dirty = 0
  for _, room in ipairs(Hotel.rooms.model:all()) do if room.cleaning_state ~= "clean" and not Hotel.room_occupied(room.id) then dirty = dirty + 1 end end
  if dirty > 0 then add("info", string.format(orion.tr("%d rooms to clean or inspect"), dirty), orion.tr("Rooms page"), "rooms") end
  for _, block in ipairs(Hotel.room_blocks.model:all()) do
    if block.blocked_from <= day and day < block.blocked_until then
      add("info", string.format(orion.tr("Room %s is out of service"), block.room and block.room.name or "?"),
        string.format(orion.tr("%s until %s"), block.reason, orion.formatDate(block.blocked_until)), "rooms")
    end
  end
  local yesterday = calendar.add(day, -1)
  if configured and #Hotel.day_closes.model:all() > 0 and not Hotel.day_closes.model:find({day = yesterday}) then
    add("tip", orion.tr("Yesterday was not closed"), orion.tr("Close each day after the last departure"), "day_close")
  end
  return rows
end

-- Rooms that can take `persons` for the whole stay, cheapest first.
function FrontDesk.free_rooms(arrival, departure, persons)
  orion.assert(intervals.valid(arrival, departure), orion.tr("Departure must follow a valid arrival date."))
  orion.assert(persons and persons >= 1, orion.tr("Enter the number of guests."))
  local nights, rows = calendar.between(arrival, departure), {}
  for _, room in ipairs(Hotel.rooms.model:all()) do
    local free = room.capacity >= persons and not Hotel.room_blocked(room.id, arrival, departure)
    for _, other in ipairs(free and Hotel.reservations.model:select({room_id = room.id}) or {}) do
      if other.status ~= "cancelled" and intervals.overlap(arrival, departure, other.arrival, other.departure) then free = false; break end
    end
    if free then
      rows[#rows + 1] = {room_id = room.id, name = room.name, category = room.category or "", capacity = room.capacity,
        rate = room.rate, total = money.minor(room.rate) * nights / 100, cleaning = orion.tr(({clean = "Clean", dirty = "Dirty"})[room.cleaning_state] or "Inspection needed")}
    end
  end
  table.sort(rows, function(a, b) if a.rate ~= b.rate then return a.rate < b.rate end return a.name < b.name end)
  return rows
end

local function night_price(row, night)
  local override = Hotel.night_rates.model:find({reservation_id = row.id, night = night})
  return override and override.amount_minor or money.minor(row.rate)
end

-- The figures of [from, to): rooms, guests, revenue, payments, sources.
function FrontDesk.period(from, to)
  orion.assert(intervals.valid(from, to), orion.tr("The period must end after it starts."))
  local days = calendar.between(from, to)
  orion.assert(days <= 366, orion.tr("Choose a period of at most a year."))
  local rooms = Hotel.rooms.model:all()
  local available = #rooms * days
  for _, block in ipairs(Hotel.room_blocks.model:all()) do
    local a, b = math.max(calendar.day(from), calendar.day(block.blocked_from)), math.min(calendar.day(to), calendar.day(block.blocked_until))
    if b > a then available = available - (b - a) end
  end
  local sold, guest_nights, room_revenue, extras, arrivals, cancelled = 0, 0, 0, 0, 0, 0
  local sources = {}
  for _, row in ipairs(Hotel.reservations.model:all()) do
    if row.arrival >= from and row.arrival < to then
      if row.status == "cancelled" then cancelled = cancelled + 1
      else
        arrivals = arrivals + 1
        local source = row.source_id ~= 0 and Hotel.booking_sources.model:find(row.source_id)
        local name = source and source.name or orion.tr("Not recorded")
        sources[name] = (sources[name] or 0) + 1
      end
    end
    if row.status ~= "cancelled" then
      local first, last = math.max(calendar.day(from), calendar.day(row.arrival)), math.min(calendar.day(to), calendar.day(row.departure))
      for n = first, last - 1 do
        local night = calendar.add(row.arrival, n - calendar.day(row.arrival))
        sold, guest_nights, room_revenue = sold + 1, guest_nights + row.persons, room_revenue + night_price(row, night)
      end
      if row.departure > from and row.departure <= to then extras = extras + money.minor(row.extras) end
    end
  end
  local methods, net = {}, 0
  for _, event in ipairs(require("orion.model"):extend("lodge.payment_events"):all()) do
    if event.posted_on and event.posted_on ~= "" and event.posted_on >= from and event.posted_on < to then
      local found = event.method_id ~= 0 and Hotel.payment_methods.model:find(event.method_id)
      local method = found and found.name or orion.tr("Not recorded")
      methods[method] = (methods[method] or 0) + event.amount_minor
      net = net + event.amount_minor
    end
  end
  return {from = from, to = to, days = days, rooms = #rooms, available = available, sold = sold, guest_nights = guest_nights,
    room_revenue = room_revenue, extras = extras, arrivals = arrivals, cancelled = cancelled, sources = sources, methods = methods, net = net,
    occupancy = available > 0 and sold * 100 / available or 0, adr = sold > 0 and room_revenue / sold or 0,
    revpar = available > 0 and room_revenue / available or 0}
end

-- The period as labelled rows, for the report table and its printout.
function FrontDesk.period_rows(report)
  local rows = {}
  local function add(section, label, value) rows[#rows + 1] = {section = section, label = label, value = value} end
  local function amount(minor) return orion.formatNumber(minor / 100, 2) end
  local rooms, revenue, payments, bookings = orion.tr("Rooms"), orion.tr("Revenue"), orion.tr("Payments"), orion.tr("Bookings")
  add(rooms, orion.tr("Room-nights available"), orion.formatNumber(report.available, 0))
  add(rooms, orion.tr("Room-nights sold"), orion.formatNumber(report.sold, 0))
  add(rooms, orion.tr("Occupancy"), orion.formatNumber(report.occupancy, 1) .. " %")
  add(rooms, orion.tr("Guest-nights"), orion.formatNumber(report.guest_nights, 0))
  add(revenue, orion.tr("Room revenue"), amount(report.room_revenue))
  add(revenue, orion.tr("Extra charges of departures"), amount(report.extras))
  add(revenue, orion.tr("Average daily rate (ADR)"), amount(report.adr))
  add(revenue, orion.tr("Revenue per available room (RevPAR)"), amount(report.revpar))
  local methods = {}
  for method in pairs(report.methods) do methods[#methods + 1] = method end
  table.sort(methods)
  for _, method in ipairs(methods) do add(payments, method, amount(report.methods[method])) end
  add(payments, orion.tr("Net payments"), amount(report.net))
  add(bookings, orion.tr("Arrivals"), orion.formatNumber(report.arrivals, 0))
  add(bookings, orion.tr("Cancelled arrivals"), orion.formatNumber(report.cancelled, 0))
  local sources = {}
  for source in pairs(report.sources) do sources[#sources + 1] = source end
  table.sort(sources)
  for _, source in ipairs(sources) do add(bookings, source, orion.formatNumber(report.sources[source], 0)) end
  return rows
end
-- The room calendar ("tape chart"): one row per room, one column per night
-- from `from`. A night shows the guest's surname, • in house, › booked;
-- × marks a room out of service. Free nights stay empty. (The marks are in
-- the UI font, IBM Plex Sans.)
FrontDesk.CALENDAR_DAYS = 14
local weekdays = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"}
function FrontDesk.calendar_titles(from)
  local titles = {orion.tr("Room")}
  for i = 0, FrontDesk.CALENDAR_DAYS - 1 do
    local night = calendar.add(from, i)
    local y, m, d = night:match("^(%d+)-(%d+)-(%d+)$")
    local weekday = os.date("*t", os.time({year = tonumber(y), month = tonumber(m), day = tonumber(d), hour = 12})).wday
    titles[#titles + 1] = string.format("%s %d", orion.tr(weekdays[weekday]), tonumber(d))
  end
  return titles
end
local function surname(name)
  return (name or ""):match("(%S+)%s*$") or ""
end
function FrontDesk.calendar(from)
  local last = calendar.add(from, FrontDesk.CALENDAR_DAYS)
  local rows = {}
  for _, room in ipairs(Hotel.rooms.model:select(nil, {order = "name"})) do
    local row = {room_id = room.id, name = room.name, free = 0}
    for _, booking in ipairs(Hotel.reservations.model:select({room_id = room.id})) do
      if (booking.status == "reserved" or booking.status == "checked_in") and intervals.overlap(from, last, booking.arrival, booking.departure) then
        local mark = booking.status == "checked_in" and "• " or "› "
        for i = 0, FrontDesk.CALENDAR_DAYS - 1 do
          local night = calendar.add(from, i)
          if night >= booking.arrival and night < booking.departure then row["d" .. (i + 1)] = mark .. surname(booking.guest and booking.guest.name) end
        end
      end
    end
    for _, block in ipairs(Hotel.room_blocks.model:select({room_id = room.id})) do
      for i = 0, FrontDesk.CALENDAR_DAYS - 1 do
        local night = calendar.add(from, i)
        if night >= block.blocked_from and night < block.blocked_until then row["d" .. (i + 1)] = "× " .. block.reason end
      end
    end
    for i = 1, FrontDesk.CALENDAR_DAYS do if not row["d" .. i] then row.free = row.free + 1 end end
    rows[#rows + 1] = row
  end
  return rows
end
-- Rooms free on each night of the calendar, for its summary line.
function FrontDesk.calendar_free(rows, night_index)
  local free = 0
  for _, row in ipairs(rows) do if not row["d" .. night_index] then free = free + 1 end end
  return free
end
return FrontDesk

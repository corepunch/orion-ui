-- Run by lodge_test.c inside the application's runtime, on an empty hotel
-- configured for Germany/EUR: the sample hotel, walk-in, extend, no-show,
-- search, the calendar, CSV export and the hotel profile.
local orion, Hotel = require("orion"), require("hotel")
local cal = require("orion.plugins").use("calendar", 1)
local Samples, FrontDesk, Exports = require("samples"), require("front_desk"), require("exports")
local t = orion.today()
assert(Samples.available(), "sample available")
assert(Samples.load())
assert(not Samples.available(), "sample only once")
assert(not Samples.load(), "second sample refused")
assert(#Hotel.rooms.model:all() == 9 and #Hotel.guests.model:all() == 8, "sample size")
local arrivals, departures, in_house = FrontDesk.movements(t)
assert(#arrivals == 2 and #departures == 2 and #in_house == 4, ("movements %d %d %d"):format(#arrivals, #departures, #in_house))
local states = {}
for _, row in ipairs(arrivals) do states[row.state] = true end
assert(states.ready and states.not_ready, "arrival states")

-- The calendar: a row per room, guests' surnames on their nights, blocks marked.
local rows = FrontDesk.calendar(t)
assert(#rows == 9, "calendar rows")
local titles = FrontDesk.calendar_titles(t)
assert(#titles == 15 and titles[1] == "Room", "calendar titles")
local by_name = {}
for _, row in ipairs(rows) do by_name[row.name] = row end
assert(by_name["101"].d1 == "• Example", tostring(by_name["101"].d1))
assert(by_name["201"].d1 == "› Morgan", tostring(by_name["201"].d1))
assert(by_name["204"].d6 and by_name["204"].d6:find("^×"), "block in calendar")
assert(by_name["102"].d1 == nil, "a departure frees its room tonight")

-- Walk-in: guest, stay, check-in and payment in one step; a dirty room refuses.
local dirty = Hotel.rooms.model:find({name = "104"})
assert(not Hotel.walk_in({name = "Walk In", nights = 1, room_id = dirty.id}), "dirty room refused")
local free = FrontDesk.free_rooms(t, cal.add(t, 2), 1)
local ready
for _, room in ipairs(free) do if Hotel.rooms.model:find(room.room_id).cleaning_state == "clean" and not Hotel.room_occupied(room.room_id) then ready = ready or room end end
assert(ready, "a ready room")
local cash = Hotel.payment_methods.model:find({name = "Cash"}).id
local before = #Hotel.guests.model:all()
local stay = assert(Hotel.walk_in({name = "  Nora Walker ", phone = "+49 1", nights = 1, persons = 1, room_id = ready.room_id, pay = 50, method_id = cash}))
assert(stay.status == "checked_in" and stay.departure == cal.add(t, 1), "walk-in stay")
assert(#Hotel.guests.model:all() == before + 1 and stay.guest.name == "Nora Walker", "walk-in guest")
assert(Hotel.paid(stay) == 50, "walk-in payment")
assert(not Hotel.walk_in({name = "", nights = 1, room_id = ready.room_id}), "name required")
assert(Hotel.guest_named("nora walker").id == stay.guest_id, "guest found again by name")

-- Extend and shorten; a stay keeps at least one night and never overlaps
-- the room's next booking.
local extended = assert(Hotel.extend(stay.id, 1))
assert(extended.departure == cal.add(t, 2))
local next_booking = Hotel.room_blocked(stay.room_id, cal.add(t, 2), cal.add(t, 3))
for _, other in ipairs(Hotel.reservations.model:select({room_id = stay.room_id})) do
  if other.id ~= stay.id and other.arrival == cal.add(t, 2) then next_booking = other end
end
if next_booking then assert(not Hotel.extend(stay.id, 1), "no overlap") end
assert(Hotel.extend(stay.id, -1))
assert(not Hotel.extend(stay.id, -1), "at least one night")

-- No-show: only a reserved arrival from its arrival day; the room is free again.
local morgan = Hotel.search("morgan")[1]
assert(morgan and morgan.status == "reserved", "search by guest")
assert(Hotel.search(tostring(morgan.id))[1].id == morgan.id, "search by number")
assert(Hotel.search("201")[1].id == morgan.id, "search by room")
local later = Hotel.search("petrova")[1]
assert(not Hotel.no_show(later.id), "future arrival is not a no-show")
assert(Hotel.no_show(morgan.id))
local cancelled = Hotel.reservations.model:find(morgan.id)
assert(cancelled.status == "cancelled" and cancelled.notes:find("^No%-show"), cancelled.notes)

-- CSV: one line per record, quoted where needed, formulas defused.
assert(Hotel.guests:save(nil, {name = '=cmd, "quoted"'}))
local guests = Exports.guests()
assert(guests:find('^id,name,phone'), "header")
assert(guests:find('"=cmd, ""quoted"""', 1, true), guests)
local reservations = Exports.reservations()
local lines = 0
for _ in reservations:gmatch("\r\n") do lines = lines + 1 end
assert(lines == #Hotel.reservations.model:all() + 1, "a line per reservation")
assert(Exports.payments():find("payment", 1, true))

-- The hotel profile heads documents; times are checked.
assert(not Hotel.save_profile({name = "Haus", check_in_time = "25:00"}), "bad time")
assert(Hotel.save_profile({name = "Haus <Am See>", address = "Seeweg 1", check_out_time = "11:00"}))
local Documents = require("documents")
local bill = Documents.bill(stay)
assert(bill:find("Haus &lt;Am See&gt;", 1, true) and bill:find("Seeweg 1", 1, true), "letterhead")
assert(Documents.registration(stay):find("11:00", 1, true), "check-out time")
assert(Documents.front_desk_list(t):find("Nora Walker", 1, true), "front desk list")

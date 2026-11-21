-- A small sample hotel dated around today, so a new user can try every page
-- before entering their own rooms. Only for an empty, configured hotel.
local orion = require("orion")
local Hotel = require("hotel")
local calendar = require("orion.plugins").use("calendar", 1)
local Samples = {}

function Samples.available()
  return #Hotel.hotel_settings.model:all() > 0 and #Hotel.rooms.model:all() == 0 and
    #Hotel.guests.model:all() == 0 and #Hotel.reservations.model:all() == 0
end

function Samples.load()
  if #Hotel.hotel_settings.model:all() == 0 then return nil, orion.tr("Choose the hotel's country and currency first.") end
  if not Samples.available() then return nil, orion.tr("The sample hotel can only be added to an empty hotel.") end
  local t = orion.today()
  local function day(offset) return calendar.add(t, offset) end
  return Hotel.write_all(function()
    local rooms, guests = {}, {}
    for i, r in ipairs({{"101", "Double", 2, 110}, {"102", "Single", 1, 85}, {"103", "Double", 2, 110}, {"104", "Twin", 2, 105},
                        {"105", "Single", 1, 85}, {"201", "Suite", 4, 180}, {"202", "Family", 4, 160}, {"203", "Double", 2, 120},
                        {"204", "Double", 2, 120}}) do
      rooms[i] = orion.assert(Hotel.rooms:save(nil, {name = r[1], category = orion.tr(r[2]), capacity = r[3], rate = r[4]})).id
      orion.assert(Hotel.rooms:save(rooms[i], {cleaning_state = (i == 4 or i == 9) and "dirty" or "clean"}))
    end
    for i, g in ipairs({{"Ada Example", "+49 30 123456", "ada@example.org", "German"}, {"Sam Taylor", "+44 20 123456", "", "British"},
                        {"Alex Morgan", "+1 212 555 0107", "", "American"}, {"Marta Kowalska", "+48 22 555 0101", "", "Polish"},
                        {"Jonas Weber", "+49 89 555 0102", "jonas.weber@example.com", "German"}, {"Petr Novak", "+420 2 555 0103", "", "Czech"},
                        {"Elena Petrova", "+7 495 555 0104", "", "Russian"}, {"Liam O'Brien", "+353 1 555 0105", "", "Irish"}}) do
      guests[i] = orion.assert(Hotel.guests:save(nil, {name = g[1], phone = g[2], email = g[3], nationality = g[4]})).id
    end
    local cash, card = Hotel.payment_methods.model:find({name = orion.tr("Cash")}), Hotel.payment_methods.model:find({name = orion.tr("Card")})
    local sources = {}
    for _, source in ipairs(Hotel.booking_sources.model:all()) do sources[#sources + 1] = source.id end
    local function stay(room, guest, from, to, persons, status, paid)
      local row = orion.assert(Hotel.save(nil, {room_id = rooms[room], guest_id = guests[guest], arrival = day(from), departure = day(to),
        persons = persons, rate = Hotel.rooms.model:find(rooms[room]).rate, extras = 0, notes = "", source_id = sources[(guest % #sources) + 1] or 0}))
      if status ~= "reserved" then orion.assert(Hotel.reservations:save(row.id, {status = "checked_in"})) end
      if paid > 0 then
        local method = (guest % 2 == 0 and card or cash)
        Hotel.payment_request, Hotel.payment_method = Hotel.next_payment_key(), method and method.id
        local ok, saved, refusal = pcall(Hotel.reservations.save, Hotel.reservations, row.id, {paid = paid})
        Hotel.payment_request, Hotel.payment_method = nil, nil
        if not ok then error(saved, 0) end
        orion.assert(saved, refusal)
      end
      return row
    end
    stay(1, 1, -2, 1, 2, "checked_in", 220)    -- in house, leaves tomorrow
    stay(2, 2, -3, 0, 1, "checked_in", 255)    -- leaves today, settled
    stay(3, 4, -1, 0, 1, "checked_in", 50)     -- leaves today, balance to settle
    stay(7, 6, -4, 3, 4, "checked_in", 500)    -- family in house
    stay(6, 3, 0, 4, 3, "reserved", 200)       -- arrives today, room ready
    stay(4, 5, 0, 2, 2, "reserved", 0)         -- arrives today, room still dirty
    stay(8, 7, 2, 5, 2, "reserved", 120)
    stay(5, 8, 1, 3, 1, "reserved", 0)
    if not Hotel.profile().name or Hotel.profile().name == "" then
      orion.assert(Hotel.save_profile({name = orion.tr("Sample Lakeside Inn"), address = orion.tr("1 Lake Road"), phone = "+49 30 000000",
        email = "desk@example.org", check_in_time = "14:00", check_out_time = "11:00"}))
    end
    orion.assert(Hotel.room_blocks:save(nil, {room_id = rooms[9], blocked_from = day(5), blocked_until = day(8), reason = orion.tr("Bathroom repair")}))
    return {rooms = #rooms}
  end)
end
return Samples

-- The assistant on the sample hotel: requests in English, German and Russian
-- become answers, and Enter again carries out a single action.
local orion, Hotel = require("orion"), require("hotel")
local cal = require("orion.plugins").use("calendar", 1)
local Engine = require("assistant_engine")
local t = orion.today()
local function first(answer) return answer.rows[1] or {} end

-- Words and dates.
assert(Engine.lower("ЗАСЕЛИТЬ Äpfel") == "заселить äpfel")
assert(Engine.date("tomorrow", t) == cal.add(t, 1))
assert(Engine.date("übermorgen", t) == cal.add(t, 2))
assert(Engine.date("завтра", t) == cal.add(t, 1))
local d = Engine.date("24.12", t)
assert(d and d:sub(6) == "12-24", tostring(d))
d = Engine.date("on 24 december", t)
assert(d and d:sub(6) == "12-24", tostring(d))
d = Engine.date("2031-02-03", t)
assert(d == "2031-02-03")
local friday = Engine.date("friday", t)
assert(friday and friday >= t and friday <= cal.add(t, 6), tostring(friday))
local nights, persons = Engine.counts("3 nights for 2 guests")
assert(nights == 3 and persons == 2)
nights, persons = Engine.counts(Engine.lower("2 Nächte 3 Personen"))
assert(nights == 2 and persons == 3)
nights = Engine.counts("на 3 ночи")
assert(nights == 3)
assert(Engine.amount("paid 50,50 cash") == 50.5)
assert(Engine.amount("€120") == 120)

-- Nothing typed: the day's suggestions, then examples.
local suggestions = Engine.answer("")
assert(#suggestions.rows > 0 and suggestions.rows[1].section == "Suggestions", "suggestions")

-- Check-in by surname: one match, so Enter again does it.
local answer = Engine.answer("check in Morgan")
assert(#answer.rows == 1 and first(answer).action == "check_in" and first(answer).confirm, answer.reply)
assert(Engine.perform(first(answer).action, first(answer).arg):find("checked in"))
assert(Hotel.search("morgan")[1].status == "checked_in")
-- German and Russian verbs reach the same action.
assert(first(Engine.answer("Weber einchecken")).action == "check_in")
assert(first(Engine.answer("заселить Weber")).action == "check_in")

-- Free rooms for a stay, then booking a room for a named new guest.
answer = Engine.answer("free rooms tomorrow 2 nights for 2")
assert(#answer.rows > 0 and first(answer).action == "prefill", answer.reply)
assert(first(answer).arg:sub(-24) == "|" .. cal.add(t, 1) .. "|" .. cal.add(t, 3) .. "|2", first(answer).arg)
answer = Engine.answer("freie Zimmer morgen")
assert(#answer.rows > 0 and first(answer).action == "prefill", answer.reply)
answer = Engine.answer("book 203 for Grace Miller " .. cal.add(t, 20) .. " 3 nights")
assert(first(answer).action == "book" and first(answer).confirm, answer.reply)
local saved = Engine.perform(first(answer).action, first(answer).arg)
assert(saved:find("Grace Miller", 1, true), saved)
local grace = Hotel.search("grace miller")[1]
assert(grace and grace.arrival == cal.add(t, 20) and grace.departure == cal.add(t, 23) and grace.room.name == "203")

-- Payments: the guest, the amount and the method; amounts are not read as dates.
answer = Engine.answer("Kowalska paid 10.50 cash")
assert(first(answer).action == "payment" and first(answer).confirm, answer.reply)
local id, minor, method = first(answer).arg:match("^(%d+):(%d+):(%d+)$")
assert(tonumber(minor) == 1050 and tonumber(method) == Hotel.payment_methods.model:find({name = "Cash"}).id, first(answer).arg)
local kowalska = Hotel.reservations.model:find(tonumber(id))
local paid = Hotel.paid(kowalska)
Engine.perform(first(answer).action, first(answer).arg)
assert(math.abs(Hotel.paid(Hotel.reservations.model:find(tonumber(id))) - paid - 10.5) < 0.001)
answer = Engine.answer("who owes")
assert(#answer.rows > 0 and first(answer).action == "payment_window", answer.reply)

-- Lists, housekeeping, reports and commands.
assert(Engine.answer("who arrives today").reply:find("arrivals on"))
assert(#Engine.answer("who leaves today").rows == 2)
assert(#Engine.answer("in house").rows >= 4)
answer = Engine.answer("dirty rooms")
assert(#answer.rows > 0 and first(answer).action == "clean")
answer = Engine.answer("mark 104 clean")
assert(first(answer).arg:match(":clean$") and first(answer).confirm)
Engine.perform(first(answer).action, first(answer).arg)
assert(Hotel.rooms.model:find({name = "104"}).cleaning_state == "clean")
assert(first(Engine.answer("отметь 104 грязный")).arg:match(":dirty$"))
assert(Engine.answer("occupancy this month").reply:find("occupancy"))
assert(first(Engine.answer("walk-in")).action == "walk_in")
assert(first(Engine.answer("close the day")).arg == "day_close")
assert(first(Engine.answer("extend Novak")).action == "extend")
assert(first(Engine.answer("no-show Petrova")).action == "no_show")
-- Anything else is a search; nonsense gets examples.
answer = Engine.answer("Novak")
assert(first(answer).action == "open_reservation", answer.reply)
answer = Engine.answer("xyzzy plugh")
assert(first(answer).action == "ask" and answer.reply:find("did not understand"), answer.reply)

-- The Lodge assistant's understanding: a sentence typed at the desk, in
-- English, German or Russian, becomes an answer and rows to choose. A row is
-- {section, kind, title, detail, action, arg}; `confirm` on the first row
-- means Enter again carries it out. Nothing here writes until perform().
--
--   "check in Ada"                 → the arrival, ready to check in
--   "free rooms friday 2 nights"   → rooms free for the stay, to book
--   "book 101 for Sam Taylor 24.11 3 nights"
--   "Ada paid 50 cash"             → a payment, ready to record
--   "who arrives tomorrow" · "who owes" · "dirty rooms" · "mark 104 clean"
--   "extend Novak" · "no-show Weber" · "occupancy this month" · "walk-in"
local orion = require("orion")
local Hotel = require("hotel")
local FrontDesk = require("front_desk")
local Plugins = require("orion.plugins")
local calendar, money = Plugins.use("calendar", 1), Plugins.use("money", 1)
local Engine = {}

-- ── Words ───────────────────────────────────────────────────────────────────

-- Lower case for Latin, German and Cyrillic letters (string.lower is ASCII only).
local upper_to_lower = {["Ä"] = "ä", ["Ö"] = "ö", ["Ü"] = "ü", ["Ё"] = "ё"}
function Engine.lower(text)
  text = text:lower()
  return (text:gsub(utf8.charpattern, function(ch)
    if upper_to_lower[ch] then return upper_to_lower[ch] end
    local code = utf8.codepoint(ch)
    if code >= 0x410 and code <= 0x42F then return utf8.char(code + 0x20) end
    return ch
  end))
end

-- Each intent and the phrases that name it; the longest phrase found wins.
local intents = {
  check_in  = {"check in", "check-in", "checkin", "einchecken", "check ein", "заселить", "заселение", "засели", "заселяй", "заезд гостя"},
  check_out = {"check out", "check-out", "checkout", "auschecken", "выселить", "выселение", "выпиши", "выписать"},
  payment   = {"paid", "pays", "pay", "payment", "deposit", "zahlung", "zahlt", "bezahlt", "anzahlung", "оплатил", "оплатила", "оплата", "оплатить", "платёж", "платеж", "внес", "внёс", "предоплата"},
  free      = {"free room", "free rooms", "free", "available", "availability", "vacant", "vacancy", "frei", "freie", "verfügbar", "свободн", "свобод"},
  book      = {"book", "reserve", "reservation for", "buchen", "buche", "reservieren", "забронировать", "забронируй", "бронь", "бронируй"},
  arrivals  = {"arrivals", "arriving", "arrives", "arrive", "who comes", "coming", "anreisen", "anreise", "ankunft", "kommt", "kommen", "заезды", "заезд", "приезжает", "приезжают", "приедет", "прибывает"},
  departures= {"departures", "departing", "leaving", "leaves", "leave", "abreisen", "abreise", "reist ab", "abfahrt", "выезды", "выезжает", "выезжают", "уезжает", "отъезд", "выезд"},
  in_house  = {"in house", "in-house", "staying", "guests now", "im haus", "anwesend", "проживают", "живут", "в отеле", "проживающие"},
  balance   = {"who owes", "owes", "owe", "unpaid", "balance", "balances", "debt", "outstanding", "offen", "offene", "schuldet", "schulden", "долг", "долги", "должен", "должны", "не оплат", "задолж"},
  housekeep = {"dirty", "to clean", "cleaning", "housekeeping", "schmutzig", "reinigen", "reinigung", "zimmerreinigung", "грязн", "уборк", "убрать"},
  mark      = {"mark", "set", "markiere", "отметь", "отметить", "пометь"},
  report    = {"occupancy", "report", "revenue", "adr", "revpar", "how full", "belegung", "auslastung", "umsatz", "bericht", "загрузк", "заполняемост", "выручк", "отчет", "отчёт"},
  extend    = {"extend", "another night", "one more night", "stay longer", "verlängern", "verlängere", "продлить", "продли", "продление"},
  shorten   = {"shorten", "leave earlier", "one night less", "verkürzen", "сократить", "сократи"},
  no_show   = {"no-show", "no show", "noshow", "did not come", "didn't come", "nicht erschienen", "nicht gekommen", "не приехал", "не приехала", "незаезд"},
  walk_in   = {"walk-in", "walk in", "walkin", "without booking", "ohne reservierung", "ohne buchung", "без брони", "без бронирования"},
  close_day = {"close day", "close the day", "close today", "night audit", "end of day", "tagesabschluss", "tag abschließen", "nachtabschluss", "закрыть день", "закрытие дня", "ночной аудит", "конец дня"},
  export    = {"export", "csv", "spreadsheet", "backup", "exportieren", "sicherung", "экспорт", "выгруз", "резервн"},
  help      = {"help", "what can you do", "examples", "hilfe", "beispiele", "помощь", "помоги", "что ты умеешь", "примеры"},
  calendar  = {"calendar", "tape chart", "room plan", "kalender", "belegungsplan", "календарь", "шахматк"},
  find      = {"find", "search", "show", "open", "look up", "where is", "suche", "finde", "zeige", "öffne", "найти", "найди", "покажи", "открой", "где"},
}
local order = {"no_show", "walk_in", "close_day", "check_in", "check_out", "extend", "shorten", "payment", "book", "free", "balance",
  "in_house", "arrivals", "departures", "housekeep", "mark", "report", "export", "calendar", "help", "find"}

-- Filler words dropped before names are matched.
local filler = {}
for word in ([[a an the for to of in on at from until till with and by is are please me my our room rooms guest guests
  night nights person persons people pers who what which today tomorrow
  der die das den dem ein eine für von bis mit und zimmer gast gäste nacht nächte bitte heute morgen
  на в с до по для к и номер номера гость гостя гостей ночь ночи ночей пожалуйста сегодня завтра кто]]):gmatch("%S+") do filler[word] = true end

local weekdays = {
  {"sunday", "sonntag", "воскресенье", "sun", "so", "вс"}, {"monday", "montag", "понедельник", "mon", "mo", "пн"},
  {"tuesday", "dienstag", "вторник", "tue", "di", "вт"}, {"wednesday", "mittwoch", "среда", "среду", "wed", "mi", "ср"},
  {"thursday", "donnerstag", "четверг", "thu", "do", "чт"}, {"friday", "freitag", "пятница", "пятницу", "fri", "fr", "пт"},
  {"saturday", "samstag", "суббота", "субботу", "sat", "sa", "сб"},
}
local months = {
  {"january", "jan", "januar", "января", "январь"}, {"february", "feb", "februar", "февраля", "февраль"},
  {"march", "mar", "märz", "марта", "март"}, {"april", "apr", "апреля", "апрель"}, {"may", "mai", "мая", "май"},
  {"june", "jun", "juni", "июня", "июнь"}, {"july", "jul", "juli", "июля", "июль"}, {"august", "aug", "августа", "август"},
  {"september", "sep", "sept", "сентября", "сентябрь"}, {"october", "oct", "oktober", "okt", "октября", "октябрь"},
  {"november", "nov", "ноября", "ноябрь"}, {"december", "dec", "dezember", "dez", "декабря", "декабрь"},
}

local function words(text)
  local list = {}
  for word in text:gmatch("[^%s,;!?]+") do list[#list + 1] = word end
  return list
end
local function contains(text, phrase)
  local at = 1
  while true do
    local s, e = text:find(phrase, at, true)
    if not s then return nil end
    local before = s == 1 or text:sub(s - 1, s - 1):match("[%s,;.!?]")
    -- Russian phrases are stems ("свобод"); others are whole words.
    local after = e == #text or text:sub(e + 1, e + 1):match("[%s,;.!?]") or phrase:find("[\208\209]")
    if before and after then return s, e end
    at = s + 1
  end
end

-- ── Facts in the sentence ───────────────────────────────────────────────────

local function weekday_of(iso)
  local y, m, d = iso:match("^(%d+)-(%d+)-(%d+)$")
  return os.date("*t", os.time({year = tonumber(y), month = tonumber(m), day = tonumber(d), hour = 12})).wday
end
-- The first date the sentence names, and the text without it.
function Engine.date(text, today)
  today = today or orion.today()
  local function cut(s, e, iso) return iso, (text:sub(1, s - 1) .. " " .. text:sub(e + 1)) end
  for _, rule in ipairs({{{"day after tomorrow", "übermorgen", "послезавтра"}, 2}, {{"tomorrow", "morgen", "завтра"}, 1},
                         {{"tonight", "today", "heute", "сегодня"}, 0}, {{"yesterday", "gestern", "вчера"}, -1}}) do
    for _, phrase in ipairs(rule[1]) do
      local s, e = contains(text, phrase)
      if s then return cut(s, e, calendar.add(today, rule[2])) end
    end
  end
  local s, e, y, m, d = text:find("(%d%d%d%d)%-(%d%d?)%-(%d%d?)")
  if s and pcall(calendar.day, string.format("%04d-%02d-%02d", y, m, d)) then return cut(s, e, string.format("%04d-%02d-%02d", y, m, d)) end
  local s2, e2, d2, m2, y2 = text:find("(%d%d?)[./](%d%d?)[./]?(%d*)")
  if s2 and not text:sub(e2 + 1, e2 + 1):match("%d") then
    local year = tonumber(y2 ~= "" and y2 or today:sub(1, 4))
    if year < 100 then year = year + 2000 end
    local iso = string.format("%04d-%02d-%02d", year, tonumber(m2), tonumber(d2))
    if pcall(calendar.day, iso) then
      if y2 == "" and iso < calendar.add(today, -31) then iso = string.format("%04d-%s", year + 1, iso:sub(6)) end
      return cut(s2, e2, iso)
    end
  end
  for index, names in ipairs(months) do
    for _, name in ipairs(names) do
      local ms, me = contains(text, name)
      if ms then
        local ds, _, day = text:sub(1, ms - 1):find("(%d%d?)%.?%s*$")
        local after_day = not ds and text:sub(me + 1):match("^%s*(%d%d?)")
        local dayn = tonumber(day or after_day)
        if dayn then
          local year = tonumber(today:sub(1, 4))
          local iso = string.format("%04d-%02d-%02d", year, index, dayn)
          if pcall(calendar.day, iso) then
            if iso < calendar.add(today, -31) then iso = string.format("%04d-%02d-%02d", year + 1, index, dayn) end
            local from = ds or ms
            local to = after_day and (me + #text:sub(me + 1):match("^%s*%d%d?")) or me
            return cut(from, to, iso)
          end
        end
      end
    end
  end
  for wday, names in ipairs(weekdays) do
    for i, name in ipairs(names) do
      -- Two-letter German and Russian abbreviations only after "on"/"am"/"в".
      local ws, we = contains(text, name)
      if ws and (i <= 4 or #name > 2 or text:sub(1, ws - 1):match("[ao][mn]%s*$")) then
        local ahead = (wday - weekday_of(today)) % 7
        return cut(ws, we, calendar.add(today, ahead))
      end
    end
  end
  return nil, text
end
-- "3 nights", "2 Nächte", "3 ночи"; "2 guests", "für 2 Personen", "на двоих".
function Engine.counts(text)
  local nights, persons
  local function take(pattern, kind)
    local s, e, n = text:find(pattern)
    if s then
      text = text:sub(1, s - 1) .. " " .. text:sub(e + 1)
      if kind == "nights" then nights = nights or tonumber(n) else persons = persons or tonumber(n) end
      return true
    end
  end
  for _, word in ipairs({"nights?", "nächte", "nacht", "ноч[ьиея]*", "n"}) do take("(%d+)%s*" .. word .. "%f[^%w\128-\255]", "nights") end
  for _, word in ipairs({"guests?", "people", "persons?", "pers", "pax", "adults?", "personen", "gäste", "erwachsene", "гост[ьяей]*", "человек[а]?", "чел"}) do
    take("(%d+)%s*" .. word .. "%f[^%w\128-\255]", "persons")
  end
  -- "for 2", "für 3", "на 2" after the nights are taken: a party of that size.
  for _, word in ipairs({"for", "für", "на"}) do
    local s, e, n = text:find(word .. "%s+(%d)%f[%D]")
    if s and (s == 1 or text:sub(s - 1, s - 1):match("%s")) and tonumber(n) >= 1 then
      persons = persons or tonumber(n); text = text:sub(1, s - 1) .. " " .. text:sub(e + 1)
    end
  end
  local pairs_ru = {["двоих"] = 2, ["троих"] = 3, ["четверых"] = 4, ["одного"] = 1}
  for word, n in pairs(pairs_ru) do local s, e = contains(text, word); if s then persons = persons or n; text = text:sub(1, s - 1) .. text:sub(e + 1) end end
  return nights, persons, text
end
-- An amount: "50", "50.50", "50,50", "€50"; the first number with a currency or decimals wins.
function Engine.amount(text)
  for _, pattern in ipairs({"[€$₽]%s*(%d+[.,]?%d?%d?)", "(%d+[.,]?%d?%d?)%s*[€$₽]", "(%d+[.,]%d%d?)%f[%D]",
                            "(%d+)%s*eur", "(%d+)%s*rub", "(%d+)%s*руб", "(%d+)%f[%D]"}) do
    local s, e, number = text:find(pattern)
    if s then
      local value = tonumber((number:gsub(",", ".")))
      if value and value > 0 then return value, text:sub(1, s - 1) .. " " .. text:sub(e + 1) end
    end
  end
end
-- A payment method by name or common word ("cash", "bar", "наличные", "card", "karte", "карта").
function Engine.method(text)
  local synonyms = {Cash = {"cash", "bar", "barzahlung", "налич", "наличными", "наличные"}, Card = {"card", "karte", "ec", "kreditkarte", "карт", "картой", "карта"},
    ["Bank transfer"] = {"transfer", "bank", "überweisung", "перевод", "банк"}}
  for _, method in ipairs(Hotel.payment_methods.model:all()) do
    local name = Engine.lower(method.name)
    if contains(text, name) then return method end
    for english, list in pairs(synonyms) do
      if Engine.lower(orion.tr(english)) == name then
        for _, word in ipairs(list) do if contains(text, word) then return method end end
      end
    end
  end
end

-- The rooms and guests the rest of the sentence names.
local function remaining(text)
  local list = {}
  for _, word in ipairs(words(text)) do
    word = word:gsub("^[\"'(]+", ""):gsub("[\"').:]+$", "")
    if word ~= "" and not filler[word] then list[#list + 1] = word end
  end
  return list
end
function Engine.room(tokens)
  for i, token in ipairs(tokens) do
    for _, room in ipairs(Hotel.rooms.model:all()) do
      if Engine.lower(room.name) == token then table.remove(tokens, i); return room end
    end
  end
end
-- Guests whose names contain every remaining word, best match first.
function Engine.guests(tokens)
  if #tokens == 0 then return {} end
  local found = {}
  for _, guest in ipairs(Hotel.guests.model:all()) do
    local name, all = Engine.lower(guest.name), true
    for _, token in ipairs(tokens) do if not name:find(token, 1, true) then all = false; break end end
    if all then found[#found + 1] = guest end
  end
  if #found == 0 and #tokens > 1 then return Engine.guests({tokens[#tokens]}) end
  table.sort(found, function(a, b) return a.name < b.name end)
  return found
end

-- ── Answers ─────────────────────────────────────────────────────────────────

local function stay_title(row)
  return string.format(orion.tr("%s · room %s"), row.guest and row.guest.name or "?", row.room and row.room.name or "?")
end
local function stay_detail(row)
  local status = ({reserved = orion.tr("Reserved"), checked_in = orion.tr("Checked in"), checked_out = orion.tr("Checked out"), cancelled = orion.tr("Cancelled")})[row.status] or row.status
  return string.format(orion.tr("%s – %s · %s · balance %s"), orion.formatDate(row.arrival), orion.formatDate(row.departure), status,
    orion.formatNumber(Hotel.balance(row), 2))
end
local function result(reply, rows) return {reply = reply, rows = rows or {}} end
local function row(section, kind, title, detail, action, arg, confirm)
  return {section = section, kind = kind, title = title, detail = detail or "", action = action or "", arg = arg and tostring(arg) or "", confirm = confirm}
end

-- The reservations the words point at: by room, by guest, or both.
local function stays(tokens, statuses)
  local room = Engine.room(tokens)
  local ids = {}
  for _, guest in ipairs(#tokens > 0 and Engine.guests(tokens) or {}) do ids[guest.id] = true end
  local list = {}
  for _, r in ipairs(Hotel.reservations.model:select(nil, {order = "arrival, id"})) do
    if statuses[r.status] then
      local match
      if room and #tokens > 0 then match = r.room_id == room.id and ids[r.guest_id]
      elseif room then match = r.room_id == room.id
      elseif #tokens > 0 then match = ids[r.guest_id]
      else match = true end
      if match then list[#list + 1] = r end
    end
  end
  return list
end

local function movement_rows(section, list, action, verb)
  local rows = {}
  for _, r in ipairs(list) do rows[#rows + 1] = row(section, action == "" and "stay" or "action", stay_title(r), stay_detail(r), action ~= "" and action or "open_reservation", r.id) end
  if #rows == 1 and action ~= "" then rows[1].confirm = true end
  return rows, verb
end

local function act_on_stays(tokens, statuses, action, section, none, one, many)
  local list = stays(tokens, statuses)
  if #list == 0 then return result(none) end
  local rows = movement_rows(section, list, action)
  if #list == 1 then return result(string.format(one, list[1].guest.name, list[1].room.name), rows) end
  return result(string.format(many, #list), rows)
end

local handlers = {}
function handlers.check_in(text)
  local tokens = remaining(text)
  return act_on_stays(tokens, {reserved = true}, "check_in", orion.tr("Check in"),
    orion.tr("No reserved arrival matches. Try the guest's surname or the room number."),
    orion.tr("Press Enter again to check in %s to room %s."), orion.tr("%d arrivals match. Choose one."))
end
function handlers.check_out(text)
  return act_on_stays(remaining(text), {checked_in = true}, "check_out", orion.tr("Check out"),
    orion.tr("No guest in house matches. Try the guest's surname or the room number."),
    orion.tr("Press Enter again to check out %s from room %s."), orion.tr("%d guests in house match. Choose one."))
end
function handlers.extend(text)
  return act_on_stays(remaining(text), {checked_in = true, reserved = true}, "extend", orion.tr("Extend by one night"),
    orion.tr("No open reservation matches."), orion.tr("Press Enter again to extend the stay of %s in room %s by one night."),
    orion.tr("%d reservations match. Choose one."))
end
function handlers.shorten(text)
  return act_on_stays(remaining(text), {checked_in = true, reserved = true}, "shorten", orion.tr("Shorten by one night"),
    orion.tr("No open reservation matches."), orion.tr("Press Enter again to shorten the stay of %s in room %s by one night."),
    orion.tr("%d reservations match. Choose one."))
end
function handlers.no_show(text)
  return act_on_stays(remaining(text), {reserved = true}, "no_show", orion.tr("Mark as a no-show"),
    orion.tr("No reserved arrival matches."), orion.tr("Press Enter again to mark %s (room %s) as a no-show and free the room."),
    orion.tr("%d arrivals match. Choose one."))
end
function handlers.payment(text)
  local amount, rest = Engine.amount(text)
  local method = Engine.method(rest or text)
  if method then
    local pattern = Engine.lower(method.name):gsub("%p", "%%%0")
    rest = (rest or text):gsub(pattern, " ")
  end
  local tokens = remaining(rest or text)
  -- Drop words naming any method or currency, so "cash" is not taken for a guest.
  for i = #tokens, 1, -1 do
    if tokens[i]:match("^[€$₽]") or ({cash = 1, card = 1, bar = 1, karte = 1, eur = 1, euro = 1, rub = 1, ["руб"] = 1, ["налич"] = 1, ["наличными"] = 1, ["картой"] = 1})[tokens[i]] then table.remove(tokens, i) end
  end
  local list = stays(tokens, {reserved = true, checked_in = true, checked_out = true})
  if #list == 0 then return result(orion.tr("Say who pays: the guest's surname or the room number, and the amount.")) end
  local rows = {}
  for _, r in ipairs(list) do
    local minor = amount and money.minor(amount) or money.minor(Hotel.balance(r))
    if minor > 0 then
      rows[#rows + 1] = row(orion.tr("Record a payment"), "money", string.format(orion.tr("%s pays %s %s"), r.guest.name, orion.formatNumber(minor / 100, 2),
        method and method.name or orion.tr("(method not recorded)")), stay_detail(r), "payment", string.format("%d:%d:%d", r.id, minor, method and method.id or 0))
    else
      rows[#rows + 1] = row(orion.tr("Record a payment"), "stay", stay_title(r), stay_detail(r), "payment_window", r.id)
    end
  end
  if #rows == 1 and rows[1].action == "payment" then
    rows[1].confirm = true
    return result(string.format(orion.tr("Press Enter again to record %s from %s."), orion.formatNumber(money.minor(amount or Hotel.balance(list[1])) / 100, 2), list[1].guest.name), rows)
  end
  return result(amount and orion.tr("Choose who pays.") or orion.tr("Choose who pays; without an amount the balance is taken."), rows)
end
function handlers.free(text, parsed)
  local today = orion.today()
  local arrival = parsed.date or today
  local nights = parsed.nights or 1
  local departure = calendar.add(arrival, nights)
  local ok, free = pcall(FrontDesk.free_rooms, arrival, departure, parsed.persons or 1)
  if not ok then return result(orion.tr("I could not read those dates. Try “free rooms 24.11 2 nights”.")) end
  local rows = {}
  for _, room in ipairs(free) do
    rows[#rows + 1] = row(orion.tr("Free rooms"), "room", string.format(orion.tr("Room %s · %s · up to %d"), room.name, room.category, room.capacity),
      string.format(orion.tr("%s a night · %s for the stay · %s"), orion.formatNumber(room.rate, 2), orion.formatNumber(room.total, 2), room.cleaning),
      "prefill", string.format("%d|%s|%s|%d", room.room_id, arrival, departure, parsed.persons or 1))
  end
  local reply = #free == 0 and string.format(orion.tr("No room is free from %s for %d nights."), orion.formatDate(arrival), nights) or
    string.format(orion.tr("%d rooms are free from %s for %d nights, cheapest first. Choose one to book it."), #free, orion.formatDate(arrival), nights)
  return result(reply, rows)
end
function handlers.book(text, parsed)
  local tokens = remaining(text)
  local room = Engine.room(tokens)
  if not room then return handlers.free(text, parsed) end
  local arrival = parsed.date or orion.today()
  local departure = calendar.add(arrival, parsed.nights or 1)
  local guests = Engine.guests(tokens)
  local name = #guests == 0 and table.concat(tokens, " ") or nil
  local rows = {}
  for _, guest in ipairs(guests) do
    rows[#rows + 1] = row(orion.tr("Book"), "action", string.format(orion.tr("Book room %s for %s"), room.name, guest.name),
      string.format(orion.tr("%s – %s · %d guests"), orion.formatDate(arrival), orion.formatDate(departure), parsed.persons or 1),
      "book", string.format("%d|%s|%s|%s|%d", room.id, guest.name, arrival, departure, parsed.persons or 1))
  end
  if name and name ~= "" then
    local display = name:gsub("(%a)([%w']*)", function(a, b) return a:upper() .. b end)
    rows[#rows + 1] = row(orion.tr("Book"), "action", string.format(orion.tr("Book room %s for a new guest, %s"), room.name, display),
      string.format(orion.tr("%s – %s · %d guests"), orion.formatDate(arrival), orion.formatDate(departure), parsed.persons or 1),
      "book", string.format("%d|%s|%s|%s|%d", room.id, display, arrival, departure, parsed.persons or 1))
  end
  rows[#rows + 1] = row(orion.tr("Book"), "command", string.format(orion.tr("Open the reservation form for room %s"), room.name),
    orion.tr("Fill in the guest and details yourself"), "prefill", string.format("%d|%s|%s|%d", room.id, arrival, departure, parsed.persons or 1))
  if #rows == 2 then
    rows[1].confirm = true
    return result(string.format(orion.tr("Press Enter again to book room %s from %s to %s."), room.name, orion.formatDate(arrival), orion.formatDate(departure)), rows)
  end
  return result(#rows == 1 and orion.tr("Who is the guest? Add the name, or open the form.") or orion.tr("Several guests match. Choose one."), rows)
end
local function list_movements(kind, parsed)
  local day = parsed.date or orion.today()
  local rows, section = {}, ({arrivals = orion.tr("Arrivals"), departures = orion.tr("Departures"), in_house = orion.tr("In house")})[kind]
  for _, r in ipairs(Hotel.reservations.model:select(nil, {order = "arrival, id"})) do
    local include = (kind == "arrivals" and r.arrival == day and r.status ~= "cancelled") or
      (kind == "departures" and r.departure == day and r.status ~= "cancelled") or
      (kind == "in_house" and r.status == "checked_in")
    if include then
      local action = (kind == "arrivals" and r.status == "reserved" and day <= orion.today()) and "check_in" or
        (kind == "departures" and r.status == "checked_in" and day <= orion.today()) and "check_out" or "open_reservation"
      rows[#rows + 1] = row(section, action == "open_reservation" and "stay" or "action", stay_title(r), stay_detail(r), action, r.id)
    end
  end
  local reply = kind == "in_house" and string.format(orion.tr("%d guests are in house."), #rows) or
    string.format(kind == "arrivals" and orion.tr("%d arrivals on %s.") or orion.tr("%d departures on %s."), #rows, orion.formatDate(day))
  if #rows > 0 and kind ~= "in_house" then reply = reply .. " " .. orion.tr("Choose one to check in or out, or open it.") end
  return result(reply, rows)
end
function handlers.arrivals(_, parsed) return list_movements("arrivals", parsed) end
function handlers.departures(_, parsed) return list_movements("departures", parsed) end
function handlers.in_house(_, parsed) return list_movements("in_house", parsed) end
function handlers.balance()
  local rows, total = {}, 0
  for _, r in ipairs(Hotel.reservations.model:select(nil, {order = "departure, id"})) do
    local balance = money.minor(Hotel.balance(r))
    if r.status ~= "cancelled" and balance > 0 and (r.status == "checked_in" or r.status == "checked_out" or r.arrival <= orion.today()) then
      total = total + balance
      rows[#rows + 1] = row(orion.tr("Open balances"), "money", stay_title(r), stay_detail(r), "payment_window", r.id)
    end
  end
  if #rows == 0 then return result(orion.tr("Nobody owes anything: every arrived guest is settled.")) end
  return result(string.format(orion.tr("%d guests owe %s in total. Choose one to take a payment."), #rows, orion.formatNumber(total / 100, 2)), rows)
end
function handlers.housekeep()
  local rows = {}
  for _, room in ipairs(Hotel.rooms.model:select(nil, {order = "name"})) do
    if room.cleaning_state ~= "clean" then
      local occupied = Hotel.room_occupied(room.id)
      rows[#rows + 1] = row(orion.tr("Rooms to clean or inspect"), "room", string.format(orion.tr("Room %s · %s"), room.name, room.cleaning_label or room.cleaning_state),
        occupied and orion.tr("Guest checked in") or orion.tr("Empty · choose to mark clean"), "clean", string.format("%d:clean", room.id))
    end
  end
  if #rows == 0 then return result(orion.tr("Every room is clean.")) end
  return result(string.format(orion.tr("%d rooms to clean or inspect. Choose a room once it is clean."), #rows), rows)
end
function handlers.mark(text)
  local tokens = remaining(text)
  local room = Engine.room(tokens)
  local state
  for _, pair in ipairs({{"inspection_needed", {"inspect", "inspection", "prüfen", "kontrolle", "провер"}}, {"dirty", {"dirty", "schmutzig", "грязн"}},
                         {"clean", {"clean", "sauber", "ready", "fertig", "чист", "убран", "готов"}}}) do
    for _, word in ipairs(pair[2]) do if text:find(word, 1, true) then state = state or pair[1] end end
  end
  if not room then return handlers.housekeep() end
  state = state or "clean"
  local label = ({clean = orion.tr("Clean"), dirty = orion.tr("Dirty"), inspection_needed = orion.tr("Inspection needed")})[state]
  return result(string.format(orion.tr("Press Enter again to mark room %s as %s."), room.name, Engine.lower(label)),
    {row(orion.tr("Housekeeping"), "room", string.format(orion.tr("Mark room %s: %s"), room.name, label), room.cleaning_label or "", "clean",
      string.format("%d:%s", room.id, state), true)})
end
function handlers.report(text, parsed)
  local today = orion.today()
  local from = today:sub(1, 8) .. "01"
  local to = calendar.add(calendar.month_end(from), 1)
  if text:find("last month", 1, true) or text:find("letzten monat", 1, true) or text:find("прошл", 1, true) then
    to = from; from = calendar.add(from, -1):sub(1, 8) .. "01"
  elseif parsed.date then from = parsed.date; to = calendar.add(from, parsed.nights or 1)
  elseif text:find("week", 1, true) or text:find("woche", 1, true) or text:find("недел", 1, true) then from = today; to = calendar.add(today, 7)
  elseif text:find("today", 1, true) or text:find("heute", 1, true) or text:find("сегодня", 1, true) then from = today; to = calendar.add(today, 1) end
  local ok, report = pcall(FrontDesk.period, from, to)
  if not ok then return result(orion.tr("I could not work out that period.")) end
  local rows = {}
  for _, line in ipairs(FrontDesk.period_rows(report)) do rows[#rows + 1] = row(line.section, "figure", line.label, line.value, "open", "reports") end
  return result(string.format(orion.tr("%s to %s: occupancy %s %%, room revenue %s, ADR %s."), orion.formatDate(from), orion.formatDate(calendar.add(to, -1)),
    orion.formatNumber(report.occupancy, 1), orion.formatNumber(report.room_revenue / 100, 2), orion.formatNumber(report.adr / 100, 2)), rows)
end
function handlers.walk_in() return result(orion.tr("Press Enter again to open the walk-in window."), {row(orion.tr("Commands"), "command", orion.tr("Walk-in guest"), orion.tr("Book, check in and take payment in one window"), "walk_in", "", true)}) end
function handlers.close_day() return result(orion.tr("Press Enter again to open the night audit."), {row(orion.tr("Commands"), "command", orion.tr("Night audit"), orion.tr("The checklist, today's money and closing the day"), "open", "day_close", true)}) end
function handlers.calendar() return result(orion.tr("Press Enter again to open the room calendar."), {row(orion.tr("Commands"), "command", orion.tr("Room calendar"), orion.tr("Who sleeps where over the next two weeks"), "open", "availability", true)}) end
function handlers.export() return result(orion.tr("Press Enter again to export reservations, guests and payments as CSV files."), {row(orion.tr("Commands"), "command", orion.tr("Export data as CSV"), orion.tr("For spreadsheets, accounting or a backup"), "export", "", true)}) end
function handlers.find(text)
  local tokens = remaining(text)
  local rows = {}
  for _, r in ipairs(Hotel.search(table.concat(tokens, " "), 12)) do
    rows[#rows + 1] = row(orion.tr("Reservations"), "stay", stay_title(r), stay_detail(r), "open_reservation", r.id)
  end
  if #rows == 0 then
    for _, token in ipairs(tokens) do
      for _, r in ipairs(Hotel.search(token, 8)) do rows[#rows + 1] = row(orion.tr("Reservations"), "stay", stay_title(r), stay_detail(r), "open_reservation", r.id) end
      if #rows > 0 then break end
    end
  end
  for _, guest in ipairs(Engine.guests(tokens)) do
    if #rows < 16 then
      local contact = table.concat({guest.phone or "", guest.email or ""}, " "):match("^%s*(.-)%s*$")
      rows[#rows + 1] = row(orion.tr("Guests"), "guest", guest.name, contact ~= "" and contact or orion.tr("Choose to start a reservation for this guest"),
        "new_for_guest", guest.id)
    end
  end
  if #rows == 0 then return nil end
  return result(string.format(orion.tr("%d matches. Choose one to open it."), #rows), rows)
end

-- Examples to try, in the user's language.
function Engine.examples()
  local section = orion.tr("Examples")
  local rows = {}
  for _, text in ipairs({orion.tr("who arrives today"), orion.tr("free rooms tomorrow 2 nights for 2"), orion.tr("check in Morgan"),
                         orion.tr("Taylor paid 50 cash"), orion.tr("who owes"), orion.tr("dirty rooms"), orion.tr("mark 104 clean"),
                         orion.tr("book 203 for Grace Miller friday 3 nights"), orion.tr("extend Novak"), orion.tr("occupancy this month")}) do
    rows[#rows + 1] = row(section, "example", text, "", "ask", text)
  end
  return rows
end
function handlers.help()
  return result(orion.tr("Type what you want in your own words: a guest's name, a room, a date. Choose a row below to try it."), Engine.examples())
end

-- What to suggest before anything is typed: the day's open tasks as actions.
function Engine.suggestions()
  local day, rows = orion.today(), {}
  local section = orion.tr("Suggestions")
  local arrivals, departures = FrontDesk.movements(day)
  for _, a in ipairs(arrivals) do
    local r = Hotel.reservations.model:find(a.reservation_id)
    if a.arrival < day then rows[#rows + 1] = row(section, "warning", string.format(orion.tr("Mark %s as a no-show"), a.guest), string.format(orion.tr("Room %s · expected %s"), a.room, orion.formatDate(a.arrival)), "no_show", r.id)
    elseif a.state == "not_ready" then rows[#rows + 1] = row(section, "room", string.format(orion.tr("Room %s is ready"), a.room), string.format(orion.tr("%s arrives today"), a.guest), "clean", string.format("%d:clean", r.room_id))
    else rows[#rows + 1] = row(section, "action", string.format(orion.tr("Check in %s"), a.guest), string.format(orion.tr("Room %s · ready"), a.room), "check_in", r.id) end
  end
  for _, d in ipairs(departures) do
    if money.minor(d.balance) > 0 then rows[#rows + 1] = row(section, "money", string.format(orion.tr("Take %s from %s"), orion.formatNumber(d.balance, 2), d.guest), string.format(orion.tr("Room %s · leaving"), d.room), "payment_window", d.reservation_id)
    else rows[#rows + 1] = row(section, "action", string.format(orion.tr("Check out %s"), d.guest), string.format(orion.tr("Room %s · settled"), d.room), "check_out", d.reservation_id) end
  end
  if #Hotel.hotel_settings.model:all() == 0 then rows[#rows + 1] = row(section, "warning", orion.tr("Choose the hotel's country and currency"), orion.tr("Setup, before the first reservation"), "open", "hotel_setup")
  elseif #Hotel.rooms.model:all() == 0 then rows[#rows + 1] = row(section, "command", orion.tr("Try the sample hotel"), orion.tr("Nine rooms and a few guests, to explore"), "sample", "") end
  while #rows > 8 do table.remove(rows) end
  for _, example in ipairs(Engine.examples()) do if #rows < 12 then rows[#rows + 1] = example end end
  local reply = #rows > 0 and rows[1].section == section and orion.tr("Here is what today still needs. Type a request, or choose a row.") or
    orion.tr("What would you like to do? Type it in your own words, or choose an example.")
  return result(reply, rows)
end

-- Reads a sentence into an answer.
function Engine.answer(text)
  local original = text or ""
  text = Engine.lower(original):gsub("%s+", " "):match("^%s*(.-)%s*$")
  if text == "" then return Engine.suggestions() end
  local parsed = {}
  -- Amounts look like dates ("10.50"), so a payment keeps its numbers.
  local paying = false
  for _, phrase in ipairs(intents.payment) do if contains(text, phrase) then paying = true end end
  if not paying then
    parsed.date, text = Engine.date(text)
    parsed.nights, parsed.persons, text = Engine.counts(text)
  end
  local best, best_length, best_s, best_e
  for _, intent in ipairs(order) do
    for _, phrase in ipairs(intents[intent]) do
      local s, e = contains(text, phrase)
      if s and (not best or #phrase > best_length) then best, best_length, best_s, best_e = intent, #phrase, s, e end
    end
  end
  local rest = best and (text:sub(1, best_s - 1) .. " " .. text:sub(best_e + 1)) or text
  if best == "mark" or (best == "housekeep" and Engine.room(remaining(rest))) then return handlers.mark(text) end
  if best and handlers[best] then
    local answer = handlers[best](rest, parsed)
    if answer then return answer end
  end
  -- A bare date or count asks for free rooms; anything else is a search.
  if not best and (parsed.date or parsed.nights) then return handlers.free(rest, parsed) end
  local found = handlers.find(rest)
  if found then return found end
  return result(orion.tr("I did not understand that. Try a guest's name, a room number, or one of these examples."), Engine.examples())
end

-- ── Doing ───────────────────────────────────────────────────────────────────
-- Carries out a row; returns the sentence to show, and the page to open, if any.
function Engine.perform(action, arg)
  local id = tonumber(arg)
  if action == "check_in" or action == "check_out" then
    local r = orion.assert(Hotel.reservations.model:find(id or 0), orion.tr("Choose a reservation."))
    orion.assert(Hotel.write_all(function() return Hotel.transition(r.id, action == "check_in" and "checked_in" or "checked_out") end))
    return string.format(action == "check_in" and orion.tr("%s checked in to room %s.") or orion.tr("%s checked out of room %s."), r.guest.name, r.room.name)
  elseif action == "extend" or action == "shorten" then
    local r = orion.assert(Hotel.reservations.model:find(id or 0), orion.tr("Choose a reservation."))
    local saved = orion.assert(Hotel.extend(r.id, action == "extend" and 1 or -1))
    return string.format(orion.tr("%s now stays until %s."), r.guest.name, orion.formatDate(saved.departure))
  elseif action == "no_show" then
    local r = orion.assert(Hotel.reservations.model:find(id or 0), orion.tr("Choose a reservation."))
    orion.assert(Hotel.no_show(r.id))
    return string.format(orion.tr("%s marked as a no-show; room %s is free again."), r.guest.name, r.room.name)
  elseif action == "payment" then
    local rid, minor, method = arg:match("^(%d+):(%d+):(%d+)$")
    rid, minor, method = tonumber(rid), tonumber(minor), tonumber(method)
    local r = orion.assert(Hotel.reservations.model:find(rid or 0), orion.tr("Choose a reservation."))
    orion.assert(Hotel.payment(r.id, minor, Hotel.next_payment_key(), method ~= 0 and math.tointeger(method) or nil))
    local after = Hotel.reservations.model:find(r.id)
    return string.format(orion.tr("Payment of %s from %s recorded. Balance: %s."), orion.formatNumber(minor / 100, 2), r.guest.name, orion.formatNumber(Hotel.balance(after), 2))
  elseif action == "clean" then
    local room_id, state = arg:match("^(%d+):([%w_]+)$")
    local room = orion.assert(Hotel.rooms.model:find(tonumber(room_id) or 0), orion.tr("Choose a room."))
    orion.assert(Hotel.write_all(function() return Hotel.rooms:save(room.id, {cleaning_state = state}) end))
    return string.format(orion.tr("Room %s is now marked %s."), room.name, Engine.lower(orion.tr(({clean = "Clean", dirty = "Dirty", inspection_needed = "Inspection needed"})[state] or state)))
  elseif action == "book" then
    local room_id, name, arrival, departure, persons = arg:match("^(%d+)|(.-)|([%d-]+)|([%d-]+)|(%d+)$")
    local room = orion.assert(Hotel.rooms.model:find(tonumber(room_id) or 0), orion.tr("Choose a room."))
    local saved = orion.assert(Hotel.write_all(function()
      local guest = orion.assert(Hotel.guest_named(name))
      return Hotel.save(nil, {room_id = room.id, guest_id = guest.id, arrival = arrival, departure = departure, persons = tonumber(persons),
        rate = room.rate, extras = 0, notes = "", source_id = 0})
    end))
    return string.format(orion.tr("Reservation %d saved: room %s for %s, %s to %s."), saved.id, room.name, saved.guest.name,
      orion.formatDate(arrival), orion.formatDate(departure))
  elseif action == "prefill" then
    local room_id, arrival, departure, persons = arg:match("^(%d+)|([%d-]+)|([%d-]+)|(%d+)$")
    Hotel.prefill = {room = tonumber(room_id), arrival = arrival, departure = departure, persons = tonumber(persons)}
    return orion.tr("The reservation form is ready with the room and dates. Choose the guest and save."), "reception"
  elseif action == "new_for_guest" then
    Hotel.prefill = {guest = id}
    return orion.tr("The reservation form is ready for this guest. Choose the room and dates, then save."), "reception"
  elseif action == "open_reservation" then
    Hotel.focus_reservation = id
    return orion.tr("The reservation is open on the Reservations page."), "reception"
  elseif action == "payment_window" then
    Hotel.pending_payment_id = id
    return orion.tr("Enter the amount in the payment window."), "payment"
  elseif action == "walk_in" then return orion.tr("Fill in the walk-in window."), "walk_in"
  elseif action == "open" then return "", arg
  elseif action == "export" then
    return string.format(orion.tr("Data exported for spreadsheets and accounting: %s"), table.concat(require("exports").all(), ", "))
  elseif action == "sample" then
    orion.assert(require("samples").load())
    return orion.tr("Sample hotel added: nine rooms and a few guests around today. Open Today to try it.")
  end
  return orion.fail(orion.tr("The assistant cannot do that yet."))
end
return Engine

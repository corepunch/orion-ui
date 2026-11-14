-- Printable documents the front desk hands out or files: the guest
-- registration form, the guest bill, the cleaning list and a period report.
-- Each is escaped HTML to open in a browser and print.
local orion = require("orion")
local Hotel = require("hotel")
local Plugins = require("orion.plugins")
local template, calendar, money = Plugins.use("template", 1), Plugins.use("calendar", 1), Plugins.use("money", 1)
local Documents = {}

local style = [[<style>body{font:14px sans-serif;max-width:760px;margin:30px auto}table{border-collapse:collapse;width:100%}
th,td{border:1px solid #888;padding:5px;text-align:left}td.n,th.n{text-align:right}.sign{margin-top:48px;border-top:1px solid;width:300px;padding-top:4px}
@page{size:A4;margin:15mm}</style>]]

local function settings() return Hotel.hotel_settings.model:all()[1] or {currency = ""} end
local function amount(minor) return orion.formatNumber(minor / 100, 2) end
local function date(iso) return iso and iso ~= "" and orion.formatDate(iso) or "" end

-- The registration form a guest fills in and signs on arrival (Germany's
-- Meldeschein, a registration card elsewhere): identity, stay and signature.
function Documents.registration(row)
  local guest = row.guest or {}
  return template.render([[<!doctype html><html><meta charset="utf-8"><title>{{title}}</title>]] .. style .. [[
<h1>{{title}}</h1><p>{{reservation_label}} {{id}} · {{room_label}} {{room}}</p>
<table><tbody>
<tr><th>{{name_label}}</th><td>{{name}}</td></tr><tr><th>{{birth_label}}</th><td>{{birth}}</td></tr>
<tr><th>{{nationality_label}}</th><td>{{nationality}}</td></tr><tr><th>{{address_label}}</th><td>{{address}}</td></tr>
<tr><th>{{document_label}}</th><td>{{document}}</td></tr><tr><th>{{arrival_label}}</th><td>{{arrival}}</td></tr>
<tr><th>{{departure_label}}</th><td>{{departure}}</td></tr><tr><th>{{persons_label}}</th><td>{{persons}}</td></tr>
</tbody></table><p>{{companions}}</p><div class="sign">{{signature}}</div></html>]], {
    title = orion.tr("Guest registration form"), reservation_label = orion.tr("Reservation"), id = row.id,
    room_label = orion.tr("Room:"), room = row.room and row.room.name or "",
    name_label = orion.tr("Guest name:"), name = guest.name or "", birth_label = orion.tr("Date of birth:"), birth = date(guest.birth_date),
    nationality_label = orion.tr("Nationality:"), nationality = guest.nationality or "", address_label = orion.tr("Home address:"), address = guest.address or "",
    document_label = orion.tr("ID or passport no.:"), document = guest.document or "",
    arrival_label = orion.tr("Arrival:"), arrival = date(row.arrival), departure_label = orion.tr("Departure:"), departure = date(row.departure),
    persons_label = orion.tr("Guests:"), persons = row.persons,
    companions = orion.tr("Accompanying guests (name, date of birth):"), signature = orion.tr("Guest's signature"),
  })
end

-- The bill of a stay: each night at its price, extra charges, payments and
-- the balance. An operational bill, not a statutory tax invoice.
function Documents.bill(row)
  local lines, nights = {}, calendar.between(row.arrival, row.departure)
  for i = 0, nights - 1 do
    local night = calendar.add(row.arrival, i)
    local override = Hotel.night_rates.model:find({reservation_id = row.id, night = night})
    lines[#lines + 1] = {text = string.format(orion.tr("Night of %s"), orion.formatDate(night)),
      amount = amount(override and override.amount_minor or money.minor(row.rate))}
  end
  if money.minor(row.extras) ~= 0 then lines[#lines + 1] = {text = orion.tr("Extra charges"), amount = amount(money.minor(row.extras))} end
  local payments = {}
  for _, event in ipairs(require("orion.model"):extend("lodge.payment_events"):select({reservation_id = row.id}, {order = "id"})) do
    local method = event.method_id ~= 0 and Hotel.payment_methods.model:find(event.method_id)
    payments[#payments + 1] = {date = date(event.posted_on), text = event.amount_minor < 0 and orion.tr("Refund") or orion.tr("Payment"),
      method = method and method.name or "", amount = amount(event.amount_minor)}
  end
  return template.render([[<!doctype html><html><meta charset="utf-8"><title>{{title}}</title>]] .. style .. [[
<h1>{{title}}</h1><p>{{guest_label}} {{guest}} · {{room_label}} {{room}}</p><p>{{arrival_label}} {{arrival}} — {{departure_label}} {{departure}}</p>
<table><thead><tr><th>{{item_label}}</th><th class="n">{{amount_label}}</th></tr></thead><tbody>
{{#lines}}<tr><td>{{text}}</td><td class="n">{{amount}}</td></tr>{{/lines}}
<tr><th>{{total_label}}</th><th class="n">{{total}}</th></tr></tbody></table>
<h2>{{payments_title}}</h2><table><tbody>{{#payments}}<tr><td>{{date}}</td><td>{{text}}</td><td>{{method}}</td><td class="n">{{amount}}</td></tr>{{/payments}}</tbody></table>
<p><b>{{balance_label}} {{balance}} {{currency}}</b></p><p>{{disclaimer}}</p></html>]], {
    title = orion.tr("Guest bill"), guest_label = orion.tr("Guest:"), guest = row.guest and row.guest.name or "",
    room_label = orion.tr("Room:"), room = row.room and row.room.name or "",
    arrival_label = orion.tr("Arrival:"), arrival = date(row.arrival), departure_label = orion.tr("Departure:"), departure = date(row.departure),
    item_label = orion.tr("Item"), amount_label = string.format(orion.tr("Amount, %s"), settings().currency), lines = lines,
    total_label = orion.tr("Total:"), total = orion.formatNumber(Hotel.total(row), 2), payments_title = orion.tr("Payments and refunds"), payments = payments,
    balance_label = orion.tr("Balance:"), balance = orion.formatNumber(Hotel.balance(row), 2), currency = settings().currency,
    disclaimer = orion.tr("This bill is an operational summary, not a tax invoice."),
  })
end

-- Housekeeping's list for the day: every room with what to do in it.
function Documents.cleaning_list(day)
  local departing, arriving = {}, {}
  for _, row in ipairs(Hotel.reservations.model:all()) do
    if row.status == "checked_in" and row.departure <= day then departing[row.room_id] = true end
    if row.status == "reserved" and row.arrival == day then arriving[row.room_id] = true end
  end
  local labels = {inspection_needed = "Inspection needed", dirty = "Dirty", clean = "Clean"}
  local rooms = {}
  for _, room in ipairs(Hotel.rooms.model:select(nil, {order = "name"})) do
    local tasks = {}
    if Hotel.room_blocked(room.id, day, calendar.add(day, 1)) then tasks[#tasks + 1] = orion.tr("Out of service")
    else
      if departing[room.id] then tasks[#tasks + 1] = orion.tr("Departure: full clean after check-out") end
      if arriving[room.id] then tasks[#tasks + 1] = orion.tr("Arrival today: prepare first") end
      if not departing[room.id] and Hotel.room_occupied(room.id) then tasks[#tasks + 1] = orion.tr("Stay-over: tidy") end
      if room.cleaning_state ~= "clean" and #tasks == 0 then tasks[#tasks + 1] = orion.tr("Clean or inspect") end
    end
    rooms[#rooms + 1] = {name = room.name, state = orion.tr(labels[room.cleaning_state] or room.cleaning_state), tasks = table.concat(tasks, "; ")}
  end
  return template.render([[<!doctype html><html><meta charset="utf-8"><title>{{title}}</title>]] .. style .. [[
<h1>{{title}}</h1><p>{{day}}</p><table><thead><tr><th>{{room_label}}</th><th>{{state_label}}</th><th>{{tasks_label}}</th><th>{{done_label}}</th></tr></thead>
<tbody>{{#rooms}}<tr><td>{{name}}</td><td>{{state}}</td><td>{{tasks}}</td><td></td></tr>{{/rooms}}</tbody></table></html>]], {
    title = orion.tr("Cleaning list"), day = orion.formatDate(day), room_label = orion.tr("Room"), state_label = orion.tr("Cleaning status"),
    tasks_label = orion.tr("To do"), done_label = orion.tr("Done"), rooms = rooms,
  })
end

function Documents.period(report, rows)
  return template.render([[<!doctype html><html><meta charset="utf-8"><title>{{title}}</title>]] .. style .. [[
<h1>{{title}}</h1><p>{{period}}</p><table><tbody>{{#rows}}<tr><td>{{section}}</td><td>{{label}}</td><td class="n">{{value}}</td></tr>{{/rows}}</tbody></table>
<p>{{note}}</p></html>]], {
    title = orion.tr("Period report"), rows = rows,
    period = string.format(orion.tr("%s to %s (exclusive) · Currency: %s"), orion.formatDate(report.from), orion.formatDate(report.to), settings().currency),
    note = orion.tr("Operational figures from Lodge's records; not a statutory or tax report."),
  })
end
return Documents

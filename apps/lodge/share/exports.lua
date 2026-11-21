-- Lodge's records as CSV, so the hotel's data never stays locked in Lodge:
-- reservations, guests and payments, one file each, with stable English
-- column names (field names) that other software can map.
local orion = require("orion")
local Hotel = require("hotel")
local money = require("orion.plugins").use("money", 1)
local Exports = {}

local function cell(value)
  if value == nil then return "" end
  if type(value) == "number" then
    if value % 1 == 0 and math.abs(value) < 1e15 then return string.format("%d", value) end
    return string.format("%.2f", value)
  end
  local text = tostring(value)
  if text:find('[",\r\n]') or text:match("^[=+%-@]") then text = '"' .. text:gsub('"', '""') .. '"' end
  return text
end
local function csv(header, rows)
  local lines = {table.concat(header, ",")}
  for _, row in ipairs(rows) do
    local cells = {}
    for i, key in ipairs(header) do cells[i] = cell(row[key]) end
    lines[#lines + 1] = table.concat(cells, ",")
  end
  return table.concat(lines, "\r\n") .. "\r\n"
end

function Exports.reservations()
  local rows = {}
  for _, row in ipairs(Hotel.reservations.model:select(nil, {order = "arrival, id"})) do
    local source = row.source_id ~= 0 and Hotel.booking_sources.model:find(row.source_id)
    rows[#rows + 1] = {id = row.id, room = row.room and row.room.name or "", guest = row.guest and row.guest.name or "",
      arrival = row.arrival, departure = row.departure, persons = row.persons, status = row.status, rate = row.rate,
      extras = row.extras, total = Hotel.total(row), paid = Hotel.paid(row), balance = Hotel.balance(row),
      source = source and source.name or "", group_id = row.group_id ~= 0 and row.group_id or nil, notes = row.notes}
  end
  return csv({"id", "room", "guest", "arrival", "departure", "persons", "status", "rate", "extras", "total", "paid", "balance", "source", "group_id", "notes"}, rows)
end

function Exports.guests()
  local rows = {}
  for _, guest in ipairs(Hotel.guests.model:select(nil, {order = "name"})) do
    rows[#rows + 1] = {id = guest.id, name = guest.name, phone = guest.phone, email = guest.email, birth_date = guest.birth_date,
      nationality = guest.nationality, address = guest.address, document = guest.document, notes = guest.notes}
  end
  return csv({"id", "name", "phone", "email", "birth_date", "nationality", "address", "document", "notes"}, rows)
end

function Exports.payments()
  local rows = {}
  for _, event in ipairs(require("orion.model"):extend("lodge.payment_events"):select(nil, {order = "id"})) do
    local method = event.method_id ~= 0 and Hotel.payment_methods.model:find(event.method_id)
    rows[#rows + 1] = {id = event.id, reservation_id = event.reservation_id, posted_on = event.posted_on, kind = event.kind,
      amount = event.amount_minor / 100, method = method and method.name or "", actor = event.actor}
  end
  return csv({"id", "reservation_id", "posted_on", "kind", "amount", "method", "actor"}, rows)
end

-- Writes the three files to the data folder; returns the folder's file paths.
function Exports.all()
  local application = require("orion.application")
  local paths = {}
  for _, name in ipairs({"reservations", "guests", "payments"}) do
    paths[#paths + 1] = orion.assert(application.write("lodge-" .. name .. ".csv", Exports[name]()))
  end
  return paths
end
Exports.minor = money.minor
return Exports

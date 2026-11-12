local orion = require("orion")
local template = require("orion.plugins").use("template", 1)
return function(day, rows)
  local lines = {}
  table.sort(rows, function(a, b) return a.id < b.id end)
  for _, row in ipairs(rows) do
    local arrival = row.status == "reserved" and row.arrival <= day
    local departure = row.status == "checked_in" and row.departure <= day
    if arrival or departure then
      lines[#lines + 1] = {id = row.id, room = row.room, guest = row.guest,
        arrival = orion.formatDate(row.arrival), departure = orion.formatDate(row.departure),
        action = orion.tr(arrival and "Overdue arrival" or "Overdue departure")}
    end
  end
  return template.render([[<!doctype html><html><meta charset="utf-8"><title>{{title}}</title>
<style>body{font:14px sans-serif;margin:30px}table{border-collapse:collapse;width:100%}th,td{border:1px solid;padding:4px;text-align:left}@page{size:A4 landscape;margin:15mm}</style>
<h1>{{title}}</h1><p>{{day_label}} {{day}}</p><p>{{description}}</p>
<table><thead><tr><th>{{id_label}}</th><th>{{room_label}}</th><th>{{guest_label}}</th><th>{{arrival_label}}</th><th>{{departure_label}}</th><th>{{action_label}}</th></tr></thead>
<tbody>{{#lines}}<tr><td>{{id}}</td><td>{{room}}</td><td>{{guest}}</td><td>{{arrival}}</td><td>{{departure}}</td><td>{{action}}</td></tr>{{/lines}}</tbody></table></html>]], {
    title = orion.tr("Overdue stays"), day_label = orion.tr("Operational day:"), day = orion.formatDate(day),
    description = orion.tr("Reservations still awaiting arrival or departure at the time of export."),
    id_label = orion.tr("Reservation"), room_label = orion.tr("Room"), guest_label = orion.tr("Guest"),
    arrival_label = orion.tr("Arrival"), departure_label = orion.tr("Departure"), action_label = orion.tr("Action"), lines = lines,
  })
end

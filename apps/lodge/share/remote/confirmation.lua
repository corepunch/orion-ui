local orion = require("orion")
local template = require("orion.plugins").use("template", 1)
return function(row)
  return template.render([[<!doctype html><html><meta charset="utf-8"><title>{{title}}</title>
<style>body{font:16px sans-serif;max-width:700px;margin:40px auto} @page{size:A4;margin:20mm}</style>
<h1>{{title}}</h1><p>{{guest_label}} {{guest}}</p><p>{{room_label}} {{room}}</p>
<p>{{arrival_label}} {{arrival}} — {{departure_label}} {{departure}}</p>
<p>{{total_label}} {{total}} · {{paid_label}} {{paid}} · {{balance_label}} {{balance}}</p></html>]], {
    title = orion.tr("Booking confirmation"), guest_label = orion.tr("Guest:"), guest = row.guest,
    room_label = orion.tr("Room:"), room = row.room, arrival_label = orion.tr("Arrival:"), arrival = orion.formatDate(row.arrival),
    departure_label = orion.tr("Departure:"), departure = orion.formatDate(row.departure),
    total_label = orion.tr("Total:"), total = orion.formatNumber(row.total, 2),
    paid_label = orion.tr("Paid:"), paid = orion.formatNumber(row.paid, 2), balance_label = orion.tr("Balance:"), balance = orion.formatNumber(row.balance, 2),
  })
end

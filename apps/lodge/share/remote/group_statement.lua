local orion = require("orion")
local plugins = require("orion.plugins")
local template, money = plugins.use("template", 1), plugins.use("money", 1)
local statuses = {reserved = "Reserved", checked_in = "Checked in", checked_out = "Checked out", cancelled = "Cancelled"}
return function(group, rows, currency)
  local lines, total, paid, balance = {}, 0, 0, 0
  table.sort(rows, function(a, b) return a.id < b.id end)
  for _, row in ipairs(rows) do
    total, paid, balance = total + money.minor(row.total), paid + money.minor(row.paid), balance + money.minor(row.balance)
    lines[#lines + 1] = {room = row.room, guest = row.guest, arrival = orion.formatDate(row.arrival), departure = orion.formatDate(row.departure),
      status = orion.tr(statuses[row.status] or row.status), total = orion.formatNumber(row.total, 2),
      paid = orion.formatNumber(row.paid, 2), balance = orion.formatNumber(row.balance, 2)}
  end
  return template.render([[<!doctype html><html><meta charset="utf-8"><title>{{title}}</title>
<style>body{font:14px sans-serif;margin:30px}table{border-collapse:collapse;width:100%}th,td{border:1px solid;padding:4px;text-align:left}@page{size:A4 landscape;margin:15mm}</style>
<h1>{{title}}</h1><p>{{group_label}} {{group}} · {{currency_label}} {{currency}}</p><p>{{explanation}}</p>
<table><thead><tr><th>{{room_label}}</th><th>{{guest_label}}</th><th>{{arrival_label}}</th><th>{{departure_label}}</th><th>{{status_label}}</th><th>{{total_label}}</th><th>{{paid_label}}</th><th>{{balance_label}}</th></tr></thead>
<tbody>{{#lines}}<tr><td>{{room}}</td><td>{{guest}}</td><td>{{arrival}}</td><td>{{departure}}</td><td>{{status}}</td><td>{{total}}</td><td>{{paid}}</td><td>{{balance}}</td></tr>{{/lines}}</tbody></table>
<p>{{total_label}} {{total}} · {{paid_label}} {{paid}} · {{balance_label}} {{balance}}</p></html>]], {
    title = orion.tr("Group reservation statement"), group_label = orion.tr("Group:"), group = group, currency_label = orion.tr("Currency:"), currency = currency,
    explanation = orion.tr("Totals sum all listed reservations, including cancelled stays. Payments and refunds are recorded per reservation; this is not a tax invoice."),
    room_label = orion.tr("Room"), guest_label = orion.tr("Guest"), arrival_label = orion.tr("Arrival"), departure_label = orion.tr("Departure"),
    status_label = orion.tr("Status"), total_label = orion.tr("Total"), paid_label = orion.tr("Paid"), balance_label = orion.tr("Balance"), lines = lines,
    total = orion.formatNumber(total / 100, 2), paid = orion.formatNumber(paid / 100, 2), balance = orion.formatNumber(balance / 100, 2),
  })
end

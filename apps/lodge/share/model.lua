local Hotel = require("hotel")
local orion = require("orion")
local labels = {reserved = "Reserved", checked_in = "Checked in", checked_out = "Checked out", cancelled = "Cancelled"}
local Model = {}
function Model:validate(table, row, previous)
  local records = Hotel[table]
  if records then return records.validate(row, previous) end
  return true
end
function Model:compute(table, row, property)
  if table ~= "reservations" then return nil end
  if property == "total" then return Hotel.total(row) end
  if property == "balance" then return Hotel.balance(row) end
  if property == "status_label" then return orion.tr(labels[row.status] or row.status) end
end
return Model

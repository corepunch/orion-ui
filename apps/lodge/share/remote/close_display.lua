local orion = require("orion")
return function(row, property)
  if property == "cancelled_count_display" then
    return (not row.cancelled_paid_count or row.cancelled_paid_count < 0) and orion.tr("Unknown") or orion.formatNumber(row.cancelled_paid_count, 0)
  end
  local field = ({net_display = "net_minor", outstanding_display = "outstanding_minor", cancelled_paid_display = "cancelled_paid_minor"})[property]
  local amount = field and tonumber(row[field])
  return amount and orion.formatNumber(amount / 100, 2) or orion.tr("Unknown")
end

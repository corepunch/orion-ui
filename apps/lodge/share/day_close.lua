local orion = require("orion")
local Hotel = require("hotel")
local DayClose = {form = "day_close", title = orion.tr("Close day")}
function DayClose:init() self.notice = "" end
function DayClose:close_day()
  orion.assert(Hotel.close_today())
  self.notice = orion.tr("Today is closed.")
end
function DayClose:data()
  local report = Hotel.day_report()
  return {values = {
    day = string.format(orion.tr("Operational day: %s"), orion.formatDate(report.day)),
    payments = string.format(orion.tr("Today's net payments: %s"), orion.formatNumber(report.net_minor / 100, 2)),
    outstanding = string.format(orion.tr("Outstanding balance: %s"), orion.formatNumber(report.outstanding_minor / 100, 2)),
    pending = string.format(orion.tr("Overdue arrivals and departures: %d"), report.pending), notice = self.notice,
  }, disabled = {close_day = report.closed}}
end
return DayClose

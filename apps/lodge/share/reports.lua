local orion = require("orion")
local FrontDesk = require("front_desk")
local Documents = require("documents")
local application = require("orion.application")
local calendar = require("orion.plugins").use("calendar", 1)
local Reports = {form = "reports", title = orion.tr("Reports")}

local function month_of(day)
  local first = day:sub(1, 8) .. "01"
  return first, calendar.add(calendar.month_end(first), 1)
end
function Reports:init()
  self.from, self.to = month_of(orion.today())
  self.notice = ""
  self:compute()
end
function Reports:compute()
  self.report = FrontDesk.period(self.from, self.to)
  self.rows = FrontDesk.period_rows(self.report)
end
function Reports:run(values)
  self.from, self.to = orion.assert(values.from, orion.tr("Enter the first day.")), orion.assert(values.to, orion.tr("Enter the day after the last."))
  self:compute()
end
function Reports:this_month() self.from, self.to = month_of(orion.today()); self:compute() end
function Reports:last_month() self.from, self.to = month_of(calendar.add(month_of(orion.today()), -1)); self:compute() end
function Reports:export()
  self:compute()
  self.notice = string.format(orion.tr("Report saved: %s"), orion.assert(application.write("period-report.html", Documents.period(self.report, self.rows))))
end
function Reports:data()
  -- The figures follow the records: recompute on every refresh.
  local ok = pcall(self.compute, self)
  return {values = {from = self.from, to = self.to, notice = self.notice,
    period = ok and string.format(orion.tr("%d days · %d rooms"), self.report.days, self.report.rooms) or ""},
    lists = {figures = self.rows or {}}}
end
return Reports

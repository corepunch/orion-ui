-- Night audit: a checklist of what the day still needs, today's money by
-- method, then closing the day, which locks its reservations and payments.
local orion = require("orion")
local Hotel = require("hotel")
local FrontDesk = require("front_desk")
local application = require("orion.application")
local overdue = require("remote.overdue")
local money = require("orion.plugins").use("money", 1)
local DayClose = {form = "day_close", title = orion.tr("Close day")}
function DayClose:init() self.notice = "" end
function DayClose:close_day()
  orion.assert(Hotel.close_today(orion.assert(self.preview, orion.tr("Preview the day before closing."))))
  self.notice = orion.tr("Today is closed. Reservations and payments resume tomorrow.")
end
function DayClose:export_overdue()
  local day, rows = Hotel.day_report().day, {}
  for _, row in ipairs(Hotel.reservations.model:all()) do
    rows[#rows + 1] = {id = row.id, room = row.room.name, guest = row.guest.name, arrival = row.arrival,
      departure = row.departure, status = row.status}
  end
  self.notice = string.format(orion.tr("Overdue stays saved: %s"), orion.assert(application.write("overdue-stays.html", overdue(day, rows))))
end
-- Each check: done (ok), blocking the close (error) or worth a look (warning).
function DayClose:checklist(report)
  local day, rows = report.day, {}
  local function add(kind, title, detail) rows[#rows + 1] = {kind = kind, title = title, detail = detail} end
  local arrivals, departures, in_house = FrontDesk.movements(day)
  local late_arrivals, late_departures, unpaid = 0, 0, 0
  for _, row in ipairs(arrivals) do if row.arrival <= day then late_arrivals = late_arrivals + 1 end end
  for _, row in ipairs(departures) do
    late_departures = late_departures + 1
    if money.minor(row.balance) ~= 0 then unpaid = unpaid + 1 end
  end
  add(late_arrivals == 0 and "ok" or "error", orion.tr("Every arrival is checked in, cancelled or a no-show"),
    late_arrivals == 0 and orion.tr("Done") or string.format(orion.tr("%d still expected · Today page"), late_arrivals))
  add(late_departures == 0 and "ok" or "error", orion.tr("Every departure is checked out"),
    late_departures == 0 and orion.tr("Done") or string.format(orion.tr("%d still in house · Today page"), late_departures))
  add(unpaid == 0 and "ok" or "warning", orion.tr("Departing guests have settled their bills"),
    unpaid == 0 and orion.tr("Done") or string.format(orion.tr("%d with a balance"), unpaid))
  local dirty = 0
  for _, room in ipairs(Hotel.rooms.model:all()) do if room.cleaning_state ~= "clean" and not Hotel.room_occupied(room.id) then dirty = dirty + 1 end end
  add(dirty == 0 and "ok" or "warning", orion.tr("Empty rooms are clean for tomorrow"),
    dirty == 0 and orion.tr("Done") or string.format(orion.tr("%d to clean or inspect · Rooms page"), dirty))
  add("info", orion.tr("Guests in house tonight"), orion.formatNumber(#in_house, 0))
  local methods, events = {}, require("orion.model"):extend("lodge.payment_events")
  for _, event in ipairs(events:select({posted_on = day})) do
    local found = event.method_id ~= 0 and Hotel.payment_methods.model:find(event.method_id)
    local name = found and found.name or orion.tr("Not recorded")
    methods[name] = (methods[name] or 0) + event.amount_minor
  end
  local names = {}
  for name in pairs(methods) do names[#names + 1] = name end
  table.sort(names)
  for _, name in ipairs(names) do add("money", string.format(orion.tr("Taken today: %s"), name), orion.formatNumber(methods[name] / 100, 2)) end
  add("money", orion.tr("Net payments today"), orion.formatNumber(report.net_minor / 100, 2))
  add("money", orion.tr("Outstanding on open reservations"), orion.formatNumber(report.outstanding_minor / 100, 2))
  if report.cancelled_paid_count > 0 then
    add("warning", orion.tr("Cancelled reservations still holding payments"),
      string.format(orion.tr("%d · %s · refund or keep as agreed"), report.cancelled_paid_count, orion.formatNumber(report.cancelled_paid_minor / 100, 2)))
  end
  return rows
end
function DayClose:data()
  local report = Hotel.day_report()
  self.preview = report
  local settings = Hotel.hotel_settings.model:all()[1]
  local status = report.closed and orion.tr("Closed. Reservations and payments resume tomorrow.") or
    (report.pending > 0 and orion.tr("Resolve the red items, then close the day.") or orion.tr("Ready to close."))
  return {values = {
    day = string.format(orion.tr("Night audit for %s"), orion.formatDate(report.day)),
    status = settings and status or orion.tr("Choose the hotel's country and currency in Setup first."),
    notice = self.notice,
  }, lists = {checklist = self:checklist(report)}, disabled = {close_day = report.closed or report.pending > 0 or not settings}}
end
return DayClose

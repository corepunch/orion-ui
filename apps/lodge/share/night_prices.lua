local orion = require("orion")
local Hotel = require("hotel")
local application = require("orion.application")
local Plugins = require("orion.plugins")
local calendar, money = Plugins.use("calendar", 1), Plugins.use("money", 1)
local Prices = {form = "night_prices", title = orion.tr("Nightly prices")}

function Prices:init()
  self.id = orion.assert(Hotel.pending_night_id, orion.tr("Choose a reservation."))
  local row = orion.assert(Hotel.reservations.model:find(self.id), orion.tr("Choose a reservation."))
  self.night, self.amount, self.notice = row.arrival, row.rate, ""
end
function Prices:select(values)
  local row = orion.assert(Hotel.reservations.model:find(self.id), orion.tr("Choose a reservation."))
  local ok = pcall(calendar.day, values.night)
  orion.assert(ok and values.night >= row.arrival and values.night < row.departure, orion.tr("Choose a night within the reservation dates."))
  self.night = values.night
  local override = Hotel.night_rates.model:find({reservation_id = self.id, night = self.night})
  return row, override
end
function Prices:show_price(values)
  local row, override = self:select(values)
  self.amount = override and override.amount_minor / 100 or row.rate
  self.notice = override and orion.tr("Custom price for this night.") or orion.tr("This night uses the base rate.")
end
function Prices:set_price(values)
  local _, override = self:select(values)
  orion.assert(Hotel.valid_amount(values.amount), orion.tr("Enter an amount from 0 to 1000000 with at most two decimal places."))
  self.amount = values.amount
  Hotel.night_rates.model:transaction(function()
    orion.assert(Hotel.night_rates:save(override and override.id, {reservation_id = self.id, night = self.night, amount_minor = money.minor(self.amount)}))
    orion.assert(application.save("lodge"))
  end)
  self.notice = orion.tr("Nightly price saved.")
end
function Prices:reset_price(values)
  local row, override = self:select(values)
  orion.assert(override, orion.tr("This night already uses the base rate."))
  Hotel.night_rates.model:transaction(function()
    orion.assert(Hotel.night_rates:delete(override.id))
    orion.assert(application.save("lodge"))
  end)
  self.amount, self.notice = row.rate, orion.tr("Base rate restored for this night.")
end
function Prices:data()
  local row = orion.assert(Hotel.reservations.model:find(self.id), orion.tr("Choose a reservation."))
  return {values = {reservation = string.format(orion.tr("Reservation %d"), self.id),
    total = string.format(orion.tr("Total: %s"), orion.formatNumber(Hotel.total(row), 2)),
    night = self.night, amount = self.amount, notice = self.notice}}
end
return Prices

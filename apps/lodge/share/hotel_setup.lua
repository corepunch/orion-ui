local orion = require("orion")
local Hotel = require("hotel")
local Setup = {form = "hotel_setup", title = orion.tr("Hotel setup")}
function Setup:init() self.notice = "" end
function Setup:choose(country, currency)
  orion.assert(Hotel.configure_hotel(country, currency))
  self.notice = orion.tr("Hotel country and currency saved.")
end
function Setup:germany() self:choose("DE", "EUR") end
function Setup:russia() self:choose("RU", "RUB") end
function Setup:data()
  local settings = Hotel.hotel_settings.model:all()[1]
  local current = settings and string.format(orion.tr("Country: %s · Currency: %s"), settings.country, settings.currency) or orion.tr("Hotel country and currency are not configured.")
  return {values = {current = current, notice = self.notice}, disabled = {germany = settings ~= nil, russia = settings ~= nil}}
end
return Setup

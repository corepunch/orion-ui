local orion = require("orion")
local Hotel = require("hotel")
local Samples = require("samples")
local Setup = {form = "hotel_setup", title = orion.tr("Hotel setup")}
local profile_fields = {"hotel_name", "hotel_address", "hotel_phone", "hotel_email", "check_in_time", "check_out_time"}
function Setup:init() self.notice = "" end
function Setup:choose(country, currency)
  orion.assert(Hotel.configure_hotel(country, currency))
  self.notice = orion.tr("Hotel country and currency saved.")
end
function Setup:germany() self:choose("DE", "EUR") end
function Setup:russia() self:choose("RU", "RUB") end
-- The hotel's name, address and times head every printed document.
function Setup:save_profile(values)
  local fields = {}
  for _, key in ipairs(profile_fields) do fields[key:gsub("^hotel_", "")] = values[key] or "" end
  orion.assert(Hotel.write_all(function() return Hotel.save_profile(fields) end))
  self.notice = orion.tr("Hotel details saved. They appear on bills, confirmations and registration forms.")
end
function Setup:sample()
  orion.assert(Samples.load())
  self.notice = orion.tr("Sample hotel added: nine rooms and a few guests around today. Open Today to try it.")
end
function Setup:export_data()
  local paths = require("exports").all()
  self.notice = string.format(orion.tr("Data exported for spreadsheets and accounting: %s"), table.concat(paths, ", "))
end
function Setup:data()
  local settings = Hotel.hotel_settings.model:all()[1]
  local current = settings and string.format(orion.tr("Country: %s · Currency: %s"), settings.country, settings.currency) or orion.tr("Hotel country and currency are not configured.")
  local profile = Hotel.profile()
  local values = {current = current, notice = self.notice}
  for _, key in ipairs(profile_fields) do values[key] = profile[key:gsub("^hotel_", "")] or "" end
  return {values = values, disabled = {germany = settings ~= nil, russia = settings ~= nil, sample = not Samples.available()}}
end
return Setup

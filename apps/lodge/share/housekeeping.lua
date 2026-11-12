local orion = require("orion")
local Hotel = require("hotel")
local application = require("orion.application")
local Housekeeping = {form = "housekeeping", title = orion.tr("Housekeeping")}

function Housekeeping:set_status(values, status)
  local selected = orion.assert(values.rooms, orion.tr("Choose a room."))
  Hotel.rooms.model:transaction(function()
    orion.assert(Hotel.rooms:save(selected.id, {cleaning_state = status}))
    orion.assert(application.save("lodge"))
  end)
end
function Housekeeping:mark_clean(values) self:set_status(values, "clean") end
function Housekeeping:mark_dirty(values) self:set_status(values, "dirty") end
function Housekeeping:mark_inspection(values) self:set_status(values, "inspection_needed") end
function Housekeeping:refresh() end
return Housekeeping

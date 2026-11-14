local orion = require("orion")
local Hotel = require("hotel")
local application = require("orion.application")
local Documents = require("documents")
local Housekeeping = {form = "rooms", title = orion.tr("Rooms")}

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
function Housekeeping:cleaning_list()
  self.notice = string.format(orion.tr("Cleaning list saved: %s"), orion.assert(application.write("cleaning-list.html", Documents.cleaning_list(orion.today()))))
end
function Housekeeping:data() return {values = {notice = self.notice or ""}} end
return Housekeeping

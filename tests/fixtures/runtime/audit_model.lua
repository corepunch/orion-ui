local orion = require("orion")
local Model = require("orion.model")
local events = Model:extend("audit_test.events")
local internal = false
local Policy = {}
function Policy:validate(table, fields, previous)
  if table == "events" then return internal and not previous, "Events are append-only." end
  if fields.name == "mutate_validation" then orion.assert(events:create({name = "illegal"})) end
  return true
end
function Policy:validate_delete(table, previous)
  return table ~= "events" and previous.name ~= "locked", "Deletion refused."
end
function Policy:after_write(table, operation, current, previous)
  if table ~= "items" then return true end
  internal = true
  local result, message = events:create({name = operation .. ":" .. (previous and previous.name or "")})
  internal = false
  orion.assert(result, message)
  if current and current.name == "remove_self" then
    orion.assert(Model:extend("audit_test.items"):find(current.id):delete())
  end
  if current and current.name == "fail_hook" then error("injected hook failure") end
  return true
end
return Policy

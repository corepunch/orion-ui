local orion = require("orion")
local tasks = require("orion.model"):extend("desk.tasks")
local Today = {form = "today"}
function Today:finish(values)
  local row = orion.assert(values.open, "Choose a task.")
  orion.assert(tasks:find(row.id):update({done = true}))
end
function Today:data()
  local open = {}
  for _, row in ipairs(tasks:all()) do if not row.done then open[#open + 1] = {id = row.id, title = row.title} end end
  return {values = {summary = string.format("%d open", #open)}, lists = {open = open}}
end
return Today

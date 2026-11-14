local orion = require("orion")
local tasks = require("orion.model"):extend("desk.tasks")
local Tasks = {form = "tasks"}
function Tasks:add(values) orion.assert(tasks:create({title = values.title, done = false})) end
return Tasks

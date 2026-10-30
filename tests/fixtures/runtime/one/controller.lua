local helper = require("helper")
local calendar = require("orion.plugins").use("calendar", 1)
return {check = function(self, expected)
  assert(helper.name == expected)
  assert(calendar == require("orion.plugins").use("calendar", 1))
  self.calls = (self.calls or 0) + 1
end, refuse = function(self, message) require("orion").assert(false, message) end}

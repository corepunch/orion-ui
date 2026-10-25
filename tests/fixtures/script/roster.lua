-- A test add-on: people of a registered database, filtered and listed in
-- the add-on's own scratch table.
local orion = require("orion")
local Model = require("orion.model")
local helper = require("roster_helper")

local People = Model:extend("club.people", {
	constraints = {name = function(_, name) if name == "" then return "A person needs a name." end end},
})

local Roster = {form = "roster", title = "Club roster"}

function Roster:init()
	self.minAge = 0
end

function Roster:data()
	local rows = {}
	local where = function(p) return p.age >= self.minAge and (not self.activeOnly or p.active) end
	for _, p in ipairs(People:select(where, {order = "age desc"})) do
		table.insert(rows, {name = p.name, age = p.age})
	end
	local summary = helper.count(#rows)
	if self.born then summary = summary .. " since " .. self.born end
	return {
		values = {min_age = self.minAge, summary = summary, active_only = self.activeOnly == true},
		lists = {lines = rows},
		disabled = {refuse = self.minAge > 100},
	}
end

function Roster:filter(values)
	self.minAge = values.min_age or 0
	self.born = values.born
	self.activeOnly = values.active_only
end

function Roster:add(values)
	orion.assert(People:create{name = values.new_name, age = 1})
end

function Roster:refuse()
	orion.fail("Not today.")
end

function Roster:broken()
	local missing = nil
	return missing.field
end

function Roster:done()
	self.app.close(7)
end

return Roster

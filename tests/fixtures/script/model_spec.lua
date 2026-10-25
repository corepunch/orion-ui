-- orion.model against the test club database. Each check reads as a fact.
local Model = require("orion.model")
local orion = require("orion")

local failures = {}
local function expect(condition, fact)
	if not condition then table.insert(failures, fact) end
end

local People, Person = Model:extend("club.people")
local Pets = Model:extend("club.pets")
function Person:label() return self.name .. " (" .. self.age .. ")" end
function People:adults() return self:select(function(p) return p.age >= 18 end) end

expect(#People:all() == 3, "all() returns every row")
local ada = People:find{name = "Ada"}
expect(ada and ada.age == 36, "find{field = value} returns the matching row")
expect(People:find(ada.id).name == "Ada", "find(key) returns the row with that key")
expect(People:find(9999) == nil, "find of a missing key is nil")
expect(ada:label() == "Ada (36)", "rows carry the methods of their row table")
expect(#People:adults() == 3, "class methods query through the model")
expect(People:count(function(p) return p.active end) == 1, "count takes a predicate")
expect(People:count{active = false} == 2, "count takes equal fields")
local sorted = People:select(nil, {order = "age desc", limit = 2})
expect(#sorted == 2 and sorted[1].name == "Cy" and sorted[2].name == "Ada", "select orders and limits")

local pet = Pets:create{owner_id = ada.id, name = "Rex"}
expect(pet and pet.id > 0, "create inserts and returns the row with its key")
expect(pet.owner and pet.owner.name == "Ada", "a join follows its relation to the record")
expect(pet:update{name = "Max"}.name == "Max", "update returns the refreshed row")
expect(Pets:find(pet.id).name == "Max", "update reaches the datasource")
expect(pet:text("owner.name") == "Ada", "text follows a join path as displayed")

local row, message = Pets:create{name = string.rep("x", 40)}
expect(row == nil and message and message:find("at most", 1, true), "a value too long is refused with a message")
local ok, err = pcall(function() return Pets:create{nickname = "x"} end)
expect(not ok and tostring(err):find("no field 'nickname'", 1, true), "an unknown field is a script error")
ok, err = pcall(function() return Pets:create{name = 5} end)
expect(not ok and tostring(err):find("takes a string", 1, true), "a value of the wrong type is a script error")

local before = Pets:count()
ok = pcall(function()
	Pets:transaction(function()
		Pets:create{owner_id = ada.id, name = "Tmp"}
		orion.fail("stop")
	end)
end)
expect(not ok and Pets:count() == before, "a refused transaction leaves nothing behind")
expect(Pets:transaction(function() return 42 end) == 42, "a transaction returns what its function returns")

local deleted, refusal = ada:delete()
expect(deleted == nil and refusal and refusal:find("in use", 1, true), "a record in use is not deleted")
expect(pet:delete() == true and Pets:find(pet.id) == nil, "delete removes the row")

-- The generated bindings check their arguments (orion.cgen, db.cgen).
local db = require("orion.db")
ok, err = pcall(function() return db.find("club", "people") end)
expect(not ok and tostring(err):find("bad argument #4 to 'find'", 1, true), "a missing argument names its position and function")
ok, err = pcall(function() return db.update("club", "people", "one", {}) end)
expect(not ok and tostring(err):find("number expected", 1, true), "an integer argument rejects a string")
ok, err = pcall(function() return db.insert("club", "people", "Ada") end)
expect(not ok and tostring(err):find("table expected", 1, true), "a table argument rejects a string")
expect(db.find("club", "people", nil, ada.id).name == "Ada", "an optional argument takes nil")
expect(#db.fetch("club", "people") == 3 and #db.fetch("club", "people", "name", "Bob") == 1, "trailing optional arguments may be left out")
expect(orion.formatNumber(2, 0) == "2" and orion.formatNumber(0.5):find("50", 1, true), "a default fills a missing argument")
expect(orion.today():match("^%d%d%d%d%-%d%d%-%d%d$"), "an output buffer comes back as the result")
expect(orion.tr("Untranslated sentence") == "Untranslated sentence", "call= binds an existing C function")
expect(select("#", orion.assert(1, "m", 3)) == 3, "nresults leaves the results to the C function")

-- The sandbox: no files, processes, native code or bytecode.
expect(io == nil and load == nil and loadfile == nil and dofile == nil, "no io and no loading of code")
expect(os.execute == nil and os.getenv == nil and os.remove == nil, "os keeps only the clock and dates")
expect(type(os.time()) == "number" and string.dump == nil, "the clock remains; bytecode does not")
expect(package.loadlib == nil, "no native modules")
expect(not pcall(require, "..secret"), "require refuses paths that leave its roots")
expect(not pcall(require, "nowhere.at.all"), "require does not search beyond the add-on and Orion")

if #failures > 0 then error(table.concat(failures, "\n"), 0) end

-- orion.model: the records of a datasource as Lua rows, after lua-objc's
-- Model. A model is a table of class methods (queries) and a row table of
-- record methods; rows are plain tables of field values.
--
--	local Model = require("orion.model")
--	local Counterparties, Counterparty = Model:extend("ib.counterparties", {
--		constraints = {name = function(_, name) if name == "" then return "A counterparty needs a name." end end},
--	})
--	function Counterparties:named(text) return self:select(function(row) return row.name:find(text, 1, true) end) end
--	function Counterparty:label() return self.inn .. " " .. self.name end
--
--	local acme = Counterparties:find{inn = "7701234567"}
--	acme.name                                        -- a field
--	acme.account                                     -- a joined row: account_id -> its record
--	orion.assert(acme:update{phone = "+7 495 000-00-00"})
--
-- Writes go through the datasource, which keeps its own rules (defaults,
-- numbering, delete rules). An expected refusal comes back as nil and a
-- message; a mistake in the script (an unknown field, a wrong type) raises.

local db = require("orion.db")

local Model = {}
Model.__index = Model

local RowBase = {}
local models = {}

local function split(source)
	local database, tableName = tostring(source):match("^([%w_]+)%.([%w_]+)$")
	if not database then error("Model:extend needs \"database.table\", got \"" .. tostring(source) .. "\"", 3) end
	return database, tableName
end

-- The model of a source, built once and shared by joins.
local function modelFor(source)
	local model = models[source]
	if not model then model = Model:extend(source) end
	return model
end

-- Model:extend(source, spec) -> Class, Row
-- spec.constraints = {field = function(fields, value) return message-or-nil end};
-- other spec fields become constants of the class (Model.enum-style lists).
function Model:extend(source, spec)
	local database, tableName = split(source)
	local info = db.schema(database, tableName)
	local Class = setmetatable({
		source = source, database = database, table = tableName, key = info.key,
		fields = info.fields, joins = {}, constraints = {},
	}, Model)
	for _, join in ipairs(info.joins) do Class.joins[join.name] = join end
	for name, value in pairs(spec or {}) do
		if name == "constraints" then Class.constraints = value else Class[name] = value end
	end
	local Row = setmetatable({}, {__index = RowBase})
	Row.__index = function(row, name)
		local method = Row[name]
		if method ~= nil then return method end
		local join = Class.joins[name]
		if join then return Class:related(row, join) end
		return nil
	end
	Row.__model = Class
	Class.Row = Row
	models[source] = models[source] or Class
	return Class, Row
end

function Model:wrap(fields)
	if fields == nil then return nil end
	return setmetatable(fields, self.Row)
end

function Model:wrapAll(list)
	for i, fields in ipairs(list) do list[i] = self:wrap(fields) end
	return list
end

function Model:all()
	return self:wrapAll(db.fetch(self.database, self.table))
end

-- find(key) or find{field = value}: the first matching row, or nil.
function Model:find(where)
	if type(where) ~= "table" then return self:wrap(db.find(self.database, self.table, nil, where)) end
	local field, value = next(where)
	if field ~= nil and next(where, field) == nil then return self:wrap(db.find(self.database, self.table, field, value)) end
	return self:select(where, {limit = 1})[1]
end

local function matches(row, where)
	if type(where) == "function" then return where(row) end
	for field, value in pairs(where) do
		if row[field] ~= value then return false end
	end
	return true
end

local function sorter(order)
	local keys = {}
	for part in order:gmatch("[^,]+") do
		local field, direction = part:match("^%s*([%w_]+)%s*(%a*)%s*$")
		if not field then error("order \"" .. order .. "\" is not \"field [asc|desc], ...\"", 4) end
		table.insert(keys, {field = field, desc = direction:lower() == "desc"})
	end
	return function(a, b)
		for _, key in ipairs(keys) do
			local x, y = a[key.field], b[key.field]
			if x ~= y then
				if key.desc then return x > y end
				return x < y
			end
		end
		return false
	end
end

-- select(where, {order = "date desc, number", limit = 10}): where is nil, a
-- table of equal fields, or a function of a row.
function Model:select(where, options)
	local rows
	local field, value
	if type(where) == "table" then field, value = next(where) end
	if field ~= nil then
		rows = db.fetch(self.database, self.table, field, value)
	else
		rows = db.fetch(self.database, self.table)
	end
	local found = {}
	for _, fields in ipairs(rows) do
		local row = self:wrap(fields)
		if where == nil or matches(row, where) then table.insert(found, row) end
	end
	options = options or {}
	if options.order then table.sort(found, sorter(options.order)) end
	if options.limit and #found > options.limit then
		for i = #found, options.limit + 1, -1 do found[i] = nil end
	end
	return found
end

function Model:count(where)
	return #self:select(where)
end

local function check(model, fields)
	for name, value in pairs(fields) do
		local constraint = model.constraints[name]
		local message = constraint and constraint(fields, value)
		if message then return nil, message end
	end
	return true
end

-- create(fields) -> row, or nil and a message
function Model:create(fields)
	local ok, message = check(self, fields)
	if not ok then return nil, message end
	local row, refusal = db.insert(self.database, self.table, fields)
	if not row then return nil, refusal end
	return self:wrap(row)
end

-- related(row, join): the record a relation field points at, or nil.
function Model:related(row, join)
	local id = rawget(row, join.field)
	if id == nil or id == 0 or join.table:find("{", 1, true) then return nil end
	local source = join.table:find(".", 1, true) and join.table or (self.database .. "." .. join.table)
	return modelFor(source):find(id)
end

-- transaction(fn): fn's changes all stay, or all go when it fails or refuses.
function Model:transaction(fn)
	db.begin(self.database)
	local results = table.pack(pcall(fn))
	if results[1] then
		db.commit(self.database)
		return table.unpack(results, 2, results.n)
	end
	db.revert(self.database)
	error(results[2], 0)
end

function RowBase:model()
	return getmetatable(self).__model
end

-- update(fields) -> the row, refreshed; or nil and a message
function RowBase:update(fields)
	local model = self:model()
	local ok, message = check(model, fields)
	if not ok then return nil, message end
	local row, refusal = db.update(model.database, model.table, rawget(self, model.key), fields)
	if not row then return nil, refusal end
	for name, value in pairs(row) do rawset(self, name, value) end
	return self
end

-- delete() -> true, or nil and a message (a record in use stays)
function RowBase:delete()
	local model = self:model()
	return db.delete(model.database, model.table, rawget(self, model.key))
end

-- text(path): a field or join path as the application displays it.
function RowBase:text(path)
	local model = self:model()
	return db.text(model.database, model.table, rawget(self, model.key), path)
end

return Model

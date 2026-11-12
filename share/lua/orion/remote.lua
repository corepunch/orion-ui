-- Shared revision and retry semantics for trusted, host-configured business RPC.
local native = require("orion.remote_native")
local json = require("orion.json")
local orion = require("orion")
local db = require("orion.db")
local Model = require("orion.model")
local Remote = {}
local Client = {}
Client.__index = Client

local function reply(body)
  local ok, value = pcall(json.decode, body or "")
  if ok and type(value) == "table" then return value end
end
local function send(client, method, body, callback)
  local id, err = native.request(client.handle, method, body, callback)
  if not id then return nil, err end
  return true
end
function Remote.new(handle, expected_application)
  assert(type(handle) == "number", "remote client needs a page handle")
  assert(type(expected_application) == "string" and expected_application ~= "", "expected application required")
  return setmetatable({handle = handle, application = expected_application, revision = nil, pending = nil, conflict = false}, Client)
end
function Remote.new_key() return native.newId() end
function Client:state()
  return {pending = self.pending ~= nil, pending_command = self.pending and self.pending.command,
    busy = self.busy == true, conflict = self.conflict, revision = self.revision}
end
function Client:refresh(callback)
  if self.pending and not self.conflict then return nil, orion.tr("Retry the pending request before refreshing.") end
  if self.busy then return nil, orion.tr("A request is already in progress.") end
  if self.pending and self.conflict then
    local cleared, clear_error = native.outboxClear(self.pending.body)
    if not cleared then return nil, clear_error end
    self.pending = nil
  end
  self.busy = true
  local ok, err = send(self, "GET", "", function(status, body, failure)
    self.busy = false
    local data = status == 200 and reply(body)
    if data and data.application ~= self.application then
      callback(nil, orion.tr("This server hosts a different application. Close the app and check the server configuration.")); return
    end
    if data and data.protocol == 1 and math.type(data.revision) == "integer" and data.revision >= 0 and data.application == self.application and type(data.role) == "string" then
      local saved, load_error = native.outboxLoad()
      if load_error then callback(nil, load_error); return end
      self.revision, self.conflict = data.revision, false
      if saved then
        local request = reply(saved)
        if not request or type(request.request_id) ~= "string" or type(request.command) ~= "string" then callback(nil, orion.tr("The saved request is invalid.")); return end
        self.pending = {id = request.request_id, command = request.command, body = saved}
      else self.pending = nil end
      callback(data, nil)
    else callback(nil, failure or orion.tr("Could not refresh the server status.")) end
  end)
  if not ok then self.busy = false; return nil, err end
  return true
end
function Client:_deliver(status, body, failure, callback)
  self.busy = false
  local response = reply(body)
  if status == 409 then
    self.conflict = true
    callback(nil, response and response.error or orion.tr("Data changed; refresh before continuing."))
  elseif status == 200 and response and type(response.ok) == "boolean" then
    if response.ok and type(response.revision) == "number" and response.request_id == self.pending.id then
      local cleared, clear_error = native.outboxClear(self.pending.body)
      if not cleared then callback(nil, clear_error); return end
      self.revision, self.pending = response.revision, nil
      callback(response.result, nil)
    elseif not response.ok then
      local cleared, clear_error = native.outboxClear(self.pending.body)
      if not cleared then callback(nil, clear_error); return end
      self.pending = nil
      callback(nil, response.error or orion.tr("The server refused the request."))
    else callback(nil, orion.tr("The request outcome is uncertain; retry the same request.")) end
  elseif status >= 400 and status < 500 and status ~= 408 and status ~= 425 and status ~= 429 then
    local cleared, clear_error = native.outboxClear(self.pending.body)
    if not cleared then callback(nil, clear_error); return end
    self.pending = nil
    callback(nil, response and response.error or orion.tr("The server refused the request."))
  else
    callback(nil, failure or orion.tr("The request outcome is uncertain; retry the same request."))
  end
end
function Client:_send_pending(callback)
  self.busy = true
  local ok, err = send(self, "POST", self.pending.body, function(status, body, failure)
    self:_deliver(status, body, failure, callback)
  end)
  if not ok then self.busy = false; return nil, err end
  return true
end
function Client:submit(command, args, callback)
  if self.busy or self.pending then return nil, orion.tr("Retry the pending request before starting another.") end
  if self.conflict or self.revision == nil then return nil, orion.tr("Refresh the server before starting a request.") end
  if type(command) ~= "string" or type(args) ~= "table" or type(callback) ~= "function" then return nil, orion.tr("Invalid remote request.") end
  local request = {request_id = native.newId(), expected_revision = self.revision, command = command, args = args}
  self.pending = {id = request.request_id, command = command, body = json.encode(request)}
  local saved, save_error = native.outboxSave(self.pending.body)
  if not saved then self.pending = nil; return nil, save_error end
  return self:_send_pending(callback)
end
function Client:retry(callback)
  if self.busy or not self.pending then return nil, orion.tr("There is no request to retry.") end
  return self:_send_pending(callback)
end

-- Replace only schema-declared transient projection tables in one database scope.
function Remote.replace_many(projections)
  assert(type(projections) == "table" and #projections > 0, "projections required")
  local prepared, database, sources = {}, nil, {}
  for _, projection in ipairs(projections) do
    local name, table_name = tostring(projection.source):match("^([%w_]+)%.([%w_]+)$")
    assert(name and (not database or name == database), "projections must share one database")
    database = name
    assert(not sources[projection.source], "duplicate projection source")
    sources[projection.source] = true
    local schema = db.schema(name, table_name)
    assert(schema.transient and type(projection.rows) == "table" and type(projection.fields) == "table", "transient projection required")
    local keys = {}
    for _, row in ipairs(projection.rows) do
      assert(type(row) == "table" and type(row[schema.key]) == "number" and row[schema.key] > 0 and not keys[row[schema.key]], "invalid projection key")
      keys[row[schema.key]] = true
    end
    prepared[#prepared + 1] = {model = Model:extend(projection.source), rows = projection.rows, fields = projection.fields}
  end
  orion.assert(db.begin(database))
  local ok, result = pcall(function()
    for _, projection in ipairs(prepared) do
      for _, row in ipairs(projection.model:all()) do orion.assert(row:delete()) end
      for _, source in ipairs(projection.rows) do
        local fields = {}
        for _, name in ipairs(projection.fields) do fields[name] = source[name] end
        orion.assert(projection.model:create(fields))
      end
    end
    return true
  end)
  if not ok then db.revert(database); error(result, 0) end
  if not db.commit(database) then db.revert(database); error("projection commit failed", 0) end
  return result
end
return Remote

-- Assembly of trusted, shipped capabilities. No request selects or loads code.
local API = 1
local roles = {admin=true, accountant=true, auditor=true}

local function name(value)
  return type(value) == "string" and value:match("^[a-z][a-z0-9_]*%.[a-z][a-z0-9_]*$")
end

local function sequence(value, label)
  assert(type(value) == "table" and not getmetatable(value), label .. " must be a list")
  local count = 0
  for k in pairs(value) do
    assert(math.type(k) == "integer" and k >= 1 and k <= #value, label .. " must be a list")
    count = count + 1
  end
  assert(count == #value, label .. " must be a dense list")
  return value
end

local function assemble(profile, manifests)
  assert(type(profile) == "table" and name(profile.id) and profile.api == API, "unsupported business profile")
  local modules, owners = {}, {}
  for _, m in ipairs(sequence(manifests, "business manifests")) do
    assert(type(m) == "table" and name(m.id) and m.api == API, "unsupported business module")
    assert(not modules[m.id], "duplicate business module: " .. m.id)
    modules[m.id] = m
  end

  local state, commands, installed, order = {}, {}, {}, {}
  local function enable(id)
    assert(name(id), "invalid dependency id")
    if state[id] == "ready" then return end
    assert(state[id] ~= "visiting", "cyclic business module dependency: " .. id)
    local m = assert(modules[id], "missing business module: " .. id)
    state[id] = "visiting"
    for _, dependency in ipairs(sequence(m.dependencies, "module dependencies")) do enable(dependency) end
    local owned = {}
    for _, resource in ipairs(sequence(m.owns, "owned resources")) do
      assert(name(resource), "invalid resource id")
      assert(not owners[resource], "duplicate resource owner: " .. resource)
      owners[resource] = id
      owned[#owned + 1] = resource
    end
    local prefix = id:match("%.([^.]+)$") .. "."
    for _, c in ipairs(sequence(m.commands, "module commands")) do
      assert(type(c) == "table" and name(c.id) and c.id:sub(1, #prefix) == prefix and
        type(c.run) == "function" and (c.mode == "read" or c.mode == "write"), "invalid business command")
      assert(not commands[c.id], "duplicate business command: " .. c.id)
      local allowed = {}
      for _, role in ipairs(sequence(c.roles, "command roles")) do
        assert(roles[role] and not allowed[role], "invalid or duplicate business role")
        assert(c.mode == "read" or role ~= "auditor", "auditors cannot write")
        allowed[role] = true
      end
      assert(next(allowed), "business command needs a role")
      commands[c.id] = {module=id, allowed=allowed, mode=c.mode, run=c.run}
      order[#order + 1] = c.id
    end
    installed[#installed + 1] = {id=id, resources=owned}
    state[id] = "ready"
  end
  for _, id in ipairs(sequence(profile.modules, "profile modules")) do enable(id) end
  table.sort(order)

  local function actor(role)
    assert(type(role) == "string" and roles[role], "invalid role")
  end
  local profile_id = profile.id
  return {
    describe = function(role)
      actor(role)
      local result = {api=API, profile=profile_id, modules={}, commands={}}
      for _, m in ipairs(installed) do
        result.modules[#result.modules + 1] = {id=m.id, resources={table.unpack(m.resources)}}
      end
      for _, id in ipairs(order) do
        local c = commands[id]
        if c.allowed[role] then
          result.commands[#result.commands + 1] = {id=id, module=c.module, mode=c.mode}
        end
      end
      return result
    end,
    invoke = function(role, id, args)
      actor(role)
      local c = type(id) == "string" and commands[id]
      assert(c, "unsupported command")
      assert(c.allowed[role], "role cannot invoke this business command")
      assert(type(args) == "table" and not getmetatable(args), "args must be an object")
      return c.run(args)
    end,
  }
end

return {assemble=assemble}

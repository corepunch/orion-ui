-- State-local plugins. Validate the complete dependency graph before factories run.
local Plugins = {api = 1}
local function valid_id(id)
  assert(type(id) == "string" and id:match("^[a-z][a-z0-9_]*$"), "invalid plugin id")
end
local function valid_version(version)
  return math.type(version) == "integer" and version > 0
end
local function names(map)
  local result = {}
  for id in pairs(map) do valid_id(id); result[#result + 1] = id end
  table.sort(result)
  return result
end

function Plugins.new(resolve)
  resolve = resolve or function(id, part) return require("orion.plugins." .. id .. "." .. part) end
  local loaded, initializing = {}, false
  local manager = {}
  local function plan(requirements, optional)
    assert(type(requirements) == "table", "plugin requirements must be a table")
    local selected, visiting, ordered = {}, {}, {}
    local function visit(id, version)
      valid_id(id)
      assert(version == false or valid_version(version), "invalid plugin interface version: " .. id)
      assert(not visiting[id], "plugin dependency cycle: " .. id)
      if selected[id] then
        assert(version == false or selected[id].version == version, "incompatible plugin interface: " .. id)
        return
      end
      local manifest = loaded[id] and loaded[id].manifest or resolve(id, "manifest")
      assert(type(manifest) == "table" and manifest.id == id, "invalid plugin manifest: " .. id)
      assert(manifest.api == Plugins.api, "unsupported Orion plugin API: " .. id)
      assert(valid_version(manifest.version), "invalid plugin version: " .. id)
      assert(version == false or version == manifest.version, "incompatible plugin interface: " .. id)
      assert(type(manifest.dependencies) == "table", "missing plugin dependencies: " .. id)
      local copy = {id = id, api = manifest.api, version = manifest.version, dependencies = {}}
      visiting[id] = true
      for _, dependency in ipairs(names(manifest.dependencies)) do
        local required = manifest.dependencies[dependency]
        assert(valid_version(required), "invalid dependency version: " .. dependency)
        copy.dependencies[dependency] = required
        visit(dependency, required)
      end
      visiting[id], selected[id] = nil, copy
      ordered[#ordered + 1] = copy
    end
    for _, id in ipairs(names(requirements)) do
      assert((optional and requirements[id] == false) or valid_version(requirements[id]), "invalid plugin interface version: " .. id)
      visit(id, requirements[id])
    end
    return ordered
  end
  -- Inspection loads trusted manifests, never factories. Returned tables are copies.
  function manager:inspect(requirements) return plan(requirements) end
  local function initialize(requirements, optional)
    assert(not initializing, "plugin factories must use injected dependencies")
    local ordered, factories, exports = plan(requirements, optional), {}, {}
    for _, manifest in ipairs(ordered) do
      if not loaded[manifest.id] then
        local factory = resolve(manifest.id, "main")
        assert(type(factory) == "function", "plugin must return a factory: " .. manifest.id)
        factories[manifest.id] = factory
      end
    end
    initializing = true
    local ok, message = pcall(function()
      for _, manifest in ipairs(ordered) do
        local id = manifest.id
        if not loaded[id] then
          local dependencies = {}
          for dependency in pairs(manifest.dependencies) do dependencies[dependency] = loaded[dependency].exports end
          local result = factories[id](dependencies)
          assert(type(result) == "table", "plugin must export a table: " .. id)
          loaded[id] = {manifest = manifest, exports = result}
        end
      end
      for id in pairs(requirements) do exports[id] = loaded[id].exports end
    end)
    initializing = false
    if not ok then error(message, 0) end
    return exports
  end
  function manager:load(requirements) return initialize(requirements, false) end
  function manager:use(id, version)
    valid_id(id)
    assert(version == nil or valid_version(version), "invalid plugin interface version")
    return initialize({[id] = version or false}, true)[id]
  end
  return manager
end

local default = Plugins.new()
function Plugins.use(id, version) return default:use(id, version) end
function Plugins.load(requirements) return default:load(requirements) end
function Plugins.inspect(requirements) return default:inspect(requirements) end
return Plugins

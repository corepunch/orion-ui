-- Portable, state-local plugins. Manifests are data; factories receive only
-- declared dependencies. Hosts compose independent plugins through callbacks.
local Plugins = {api = 1}

function Plugins.new(resolve)
  resolve = resolve or function(id, part) return require("orion.plugins." .. id .. "." .. part) end
  local loaded, active = {}, {}
  local manager = {}
  function manager:use(id, version)
    assert(type(id) == "string" and id:match("^[a-z][a-z0-9_]*$"), "invalid plugin id")
    assert(version == nil or (math.type(version) == "integer" and version > 0), "invalid plugin interface version")
    if loaded[id] then
      assert(not version or loaded[id].version == version, "incompatible plugin interface: " .. id)
      return loaded[id].exports
    end
    assert(not active[id], "plugin dependency cycle: " .. id)
    active[id] = true
    local ok, result = pcall(function()
      local manifest = resolve(id, "manifest")
      assert(type(manifest) == "table" and manifest.id == id, "invalid plugin manifest: " .. id)
      assert(manifest.api == Plugins.api, "unsupported Orion plugin API: " .. id)
      assert(math.type(manifest.version) == "integer" and manifest.version > 0, "invalid plugin version: " .. id)
      assert(not version or version == manifest.version, "incompatible plugin interface: " .. id)
      assert(type(manifest.dependencies) == "table", "missing plugin dependencies: " .. id)
      local dependencies, names = {}, {}
      for name in pairs(manifest.dependencies) do
        assert(type(name) == "string" and name:match("^[a-z][a-z0-9_]*$"), "invalid dependency id: " .. id)
        names[#names + 1] = name
      end
      table.sort(names)
      for _, name in ipairs(names) do
        local required = manifest.dependencies[name]
        assert(math.type(required) == "integer" and required > 0, "invalid dependency version: " .. name)
        dependencies[name] = self:use(name, required)
      end
      local factory = resolve(id, "main")
      assert(type(factory) == "function", "plugin must return a factory: " .. id)
      local exports = factory(dependencies)
      assert(type(exports) == "table", "plugin must export a table: " .. id)
      loaded[id] = {version = manifest.version, exports = exports}
      return exports
    end)
    active[id] = nil
    if not ok then error(result, 0) end
    return result
  end
  return manager
end

local default = Plugins.new()
function Plugins.use(id, version) return default:use(id, version) end
return Plugins

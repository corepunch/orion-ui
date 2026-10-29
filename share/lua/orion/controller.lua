-- Each package owns local modules; core/host modules belong to the app VM.
-- Native supplies a text-only reader restricted to the controller directory.
local Controller = {}
function Controller.scope(read, shared, globals)
  local loaded, active = {}, {}
  local environment = setmetatable({}, {__index = globals})
  environment.package = {loaded = loaded}
  local function import(name)
    assert(type(name) == "string" and name:match("^[%a_][%w_.]*$") and not name:find("..", 1, true), "invalid module name")
    if name == "orion" or name:sub(1, 6) == "orion." then return shared(name) end
    if loaded[name] ~= nil then return loaded[name] end
    local chunk = read(name)
    if not chunk then return shared(name) end
    assert(not active[name], "controller module cycle: " .. name)
    active[name] = true
    local ok, value = pcall(chunk, environment)
    active[name] = nil
    if not ok then error(value, 0) end
    loaded[name] = value == nil and true or value
    return loaded[name]
  end
  environment.require = import
  return environment
end
return Controller

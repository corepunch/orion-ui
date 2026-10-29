return function()
  local orion = require("orion")
  local Model = require("orion.model")
  local Records = {}
  function Records.bind(source, validate)
    local model = Model:extend(source)
    local records = {model = model}
    function records:save(id, fields)
      local row = id and model:find(id)
      if id and not row then return nil, orion.tr("Record no longer exists.") end
      local merged = {}
      for k, v in pairs(row or {}) do merged[k] = v end
      for k, v in pairs(fields) do merged[k] = v end
      if validate then
        local ok, message = validate(merged, row)
        if not ok then return nil, message end
      end
      if row then return row:update(fields) end
      return model:create(fields)
    end
    function records:delete(id)
      local row = model:find(id)
      if not row then return nil, orion.tr("Record no longer exists.") end
      return row:delete()
    end
    return records
  end
  return Records
end

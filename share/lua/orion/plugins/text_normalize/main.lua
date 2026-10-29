return function()
  local Normalize = {}
  function Normalize.new(mappings)
    mappings = mappings or {}
    return function(text)
      local out = {}
      for _, code in utf8.codes(text) do
        if code >= 65 and code <= 90 then code = code + 32 end
        if code == 0xa0 or code == 0x202f then code = 32 end
        local character = utf8.char(code)
        out[#out + 1] = mappings[character] or character
      end
      return table.concat(out)
    end
  end
  return Normalize
end

-- Shared by vocabulary compilation, command tokenization and record matching.
local mappings = require('vocabulary').normalization
return function(text)
  local out = {}
  for _, c in utf8.codes(text) do
    if c >= 65 and c <= 90 then c = c + 32 end
    if c == 0xa0 or c == 0x202f then c = 32 end
    local character = utf8.char(c)
    out[#out+1] = mappings[character] or character
  end
  return table.concat(out)
end


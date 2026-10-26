-- Shared by vocabulary compilation, command tokenization and record matching.
return function(text)
  local out = {}
  for _, c in utf8.codes(text) do
    if c >= 65 and c <= 90 or c >= 0x410 and c <= 0x42f then c = c + 32 end
    if c == 0x401 or c == 0x451 then c = 0x435 end -- ё -> е
    if c == 0xc4 or c == 0xd6 or c == 0xdc then c = c + 32 end
    if c == 0x1e9e then c = 0xdf end -- capital sharp S
    if c == 0xa0 or c == 0x202f then c = 32 end
    out[#out+1] = utf8.char(c)
  end
  return table.concat(out)
end


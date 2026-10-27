-- Productive German cardinal compounds; all spellings come from the language XML.
local G = {}
local language = require('vocabulary').languages.de
local config = language and language.compound_numbers
local ones,small,tens = config and config.ones,config and config.small,config and config.tens
local excluded = {}
for _,word in ipairs(config and config.excluded_prefixes or {}) do excluded[word]=true end
local function split(word,particle)
  local first,last=word:find(particle,1,true)
  if first then return word:sub(1,first-1),word:sub(last+1) end
end
local function under_hundred(w)
  if ones[w] then return ones[w] end
  if small[w] then return small[w] end
  if tens[w] then return tens[w] end
  local a,b=split(w,config.conjunction)
  if a and ones[a] and not excluded[a] and tens[b] then return ones[a]+tens[b] end
end
local function under_thousand(w)
  local a,b=split(w,config.multiplier)
  if not a then return under_hundred(w) end
  local n=a=='' and 1 or ones[a]
  local rest=b=='' and 0 or under_hundred(b)
  if n and not excluded[a] and rest then return n*config.multiplier_value+rest end
end
function G.number(word)
  if not config then return end
  local w=word
  for _,rule in ipairs(config.replacements) do
    local pattern=rule.source:gsub('(%W)','%%%1')
    w=w:gsub(pattern,function() return rule.target end)
  end
  local a,b=split(w,config.scale)
  if not a then return under_thousand(w) end
  local n=a=='' and 1 or under_thousand(a)
  local rest=b=='' and 0 or under_thousand(b)
  if n and n>0 and rest then return n*config.scale_value+rest end
end
return G

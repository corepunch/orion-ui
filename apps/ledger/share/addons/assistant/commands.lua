-- The command catalog: Ledger's menu tree (ledger.commands()), searched by
-- the words of a request, and the commands that belong next to a result.
local normalize = require('normalize')
local C = {}; C.__index = C

-- Commands that make no sense from the assistant itself.
local HIDDEN = {['file.quit']=true, ['help.assistant']=true, ['help.find_command']=true}
-- What to offer next to a list of records of each kind.
local RELATED = {
  counterparties={'view.counterparties', 'catalog.new_counterparty'},
  documents={'view.journal', 'doc.new'},
  items={'view.items', 'catalog.new_item'},
}

local function words(text)
  local out = {}
  for word in normalize(text):gsub('…', ' '):gmatch('[^%s%p]+') do out[#out+1] = word end
  return out
end

-- list: what ledger.commands() returns ({name, label, group, shortcut}).
function C.new(list)
  local self = setmetatable({items={}, by_name={}}, C)
  for _, c in ipairs(list or {}) do
    if not HIDDEN[c.name] then
      local item = {name=c.name, label=(c.label:gsub('%.%.%.$', ''):gsub('…$', '')), shortcut=c.shortcut, words=words(c.label)}
      self.items[#self.items+1] = item
      self.by_name[c.name] = item
    end
  end
  return self
end

-- Every word of the request starts a word of the caption ("нов док" finds
-- "Новый документ"); a request of no words finds nothing.
function C:search(text, limit)
  local wanted, found = words(text or ''), {}
  if #wanted == 0 then return found end
  for _, item in ipairs(self.items) do
    local all = true
    for _, w in ipairs(wanted) do
      local hit = false
      for _, label_word in ipairs(item.words) do
        if label_word:sub(1, #w) == w then hit = true; break end
      end
      if not hit then all = false; break end
    end
    if all then found[#found+1] = item; if #found >= (limit or 5) then break end end
  end
  return found
end

function C:related(entity)
  local out = {}
  for _, name in ipairs(RELATED[entity] or {}) do
    if self.by_name[name] then out[#out+1] = self.by_name[name] end
  end
  return out
end

return C

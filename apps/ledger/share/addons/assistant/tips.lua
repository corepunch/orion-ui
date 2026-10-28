-- Suggestions from the books, the way Office's assistant offered tips: what
-- is left undone and what one step would settle. Each tip is a row of the
-- balloon with the command that acts on it.
local tr = require('messages')
local T = {}

-- documents: {id, number, posted, counterparty_id, amount, sale}
-- counterparties: {id, name, debt} (debt < 0 is an advance received)
function T.find(documents, counterparties)
  local tips, drafts = {}, {}
  for _, d in ipairs(documents) do if not d.posted then drafts[#drafts+1] = d end end
  if #drafts > 0 then
    tips[#tips+1] = {name=string.format(tr('Documents not posted: %d'), #drafts), action='submit', arg=tr('show draft documents')}
  end
  local advance = {}
  for _, c in ipairs(counterparties) do if (c.debt or 0) < 0 then advance[c.id] = c end end
  for _, d in ipairs(drafts) do
    local c = d.sale and advance[d.counterparty_id]
    if c then
      tips[#tips+1] = {name=string.format(tr('%s offsets the advance from %s'), d.number, c.name),
                       action='open', arg='documents', record_id=d.id}
    end
  end
  return tips
end

return T

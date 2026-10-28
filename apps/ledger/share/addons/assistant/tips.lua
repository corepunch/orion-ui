-- Suggestions from the books, the way Office's assistant offered tips: what
-- is left undone and what one step would settle. They are the books' own
-- checks (ib.home_rows, card 1, built by home.c), the list the Home page
-- shows; choosing one settles it as the page's click does.
local T = {}

-- checks: rows {id, kind, title, detail, action}; section: the group caption.
function T.from_checks(checks, section)
  local tips = {}
  for _, c in ipairs(checks) do
    tips[#tips+1] = {section=section, kind='tip', name=c.title, action='settle', record_id=c.id}
  end
  return tips
end

return T

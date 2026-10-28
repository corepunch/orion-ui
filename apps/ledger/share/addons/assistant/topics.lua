-- Questions about the state of the books, the way Office's Answer Wizard
-- took a question in plain words: a topic word anywhere in the request
-- ("почему красное сальдо по 60", "кому мы должны", "акт сверки с Альфой")
-- picks the answer, where the command grammar wants every word to be known.
-- Topic words are the vocabulary's <topic> entries, in every language.
local normalize = require('normalize')
local vocabulary = require('vocabulary').lexicon
local T = {}

local function words(text)
  local out = {}
  for word in normalize(text or ''):gmatch('[^%s%p]+') do out[#out+1] = word end
  return out
end

-- Topic phrases as word lists, longest first, so "кому мы должны" wins over "должны".
local phrases = {}
for topic, aliases in pairs(vocabulary.topics or {}) do
  for _, alias in ipairs(aliases) do phrases[#phrases+1] = {topic=topic, words=words(alias)} end
end
table.sort(phrases, function(a, b)
  if #a.words ~= #b.words then return #a.words > #b.words end
  return a.topic < b.topic
end)

local function matches_at(tokens, i, phrase)
  for k, w in ipairs(phrase) do if tokens[i+k-1] ~= w then return false end end
  return true
end

-- The topic of a request and the words that are not part of it.
function T.match(text)
  local tokens, used, topic = words(text), {}, nil
  for i = 1, #tokens do
    for _, p in ipairs(phrases) do
      if not used[i] and #p.words > 0 and matches_at(tokens, i, p.words) then
        topic = topic or p.topic
        for k = 0, #p.words - 1 do used[i+k] = true end
        break
      end
    end
  end
  if not topic then return nil end
  local rest = {}
  for i, w in ipairs(tokens) do if not used[i] then rest[#rest+1] = w end end
  return topic, rest
end

-- Counterparties a request names: a quoted name, or a word whose stem starts a
-- word of the name ("альфой" finds ООО "Торговый дом Альфа").
function T.parties(text, rest, counterparties)
  local quoted = (text or ''):match('"([^"]+)"')
  local found = {}
  for _, c in ipairs(counterparties) do
    local name = normalize(c.name or '')
    local hit = quoted and name:find(normalize(quoted), 1, true)
    if not quoted then
      for _, w in ipairs(rest) do
        local n = utf8.len(w) or 0
        if n >= 4 then   -- drop a case ending: "альфой" is "альф", "бетой" is "бет"
          local stem = w:sub(1, utf8.offset(w, math.max(3, n - 2) + 1) - 1)
          for part in name:gmatch('[^%s%p]+') do
            if part:sub(1, #stem) == stem then hit = true; break end
          end
        end
        if hit then break end
      end
    end
    if hit then found[#found+1] = c end
  end
  return found
end

return T

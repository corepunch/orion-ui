-- Questions about the state of the books, the way Office's Answer Wizard
-- took a question in plain words: a topic word anywhere in the request
-- ("почему красное сальдо по 60", "кому мы должны", "акт сверки с Альфой")
-- picks the answer, where the command grammar wants every word to be known.
-- Topic words are the vocabulary's <topic> entries, in every language.
local normalize = require('normalize')
local vocabulary = require('vocabulary').lexicon
local tr = require('messages')
local T = {}

local function words(text)
  local out = {}
  for word in normalize(text or ''):gmatch('[^%s%p]+') do out[#out+1] = word end
  return out
end
local function without_quotes(text)
  return (text or ''):gsub('"[^"]*"', ' ')
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
  local tokens, used, topic, topics = words(without_quotes(text)), {}, nil, {}
  for i = 1, #tokens do
    for _, p in ipairs(phrases) do
      if not used[i] and #p.words > 0 and matches_at(tokens, i, p.words) then
        topic = topic or p.topic
        topics[p.topic] = true
        for k = 0, #p.words - 1 do used[i+k] = true end
        break
      end
    end
  end
  if not topic then return nil end
  local rest = {}
  for i, w in ipairs(tokens) do if not used[i] then rest[#rest+1] = w end end
  local count = 0
  for _ in pairs(topics) do count = count + 1 end
  if count > 1 then return topic, rest, tr('Your question names more than one accounting topic. Ask about one topic at a time.') end
  if topic == 'period' then return topic, rest, tr('That question includes a filter or period this answer cannot apply. Remove it or use a supported list command.') end
  local scope_error = T.scope_error(topic, text, rest)
  return topic, rest, scope_error
end

local function phrase_present(tokens, aliases)
  for _, alias in ipairs(aliases or {}) do
    local phrase = words(alias)
    for i = 1, #tokens - #phrase + 1 do if matches_at(tokens, i, phrase) then return true end end
  end
  return false
end

-- Topics answer a fixed scope. Never let an account, amount, date, comparison
-- or quoted filter disappear silently into a broad answer. Reconciliation is
-- the sole topic that accepts a quoted entity name.
function T.scope_error(topic, text, rest)
  local raw = normalize(text or '')
  local unquoted = normalize(without_quotes(text or ''))
  local has_number = unquoted:match('%d') ~= nil
  for _, token in ipairs(rest or {}) do
    if vocabulary.numbers and vocabulary.numbers[token] ~= nil then has_number = true end
    if vocabulary.scales and vocabulary.scales[token] ~= nil then has_number = true end
  end
  local comparison_aliases = {}
  for _, aliases in pairs(vocabulary.comparisons or {}) do
    for _, alias in ipairs(aliases) do comparison_aliases[#comparison_aliases+1] = alias end
  end
  local has_comparison = phrase_present(rest or {}, comparison_aliases)
  local quotes, quoted = 0, {}
  for name in raw:gmatch('"([^"]*)"') do quotes=quotes+1; quoted[#quoted+1]=name end
  local unclosed = select(2,raw:gsub('"',''))%2~=0
  if unclosed or (#quoted>0 and (#quoted>1 or normalize(quoted[1])=='')) then
    return tr('Name one counterparty in a single pair of quotes.')
  end
  if has_number or has_comparison or quotes>0 then
    if topic ~= 'reconciliation' or has_number or has_comparison then
      return tr('That question includes a filter or period this answer cannot apply. Remove it or use a supported list command.')
    end
  end
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

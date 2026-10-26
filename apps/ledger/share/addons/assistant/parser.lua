local tr = require('messages')
-- Zork-inspired command grammar: verbs, noun phrases, qualifiers and orphans.
-- Written for Ledger; no Infocom source or VM is embedded.
local vocabulary = require('lexicon')
local german = require('german_numbers')
local P = {}

P.normalize = require('normalize')

local function tokenize(text)
  local words = {}
  -- Quoted names remain one token. Unknown words are never silently discarded.
  local normalized, i = P.normalize(text), 1
  while i <= #normalized do
    local c = normalized:sub(i,i)
    if c:match('%s') or c == '?' or c == '!' or c == ';' then i = i + 1
    elseif c == '"' then
      local last = normalized:find('"', i+1, true)
      if not last then return nil, tr('Close the quotes around the name.') end
      words[#words+1] = {text=normalized:sub(i+1,last-1), quoted=true}; i=last+1
    else
      local last = normalized:find('%s', i) or #normalized+1
      local word = normalized:sub(i,last-1):gsub('[,.!?;]+$','')
      words[#words+1] = {text=word}; i=last
    end
  end
  return words
end

local dictionary = require('dictionary').compile(vocabulary)
local grammar_roles={'verbs','nouns','adjectives','fields','comparisons','relations','directions',
  'ordinals','pronouns','noise','units'}
local function lookup(words,i)
  local entry,n=dictionary:lookup(words,i)
  if not entry then return end
  -- Keep all word roles in the dictionary; this grammar defines their precedence.
  for _,role in ipairs(grammar_roles) do
    if entry.roles[role]~=nil then return role,entry.roles[role],n end
  end
end
local function numeric_roles(word)
  local entry=dictionary.words[word]
  return entry and entry.roles or {}
end

function P.number(words, start)
  local total,group,i,seen,last = 0,0,start,false,nil
  while words[i] and not words[i].quoted do
    local w=words[i].text
    local roles=numeric_roles(w)
    -- With multilingual input, 50.000 / 50,000 is ambiguous. Use 50000 or 50 000.
    if w:match('^%d+[.,]%d%d%d$') then return nil end
    local numeric=w:match('^%d+[.,]?%d*$') and tonumber((w:gsub(',','.')))
    if numeric then
      if seen then break end
      group=numeric; seen=true; last=numeric
      -- Dictation may insert a space as the thousands separator: 50 000.
      while words[i+1] and words[i+1].text:match('^%d%d%d$') do
        group=group*1000+tonumber(words[i+1].text); i=i+1
      end
    elseif not roles.scales and not roles.multipliers and (roles.numbers~=nil or german.number(w)~=nil) then
      local n=roles.numbers or german.number(w)
      if last and n >= last then break end
      group=group+n; seen=true; last=n
    elseif roles.multipliers then
      group=(group==0 and 1 or group)*roles.multipliers; seen=true; last=roles.multipliers
    elseif roles.scales then
      total=total+(group==0 and 1 or group)*roles.scales; group=0; seen=true; last=nil
    elseif roles.number_conjunctions and seen and words[i+1] and numeric_roles(words[i+1].text).numbers~=nil then
      -- Numeric conjunction, only inside a number.
    else break end
    i=i+1
  end
  if seen then return total+group,i end
end

local function copy(t)
  if type(t)~='table' then return t end
  local out={}; for k,v in pairs(t) do out[k]=copy(v) end; return out
end
P.copy=copy

function P.parse(text,context)
  if type(text)~='string' or #text>2048 then return nil,tr('Enter a command of at most 2048 bytes.') end
  local ok,words,err=pcall(tokenize,text)
  if not ok then return nil,tr('Invalid UTF-8 text.') end
  if not words then return nil,err end
  if #words==0 then return nil,tr('What should I show? Try: show debtors.') end
  local ast, i, field = {filters={}}, 1, nil
  -- Keep all parsing local: an invalid suffix must not alter dialogue context.
  i=1
  while i<=#words do
    local w=words[i]
    if w.quoted then
      if ast.name then return nil,tr('Specify one quoted name.') end
      ast.name=w.text; i=i+1
    else
      local category,value,n=lookup(words,i)
      if category=='verbs' then
        if ast.verb then return nil,tr('Use one command at a time.') end
        ast.verb=value; i=i+n
      elseif category=='nouns' then
        local entity=value=='debtors' and 'counterparties' or value
        if ast.entity and ast.entity~=entity then return nil,tr('Specify one object type.') end
        ast.entity=entity
        if value=='debtors' then ast.debtors=true end
        i=i+n
      elseif category=='adjectives' then
        if value=='debtors' then ast.debtors=true
        else ast.filters[#ast.filters+1]={field='posted',op='eq',value=value=='posted'} end
        i=i+n
      elseif category=='fields' then
        field=value; ast.field=value; if value=='debt' and ast.verb~='sort' then ast.debtors=true end; i=i+n
      elseif category=='comparisons' then
        local amount,last=P.number(words,i+n)
        if not amount then return nil,tr('Which amount should I use?') end
        ast.filters[#ast.filters+1]={field=field,op=value,value=amount}; i=last
      elseif category=='relations' then
        local parts={}
        for j=i+n,#words do
          local token=words[j]
          parts[#parts+1]=token.quoted and ('"'..token.text..'"') or token.text
        end
        local ref,message=P.parse('open '..table.concat(parts,' '),{})
        if not ref then return nil,message end
        if ref.for_party or ref.entity and ref.entity~='counterparties' then
          return nil,tr('Document relations require a customer.')
        end
        ast.for_party=ref; i=#words+1
      elseif category=='directions' then ast.direction=value; i=i+n
      elseif category=='ordinals' then
        if ast.ordinal then return nil,tr('Specify one row number.') end
        ast.ordinal=value; i=i+n
      elseif category=='pronouns' then ast.pronoun=true; i=i+n
      elseif category=='noise' then i=i+n
      elseif category=='units' then
        -- Currency conversion is not implicit; explicit currency amounts need a future grammar.
        return nil,tr('Enter amounts without a currency, in the accounting currency.')
      else
        local number,last=P.number(words,i)
        if number and ast.verb=='open' then
          if ast.ordinal then return nil,tr('Specify one row number.') end
          ast.ordinal=number; i=last
        else return nil,string.format(tr('Unknown word: %s. Put names in quotes.'),w.text) end
      end
    end
  end
  if not ast.verb and not ast.entity and not ast.name and not ast.debtors and #ast.filters==0 and not ast.field and not ast.direction and not ast.ordinal and not ast.pronoun then
    return nil,tr('Enter a command, for example: show debtors.')
  end
  ast.verb=ast.verb or 'list'
  if ast.verb=='help' or ast.verb=='reset' or ast.verb=='cancel' then
    if #words~=1 then return nil,tr('This command takes no parameters.') end
    return ast
  end
  if ast.verb=='open' then
    if ast.name=='' then return nil,tr('Specify a nonempty name.') end
    if #ast.filters>0 or ast.field or ast.debtors or ast.direction then return nil,tr('Find objects first, then open one.') end
    if ast.ordinal and (ast.ordinal<1 or ast.ordinal%1~=0) then return nil,tr('Use a whole row number starting at 1.') end
    if (ast.pronoun and (ast.name or ast.ordinal)) or (ast.name and ast.ordinal) then
      return nil,tr('Specify one object reference.')
    end
    if ast.for_party and ast.entity~='documents' then return nil,tr('Only documents can be filtered by customer.') end
    return ast
  end
  if ast.ordinal or ast.pronoun then return nil,tr('Use a row number with the open command.') end
  local previous=context and context.query
  local refinement=not ast.entity and not ast.name and previous
  if refinement then
    ast.entity=previous.entity; ast.name=previous.name
    local added=ast.filters; ast.filters=copy(previous.filters)
    for _,f in ipairs(added) do ast.filters[#ast.filters+1]=f end
    ast.debtors=ast.debtors or previous.debtors
    ast.party_id=previous.party_id
    if ast.verb~='sort' then ast.sort=copy(previous.sort) end
  end
  if ast.debtors and not ast.entity then ast.entity='counterparties' end
  if not ast.entity then return nil,tr('What should I find: customers, documents or items?') end
  if ast.for_party and ast.entity~='documents' then return nil,tr('Only documents can be filtered by customer.') end
  if ast.name=='' then return nil,tr('Specify a nonempty name.') end
  if ast.debtors and ast.entity~='counterparties' then return nil,tr('Debt filters apply only to counterparties.') end
  for _, f in ipairs(ast.filters) do
    f.field=f.field or (ast.entity=='counterparties' and 'debt' or 'amount')
    if f.field=='posted' and ast.entity~='documents' or f.field=='debt' and ast.entity~='counterparties' or f.field=='amount' and ast.entity=='counterparties' or f.field=='name' then
      return nil,tr('This filter does not apply to these objects.')
    end
  end
  if ast.verb=='sort' then
    local key=ast.field or (ast.entity=='counterparties' and 'debt' or 'amount')
    if key=='debt' and ast.entity~='counterparties' or key=='amount' and ast.entity=='counterparties' then return nil,tr('This sort field does not apply to these objects.') end
    ast.sort={field=key,direction=ast.direction or 'desc'}
  elseif ast.direction then return nil,tr('Use the sort command to set a direction.') end
  return ast
end
function P.has_verb(text)
  if type(text)~='string' or #text>2048 then return false end
  local ok,words=pcall(tokenize,text)
  if not ok or not words then return false end
  for i=1,#words do if lookup(words,i)=='verbs' then return true end end
  return false
end

-- An orphan answer is a noun phrase, never another verb or a command fragment.
function P.reference(text)
  if type(text)~='string' or #text>2040 then return nil end
  local ok,words=pcall(tokenize,text)
  if not ok or not words or #words==0 then return nil end
  for i=1,#words do
    local category=lookup(words,i)
    if category=='verbs' or category=='relations' then return nil end
  end
  local ref=P.parse('open '..text,{})
  if ref and (ref.entity or ref.name or ref.ordinal or ref.pronoun) then return ref end
end
return P

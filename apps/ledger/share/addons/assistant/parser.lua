local tr = require('messages')
-- Zork-inspired command grammar: verbs, noun phrases, qualifiers and orphans.
-- Written for Ledger; no Infocom source or VM is embedded.
local vocabulary = require('lexicon')
local german = require('german_numbers')
local P = {}

function P.normalize(text)
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

local dictionary = {}
for category, groups in pairs(vocabulary) do
  if category ~= 'ordinals' and category ~= 'noise' and category ~= 'units' and category ~= 'pronouns' then
    for value, aliases in pairs(groups) do
      for _, alias in ipairs(aliases) do
        local tokens = {}; for token in P.normalize(alias):gmatch('%S+') do tokens[#tokens+1]=token end
        dictionary[#dictionary+1] = {category=category,value=value,tokens=tokens}
      end
    end
  end
end
table.sort(dictionary,function(a,b) return #a.tokens > #b.tokens end)
local function lookup(words,i)
  for _, entry in ipairs(dictionary) do
    local matches=true
    for n,t in ipairs(entry.tokens) do
      if not words[i+n-1] or words[i+n-1].quoted or words[i+n-1].text ~= t then matches=false; break end
    end
    if matches then return entry.category,entry.value,#entry.tokens end
  end
end
local ignored = {}
for _, w in ipairs(vocabulary.noise) do ignored[P.normalize(w)] = true end
local units, pronouns = {}, {}
for _, w in ipairs(vocabulary.units) do units[w] = true end
for _, w in ipairs(vocabulary.pronouns) do pronouns[w] = true end

local numbers = {
  ['ноль']=0,['один']=1,['одна']=1,['одну']=1,['два']=2,['две']=2,['три']=3,['четыре']=4,['пять']=5,
  ['шесть']=6,['семь']=7,['восемь']=8,['девять']=9,['десять']=10,['одиннадцать']=11,['двенадцать']=12,
  ['тринадцать']=13,['четырнадцать']=14,['пятнадцать']=15,['шестнадцать']=16,['семнадцать']=17,['восемнадцать']=18,['девятнадцать']=19,
  ['двадцать']=20,['тридцать']=30,['сорок']=40,['пятьдесят']=50,['шестьдесят']=60,['семьдесят']=70,['восемьдесят']=80,['девяносто']=90,
  ['сто']=100,['двести']=200,['триста']=300,['четыреста']=400,['пятьсот']=500,['шестьсот']=600,['семьсот']=700,['восемьсот']=800,['девятьсот']=900,
  zero=0,one=1,two=2,three=3,four=4,five=5,six=6,seven=7,eight=8,nine=9,ten=10,
  eleven=11,twelve=12,thirteen=13,fourteen=14,fifteen=15,sixteen=16,seventeen=17,eighteen=18,nineteen=19,
  twenty=20,thirty=30,forty=40,fifty=50,sixty=60,seventy=70,eighty=80,ninety=90,
  null=0,ein=1,eins=1,eine=1,zwei=2,drei=3,vier=4,['fünf']=5,sechs=6,sieben=7,acht=8,neun=9,zehn=10,
  zwanzig=20,['fünfzig']=50,
}
local scales = {['тысяча']=1000,['тысячи']=1000,['тысяч']=1000,['тыс']=1000,thousand=1000,tausend=1000,
  ['миллион']=1000000,['миллиона']=1000000,['миллионов']=1000000,million=1000000,millions=1000000,millionen=1000000}
for word,value in pairs({['одной']=1,['одного']=1,['двух']=2,['трех']=3,['четырех']=4,['пяти']=5,['шести']=6,['семи']=7,['восьми']=8,['девяти']=9,
  ['десяти']=10,['одиннадцати']=11,['двенадцати']=12,['тринадцати']=13,['четырнадцати']=14,['пятнадцати']=15,
  ['двадцати']=20,['тридцати']=30,['сорока']=40,['пятидесяти']=50,['шестидесяти']=60,['семидесяти']=70,['восьмидесяти']=80,['девяноста']=90,
  ['ста']=100,['двухсот']=200,['трехсот']=300,['четырехсот']=400,['пятисот']=500,['шестисот']=600,['семисот']=700,['восьмисот']=800,['девятисот']=900,['шестнадцати']=16,['семнадцати']=17,['восемнадцати']=18,['девятнадцати']=19}) do numbers[word]=value end
function P.number(words, start)
  local total,group,i,seen,last = 0,0,start,false,nil
  while words[i] and not words[i].quoted do
    local w=words[i].text
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
    elseif not scales[w] and w~='hundert' and (numbers[w] ~= nil or german.number(w) ~= nil) then
      local n=numbers[w] or german.number(w)
      if last and n >= last then break end
      group=group+n; seen=true; last=n
    elseif w=='hundred' or w=='hundert' then
      group=(group==0 and 1 or group)*100; seen=true; last=100
    elseif scales[w] then
      total=total+(group==0 and 1 or group)*scales[w]; group=0; seen=true; last=nil
    elseif w=='and' and seen and words[i+1] and numbers[words[i+1].text] then
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
      elseif category=='directions' then ast.direction=value; i=i+n
      elseif vocabulary.ordinals[w.text] then
        if ast.ordinal then return nil,tr('Specify one row number.') end
        ast.ordinal=vocabulary.ordinals[w.text]; i=i+1
      elseif pronouns[w.text] then ast.pronoun=true; i=i+1
      elseif ignored[w.text] then i=i+1
      elseif units[w.text] then
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
  if ast.verb=='help' or ast.verb=='reset' then
    if #words~=1 then return nil,tr('This command takes no parameters.') end
    return ast
  end
  if ast.verb=='open' then
    if #ast.filters>0 or ast.field or ast.debtors or ast.direction then return nil,tr('Find objects first, then open one.') end
    if ast.ordinal and (ast.ordinal<1 or ast.ordinal%1~=0) then return nil,tr('Use a whole row number starting at 1.') end
    if ast.pronoun and not ast.ordinal then ast.ordinal=context and context.selected end
    if not ast.name and not ast.ordinal then return nil,tr('Which object should I open? Try: open first.'),{verb='open'} end
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
    if ast.verb~='sort' then ast.sort=copy(previous.sort) end
  end
  if ast.debtors and not ast.entity then ast.entity='counterparties' end
  if not ast.entity then return nil,tr('What should I find: customers, documents or items?') end
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
return P

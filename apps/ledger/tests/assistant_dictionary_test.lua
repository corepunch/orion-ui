local D=require('dictionary')
local normalize=require('normalize')
local P=require('parser')
local checks=0
local function check(value,label) assert(value,label); checks=checks+1 end
local function tokens(text)
  local out={}
  for word in normalize(text):gmatch('%S+') do out[#out+1]={text=word} end
  return out
end
local function lookup(d,text)
  return d:lookup(tokens(text),1)
end
local source=require('lexicon')
local dictionary=D.compile(source)
local function verify(alias,role,value)
  local phrase=tokens(alias)
  local entry,n=dictionary:lookup(phrase,1)
  check(entry and n==#phrase,'complete lookup: '..alias)
  check(entry.roles[role]==value,'meaning: '..alias)
  check(dictionary.entries[entry.id]==entry and dictionary.words[entry.text]==entry,'interning: '..alias)
end
-- Every authored alias, including the auxiliary roles previously outside the dictionary.
for role,groups in pairs(source) do
  if role=='noise' or role=='pronouns' or role=='units' or role=='number_conjunctions' then
    for _,alias in ipairs(groups) do verify(alias,role,true) end
  elseif role=='numbers' or role=='scales' or role=='multipliers' or role=='ordinals' then
    for alias,value in pairs(groups) do verify(alias,role,value) end
  else
    for value,aliases in pairs(groups) do
      for _,alias in ipairs(aliases) do verify(alias,role,value) end
    end
  end
end
local examples={
  {'не меньше пятидесяти тысяч','comparisons','ge',2},
  {'не больше пятидесяти тысяч','comparisons','le',2},
  {'greater than fifty thousand','comparisons','gt',2},
  {'at least fifty thousand','comparisons','ge',2},
  {'mehr als fünfzigtausend','comparisons','gt',2},
  {'weniger als eintausend','comparisons','lt',2},
  {'öffne den ersten','verbs','open',1},
  {'show customers','verbs','list',1},
  {'покажи клиентов','verbs','list',1},
  {'рублей','units',true,1},
  {'this','pronouns',true,1},
  {'first','ordinals',1,1},
  {'ноль','numbers',0,1},
  {'millionen','scales',1000000,1},
}
for _,row in ipairs(examples) do
  local entry,n=lookup(dictionary,row[1])
  check(entry and entry.roles[row[2]]==row[3] and n==row[4],row[1])
end
for _,row in ipairs({{'show','zeige','покажи'},{'debt','schulden','долг'},{'customer','kunden','клиент'}}) do
  local role=row[1]=='show' and 'verbs' or row[1]=='debt' and 'fields' or 'nouns'
  check(dictionary.words[row[1]].roles[role]==dictionary.words[row[2]].roles[role]
    and dictionary.words[row[2]].roles[role]==dictionary.words[row[3]].roles[role],'cross-language canonical meaning')
end
-- Full Unicode spellings; neither byte-prefix truncation nor stemming.
do
  local d=D.compile{nouns={supplier={'creditor'},memo={'credits'}},verbs={open={'ÖFFNE','öffne'}},
    numbers={['трёх']=3,['трех']=3}}
  check(d.words.creditor.id~=d.words.credits.id,'six-character prefixes stay distinct')
  check(lookup(d,'creditor').roles.nouns=='supplier','creditor meaning')
  check(lookup(d,'credits').roles.nouns=='memo','credits meaning')
  check(lookup(d,'öffne').roles.verbs=='open','umlaut normalization')
  check(d.words['трех'].roles.numbers==3 and not d.words['трёх'],'yo normalization')
  check(not lookup(d,'credit') and not lookup(d,'creditors'),'no arbitrary truncation or suffix stripping')
end
-- A word can have several grammatical roles; adding one never erases another.
do
  local d=D.compile{verbs={list={'list'}},nouns={documents={'list'}},noise={'please'}}
  local entry=lookup(d,'list')
  check(entry.roles.verbs=='list' and entry.roles.nouns=='documents','preserve multiple roles')
  check(#d.entries==2,'one interned record per surface form')
  check(entry==d.words.list,'lookup returns the interned record')
end
-- Longest COMPLETE phrase wins. A failed continuation rolls back to the last terminal.
do
  local d=D.compile{comparisons={gt={'over','over and above'},ge={'at least'}},nouns={items={'at'}}}
  local entry,n=lookup(d,'over and above 50'); check(entry.roles.comparisons=='gt' and n==3,'longest phrase')
  entry,n=lookup(d,'over and unknown'); check(entry.text=='over' and n==1,'terminal rollback')
  entry,n=lookup(d,'at unknown'); check(entry.roles.nouns=='items' and n==1,'shorter role fallback')
  check(not lookup(d,'unknown over'),'unknown first token never skipped')
  entry,n=d:lookup(tokens('unknown at least 5'),2); check(entry.text=='at least' and n==2,'offset lookup')
  check(not d:lookup({{text='over',quoted=true}},1),'quoted word bypasses vocabulary')
  entry,n=d:lookup({{text='over'},{text='and',quoted=true},{text='above'}},1)
  check(entry.text=='over' and n==1,'phrase cannot cross quoted name')
end
-- All IDs and role records are independent of authoring table traversal order.
do
  local reversed={}
  local roles={}; for role in pairs(source) do roles[#roles+1]=role end; table.sort(roles)
  for i=#roles,1,-1 do
    local role=roles[i]; local values={}; reversed[role]=values
    local keys={}; for key in pairs(source[role]) do keys[#keys+1]=key end
    table.sort(keys,function(a,b) return tostring(a)<tostring(b) end)
    for j=#keys,1,-1 do values[keys[j]]=P.copy(source[role][keys[j]]) end
  end
  local other=D.compile(reversed)
  check(#dictionary.entries==#other.entries,'stable record count')
  for i,entry in ipairs(dictionary.entries) do
    local second=other.entries[i]
    check(entry.id==second.id and entry.text==second.text,'stable word id '..entry.text)
    for role,value in pairs(entry.roles) do check(second.roles[role]==value,'stable role '..entry.text) end
  end
end
-- Compiled tables belong to the compiler result, not to the mutable authored source.
do
  local authored={verbs={open={'open'}}}
  local a=D.compile(authored); local b=D.compile(authored)
  authored.verbs.open[1]='delete'
  check(a.words.open.roles.verbs=='open' and not a.words.delete,'source changes require explicit recompilation')
  a.words.open.roles.verbs='test mutation'
  check(b.words.open.roles.verbs=='open','independent compiled instances')
end
local invalid={
  {'same-role conflict',{verbs={list={'SHOW'},open={'show'}}},'conflicting verbs'},
  {'normalized conflict',{numbers={['трех']=3,['трёх']=4}},'conflicting numbers'},
  {'empty alias',{verbs={open={''}}},'must not be empty'},
  {'whitespace alias',{verbs={open={' \194\160 '}}},'must not be empty'},
  {'invalid UTF-8',{verbs={open={string.char(255)}}},'not valid UTF-8'},
  {'unknown role',{verbes={open={'open'}}},'unknown vocabulary role'},
  {'alias list type',{verbs={open='open'}},'must be an alias list'},
  {'empty list',{verbs={open={}}},'nonempty dense'},
  {'sparse list',{verbs={open={[1]='open',[3]='öffne',[5]='открой'}}},'dense'},
  {'keyed list',{verbs={open={alias='open'}}},'dense'},
  {'numeric alias',{verbs={open={1}}},'must be strings'},
  {'punctuation',{verbs={open={'open!'}}},'tokenizer punctuation'},
  {'quotes',{verbs={open={'"open"'}}},'tokenizer punctuation'},
  {'nonfinite number',{numbers={infinity=math.huge}},'finite nonnegative'},
  {'negative number',{numbers={minus=-1}},'finite nonnegative'},
  {'fractional ordinal',{ordinals={half=0.5}},'finite nonnegative'},
  {'zero ordinal',{ordinals={zeroth=0}},'must be positive'},
  {'zero scale',{scales={thousand=0}},'must be positive'},
}
for _,row in ipairs(invalid) do
  local ok,err=pcall(D.compile,row[2])
  check(not ok and err:find('assistant dictionary:',1,true) and err:find(row[3],1,true),row[1]..': '..tostring(err))
end
-- The main parser compiles at module load, rather than for each command.
do
  local original_compile,original_parser=D.compile,package.loaded.parser
  local calls=0
  D.compile=function(...) calls=calls+1; return original_compile(...) end
  package.loaded.parser=nil
  local fresh=require('parser')
  for _=1,100 do check(fresh.parse('show debtors over fifty thousand'),'reused dictionary') end
  check(calls==1 and require('parser')==fresh,'one compilation per loaded parser module')
  D.compile,package.loaded.parser=original_compile,original_parser
end
-- More vocabulary must not change how existing phrases resolve.
do
  local extended=P.copy(source); extended.nouns.stress={}
  for i=1,10000 do extended.nouns.stress[i]=('synthetic%d'):format(i) end
  local large=D.compile(extended)
  check(#large.entries==#dictionary.entries+10000,'large vocabulary')
  local entry,n=lookup(large,'не меньше 50000'); check(entry.roles.comparisons=='ge' and n==2,'large phrase lookup')
  check(lookup(large,'synthetic10000').roles.nouns=='stress','large exact lookup')
end
print(('assistant compiled dictionary: %d word records, %d checks passed'):format(#dictionary.entries,checks))

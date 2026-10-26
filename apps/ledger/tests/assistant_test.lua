-- Run with Lua 5.4+ from the repository root. No network, model or Ledger UI.
package.path='apps/ledger/share/addons/assistant/?.lua;'..package.path
local P,E=require('parser'),require('engine')
local function parse(text,ctx)
  local ast,err=P.parse(text,ctx); assert(ast,err); return ast
end
-- Fictional fixtures created for this unit test; no real customer/company data.
local people={
  {id=11,name='Альфа',debt=60000,amount=60000},
  {id=12,name='Бета',debt=45000,amount=45000},
  {id=13,name='Гамма',debt=125000,amount=125000},
  {id=14,name='Аванс',debt=-1000,amount=-1000},
}
local opened={}
local e=E.new{
  rows=function(entity)
    if entity=='counterparties' then return people end
    if entity=='documents' then return {{id=31,name='РТ-1',amount=120000,posted=true},{id=32,name='РТ-2',amount=1000,posted=false}} end
    return {{id=41,name='Карандаш',amount=10}}
  end,
  open=function(entity,id) opened[#opened+1]={entity,id}; return true end,
}
local r=e:submit('ПОКАЖИ КЛИЕНТОВ, которые задолжали больше пятидесяти тысяч')
-- A comma is normal dictation punctuation, not part of a noun.
assert(r.ok,r.message); assert(#r.rows==2)
r=e:submit('отсортируй по долгу по убыванию'); assert(r.ok,r.message); assert(r.rows[1].id==13)
r=e:submit('открой первого'); assert(r.ok,r.message); assert(opened[1][2]==13)
r=e:submit('открой его'); assert(r.ok,r.message); assert(opened[2][2]==13)
r=e:submit('меньше ста тысяч'); assert(r.ok,r.message); assert(#r.rows==1 and r.rows[1].id==11)
local before=P.copy(e.context)
r=e:submit('покажи клиентов и удали всех'); assert(not r.ok); assert(e.context.query.entity==before.query.entity)
assert(#e.rows==1 and #opened==2)
for _,text in ipairs({'больше','покажи документы с долгом больше 10','покажи товары проведенные','покажи клиентов завтра',
  'покажи должников кроме Альфа','открой 0','открой 99','покажи клиентов больше 5 евро','покажи клиентов больше пять пять'}) do
  assert(not e:submit(text).ok,text)
end
for _,text in ipairs({'покажи должников','покажи клиентов с долгом больше 50 тысяч','покажи клиентов которые должны больше 50 000',
  'show customers with debt greater than fifty thousand','zeige kunden mit schulden über fünfzig tausend'}) do
  local a=parse(text); assert(a.entity=='counterparties',text)
end
assert(parse('покажи клиентов больше 50,5 тысяч').filters[1].value==50500)
assert(parse('покажи клиентов не меньше 50 тысяч').filters[1].op=='ge')
r=e:submit('покажи документы'); assert(r.ok,r.message); assert(#r.rows==2)
r=e:submit('только проведенные'); assert(r.ok,r.message); assert(#r.rows==1 and r.rows[1].id==31)
r=e:submit('открой'); assert(r.ok and r.inferred)
r=e:submit('открой первого'); assert(r.ok,r.message); assert(opened[#opened][2]==31)
r=e:submit('найди клиента "Альфа"'); assert(r.ok,r.message); assert(#r.rows==1 and r.rows[1].id==11)
r=e:submit('открой клиента "Бета"'); assert(r.ok,r.message); assert(opened[#opened][2]==12)
r=e:submit('сброс'); assert(r.ok); assert(#e.rows==0)
assert(not e:submit('больше 100').ok)
assert(not P.parse(string.char(255)))
assert(not P.parse(string.rep('x',2049)))
assert(not P.parse('найди клиента "Альфа'))
print('assistant grammar, dialogue, filtering and action tests passed')
-- The native test host loads this second chunk explicitly: its sandbox has no dofile.
if dofile then
  dofile('apps/ledger/tests/assistant_examples.lua')
  dofile('apps/ledger/tests/assistant_dialogue_test.lua')
end

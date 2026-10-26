-- Accounting counterparts of THIS-IS-IT, ORPHAN-MERGE and typed GWIM.
local P,E=require('parser'),require('engine')
local checks=0
local function check(value,label) assert(value,label); checks=checks+1 end
local function same(a,b)
  if type(a)~=type(b) then return false end
  if type(a)~='table' then return a==b end
  for k,v in pairs(a) do if not same(v,b[k]) then return false end end
  for k in pairs(b) do if a[k]==nil then return false end end
  return true
end
local function fixture()
  local records={
    counterparties={{id=11,name='Alpha AG',debt=60000},{id=12,name='Alpha GmbH',debt=20000},{id=13,name='Beta',debt=0}},
    documents={{id=21,name='Invoice A',amount=60000,posted=true,counterparty_id=11},
      {id=22,name='Credit A',amount=5000,posted=false,counterparty_id=11},
      {id=23,name='Invoice B',amount=20000,posted=true,counterparty_id=12}},
    items={{id=31,name='Pencil',amount=1}},
  }
  local opened={}
  local host={rows=function(entity) return assert(records[entity],entity) end,
    open=function(entity,id) opened[#opened+1]={entity=entity,id=id}; return true end}
  return E.new(host),records,opened,host
end
local dialogues={
  {name='English receivables review',steps={
    {'find customer "Alpha AG"',true,1}, {'show documents for it',true,2},
    {'only posted',true,1}, {'open this',true,nil,21},
    {'show customers',true,3}, {'open it',true,nil,21}}},
  {name='German receivables review',steps={
    {'suche Kunde "Alpha AG"',true,1}, {'zeige Belege für diesen Kunden',true,2},
    {'nur gebuchte',true,1}, {'öffne dieses',true,nil,21}}},
  {name='Russian receivables review',steps={
    {'найди клиента "Alpha AG"',true,1}, {'покажи документы для этого клиента',true,2},
    {'только проведенные',true,1}, {'открой это',true,nil,21}}},
  {name='Question, disambiguation, answer',steps={
    {'show posted documents for',false,3}, {'"Alpha"',false,2}, {'second',true,1},
    {'open it',true,nil,23}}},
  {name='German question',steps={
    {'zeige gebuchte Belege für',false,3}, {'"Alpha"',false,2}, {'den zweiten',true,1},
    {'öffne es',true,nil,23}}},
  {name='Russian question',steps={
    {'покажи проведенные документы для',false,3}, {'"Alpha"',false,2}, {'второго',true,1},
    {'открой его',true,nil,23}}},
  {name='Explicitly scoped open question',steps={
    {'open customer "Alpha"',false,2}, {'"Beta"',false,2}, {'99',false,2},
    {'help',true,2}, {'1',true,nil,11}}},
  {name='A new verb replaces an orphan',steps={
    {'show documents for',false,3}, {'show items',true,1}, {'open',true,nil,31}, {'2',false}}},
  {name='Explicit cancellation',steps={
    {'open customer "Alpha"',false,2}, {'cancel',true,2}, {'1',false}, {'open it',false}}},
  {name='No accounting writes inferred',steps={
    {'find customer "Alpha AG"',true,1}, {'post documents for it',false},
    {'delete it',false}, {'pay it',false}, {'reconcile it',false}, {'open it',true,nil,11}}},
  {name='Wrong types and no focus',steps={
    {'open it',false}, {'show documents for it',false}, {'show items',true,1},
    {'show documents for it',false}, {'open this customer',false}, {'show items for customer "Alpha"',false}}},
  {name='Related singleton document inferred',steps={
    {'open document for customer "Alpha GmbH"',true,nil,23}}},
  {name='Related ambiguous document is not guessed',steps={
    {'open document for customer "Alpha AG"',false,2}, {'2',true,nil,21}}},
  {name='Two successive missing-object questions',steps={
    {'open document for customer "Alpha"',false,2}, {'1',false,2}, {'2',true,nil,21}}},
  {name='No result preserves last mentioned customer',steps={
    {'find customer "Beta"',true,1}, {'show documents for it',true,0}, {'open it',true,nil,13}}},
}
for _,dialogue in ipairs(dialogues) do
  local e,records,opened=fixture()
  local original=P.copy(records)
  for _,step in ipairs(dialogue.steps) do
    local r=e:submit(step[1]); local label=dialogue.name..' / '..step[1]..' / '..r.message
    check(r.ok==step[2],label)
    if step[3] then check(#r.rows==step[3],label..' / count') end
    if step[4] then check(r.opened and r.opened.id==step[4],label..' / id') end
    check(same(records,original),label..' / no writes')
  end
end
-- A unique compatible record may be inferred, but zero/multiple candidates may not.
for _,d in ipairs({{'open item',31},{'öffne Artikel',31},{'открой товар',31}}) do
  local e=fixture(); local r=e:submit(d[1]); check(r.ok and r.inferred and r.opened.id==d[2],d[1])
end
for _,text in ipairs({'open customer','öffne Kunden','открой клиента'}) do
  local e,_,opened=fixture(); local r=e:submit(text)
  check(not r.ok and r.clarification=='object' and #opened==0,text)
end
-- Stable identity, rather than a positional alias: sorting must never retarget it.
for _,text in ipairs({'sort customers by debt ascending','sort customers by debt descending','show documents','show items'}) do
  local e,records,opened=fixture()
  check(e:submit('open customer "Alpha AG"').ok,'select Alpha')
  check(e:submit(text).ok,text)
  local expected=text=='show items' and 31 or 11 -- a singleton becomes THIS-IS-IT
  local r=e:submit('open it'); check(r.ok and r.opened.id==expected,'identity after '..text)
end
-- Revalidate references and candidate rows against the host, including failures.
for _,reference in ipairs({'open it','open first'}) do
  local e,records,opened=fixture()
  check(e:submit('find customer "Alpha AG"').ok,'select')
  table.remove(records.counterparties,1)
  local r=e:submit(reference)
  check(not r.ok and #opened==0,'deleted record: '..reference)
end
for _,failure in ipairs({'throw','refuse','query'}) do
  local e,_,opened,host=fixture()
  check(e:submit('find customer "Alpha AG"').ok,'select')
  local before=P.copy(e.context)
  if failure=='query' then host.rows=function() error('datasource unavailable') end
  else host.open=function() if failure=='throw' then error('cannot open') end; return false end end
  local r=e:submit('open it')
  check(not r.ok and same(e.context,before) and #opened==0,'host failure '..failure)
end
-- Failed answers preserve the exact pending request; a later valid answer completes it.
local bad_answers={'','please','bitte','пожалуйста','99','0','first second','"Missing"',
  '"Alpha" delete all','customer "Alpha" over 5','document','it','"Alpha','5 euro',string.char(255)}
for _,text in ipairs(bad_answers) do
  local e,_,opened=fixture()
  check(not e:submit('show posted documents for').ok,'ask')
  local pending=P.copy(e.pending)
  local r=e:submit(text)
  check(not r.ok and same(e.pending,pending) and #opened==0,'preserve orphan: '..text)
  r=e:submit('"Alpha GmbH"')
  check(r.ok and #r.rows==1 and r.rows[1].id==23,'resume after '..text)
end
-- A repeated customer selection must not leak the previous customer's document filter.
do
  local e=fixture()
  check(e:submit('show documents for customer "Alpha AG"').ok,'Alpha docs')
  local r=e:submit('show documents for customer "Alpha GmbH"')
  check(r.ok and #r.rows==1 and r.rows[1].id==23,'replace relation')
  check(e:submit('reset').ok and not e.context.it and not e.pending,'reset identities and slots')
end
-- A document moved to another customer after clarification must not be opened.
do
  local e,records,opened=fixture()
  check(not e:submit('open document for customer "Alpha AG"').ok,'ask for document')
  records.documents[1].counterparty_id=12
  check(not e:submit('2').ok and #opened==0,'revalidate relation before open')
end
-- Type-based inference works without a focus only when the eligible set is unique.
do
  local e,records=fixture()
  records.counterparties={records.counterparties[1]}
  local r=e:submit('show documents for')
  check(r.ok and r.inferred and #r.rows==2,'infer sole customer')
  e,records=fixture(); records.counterparties={}
  r=e:submit('show documents for')
  check(not r.ok and not e.pending,'no suitable customer')
end
for _,text in ipairs({'open first it','open customer ""','show documents for customer ""',
  'show documents for document "Invoice A"','show documents for customer "Alpha" for customer "Beta"',
  'show documents for customer "Alpha" and delete all','show documents for customer "Alpha" over',
  'show documents for customer "Alpha" euro','show documents for '..string.rep('for ',200),
  'show invoices due tomorrow','reconcile bank statement','pay customer "Alpha"','post all documents',
  'delete customer "Alpha"','show documents for this item'}) do
  local e,records,opened=fixture(); local before=P.copy(records)
  local r=e:submit(text)
  check(not r.ok and #opened==0 and same(records,before),'unsupported/invalid request: '..text)
end
-- Pronoun answers and inferred answers cannot escape an ambiguity candidate set.
do
  local e,_,opened=fixture()
  check(e:submit('open customer "Beta"').ok,'remember outside candidate')
  check(not e:submit('open customer "Alpha"').ok,'ask Alpha')
  check(not e:submit('it').ok and #opened==1,'pronoun answer bounded')
  check(not e:submit('customer').ok and #opened==1,'inferred answer bounded')
  check(e:submit('1').ok and #opened==2,'explicit candidate completes')
end
-- Refining a relation query must not replace the remembered document with its customer.
do
  local e=fixture()
  check(e:submit('show documents for customer "Alpha AG"').ok,'relation')
  check(e:submit('open first').ok,'remember credit note')
  check(e:submit('sort by amount descending').ok,'sort related documents')
  local r=e:submit('open it'); check(r.ok and r.opened.id==22,'sort retains document identity')
  check(not e:submit('open document it for customer "Alpha GmbH"').ok,'explicit relation constrains pronoun')
end
-- Larger realistic ledger: overlapping names, thousands of documents and repeated scope changes.
do
  local e,records,opened=fixture()
  records.counterparties={}; records.documents={}
  for i=1,120 do
    records.counterparties[i]={id=i,name=('Customer %03d'):format(i),debt=i*1000}
    for j=1,20 do
      records.documents[#records.documents+1]={id=i*100+j,name=('Invoice %03d-%02d'):format(i,j),
        counterparty_id=i,amount=j*100,posted=j%2==0}
    end
  end
  for i=1,120 do
    local r=e:submit(('find customer "Customer %03d"'):format(i))
    check(r.ok and #r.rows==1,'large lookup '..i)
    r=e:submit('show documents for it'); check(r.ok and #r.rows==20,'large relation '..i)
    r=e:submit('only posted'); check(r.ok and #r.rows==10,'large posted '..i)
    r=e:submit('sort by amount descending'); check(r.ok and r.rows[1].id==i*100+20,'large sort '..i)
    r=e:submit('open first'); check(r.ok and r.opened.id==i*100+20,'large open '..i)
    check(e:submit('show customers').ok,'change scope '..i)
    r=e:submit('open it'); check(r.ok and r.opened.id==i*100+20,'large stable identity '..i)
  end
  check(#opened==240,'large exactly two opens per cycle')
end
print(('assistant Zork accounting dialogues: %d checks passed'):format(checks))

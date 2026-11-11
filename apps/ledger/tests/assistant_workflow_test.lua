package.path='apps/ledger/share/addons/assistant/?.lua;build/share/ledger/addons/assistant/?.lua;'..package.path
local E,P=require('engine'),require('parser')
local checks=0
local function check(v,label) assert(v,label); checks=checks+1 end
local records={counterparties={
  {id=1,name='Alpha',tax_id='42',country_code='DE',debt=10,owed=900},
  {id=2,name='Beta',tax_id='42',country_code='DE',debt=0,owed=50},
  {id=3,name='Gamma',tax_id='77',country_code='DE',debt=0,owed=0}},
  documents={{id=11,name='A',date='2026-01-01',posted=true,amount=10,counterparty_id=1},
    {id=12,name='B',date='2026-01-31',posted=false,amount=20,counterparty_id=1},
    {id=13,name='C',date='2026-02-01',posted=true,amount=30,counterparty_id=2}},items={}}
local opened={}
local e=E.new{rows=function(entity) return records[entity] end,
  open=function(entity,id) opened[#opened+1]={entity=entity,id=id}; return true end}
local r=e:submit('show creditors over 100')
check(r.ok and #r.rows==1 and r.rows[1].id==1,'creditors filter payable amount, not receivables')
r=e:submit('show duplicate customers')
check(r.ok and #r.rows==2 and r.message:find('Possible duplicates'),'duplicate candidates explained')
check(r.duplicate_keys[1]=='de:42' and r.duplicate_keys[2]=='de:42','matching identifier accompanies candidate records')
r=e:submit('over 5')
check(r.ok and #r.rows==1 and r.rows[1].id==1,'duplicate grouping precedes refinement')
r=e:submit('open first')
check(r.ok and opened[#opened].id==1,'diagnostic row opens by identity')
r=e:submit('show documents date at least 2026-01-01 date at most 2026-01-31')
check(r.ok and #r.rows==2,'inclusive period excludes February')
r=e:submit('only posted')
check(r.ok and #r.rows==1 and r.rows[1].id==11,'posted refinement preserves period')
local before=P.copy(e.context.query)
check(not e:submit('show documents date exactly 2026-02-30').ok,'invalid date refused')
check(e.context.query.filters[1].value==before.filters[1].value,'refusal preserves date scope')
-- Topics and reconciliation become the same conversational object scope.
e:adopt('counterparties',{records.counterparties[2],records.counterparties[1]},
  {verb='list',entity='counterparties',creditors=true,filters={},sort={field='owed',direction='asc'}})
r=e:submit('open first')
check(r.ok and opened[#opened].id==2,'topic ordinal follows displayed order, not old document list')
r=e:submit('over 100')
check(r.ok and #r.rows==1 and r.rows[1].id==1,'topic amount refinement uses payable field')
e:adopt('documents',{records.documents[1],records.documents[2]})
r=e:submit('only posted')
check(r.ok and #r.rows==1 and r.rows[1].id==11,'snapshot refinement cannot include unrelated posted documents')
e:adopt(nil,{})
check(not e:submit('open first').ok,'non-object topic clears stale ordinal scope')
check(not e:submit('open it').ok,'non-object topic clears stale pronoun')
check(#opened==2,'only requested existing records were opened')
print(string.format('assistant workflows: %d checks passed',checks))

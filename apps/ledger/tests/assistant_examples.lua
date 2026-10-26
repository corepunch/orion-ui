-- Executable example tables. All languages map directly to the same typed AST.
local P,E=require('parser'),require('engine')
local count=0
local function check(value,label)
  assert(value,label); count=count+1
end
local function equal(a,b)
  if type(a)~=type(b) then return false end
  if type(a)~='table' then return a==b end
  for k,v in pairs(a) do if not equal(v,b[k]) then return false end end
  for k in pairs(b) do if a[k]==nil then return false end end
  return true
end
local examples={
  -- English, German, Russian, expected canonical properties.
  {'show customers','zeige die Kunden','покажи клиентов',{verb='list',entity='counterparties'}},
  {'list debtors','zeige Schuldner','покажи должников',{entity='counterparties',debtors=true}},
  {'find customer "Alpha"','suche den Kunden "Alpha"','найди клиента "Alpha"',{verb='find',name='alpha'}},
  {'show documents','zeige Belege','покажи документы',{entity='documents'}},
  {'show only posted documents','zeige nur gebuchte Belege','покажи только проведенные документы',{filters={{field='posted',op='eq',value=true}}}},
  {'show draft documents','zeige ungebuchte Belege','покажи непроведенные документы',{filters={{field='posted',op='eq',value=false}}}},
  {'list items','zeige Artikel','покажи товары',{entity='items'}},
  {'show debtors over fifty thousand','zeige Schuldner über fünfzigtausend','покажи должников больше пятидесяти тысяч',{filters={{field='debt',op='gt',value=50000}}}},
  {'show debtors at least sixty thousand','zeige Schuldner mindestens sechzigtausend','покажи должников не менее шестидесяти тысяч',{filters={{field='debt',op='ge',value=60000}}}},
  {'show documents under one thousand','zeige Belege weniger als eintausend','покажи документы меньше одной тысячи',{filters={{field='amount',op='lt',value=1000}}}},
  {'show items at most twenty five','zeige Artikel höchstens fünfundzwanzig','покажи товары не больше двадцати пяти',{filters={{field='amount',op='le',value=25}}}},
  {'show items exactly twelve','zeige Artikel genau zwölf','покажи товары ровно двенадцать',{filters={{field='amount',op='eq',value=12}}}},
  {'sort customers by name ascending','sortiere Kunden nach Namen aufsteigend','отсортируй клиентов по названию по возрастанию',{sort={field='name',direction='asc'}}},
  {'open first','öffne den ersten','открой первого',{verb='open',ordinal=1}},
  {'open second','öffne die zweite','открой вторую',{verb='open',ordinal=2}},
  {'help','Hilfe','помощь',{verb='help'}},
  {'reset','zurücksetzen','сброс',{verb='reset'}},
}
for _,row in ipairs(examples) do
  local baseline
  for i=1,3 do
    local ast,err=P.parse(row[i]); check(ast,row[i]..': '..tostring(err))
    for k,v in pairs(row[4]) do check(equal(ast[k],v),row[i]..' / '..k) end
    if baseline then check(equal(ast,baseline),'language equivalence: '..row[i]) end
    baseline=ast
  end
end
local numbers={
  {'eleven',11},{'nineteen',19},{'twenty one',21},{'one hundred and twenty five',125},
  {'two million three hundred thousand',2300000},
  {'elf',11},{'zwölf',12},{'zwoelf',12},{'sechzehn',16},{'siebzehn',17},{'dreißig',30},{'dreissig',30},
  {'einundzwanzig',21},{'fünfundfünfzig',55},{'fuenfundfuenfzig',55},{'neunundneunzig',99},
  {'hundert',100},{'einhundertfünfundzwanzig',125},{'zweihundert',200},
  {'tausend',1000},{'fünfzig tausend',50000},{'fünfzigtausend',50000},
  {'einundzwanzigtausenddreihundertfünfundvierzig',21345},{'zwei Millionen',2000000},
  {'пятидесяти тысяч',50000},{'шестнадцати тысяч',16000},{'девятнадцати тысяч',19000},
  {'шестисот тысяч',600000},{'семисот тысяч',700000},{'восьмисот тысяч',800000},{'девятисот тысяч',900000},
  {'трёх тысяч',3000},{'50 000',50000},{'50\194\160000',50000},{'50\226\128\175000',50000},{'50,5 тысяч',50500},
}
for _,row in ipairs(numbers) do
  local ast,err=P.parse('show debtors over '..row[1])
  check(ast,row[1]..': '..tostring(err)); check(ast.filters[1].value==row[2],row[1])
end
-- A fresh host per dialogue: no persistence or dependence on test order.
local function engine()
  local opened={}
  local e=E.new{rows=function(entity)
    if entity=='documents' then return {{id=4,name='Invoice',amount=10,posted=true},{id=5,name='Draft',amount=1,posted=false}} end
    return {{id=1,name='Alpha GmbH',debt=50000},{id=2,name='Alpha AG',debt=60000},{id=3,name='Zero',debt=0}}
  end,open=function(entity,id) opened[#opened+1]={entity=entity,id=id}; return true end}
  return e,opened
end
local dialogues={
  {'show debtors','over fifty thousand','sort by debt descending','open','first','open it'},
  {'zeige Schuldner','mehr als fünfzigtausend','sortiere nach Schulden absteigend','öffne','den ersten','öffne ihn'},
  {'покажи должников','больше пятидесяти тысяч','отсортируй по долгу по убыванию','открой','первого','открой его'},
}
for _,d in ipairs(dialogues) do
  local e,opened=engine()
  for i,text in ipairs(d) do
    local r=e:submit(text); check(r.ok==(i~=4),text..': '..r.message)
  end
  check(#opened==2 and opened[1].id==2 and opened[2].id==2,'contextual opening')
end
for _,text in ipairs({'open customer "Alpha"','öffne Kunde "Alpha"','открой клиента "Alpha"'}) do
  local e,opened=engine()
  local r=e:submit(text); check(not r.ok and #r.rows==2,text)
  r=e:submit('2'); check(r.ok and #opened==1 and opened[1].id==1,'bare ambiguity answer')
end
local rejected={
  '', 'please', 'bitte', 'пожалуйста', 'show customers delete all', 'zeige Kunden löschen', 'покажи клиентов удали всех',
  'show customers tomorrow', 'zeige Kunden morgen', 'покажи клиентов завтра',
  'show documents with debt over 10','zeige gebuchte Artikel','покажи товары проведенные',
  'show debtors over','zeige Schuldner mehr als','покажи должников больше',
  'show debtors over 5 euro','zeige Schuldner über fünf Euro','покажи должников больше пяти евро',
  'open 0','open 1.5','open 99','open first second','öffne ersten zweiten','открой первого второго',
  'open 1 2','show customers and documents','help customers','reset all',
  'find customer "Alpha','find customer "Alpha" "Beta"',
  'show debtors over 50.000','show debtors over 50,000','show debtors over fünfzigbananen','show debtors over einsundzwanzig',
  string.char(255),string.rep('x',2049),
}
for _,text in ipairs(rejected) do
  local e,opened=engine(); check(e:submit('show debtors').ok,'setup')
  local before,rows=P.copy(e.context),P.copy(e.rows)
  local r=e:submit(text)
  check(not r.ok,'must reject: '..text)
  check(equal(e.context,before) and equal(e.rows,rows) and #opened==0,'atomic rejection: '..text)
end
for _,d in ipairs({{'show documents','only posted'},{'zeige Belege','nur gebuchte'},{'покажи документы','только проведенные'}}) do
  local e=engine(); check(e:submit(d[1]).ok,d[1]); local r=e:submit(d[2])
  check(r.ok and #r.rows==1 and r.rows[1].id==4,d[2])
  check(e:submit('reset').ok,'reset'); check(not e:submit('over 100').ok,'no stale context')
end
-- Catalog messages remain whole sentences; data and substitutions are never translated.
check(P.normalize('ÄÖÜ ẞ Ё')=='äöü ß е','case folding')
local ast=P.parse('find customer "BÜRO Ёлка"')
check(ast and ast.name=='büro елка','quoted Unicode name')
print(('assistant example tables: %d checks passed'):format(count))

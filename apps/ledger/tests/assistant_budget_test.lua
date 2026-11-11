package.path='apps/ledger/share/addons/assistant/?.lua;build/share/ledger/addons/assistant/?.lua;share/lua/?.lua;'..package.path
local B=require('budget')
local checks=0
local function check(v,message) assert(v,message); checks=checks+1 end
local function rejects(f) check(not pcall(f),'must reject invalid operation') end
local plan,err=B.request('Сверстай бюджет за первый квартал')
check(plan==false and err:find('year'),'missing year asks, never assumes')
plan=B.request('2026',true)
check(plan.year==2026,'pending year completion')
check(B.request('2026')==nil,'bare year without pending question is not a budget')
for _,s in ipairs({'составь бюджет за первый квартал 2026','ПОКАЖИ БЮДЖЕТ ЗА 1 КВАРТАЛ 2026','сверстай бюджет за i квартал 2026'}) do check(B.request(s).year==2026,s) end
for _,s in ipairs({'сверстай бюджет за второй квартал 2026','составь бюджет за первый квартал 2026 и проведи документы','бюджет на следующий год','бюджет прогноз +10%','бюджет за первый квартал 2026 2027'}) do check(B.request(s)==false,'unsupported budget must be refused: '..s) end
check(B.request('покажи клиентов')==nil,'existing assistant requests are left alone')
rejects(function() B.validate{operation='post',version=1,year=2026} end)
rejects(function() B.validate{operation=B.operation,version=1,year=2026,amount=100} end)
rejects(function() B.validate{operation=B.operation,version=1,year=2026,code='os.execute()'} end)
for _,year in ipairs({0,2026.5,'2026',10000}) do rejects(function() B.validate{operation=B.operation,version=1,year=year} end) end
local data={
 ['90.01']={{date='2026-01-01',document=1,number='SALE1',account='90.01',corr='62.01',debit=0,credit=100.01},
             {date='2026-03-31',document=2,number='RETURN',account='90.01',corr='62',debit=10,credit=0},
             {date='2026-03-31',document=3,number='CLOSE',account='90.01',corr='90.09',debit=90.01,credit=0}},
 ['26']={{date='2026-02-28',document=4,number='RENT',account='26',corr='60',debit=25.52,credit=0},
         {date='2026-02-28',document=5,number='TRANSFER',account='26',corr='90.08',debit=0,credit=25.52}},
 ['90.08']={{date='2026-02-28',document=5,number='TRANSFER',account='90.08',corr='26',debit=25.52,credit=0}},
}
local calls={}
local api={configuration='ru-2026',account=function(code) return {name='Account '..code} end,movements=function(code,f)
 check(f.from=='2026-01-01' and f.to=='2026-03-31','exact inclusive Q1 bounds')
 check(not calls[code],'each mapped root queried only once'); calls[code]=true
 return data[code] or {}
end}
local report=B.run(plan,api)
check(report.currency=='RUB' and #report.rows==9,'reviewed RU mapping')
check(report.rows[1].actual[1]==10001 and report.rows[1].actual[3]==-1000,'integer cent aggregation including reversal')
check(report.rows[4].actual[2]==2552 and report.rows[8].actual[2]==0,'internal transfers not counted twice')
check(#report.sources==3 and #report.excluded==3,'trace included and excluded sides')
check(report.sources[1].document==1,'source identity retained')
check(report.rows[1].proposed[1]==nil,'never copies actual into forecast')
B.edit('план 90.01 1 250,50',report)
check(report.rows[1].proposed[1]==25050 and report.rows[1].actual[1]==10001,'user proposal separate from actuals')
local text=B.render(report)
check(text:find('250.50',1,true) and text:find('not set',1,true),'editable proposals and missing amounts distinct')
check(text:find('#1 / SALE1',1,true),'render source evidence')
for _,s in ipairs({'план 90.01 4 10','план 90.01 1 -10','план 51 1 10','план 90.01 1 1.005','план 90.01 1 nan','план 90.01 1 1 и проведи'}) do rejects(function() B.edit(s,report) end) end
rejects(function() B.edit('план 90.01 1 10') end)
rejects(function() B.propose(report,'90.01',1,0/0) end)
rejects(function() B.run(plan,{configuration='unknown'}) end)
rejects(function() B.run(plan,{configuration='ru-2026',account=function() return nil end}) end)
rejects(function() B.run(plan,{configuration='ru-2026',account=api.account,movements=function() return {{date='2026-04-01'}} end}) end)
for _,movement in ipairs({
  {date='2026-02-30',document=1,number='BAD',account='90.01',corr='62',debit=0,credit=1},
  {date='2026-02-01',document=1.5,number='BAD',account='90.01',corr='62',debit=0,credit=1},
  {date='2026-02-01',document=1,number='BAD',account='90.01',corr='62',debit=0/0,credit=1},
  {date='2026-02-01',document=1,number=2,account='90.01',corr='62',debit=0,credit=1},
  {date='2026-02-01',document=1,number='BAD',account=90.01,corr='62',debit=0,credit=1},
  {date='2026-02-01',document=1,number='BAD',account='90.01',debit=0,credit=1},
}) do rejects(function() B.run(plan,{configuration='ru-2026',account=api.account,movements=function() return {movement} end}) end) end
local de=B.run(plan,{configuration='de-2026',account=api.account,movements=function() return {} end})
local reversal=B.run(plan,{configuration='ru-2026',account=api.account,movements=function(code)
  return code=='90.01' and {{date='2026-02-01',document=1,number='REV',account=code,corr='62',debit=0,credit=-10}} or {}
end})
check(reversal.rows[1].actual[2]==-1000,'signed native storno movements reduce income')
check(de.currency=='EUR' and #de.rows==14,'DE has independent chart mapping')
check(B.render(de):find('coverage unknown',1,true),'empty months flagged as unknown coverage')
print(string.format('assistant budget: %d checks passed',checks))

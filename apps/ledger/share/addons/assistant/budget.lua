-- Read-only quarterly worksheet. The planner supplies data, never code or amounts.
local tr=require('messages')
local normalize=require('normalize')
local B={operation='budget.q1',version=1}
local profiles={
  ['ru-2026']={currency='RUB',income={'90.01','90.03','91.01'},expense={'26','44','90.02','90.07','90.08','91.02'},closing={'00','90.09','91.09','99'}},
  ['de-2026']={currency='EUR',income={'2650','2660','8300','8400'},expense={'2150','3300','3400','4120','4130','4210','4830','4855','4930','4950'},closing={'9000'}},
}
local function finite(n) return type(n)=='number' and n==n and math.abs(n)<1e12 end
local function cents(n)
  assert(finite(n),tr('Invalid financial amount.'))
  return n>=0 and math.floor(n*100+0.5) or math.ceil(n*100-0.5)
end
local function within(code,root) return code==root or code:sub(1,#root+1)==root..'.' end
local function valid_date(value)
  if type(value)~='string' then return false end
  local y,m,d=value:match('^(%d%d%d%d)%-(%d%d)%-(%d%d)$')
  if not y then return false end
  y,m,d=tonumber(y),tonumber(m),tonumber(d)
  if m<1 or m>12 then return false end
  local days=({31, y%4==0 and (y%100~=0 or y%400==0) and 29 or 28,31,30,31,30,31,31,30,31,30,31})[m]
  return d>=1 and d<=days
end
local function add_cents(a,b)
  local total=a+b
  assert(math.abs(total)<=9007199254740991,tr('Budget total exceeds the safe financial range.'))
  return total
end
local function member(code,roots)
  for _,root in ipairs(roots) do if within(code,root) then return true end end
  return false
end
local function strict(t,allowed)
  assert(type(t)=='table',tr('Invalid budget plan.'))
  for k in pairs(t) do assert(allowed[k],tr('Invalid budget plan.')) end
end
function B.validate(plan)
  strict(plan,{operation=true,version=true,year=true})
  assert(plan.operation==B.operation and plan.version==B.version,tr('Unsupported budget operation.'))
  assert(type(plan.year)=='number' and plan.year%1==0 and plan.year>=1900 and plan.year<=9999,tr('Specify a four-digit budget year.'))
  return plan
end
-- Exact, bounded Russian grammar. Unrecognized budget requests never fall through
-- to executable menu commands or topic-word inference.
function B.request(text,pending)
  if type(text)~='string' or #text>2048 then return nil end
  local s=normalize(text):gsub('[.!?]$',''):match('^%s*(.-)%s*$')
  if pending and s:match('^%d%d%d%d$') then return {operation=B.operation,version=1,year=tonumber(s)} end
  s=s:gsub('%s+года$',''):gsub('%s+год$','')
  local stem=s:match('^(.-)%s+%d%d%d%d$') or s
  local year=stem~=s and tonumber(s:match('(%d%d%d%d)$')) or nil
  local known=false
  for _,verb in ipairs({'сверстай','составь','покажи'}) do
    for _,quarter in ipairs({'первый','1','i'}) do
      if stem==verb..' бюджет за '..quarter..' квартал' then known=true end
    end
  end
  if known then
    if year then return {operation=B.operation,version=1,year=year} end
    return false,tr('Which year? Enter four digits, for example 2026. No budget assumptions will be made.')
  end
  if s:find('бюджет',1,true) or s:match('^план%s') then
    return false,tr('Supported: составь бюджет за первый квартал 2026. Proposals: план ACCOUNT MONTH AMOUNT (month 1–3). Posting and forecasts are not supported.')
  end
end
function B.run(plan,api)
  B.validate(plan)
  local profile=assert(profiles[api.configuration],tr('No reviewed budget account mapping for this configuration.'))
  local report={year=plan.year,currency=profile.currency,from=string.format('%04d-01-01',plan.year),to=string.format('%04d-03-31',plan.year),rows={},sources={},excluded={},coverage={0,0,0}}
  local roots={}
  for _,kind in ipairs({'income','expense'}) do
    for _,code in ipairs(profile[kind]) do
      assert(not member(code,roots),tr('Overlapping budget account mapping.')); roots[#roots+1]=code
      local account=assert(api.account(code),tr('A mapped budget account is missing.'))
      report.rows[#report.rows+1]={code=code,name=account.name,kind=kind,actual={0,0,0},proposed={}}
    end
  end
  for _,row in ipairs(report.rows) do
    for _,m in ipairs(api.movements(row.code,{from=report.from,to=report.to})) do
      assert(valid_date(m.date) and m.date>=report.from and m.date<=report.to,tr('Movement outside the requested quarter.'))
      local month=tonumber(m.date:sub(6,7))
      assert(month>=1 and month<=3 and type(m.account)=='string' and m.account~='' and within(m.account,row.code) and
        type(m.document)=='number' and m.document%1==0 and m.document>0 and type(m.number)=='string' and
        type(m.corr)=='string' and m.corr~='',tr('Invalid source movement.'))
      assert(finite(m.debit) and finite(m.credit),tr('Invalid source movement.'))
      local value=cents(m.credit)-cents(m.debit)
      if row.kind=='expense' then value=-value end
      local source={date=m.date,document=m.document,number=m.number,account=m.account,corr=m.corr,amount=value,kind=row.kind,content=m.content}
      -- Transfers between expense categories and period closing are not business
      -- income/expense a second time. Retain excluded sides for audit.
      if member(m.corr,roots) or member(m.corr,profile.closing) then report.excluded[#report.excluded+1]=source
      else
        row.actual[month]=add_cents(row.actual[month],value)
        report.coverage[month]=report.coverage[month]+1
        report.sources[#report.sources+1]=source
      end
    end
  end
  return report
end
function B.propose(report,code,month,amount)
  assert(type(month)=='number' and month%1==0 and month>=1 and month<=3,tr('Use a month from 1 to 3.'))
  local value=cents(amount)
  assert(value>=0,tr('Proposed amounts must be nonnegative.'))
  for _,row in ipairs(report.rows) do
    if row.code==code then row.proposed[month]=code=='90.03' and -value or value; return true end
  end
  error(tr('Choose an account in this budget.'))
end
function B.edit(text,report)
  local s=normalize(text)
  if not s:match('^план%s') then return nil end
  local code,month,amount=s:match('^план%s+([%d.]+)%s+([123])%s+(%d+[.,]?%d*)%s*$')
  assert(report and code,tr('Proposals: план ACCOUNT MONTH AMOUNT. Create a budget first.'))
  amount=amount:gsub(',','.')
  assert(not amount:match('%.%d%d%d'),tr('Use at most two decimal places.'))
  B.propose(report,code,tonumber(month),tonumber(amount))
  return true
end
function B.render(report,format)
  format=format or function(n) return string.format('%.2f',n) end
  local function money(n) return n~=nil and format(n/100) or tr('not set') end
  local out={string.format(tr('Q1 %d · %s · accrual worksheet'),report.year,report.currency),report.from..' — '..report.to,
    tr('Actuals: posted entries on mapped accounts. Proposals: user input only; blank is not zero.'),
    tr('No records does not prove zero activity. This is not a cash-flow or tax budget.'),
    tr('Edit proposals: план ACCOUNT MONTH AMOUNT. Example: план 90.01 1 100000.'),'',}
  local actual,proposed,complete={income={0,0,0},expense={0,0,0}},{income={0,0,0},expense={0,0,0}},{income={true,true,true},expense={true,true,true}}
  for _,row in ipairs(report.rows) do
    out[#out+1]=row.code..' · '..row.name
    local total,p,all=0,0,true
    for month=1,3 do
      total=add_cents(total,row.actual[month])
      actual[row.kind][month]=add_cents(actual[row.kind][month],row.actual[month])
      if row.proposed[month]==nil then all=false; complete[row.kind][month]=false else p=add_cents(p,row.proposed[month]); proposed[row.kind][month]=add_cents(proposed[row.kind][month],row.proposed[month]) end
      out[#out+1]=string.format(tr('Month %d: actual %s | proposed %s'),month,money(row.actual[month]),money(row.proposed[month]))
    end
    out[#out+1]=string.format(tr('Quarter: actual %s | proposed %s'),money(total),money(all and p or nil))
  end
  for _,kind in ipairs({'income','expense'}) do
    out[#out+1]=kind=='income' and tr('Income totals') or tr('Expense totals')
    local a,p,all=0,0,true
    for month=1,3 do
      a=add_cents(a,actual[kind][month]); p=add_cents(p,proposed[kind][month]); all=all and complete[kind][month]
      out[#out+1]=string.format(tr('Month %d: actual %s | proposed %s'),month,money(actual[kind][month]),money(complete[kind][month] and proposed[kind][month] or nil))
    end
    out[#out+1]=string.format(tr('Quarter: actual %s | proposed %s'),money(a),money(all and p or nil))
  end
  local net=0
  out[#out+1]=tr('Income less expenses')
  for month=1,3 do
    local a=add_cents(actual.income[month],-actual.expense[month]); net=add_cents(net,a)
    out[#out+1]=string.format(tr('Month %d: actual %s | proposed %s'),month,money(a),money(complete.income[month] and complete.expense[month] and add_cents(proposed.income[month],-proposed.expense[month]) or nil))
    if report.coverage[month]==0 then out[#out+1]=string.format(tr('Month %d: no mapped source entries; coverage unknown.'),month) end
  end
  out[#out+1]=string.format(tr('Quarter actual: %s'),money(net))
  out[#out+1]=tr('Sources: date · document ID / number · account / corresponding account · signed amount')
  for _,s in ipairs(report.sources) do out[#out+1]=string.format('%s · #%d / %s · %s / %s · %s',s.date,s.document,s.number,s.account,s.corr,money(s.amount)) end
  out[#out+1]=string.format(tr('Excluded internal/closing sides: %d. Mapping is limited to the reviewed demo chart.'),#report.excluded)
  return table.concat(out,'\n')
end
return B

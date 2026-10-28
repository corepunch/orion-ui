local tr = require('messages')
local orion = require('orion')
local Model = require('orion.model')
local ledger = require('ledger')
local Engine = require('engine')
local animations = require('animations')
local Models = {
  counterparties=Model:extend('ib.counterparties'),
  documents=Model:extend('ib.documents'),
  items=Model:extend('ib.items'),
}
local DocumentTypes=Model:extend('cfg.doc_types')
local HomeRows=Model:extend('ib.home_rows')
local Results=Model:extend('assistant.results')
local Assistant={form='assistant',title=tr('Ledger — Assistant')}

-- The settlement account group of the sales book's debit (book 1, receivables)
-- or the purchase book's credit (book 2, payables), read from the chart.
local function settlement_account(book)
  local account
  for _,t in ipairs(DocumentTypes:all()) do
    if t.form=='document' and t.vat_book==book then
      local configured=ledger.account(book==1 and t.debit_id or t.credit_id)
      if configured and configured.analytics=='counterparties' then
        -- Read the chart at runtime, including the configured account's group.
        local parent=ledger.account(configured.code:match('^[^.]+'))
        local id=parent and parent.analytics=='counterparties' and parent.id or configured.id
        assert(not account or account==id,tr('The configuration has multiple receivables accounts; specify an account.'))
        account=id
      end
    end
  end
  return account
end

local function balances(account)
  local net={}
  if not account then return net end
  local _,to=ledger.period()
  for _,entry in ipairs(ledger.movements(account,to~='' and {to=to} or {})) do
    net[entry.subconto]=(net[entry.subconto] or 0)+entry.debit-entry.credit
  end
  return net
end

local function rows(entity)
  local out={}
  local debt,owed={},{}
  if entity=='counterparties' then
    local receivable=settlement_account(1)
    assert(receivable,tr('No receivables account was found in the configuration.'))
    debt=balances(receivable)
    local payable=settlement_account(2)
    if payable and payable~=receivable then
      for id,net in pairs(balances(payable)) do owed[id]=-net end
    end
  end
  for _,record in ipairs(Models[entity]:all()) do
    local row={id=record.id,name=record.name or record.number or '',number=record.number,date=record.date}
    if entity=='counterparties' then row.debt=debt[record.id] or 0; row.owed=owed[record.id] or 0; row.amount=row.debt
    elseif entity=='documents' then row.counterparty_id=record.counterparty_id; row.amount=record.total; row.posted=record.posted==true or record.posted==1
    elseif entity=='items' then row.amount=record.price end
    out[#out+1]=row
  end
  return out
end

-- The balloon is a command palette in Office 97's question balloon: the
-- answer, then rows in groups (the answer's records, commands, tips,
-- examples). A row says what choosing it does: choose answers the dialogue
-- with its ordinal, command runs a Ledger command, submit runs a request,
-- open opens a record.
local LIMIT=6
local Commands=require('commands')
local Tips=require('tips')
local Topics=require('topics')
local SECTIONS={counterparties=tr('Counterparties'),documents=tr('Documents'),items=tr('Items')}
local EXAMPLES={tr('check the books'),tr('show customers'),tr('who do we owe'),tr('how much money do we have')}
-- The empty command box suggests one command the engine understands, a
-- different one each time the bubble opens or answers.
local HINTS={tr('Show debtors'),tr('Show draft documents'),tr('Check the books'),tr('Who do we owe?'),
  tr('How much money do we have?'),tr('Show documents under one thousand'),tr('Show debtors over fifty thousand')}
local function next_hint(previous)
  local hint
  repeat hint=HINTS[math.random(#HINTS)] until hint~=previous
  return hint
end

local function command_rows(items,list,section)
  for _,c in ipairs(list) do
    items[#items+1]={section=section or tr('Commands'),kind='command',name=c.label..(c.shortcut~='' and '  '..c.shortcut or ''),action='command',arg=c.name}
  end
end

function Assistant:init()
  self.engine=Engine.new{rows=rows,open=function(entity,id) return ledger.open(entity,id) end}
  self.catalog=Commands.new(ledger.commands())
  self.animation='WAVE'
  self.expanded=true
  self.revision=1
  self.balloon={}
  self.items={}
  self.lines={}; self.command=''
  self.hint=next_hint()
  self:suggest()
end

-- The books' rows of one card of the Home page (home.c), in their order.
local function home_rows(card)
  local out={}
  for _,row in ipairs(HomeRows:all()) do if row.card==card then out[#out+1]=row end end
  table.sort(out,function(a,b) return a.id<b.id end)
  return out
end

-- Tips for an empty balloon: the books' own checks, as the Home page lists
-- them; the greeting stays compact, "check the books" lists them all.
local TIPS=3
function Assistant:suggest()
  local checks=home_rows(1)
  while #checks>TIPS do table.remove(checks) end
  self.tips=Tips.from_checks(checks,tr('Suggestions'))
  if not self.balloon.text then self.items=self.tips end
end

local function money(v) return orion.formatNumber(v,2) end

-- A question about the state of the books: its answer and rows.
function Assistant:topic(text)
  local topic,rest=Topics.match(text)
  if not topic then return nil end
  local items,message={},nil
  local function checks(filter,section)
    for _,row in ipairs(home_rows(1)) do
      if not filter or filter(row) then
        items[#items+1]={section=section or tr('Needs attention'),kind='tip',name=row.title,amount=0,action='settle',record_id=row.id}
      end
    end
  end
  local function figures(card,kinds,section)
    local total=0
    for _,row in ipairs(home_rows(card)) do
      if not kinds or kinds[row.kind] then
        total=total+row.amount
        items[#items+1]={section=section,kind='record',name=row.title..(row.detail~='' and ' · '..row.detail or ''),
          amount=row.amount,action=row.command~='' and 'settle' or nil,record_id=row.id}
      end
    end
    return total
  end
  if topic=='checks' then
    checks()
    message=#items>0 and string.format(tr('Needs attention: %d. Choose a row to settle it.'),#items) or tr('The books are in order: nothing is waiting.')
  elseif topic=='negative' then
    checks(function(r) return r.kind=='error' and r.command=='open' end)
    message=#items>0 and tr('These balances went below zero. Open the document that took each one there.')
      or tr('No money or stock balance went below zero.')
  elseif topic=='closing' then
    checks(function(r) return r.kind=='month' or r.command=='repost' end)
    message=#items>0 and tr('Closing needs attention.') or tr('Every finished month is closed.')
  elseif topic=='money' then
    local total=figures(2,nil,tr('Money'))
    message=string.format(tr('Money in total: %s.'),money(total))
  elseif topic=='taxes' then
    local total=figures(3,{taxes=true,input_tax=true,wages=true},tr('Settlements'))
    checks(function(r) return r.kind=='tax' end)
    message=total~=0 and tr('Taxes and payroll as the books hold them.') or tr('No taxes are payable.')
  elseif topic=='payables' or topic=='receivables' then
    local field=topic=='payables' and 'owed' or 'debt'
    local people,total={},0
    for _,row in ipairs(rows('counterparties')) do if (row[field] or 0)>0.005 then people[#people+1]=row end end
    table.sort(people,function(a,b) return a[field]>b[field] end)
    for _,row in ipairs(people) do
      total=total+row[field]
      items[#items+1]={section=tr('Counterparties'),kind='counterparties',name=row.name,amount=row[field],action='open',arg='counterparties',record_id=row.id}
    end
    if topic=='payables' then
      message=#people>0 and string.format(tr('We owe %s in total.'),money(total)) or tr('The company owes no one.')
    else
      message=#people>0 and string.format(tr('Owed to us: %s in total.'),money(total)) or tr('No one owes the company.')
    end
  elseif topic=='reconciliation' then
    return self:reconcile(text,rest)
  end
  return {ok=true,message=message,items=items,animation=#items>0 and 'CHECKINGSOMETHING' or 'CONGRATULATE'}
end

-- An act of reconciliation (акт сверки) with one counterparty: their
-- movements on the receivables and payables groups and what is left.
function Assistant:reconcile(text,rest)
  local people=Topics.parties(text,rest,rows('counterparties'))
  if #people~=1 then
    local items={}
    for _,row in ipairs(#people>1 and people or {}) do
      items[#items+1]={section=tr('Counterparties'),kind='counterparties',name=row.name,amount=0,action='submit',
        arg=string.format(tr('reconcile with "%s"'),row.name)}
    end
    return {ok=false,message=#people>1 and tr('Which counterparty? Choose one.')
      or tr('Reconcile with whom? Name the counterparty, for example: reconcile with "Alpha".'),items=items,animation='IDLEHEADSCRATCH'}
  end
  local party=people[1]
  local items,seen,balance={},{},0
  for book=1,2 do
    local account=settlement_account(book)
    if account and not seen[account] then
      seen[account]=true
      local _,to=ledger.period()
      local filter={subconto=party.id}
      if to~='' then filter.to=to end
      for _,m in ipairs(ledger.movements(account,filter)) do
        balance=balance+m.debit-m.credit
        items[#items+1]={section=tr('Movements'),kind='documents',name=orion.formatDate(m.date)..'  '..m.number..'  '..m.account,
          amount=m.debit-m.credit,action='open',arg='documents',record_id=m.document}
      end
    end
  end
  local message
  if balance>0.005 then message=string.format(tr('%s owes the company %s.'),party.name,money(balance))
  elseif balance<-0.005 then message=string.format(tr('The company owes %s %s, or holds their advance.'),party.name,money(-balance))
  else message=string.format(tr('Settlements with %s are closed.'),party.name) end
  return {ok=true,message=message,items=items,animation='CHECKINGSOMETHING'}
end

function Assistant:data()
  local clip=assert(animations[self.animation])
  return {
    values={command=self.command,summary=self.balloon.text or '',more=self.balloon.more or ''},
    lists={palette=self.items},
    hidden={bubble=not self.expanded,palette=#self.items==0},
    placeholders={command=self.hint},
    sprites={mascot={path=clip.path,columns=clip.columns or 4,rows=clip.rows or 2,count=clip.count or 8,interval=clip.interval,loop=clip.loop,revision=self.revision}},
  }
end

function Assistant:show(result,text)
  self.expanded=true
  self.revision=self.revision+1
  self.animation=result.animation
  self.hint=next_hint(self.hint)
  self.lines={}
  for i,row in ipairs(result.rows or {}) do
    self.lines[#self.lines+1]={id=i,record_id=row.id,name=row.name,amount=row.amount or 0,date=row.date or ''}
  end
  for _,row in ipairs(Results:all()) do orion.assert(row:delete()) end
  for _,row in ipairs(self.lines) do orion.assert(Results:create(row)) end
  -- A refusal keeps the previous list in the engine, but the balloon shows
  -- records only for an answer or a question about them.
  local items,entity={},self.engine.scope and self.engine.scope.entity
  local shown=(result.ok or result.clarification) and result.rows or {}
  for i,row in ipairs(shown) do
    if i>LIMIT then break end
    items[#items+1]={section=SECTIONS[entity] or tr('Results'),kind=entity or 'record',name=(row.name or ''):gsub('[\r\n]+',' '),
      amount=row.amount or 0,action='choose',ordinal=i}
  end
  local more=#shown>LIMIT and string.format(tr('And %d more. Ask to narrow the list.'),#shown-LIMIT) or nil
  -- No debtors: who paid in advance is the other side of the same question.
  local query=self.engine.context.query
  local message=result.message
  if result.ok and #(result.rows or {})==0 and query and query.debtors then
    message=tr('No one owes the company.')
    for _,row in ipairs(rows('counterparties')) do
      if (row.debt or 0)<0 then
        items[#items+1]={section=tr('Advances received'),kind='counterparties',name=row.name,amount=-row.debt,action='open',arg='counterparties',record_id=row.id}
      end
    end
  end
  if result.ok and entity and not result.opened then command_rows(items,self.catalog:related(entity)) end
  -- A request the grammar does not know may still name a command.
  if not result.ok and not result.clarification and text then
    local found=self.catalog:search(text)
    if #found>0 then command_rows(items,found); message=tr('These commands match your request.') end
  end
  self.balloon={text=message,more=more}
  self.items=items
  self.app.focus('command')
end

function Assistant:submit(values)
  local text=values.command or ''
  local result=self.engine:submit(text)
  -- What the command grammar does not take may still ask about the books.
  if not result.ok and not result.clarification then
    local ok,answer=pcall(self.topic,self,text)
    if not ok then answer={ok=false,message=string.format(tr('Could not retrieve data: %s'),tostring(answer)),items={},animation='ALERT'} end
    if answer then
      self.command=answer.ok and '' or text
      self.expanded=true
      self.revision=self.revision+1
      self.animation=answer.animation
      self.hint=next_hint(self.hint)
      self.balloon={text=answer.message}
      self.items=answer.items
      self.app.focus('command')
      return
    end
  end
  self.command=result.ok and '' or text
  self:show(result,text)
end

function Assistant:choose(values,control)
  local row=values.palette
  if control~='palette' or not row then return nil,tr('That row is not in the current list.') end
  self.command=values.command or ''
  if row.action=='choose' then return self:show(self.engine:choose(row.ordinal)) end
  if row.action=='submit' then return self:submit{command=row.arg} end
  if row.action=='settle' then
    local ok,err=ledger.settle(row.record_id)
    if not ok then return nil,err end
    self.expanded=false   -- step aside for the window or the answer it brings
    return
  end
  if row.action=='open' then
    if not ledger.open(row.arg,row.record_id) then return nil,tr('Could not open the record.') end
    return
  end
  if row.action=='command' then
    local ok,err=ledger.command(row.arg)
    if not ok then return nil,err end
    self.expanded=false   -- step aside for the window the command opens
    return
  end
  return nil,tr('That row is not in the current list.')
end

function Assistant:help(values)
  self.command=values.command or ''
  self.revision=self.revision+1
  self.balloon={text=tr('Try “show debtors”, then “open first”.')}
  self.items={}
  for _,example in ipairs(EXAMPLES) do
    self.items[#self.items+1]={section=tr('Examples'),kind='example',name=example,action='submit',arg=example}
  end
  self.animation='EXPLAIN'
  self.app.focus('command')
end

-- Ledger saved, posted or deleted something: the tips follow the books, and
-- Clippy asks for attention when there is something new to suggest.
function Assistant:books_changed(values)
  self.command=values.command or ''
  local before=#(self.tips or {})
  self:suggest()
  if #self.tips>before and not self.expanded then
    self.animation='GETATTENTION'
    self.revision=self.revision+1
  end
end

function Assistant:expand(values)
  self.command=values.command or ''
  self.expanded=true
  self.hint=next_hint(self.hint)
  self.revision=self.revision+1
  self.app.focus('command')
  self.animation='HEARING_1'
end
function Assistant:collapse(values)
  self.command=values.command or ''
  self.expanded=false
  self.app.focus('mascot')
end
function Assistant:greet(values)
  if self.expanded then self:collapse(values) else self:expand(values) end
end
return Assistant

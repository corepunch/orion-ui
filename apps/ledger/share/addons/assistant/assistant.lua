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
local Results=Model:extend('assistant.results')
local Assistant={form='assistant',title=tr('Ledger — Assistant')}

local function rows(entity)
  local out={}
  local debt={}
  if entity=='counterparties' then
    local account
    for _,sale in ipairs(DocumentTypes:all()) do
      if sale.form=='document' and sale.vat_book==1 then
        local configured=ledger.account(sale.debit_id)
        if configured and configured.analytics=='counterparties' then
          -- Read the chart at runtime, including the configured account's group.
          local parent=ledger.account(configured.code:match('^[^.]+'))
          local id=parent and parent.analytics=='counterparties' and parent.id or configured.id
          assert(not account or account==id,tr('The configuration has multiple receivables accounts; specify an account.'))
          account=id
        end
      end
    end
    assert(account,tr('No receivables account was found in the configuration.'))
    local _,to=ledger.period()
    for _,entry in ipairs(ledger.movements(account,to~='' and {to=to} or {})) do
      debt[entry.subconto]=(debt[entry.subconto] or 0)+entry.debit-entry.credit
    end
  end
  for _,record in ipairs(Models[entity]:all()) do
    local row={id=record.id,name=record.name or record.number or '',number=record.number,date=record.date}
    if entity=='counterparties' then row.debt=debt[record.id] or 0; row.amount=row.debt
    elseif entity=='documents' then row.counterparty_id=record.counterparty_id; row.amount=record.total; row.posted=record.posted==true or record.posted==1
    elseif entity=='items' then row.amount=record.price end
    out[#out+1]=row
  end
  return out
end

function Assistant:init()
  self.engine=Engine.new{rows=rows,open=function(entity,id) return ledger.open(entity,id) end}
  self.animation='WAVE'
  self.expanded=true
  self.revision=1
  self.message=tr('How can I help?')
  self.lines={}; self.command=''
end
function Assistant:data()
  local clip=assert(animations[self.animation])
  return {
    values={command=self.command,summary=self.message},
    hidden={bubble=not self.expanded},
    sprites={mascot={path=clip.path,columns=clip.columns or 4,rows=clip.rows or 2,count=clip.count or 8,interval=clip.interval,loop=clip.loop,revision=self.revision}},
  }
end
function Assistant:submit(values)
  self.expanded=true
  self.revision=self.revision+1
  local result=self.engine:submit(values.command or '')
  self.message,self.animation=result.message,result.animation
  self.command=result.ok and '' or values.command
  self.lines={}
  for i,row in ipairs(result.rows or {}) do
    self.lines[#self.lines+1]={id=i,record_id=row.id,name=row.name,amount=row.amount or 0,date=row.date or ''}
  end
  for _,row in ipairs(Results:all()) do orion.assert(row:delete()) end
  for _,row in ipairs(self.lines) do orion.assert(Results:create(row)) end
  if #self.lines>0 then
    local preview={self.message}
    for i=1,math.min(3,#self.lines) do
      local row=self.lines[i]
      local name=row.name:gsub('[\r\n]+',' ')
      local cut=utf8.offset(name,35)
      if cut then name=name:sub(1,cut-1)..'…' end
      preview[#preview+1]=string.format(tr('%d. %s — %s'),i,name,orion.formatNumber(row.amount))
    end
    if #self.lines>3 then preview[#preview+1]=string.format(tr('And %d more. Ask to narrow the list.'),#self.lines-3) end
    self.message=table.concat(preview,'\n')
  end
  self.app.focus('command')
end
function Assistant:help(values)
  self.command=values.command or ''
  self.revision=self.revision+1
  self.message=tr('Try “show debtors”, then “open first”.')
  self.animation='EXPLAIN'
  self.app.focus('command')
end
function Assistant:expand(values)
  self.command=values.command or ''
  self.expanded=true
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

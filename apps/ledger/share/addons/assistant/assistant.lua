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
local Assistant={form='assistant',title='Ledger — помощник'}

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
          assert(not account or account==id,'В конфигурации несколько счетов расчётов с покупателями; уточните счёт.')
          account=id
        end
      end
    end
    assert(account,'В конфигурации не найден счёт расчётов с покупателями.')
    local _,to=ledger.period()
    for _,entry in ipairs(ledger.movements(account,to~='' and {to=to} or {})) do
      debt[entry.subconto]=(debt[entry.subconto] or 0)+entry.debit-entry.credit
    end
  end
  for _,record in ipairs(Models[entity]:all()) do
    local row={id=record.id,name=record.name or record.number or '',number=record.number,date=record.date}
    if entity=='counterparties' then row.debt=debt[record.id] or 0; row.amount=row.debt
    elseif entity=='documents' then row.amount=record.total; row.posted=record.posted==true or record.posted==1
    elseif entity=='items' then row.amount=record.price end
    out[#out+1]=row
  end
  return out
end

function Assistant:init()
  self.engine=Engine.new{rows=rows,open=function(entity,id) return ledger.open(entity,id) end}
  self.animation='RESTPOSE'
  self.revision=1
  self.message='Привет! Скажите или напишите: «покажи должников».'
  self.lines={}; self.command=''
end
function Assistant:data()
  local clip=assert(animations[self.animation])
  return {
    values={command=self.command,summary=self.message},
    lists={results=self.lines},
    sprites={mascot={path=clip.path,columns=clip.columns or 4,rows=clip.rows or 2,count=clip.count or 8,interval=clip.interval,loop=clip.loop,revision=self.revision}},
  }
end
function Assistant:submit(values)
  self.revision=self.revision+1
  local result=self.engine:submit(values.command or '')
  self.message,self.animation=result.message,result.animation
  self.command=result.ok and '' or values.command
  self.lines={}
  for i,row in ipairs(result.rows or {}) do
    self.lines[#self.lines+1]={id=i,record_id=row.id,name=row.name,amount=row.amount or 0,date=row.date or ''}
  end
end
function Assistant:help()
  self.revision=self.revision+1
  self.message=self.engine:submit('помощь').message
  self.animation='EXPLAIN'
end
function Assistant:greet()
  self.revision=self.revision+1
  self.app.focus('command')
  self.message='Я слушаю. Введите команду или используйте системную диктовку в поле ввода.'
  self.animation='HEARING_1'
end
return Assistant

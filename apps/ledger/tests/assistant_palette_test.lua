-- The palette's catalog and tips. Fictional fixtures; no Ledger UI.
local Commands,Tips=require('commands'),require('tips')
local checks=0
local function check(value,label) assert(value,label); checks=checks+1 end
local catalog=Commands.new{
  {name='doc.new',label='Новый документ...',group='doc',shortcut='Ctrl+N'},
  {name='doc.new_payroll',label='Новый документ начисления зарплаты...',group='doc',shortcut='Ctrl+W'},
  {name='doc.close_month',label='Закрыть месяц...',group='doc',shortcut='Ctrl+E'},
  {name='view.counterparties',label='Контрагенты',group='view',shortcut='Ctrl+2'},
  {name='file.quit',label='Выход',group='file',shortcut=''},
  {name='help.find_command',label='Найти команду...',group='help',shortcut='Ctrl+K'},
}
local found=catalog:search('нов док')
check(#found==2 and found[1].name=='doc.new','every word starts a word of the caption')
check(found[1].label=='Новый документ','the ellipsis of a menu caption is dropped')
check(#catalog:search('закр мес')==1,'Cyrillic prefixes match')
check(#catalog:search('выход')==0,'the assistant does not offer to quit Ledger')
check(#catalog:search('найти команду')==0,'nor itself')
check(#catalog:search('')==0,'no words find nothing')
check(#catalog:search('нов',1)==1,'the limit is kept')
local related=catalog:related('counterparties')
check(#related==1 and related[1].name=='view.counterparties','related commands exist in the catalog only')
local tips=Tips.find(
  {{id=1,number='РТ-1',posted=false,counterparty_id=7,sale=true},{id=2,number='ПТ-1',posted=false,counterparty_id=8},
   {id=3,number='РТ-2',posted=true,counterparty_id=7,sale=true}},
  {{id=7,name='Бета',debt=-60000},{id=8,name='Альфа',debt=1000}})
check(#tips==2,'drafts and an advance a draft sale would offset')
check(tips[1].action=='submit','the drafts tip lists them')
check(tips[2].action=='open' and tips[2].record_id==1 and tips[2].name:find('РТ%-1'),'the advance tip opens the sale')
check(#Tips.find({{id=3,posted=true}},{})==0,'nothing undone, no tips')
print(string.format('assistant palette: %d checks passed',checks))

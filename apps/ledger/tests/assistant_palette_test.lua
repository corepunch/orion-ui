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
  {name='help.find_command',label='Найти команду...',group='help',shortcut='Ctrl+Shift+K'},
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
local tips=Tips.from_checks({{id=4,kind='draft',title='Не проведено документов: 2'},{id=9,kind='tip',title='Совет: аванс Беты не закрыт'}},'Советы')
check(#tips==2,'every check of the books is a tip')
check(tips[1].action=='settle' and tips[1].record_id==4,'a tip settles its check as the Home page does')
check(tips[2].section=='Советы' and tips[2].name:find('Беты'),'the tip shows the check in its group')
check(#Tips.from_checks({},'Советы')==0,'nothing undone, no tips')
local Topics=require('topics')
local function topic(text) return (Topics.match(text)) end
check(topic('проверь учет')=='checks','a check of the books')
check(topic('почему красное сальдо по 60')=='negative','a red balance, among unknown words')
check(topic('кому мы должны')=='payables' and topic('кто нам должен')=='receivables','who owes whom')
check(topic('сколько денег в кассе')=='money','money')
check(topic('Wer schuldet uns Geld?')=='receivables' or topic('Wer schuldet uns Geld?')=='money','German topics')
check(topic('who do we owe')=='payables','English topics')
check(topic('show debtors')==nil,'commands are not topics')
local _,rest=Topics.match('акт сверки с альфой')
check(Topics.match('акт сверки с альфой')=='reconciliation','reconciliation')
found=Topics.parties('акт сверки с альфой',rest,{{id=1,name='ООО "Торговый дом Альфа"'},{id=2,name='ЗАО "Бета-Снаб"'}})
check(#found==1 and found[1].id==1,'a counterparty named in a case form')
found=Topics.parties('сверка с "Бета"',{},{{id=1,name='ООО "Торговый дом Альфа"'},{id=2,name='ЗАО "Бета-Снаб"'}})
check(#found==1 and found[1].id==2,'a quoted counterparty')
print(string.format('assistant palette: %d checks passed',checks))

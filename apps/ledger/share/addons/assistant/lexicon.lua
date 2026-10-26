-- Explicit vocabulary, not substring matching. Add inflections and synonyms here.
local lexicon = {
  verbs = {
    list = {'покажи','показать','выведи','список','show','list','zeige','anzeigen'},
    find = {'найди','найти','find','suche'},
    open = {'открой','открыть','open','öffne'},
    sort = {'отсортируй','сортируй','sort','sortiere'},
    help = {'помощь','команды','help','hilfe'},
    cancel = {'cancel','abbrechen','отмена'},
    reset = {'сброс','сбрось','reset','zurücksetzen'},
  },
  nouns = {
    counterparties = {'клиенты','клиентов','клиента','клиент','покупатели','покупателей','контрагенты','контрагентов','контрагента','customers','customer','clients','counterparties','kunden','kunde'},
    debtors = {'должники','должников','должника','debtors','schuldner'},
    documents = {'документы','документов','документ','документа','documents','document','belege','beleg'},
    items = {'товары','товаров','товар','товара','items','item','artikel'},
  },
  adjectives = {
    posted = {'проведенные','проведенных','проведенный','posted','gebucht','gebuchte','gebuchten','gebuchter','gebuchtes'},
    draft = {'непроведенные','непроведенных','черновики','draft','drafts','ungebucht','ungebuchte','ungebuchten','ungebuchter','ungebuchtes'},
    debtors = {'задолжали','должны','должен'},
  },
  fields = {
    debt = {'долг','долгом','долгу','долга','задолженность','задолженностью','задолженности','debt','schulden'},
    amount = {'сумма','сумме','суммой','сумму','суммы','amount','total','betrag','beträge','betraege'},
    name = {'названию','название','имени','name','namen'},
  },
  comparisons = {
    gt = {'больше','свыше','выше','greater than','more than','over','über','ueber','mehr als'},
    lt = {'меньше','ниже','less than','under','unter','weniger als'},
    ge = {'не меньше','не менее','at least','mindestens'},
    le = {'не больше','не более','at most','höchstens','hoechstens'},
    eq = {'равно','ровно','exactly','equals','genau'},
  },
  relations = {counterparty={'for','für','fuer','для'}},
  directions = {desc={'убыванию','descending','absteigend'}, asc={'возрастанию','ascending','aufsteigend'}},
  ordinals = {
    ['первого']=1, ['первый']=1, ['первую']=1, first=1, erste=1,ersten=1,erstes=1,
    ['второго']=2, ['второй']=2, ['вторую']=2, second=2, zweite=2,zweiten=2,zweites=2,
    ['третьего']=3, ['третий']=3, third=3, dritte=3,dritten=3,drittes=3,
    ['четвертого']=4, fourth=4, ['пятого']=5, fifth=5,
  },
  pronouns = {'его','ее','этого','эту','it','this','ihn','sie','es','diesen','diese','dieses','это','этот','этой','нему','ней','него','нее'},
  noise = {'пожалуйста','мне','всех','все','которые','который','с','со','по','на','только','please','the','all','with','by','only','nur','bitte','mit','nach','der','die','das','den','dem','alle'},
  units = {'рублей','рубля','руб','евро','euros','euro','rubles'},
}

local numbers = {
  ['ноль']=0,['один']=1,['одна']=1,['одну']=1,['два']=2,['две']=2,['три']=3,['четыре']=4,['пять']=5,
  ['шесть']=6,['семь']=7,['восемь']=8,['девять']=9,['десять']=10,['одиннадцать']=11,['двенадцать']=12,
  ['тринадцать']=13,['четырнадцать']=14,['пятнадцать']=15,['шестнадцать']=16,['семнадцать']=17,['восемнадцать']=18,['девятнадцать']=19,
  ['двадцать']=20,['тридцать']=30,['сорок']=40,['пятьдесят']=50,['шестьдесят']=60,['семьдесят']=70,['восемьдесят']=80,['девяносто']=90,
  ['сто']=100,['двести']=200,['триста']=300,['четыреста']=400,['пятьсот']=500,['шестьсот']=600,['семьсот']=700,['восемьсот']=800,['девятьсот']=900,
  zero=0,one=1,two=2,three=3,four=4,five=5,six=6,seven=7,eight=8,nine=9,ten=10,
  eleven=11,twelve=12,thirteen=13,fourteen=14,fifteen=15,sixteen=16,seventeen=17,eighteen=18,nineteen=19,
  twenty=20,thirty=30,forty=40,fifty=50,sixty=60,seventy=70,eighty=80,ninety=90,
  null=0,ein=1,eins=1,eine=1,zwei=2,drei=3,vier=4,['fünf']=5,sechs=6,sieben=7,acht=8,neun=9,zehn=10,
  zwanzig=20,['fünfzig']=50,
}
local scales = {['тысяча']=1000,['тысячи']=1000,['тысяч']=1000,['тыс']=1000,thousand=1000,tausend=1000,
  ['миллион']=1000000,['миллиона']=1000000,['миллионов']=1000000,million=1000000,millions=1000000,millionen=1000000}
for word,value in pairs({['одной']=1,['одного']=1,['двух']=2,['трех']=3,['четырех']=4,['пяти']=5,['шести']=6,['семи']=7,['восьми']=8,['девяти']=9,
  ['десяти']=10,['одиннадцати']=11,['двенадцати']=12,['тринадцати']=13,['четырнадцати']=14,['пятнадцати']=15,
  ['двадцати']=20,['тридцати']=30,['сорока']=40,['пятидесяти']=50,['шестидесяти']=60,['семидесяти']=70,['восьмидесяти']=80,['девяноста']=90,
  ['ста']=100,['двухсот']=200,['трехсот']=300,['четырехсот']=400,['пятисот']=500,['шестисот']=600,['семисот']=700,['восьмисот']=800,['девятисот']=900,['шестнадцати']=16,['семнадцати']=17,['восемнадцати']=18,['девятнадцати']=19}) do numbers[word]=value end

lexicon.numbers,lexicon.scales=numbers,scales
lexicon.multipliers={hundred=100,hundert=100}
lexicon.number_conjunctions={'and'}
return lexicon

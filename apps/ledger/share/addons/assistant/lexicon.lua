-- Explicit vocabulary, not substring matching. Add inflections and synonyms here.
return {
  verbs = {
    list = {'покажи','показать','выведи','список','show','list','zeige'},
    find = {'найди','найти','find','suche'},
    open = {'открой','открыть','open','öffne'},
    sort = {'отсортируй','сортируй','sort','sortiere'},
    help = {'помощь','команды','help','hilfe'},
    reset = {'сброс','сбрось','reset','zurücksetzen'},
  },
  nouns = {
    counterparties = {'клиенты','клиентов','клиента','клиент','покупатели','покупателей','контрагенты','контрагентов','контрагента','customers','customer','clients','counterparties','kunden'},
    debtors = {'должники','должников','должника','debtors','schuldner'},
    documents = {'документы','документов','документ','документа','documents','document','belege','beleg'},
    items = {'товары','товаров','товар','товара','items','item','artikel'},
  },
  adjectives = {
    posted = {'проведенные','проведенных','проведенный','posted','gebuchte'},
    draft = {'непроведенные','непроведенных','черновики','draft','drafts','ungebuchte'},
    debtors = {'задолжали','должны','должен'},
  },
  fields = {
    debt = {'долг','долгом','долгу','долга','задолженность','задолженностью','задолженности','debt','schulden'},
    amount = {'сумма','сумме','суммой','сумму','суммы','amount','total','betrag'},
    name = {'названию','название','имени','name','namen'},
  },
  comparisons = {
    gt = {'больше','свыше','выше','greater than','more than','over','über'},
    lt = {'меньше','ниже','less than','under','unter'},
    ge = {'не меньше','не менее','at least','mindestens'},
    le = {'не больше','не более','at most','höchstens'},
    eq = {'равно','ровно','exactly','equals','genau'},
  },
  directions = {desc={'убыванию','descending','absteigend'}, asc={'возрастанию','ascending','aufsteigend'}},
  ordinals = {
    ['первого']=1, ['первый']=1, ['первую']=1, first=1, ersten=1,
    ['второго']=2, ['второй']=2, ['вторую']=2, second=2, zweiten=2,
    ['третьего']=3, ['третий']=3, third=3, dritten=3,
    ['четвертого']=4, fourth=4, ['пятого']=5, fifth=5,
  },
  pronouns = {'его','ее','этого','эту','it','this','ihn'},
  noise = {'пожалуйста','мне','всех','все','которые','который','с','со','по','на','только','please','the','all','with','by','nur','bitte','mit','nach'},
  units = {'рублей','рубля','руб','евро','euros','euro','rubles'},
}

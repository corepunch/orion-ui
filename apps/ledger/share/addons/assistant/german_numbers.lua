-- Productive German cardinal compounds, independent of command vocabulary.
local G = {}
local ones = {ein=1,eins=1,zwei=2,drei=3,vier=4,['fünf']=5,sechs=6,sieben=7,acht=8,neun=9}
local small = {null=0,elf=11,['zwölf']=12,dreizehn=13,vierzehn=14,['fünfzehn']=15,
  sechzehn=16,siebzehn=17,achtzehn=18,neunzehn=19,zehn=10}
local tens = {zwanzig=20,['dreißig']=30,vierzig=40,['fünfzig']=50,sechzig=60,siebzig=70,achtzig=80,neunzig=90}
local function under_hundred(w)
  if ones[w] then return ones[w] end
  if small[w] then return small[w] end
  if tens[w] then return tens[w] end
  local a,b=w:match('^(.-)und(.+)$')
  if a and ones[a] and a~='eins' and tens[b] then return ones[a]+tens[b] end
end
local function under_thousand(w)
  local a,b=w:match('^(.-)hundert(.*)$')
  if not a then return under_hundred(w) end
  local n=a=='' and 1 or ones[a]
  local rest=b=='' and 0 or under_hundred(b)
  if n and a~='eins' and rest then return n*100+rest end
end
function G.number(word)
  local w=word:gsub('ae','ä'):gsub('oe','ö'):gsub('ue','ü'):gsub('ss','ß')
  local a,b=w:match('^(.-)tausend(.*)$')
  if not a then return under_thousand(w) end
  local n=a=='' and 1 or under_thousand(a)
  local rest=b=='' and 0 or under_thousand(b)
  if n and n>0 and rest then return n*1000+rest end
end
return G

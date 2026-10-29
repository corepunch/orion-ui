local lexicon = require('orion.plugins').use('lexicon', 1)
local normalize = require('normalize')
local schemas={
  verbs='groups',nouns='groups',adjectives='groups',fields='groups',comparisons='groups',
  relations='groups',directions='groups',ordinals='numbers',numbers='numbers',scales='numbers',
  multipliers='numbers',pronouns='list',noise='list',units='list',number_conjunctions='list',
  topics='groups',
}
return {compile = function(source)
  return lexicon.compile(source, {schemas = schemas, normalize = normalize, label = "assistant dictionary",
    positive = {ordinals = true, scales = true, multipliers = true}})
end}

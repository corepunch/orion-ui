"""Check XML vocabulary packaging and language-independent parser integration."""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
ADDON = ROOT / 'apps/ledger/share/addons/assistant'
spec = importlib.util.spec_from_file_location('vocabulary', ROOT / 'tools/generate_assistant_vocabulary.py')
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class AssistantVocabularyTest(unittest.TestCase):
    def test_all_xml_aliases_and_an_additional_language(self):
        runtime = shutil.which('lua5.4') or shutil.which('lua')
        self.assertIsNotNone(runtime, 'Lua 5.4+ is required')
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for path in (ADDON / 'vocabulary').glob('*.xml'):
                shutil.copy(path, directory)
            (directory / 'xx.xml').write_text('''<vocabulary language="xx">
              <verb source="show">display</verb>
              <noun source="debtor">defaulters</noun>
              <number value="70">seventyish</number>
            </vocabulary>''')
            output = directory / 'vocabulary.lua'
            output.write_text(generator.generate(directory), encoding='utf-8')
            subprocess.run([runtime, '-', str(output)], cwd=ROOT, check=True, input='''
package.path='apps/ledger/share/addons/assistant/?.lua;'..package.path
package.loaded.vocabulary=assert(loadfile(arg[1]))()
local P=require('parser')
local ast=assert(P.parse('display defaulters over seventyish thousand'))
assert(ast.entity=='counterparties' and ast.filters[1].value==70000)
local dictionary=require('dictionary').compile(require('vocabulary').lexicon)
assert(dictionary.words.zeige.roles.verbs=='list')
assert(dictionary.words['покажи'].roles.verbs=='list')
assert(dictionary.words.show.roles.verbs=='list')
''', text=True)
            # Compare each authored word against the generated Lua table, not just examples:
            # headwords (unless their paradigm has no "=" slot), synonyms and irregular forms.
            checks, meanings = [], {}
            roots = [ET.parse(path).getroot() for path in directory.glob('*.xml')]
            for root in roots:
                for node in root:
                    if 'means' in node.attrib:
                        meanings[node.tag, node.get('source')] = node.get('means')
            for root in roots:
                endings = {p.get('name'): p.text.split() for p in root.iterfind('morphology/paradigm')}
                for node in root:
                    role = generator.ELEMENTS.get(node.tag)
                    if role is None:
                        continue
                    head = (node.text or '').strip() or node.get('source')
                    words = node.get('synonyms', '').split() + node.get('forms', '').split()
                    if '=' in endings.get(node.get('paradigm'), '='):
                        words.append(head)
                    for word in words:
                        alias, key = json.dumps(word.lower(), ensure_ascii=False), json.dumps(role)
                        if role in generator.LISTS:
                            checks.append(f'assert(has(source[{key}],{alias}),{alias})')
                        elif role in generator.NUMBERS:
                            checks.append(f'assert(source[{key}][{alias}]=={int(node.get("value"))},{alias})')
                        else:
                            meaning = json.dumps(meanings[node.tag, node.get('source')])
                            checks.append(f'assert(has(source[{key}][{meaning}],{alias}),{alias})')
            subprocess.run([runtime, '-', str(output)], check=True, input='''
local source=assert(loadfile(arg[1]))().lexicon
local function has(list,alias)
  for _,item in ipairs(list) do if item==alias then return true end end
end
''' + '\n'.join(checks), text=True)

    def test_compound_vocabulary_and_normalization_are_xml_data(self):
        runtime = shutil.which('lua5.4') or shutil.which('lua')
        self.assertIsNotNone(runtime, 'Lua 5.4+ is required')
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for path in (ADDON / 'vocabulary').glob('*.xml'):
                shutil.copy(path, directory)
            root = ET.parse(directory / 'de.xml').getroot()
            replacements = {'fünf': 'pent', 'hundert': 'cent', 'tausend': 'kilo'}
            for entry in root:
                entry.text = replacements.get(entry.text, entry.text)
            compound = root.find('compound-numbers')
            compound.set('conjunction', 'plus')
            compound.set('multiplier', 'cent')
            compound.set('scale', 'kilo')
            ET.SubElement(compound, 'replace', source='q.', target='pent')
            ET.ElementTree(root).write(directory / 'de.xml', encoding='utf-8')
            (directory / 'xx.xml').write_text('''<vocabulary language="xx">
              <normalization><map source="Ł" target="l"/></normalization>
            </vocabulary>''', encoding='utf-8')
            output = directory / 'vocabulary.lua'
            output.write_text(generator.generate(directory), encoding='utf-8')
            subprocess.run([runtime, '-', str(output)], cwd=ROOT, check=True, input='''
package.path='apps/ledger/share/addons/assistant/?.lua;'..package.path
package.loaded.vocabulary=assert(loadfile(arg[1]))()
local G=require('german_numbers')
assert(G.number('pentpluszwanzig')==25)
assert(G.number('q.pluszwanzig')==25)
assert(G.number('pentcentpent')==505)
assert(G.number('pentkilopentcentpent')==5505)
assert(not G.number('fünfundzwanzig'))
assert(not G.number('einspluszwanzig'))
assert(not G.number('einscent'))
assert(require('normalize')('ŁÄЁ')=='läе')
local P=require('parser')
local ast=assert(P.parse('zeige schuldner über pentkilopentcentpent'))
assert(ast.filters[1].value==5505)
''', text=True)

    def test_references_do_not_require_an_english_open_alias(self):
        runtime = shutil.which('lua5.4') or shutil.which('lua')
        self.assertIsNotNone(runtime, 'Lua 5.4+ is required')
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            # The source word for opening is renamed, so "open" is no command word.
            for name in ('en.xml', 'de.xml'):
                text = (ADDON / 'vocabulary' / name).read_text(encoding='utf-8')
                text = text.replace('<verb source="open"', '<verb source="launch"')
                (directory / name).write_text(text, encoding='utf-8')
            output = directory / 'vocabulary.lua'
            output.write_text(generator.generate(directory), encoding='utf-8')
            subprocess.run([runtime, '-', str(output)], cwd=ROOT, check=True, input='''
package.path='apps/ledger/share/addons/assistant/?.lua;'..package.path
package.loaded.vocabulary=assert(loadfile(arg[1]))()
local P=require('parser')
for _,word in ipairs(require('vocabulary').lexicon.verbs.open) do assert(word~='open') end
local ref=assert(P.reference('den ersten'))
assert(ref.verb=='open' and ref.ordinal==1)
local ast=assert(P.parse('zeige belege für den ersten'))
assert(ast.for_party.ordinal==1 and ast.for_party.verb=='open')
assert(P.parse('zeige belege für').for_party.verb=='open')
assert(not P.parse('open first'))
''', text=True)

    def test_every_language_translates_every_source_word(self):
        _, missing = generator.compile_vocabulary(ADDON / 'vocabulary')
        self.assertEqual(set(missing), {'en', 'de', 'ru'})
        for language, words in missing.items():
            self.assertEqual(words, [], f'{language}.xml lacks translations')
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            shutil.copy(ADDON / 'vocabulary/en.xml', directory)
            (directory / 'xx.xml').write_text('<vocabulary language="xx"><verb source="show">display</verb></vocabulary>')
            _, missing = generator.compile_vocabulary(directory)
            self.assertIn('verb open', missing['xx'])
            self.assertNotIn('verb show', missing['xx'])

    def test_paradigms_synonyms_and_spellings(self):
        runtime = shutil.which('lua5.4') or shutil.which('lua')
        self.assertIsNotNone(runtime, 'Lua 5.4+ is required')
        data, _ = generator.compile_vocabulary(ADDON / 'vocabulary')
        lexicon = data['lexicon']
        for role, meaning, word in (
                ('nouns', 'counterparties', 'customers'), ('nouns', 'counterparties', 'counterparties'),
                ('nouns', 'counterparties', 'clients'), ('nouns', 'counterparties', 'клиентов'),
                ('nouns', 'counterparties', 'контрагентам'), ('nouns', 'counterparties', 'покупателей'),
                ('nouns', 'counterparties', 'kunden'), ('nouns', 'debtors', 'должников'),
                ('nouns', 'documents', 'belege'), ('fields', 'debt', 'задолженностью'),
                ('fields', 'amount', 'сумм'), ('adjectives', 'posted', 'проведенных'),
                ('adjectives', 'posted', 'gebuchten'), ('verbs', 'list', 'выведи'),
                ('verbs', 'open', 'oeffne'), ('relations', 'counterparty', 'fuer')):
            with self.subTest(word=word):
                self.assertIn(word, lexicon[role][meaning])
        for role, word, value in (('scales', 'тысячами', 1000), ('scales', 'millionen', 1000000),
                                  ('ordinals', 'вторую', 2), ('ordinals', 'ersten', 1),
                                  ('ordinals', 'fuenfte', 5), ('numbers', 'zwoelf', 12)):
            with self.subTest(word=word):
                self.assertEqual(lexicon[role][word], value)
        self.assertNotIn('erst', lexicon['ordinals'], 'a "-" slot adds no form')
        self.assertEqual(generator.inflect({'p': (1, ['=', '0', '-', 'ей'])}, 'тысяча', 'p', 'test'),
                         ['тысяча', 'тысяч', 'тысячей'])
        subprocess.run([runtime, '-'], cwd=ROOT, check=True, input='''
package.path='apps/ledger/share/addons/assistant/?.lua;build/share/ledger/addons/assistant/?.lua;'..package.path
local P=require('parser')
for _,text in ipairs{'покажи покупателей с задолженностью больше ста тысячами','oeffne den vierten',
  'zeige Kunden mit Schulden über fünfzig tausend','show counterparties owing over 5'} do
  assert(P.parse(text,{}),text)
end
''', text=True)

    def test_invalid_catalogs(self):
        en = '<verb source="show" means="list"/>'
        morphology = '<morphology><class name="noun" slots="sg pl"/><paradigm name="noun" class="noun">= s</paradigm></morphology>'
        cases = (
            '<strings language="en"/>',
            '<vocabulary language="en"><verbes source="show" means="list"/></vocabulary>',
            '<vocabulary language="en"><verb source="show"/></vocabulary>',
            '<vocabulary language="en"><verb source="show" means="list">  </verb><verb source="show"/></vocabulary>',
            f'<vocabulary language="en">{en}<verb source="shw">zeige</verb></vocabulary>',
            f'<vocabulary language="en">{en}<verb source="show" means="open"/></vocabulary>',
            f'<vocabulary language="en">{en}<number source="show">one</number></vocabulary>',
            '<vocabulary language="en"><number value="0.5">half</number></vocabulary>',
            '<vocabulary language="en"><scale value="0">zero</scale></vocabulary>',
            '<vocabulary language="en"><noise value="1">um</noise></vocabulary>',
            '<vocabulary language="en"><verb source="show" means="list"/><verb source="open" means="open">show</verb></vocabulary>',
            f'<vocabulary language="en">{en}<verb source="show" paradigm="noun">list</verb></vocabulary>',
            f'<vocabulary language="en">{morphology}{en}<verb source="show" paradigm="none">list</verb></vocabulary>',
            '<vocabulary language="en"><morphology><class name="noun" slots="sg pl"/>'
            '<paradigm name="noun" class="noun">=</paradigm></morphology></vocabulary>',
            '<vocabulary language="en"><morphology><paradigm name="noun" class="noun">= s</paradigm></morphology></vocabulary>',
            '<vocabulary language="en"><morphology><class name="noun" slots="sg pl"/>'
            '<paradigm name="noun" class="noun" cut="2">= s</paradigm></morphology>'
            '<noun source="go" means="items" paradigm="noun"/></vocabulary>',
            '<vocabulary language="en"><morphology><spelling from="ue" to="ü"/></morphology></vocabulary>',
            '<vocabulary language="de"><normalization><map source="ÄÖ" target="ä"/></normalization></vocabulary>',
            '<vocabulary language="de"><normalization><map source="Ä" target="ä"/>'
            '<map source="Ä" target="a"/></normalization></vocabulary>',
            '<vocabulary language="de"><compound-numbers conjunction="und"/></vocabulary>',
            '<vocabulary language="de"><number value="1" compound="ones">ein</number></vocabulary>',
            '<vocabulary language="de"><compound-numbers conjunction="und" multiplier="hundert" scale="tausend"/>'
            '<number value="20" compound="ones">zwanzig</number></vocabulary>',
            '<vocabulary language="de"><compound-numbers conjunction="und" multiplier="hundert" scale="tausend"/>'
            '<number value="1" compound="ones" synonyms="eins">ein</number></vocabulary>',
        )
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            with self.assertRaises(ValueError):
                generator.generate(directory)
            for xml in cases:
                with self.subTest(xml=xml):
                    (directory / 'en.xml').write_text(xml)
                    with self.assertRaises(ValueError):
                        generator.generate(directory)

    def test_duplicate_language(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for name in ('en.xml', 'other.xml'):
                (directory / name).write_text('<vocabulary language="en"/>')
            with self.assertRaisesRegex(ValueError, 'duplicate language'):
                generator.generate(directory)


if __name__ == '__main__':
    unittest.main()

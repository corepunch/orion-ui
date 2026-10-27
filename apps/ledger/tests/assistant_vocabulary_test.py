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
              <aliases role="verbs" value="list"><alias>display</alias></aliases>
              <aliases role="nouns" value="debtors"><alias>defaulters</alias></aliases>
              <aliases role="numbers" value="70"><alias>seventyish</alias></aliases>
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
            # Compare each XML alias against the generated Lua table, not just examples.
            checks = []
            for path in directory.glob('*.xml'):
                for group in ET.parse(path).getroot():
                    if group.tag != 'aliases':
                        continue
                    role, value = group.get('role'), group.get('value')
                    for node in group:
                        alias = json.dumps(node.text, ensure_ascii=False)
                        key = json.dumps(role)
                        if role in generator.LISTS:
                            checks.append(f'assert(has(source[{key}],{alias}))')
                        elif role in generator.NUMBERS:
                            checks.append(f'assert(source[{key}][{alias}]=={int(value)})')
                        else:
                            meaning = json.dumps(value)
                            checks.append(f'assert(has(source[{key}][{meaning}],{alias}))')
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
            for alias in root.findall('aliases/alias'):
                alias.text = replacements.get(alias.text, alias.text)
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
            shutil.copy(ADDON / 'vocabulary/de.xml', directory)
            output = directory / 'vocabulary.lua'
            output.write_text(generator.generate(directory), encoding='utf-8')
            subprocess.run([runtime, '-', str(output)], cwd=ROOT, check=True, input='''
package.path='apps/ledger/share/addons/assistant/?.lua;'..package.path
package.loaded.vocabulary=assert(loadfile(arg[1]))()
local P=require('parser')
assert(not require('vocabulary').lexicon.verbs.open[2])
local ref=assert(P.reference('den ersten'))
assert(ref.verb=='open' and ref.ordinal==1)
local ast=assert(P.parse('zeige belege für den ersten'))
assert(ast.for_party.ordinal==1 and ast.for_party.verb=='open')
assert(P.parse('zeige belege für').for_party.verb=='open')
assert(not P.parse('open first'))
''', text=True)

    def test_invalid_catalogs(self):
        cases = (
            '<strings language="en"/>',
            '<vocabulary language="en"><aliases role="verbes"><alias>show</alias></aliases></vocabulary>',
            '<vocabulary language="en"><aliases role="verbs" value="list"/></vocabulary>',
            '<vocabulary language="en"><aliases role="verbs" value="list"><alias/></aliases></vocabulary>',
            '<vocabulary language="en"><aliases role="numbers" value="0.5"><alias>half</alias></aliases></vocabulary>',
            '<vocabulary language="en"><aliases role="scales" value="0"><alias>zero</alias></aliases></vocabulary>',
            '<vocabulary language="en"><aliases role="verbs" value="list"><alias>show</alias></aliases>'
            '<aliases role="verbs" value="open"><alias>show</alias></aliases></vocabulary>',
            '<vocabulary language="de"><normalization><map source="ÄÖ" target="ä"/></normalization></vocabulary>',
            '<vocabulary language="de"><normalization><map source="Ä" target="ä"/>'
            '<map source="Ä" target="a"/></normalization></vocabulary>',
            '<vocabulary language="de"><compound-numbers conjunction="und"/></vocabulary>',
            '<vocabulary language="de"><aliases role="numbers" value="1">'
            '<alias compound="ones">ein</alias></aliases></vocabulary>',
            '<vocabulary language="de"><compound-numbers conjunction="und" multiplier="hundert" scale="tausend"/>'
            '<aliases role="numbers" value="20"><alias compound="ones">zwanzig</alias></aliases></vocabulary>',
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

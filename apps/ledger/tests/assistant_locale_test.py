"""Validate whole-sentence assistant catalogs and exercise the translation seam."""
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
ADDON = ROOT / 'apps/ledger/share/addons/assistant'


class AssistantLocaleTest(unittest.TestCase):
    def test_catalogs(self):
        sources = set()
        for name in ('parser.lua', 'engine.lua', 'assistant.lua'):
            sources.update(re.findall(r"\btr\('([^']*)'\)", (ADDON / name).read_text()))
        for node in ET.parse(ADDON / 'assistant.orion').iter():
            sources.update(node.attrib[key] for key in ('title', 'text') if key in node.attrib)
        sources.discard('№')
        runtime = shutil.which('lua5.4') or shutil.which('lua') or shutil.which('texlua')
        self.assertIsNotNone(runtime, 'Lua 5.3+ or texlua is required')
        for language in ('de', 'ru'):
            with self.subTest(language=language):
                entries = ET.parse(ADDON / 'locale' / f'{language}.xml').getroot()
                catalog = {node.attrib['source']: node.text for node in entries}
                self.assertEqual(len(catalog), len(entries), 'duplicate sources')
                self.assertFalse(sources - catalog.keys(), sources - catalog.keys())
                for source, translated in catalog.items():
                    self.assertTrue(translated, source)
                    self.assertEqual(re.findall(r'%[ds]', source), re.findall(r'%[ds]', translated))
                # json strings are valid Lua strings for these Unicode catalogs.
                table = ',\n'.join(f'[{json.dumps(k, ensure_ascii=False)}]={json.dumps(v, ensure_ascii=False)}'
                                    for k, v in catalog.items())
                script = '''package.path='apps/ledger/share/addons/assistant/?.lua;'..package.path
local catalog={%s}
local seen={}
package.preload.orion=function() return {tr=function(text)
  assert(catalog[text],text); seen[text]=true; return catalog[text]
end} end
local P,E=require('parser'),require('engine')
local _,message=P.parse('open 0')
assert(message==catalog['Use a whole row number starting at 1.'])
local e=E.new{rows=function() return {{id=1,name='Untouched Ё GmbH',debt=5}} end,
  open=function() return true end}
local r=e:submit('show customers')
assert(r.message==string.format(catalog['Found: %%d. Amounts are in the accounting currency.'],1))
r=e:submit('open first')
assert(r.message==string.format(catalog['Opened record: %%s'],'Untouched Ё GmbH'))
assert(e:submit('reset').message==catalog['Context cleared.'])
''' % table
                with tempfile.NamedTemporaryFile('w', suffix='.lua') as test:
                    test.write(script)
                    test.flush()
                    subprocess.run([runtime, test.name], cwd=ROOT, check=True)


if __name__ == '__main__':
    unittest.main()

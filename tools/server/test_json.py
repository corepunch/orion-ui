import json
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
LUA = next((shutil.which(name) for name in ('lua5.4', 'lua-5.4', 'lua') if shutil.which(name)), None)


@unittest.skipUnless(LUA, 'Lua is required')
class JsonTests(unittest.TestCase):
    def lua(self, source):
        script = "local json = dofile('share/lua/orion/json.lua')\n" + source
        result = subprocess.run([LUA, '-'], input=script, cwd=ROOT, text=True, capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.strip()

    def test_unicode_numbers_null_arrays_and_objects(self):
        source = r'''local text = '{"empty":[],"object":{},"null":null,"unicode":"\\uD83D\\uDE80","negative":-1.25e2,"integer":9223372036854775807}'
local value = json.decode(text)
assert(value.integer == math.maxinteger)
assert(value.unicode == utf8.char(0x1f680))
print(json.encode(value))'''
        result = json.loads(self.lua(source))
        self.assertEqual(result, {'empty': [], 'object': {}, 'null': None, 'unicode': '🚀', 'negative': -125, 'integer': 9223372036854775807})

    def test_reject_ambiguous_invalid_and_unbounded_data(self):
        self.lua(r'''for _, text in ipairs({'01', '-01', '1.', '+1', '1e', 'NaN', '1e999', '9223372036854775808',
  '{"x":1,"x":2}', '[1,]', '{"x":}', 'true false', '"\\uD800"', '"\\uDC00"', '"\\uD800\\u0000"', '"' .. string.char(255) .. '"',
  string.rep('[', 70) .. '0' .. string.rep(']', 70)}) do
  assert(not pcall(json.decode, text), text)
end
local recursive = {}; recursive.self = recursive
assert(not pcall(json.encode, recursive))
assert(not pcall(json.encode, {math.huge}))
assert(not pcall(json.encode, {[1] = true, [3] = true}))
assert(not pcall(json.encode, {[1] = true, x = true}))
''')

    def test_strings_remain_data(self):
        text = {'message': "\0\n\t'\\\"; os.execute('evil') --", 'bool': False}
        # JSON source is embedded as a long Lua string only inside this test.
        encoded = json.dumps(text)
        result = self.lua('print(json.encode(json.decode([==[' + encoded + ']==])))')
        self.assertEqual(json.loads(result), text)


if __name__ == '__main__':
    unittest.main()

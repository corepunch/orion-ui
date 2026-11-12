import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

VALIDATOR = Path(__file__).with_name("validate_package.lua")
LUA = next((shutil.which(name) for name in ("lua5.4", "lua-5.4", "lua") if shutil.which(name)), None)


@unittest.skipUnless(LUA, "Lua is required")
class ValidatePackageTests(unittest.TestCase):
    def validate(self, manifest, main="return function() end"):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "manifest.lua").write_text(manifest)
            (path / "main.lua").write_text(main)
            return subprocess.run([LUA, str(VALIDATOR), directory], capture_output=True, text=True, timeout=5)

    def test_sorted_manifest(self):
        result = self.validate('return {id="sample", api=1, version=2, dependencies={zeta=3, alpha=1}}')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "ROOT\tsample\t1\t2\nDEP\talpha\t1\nDEP\tzeta\t3\n")

    def test_invalid_manifest_and_factory_syntax(self):
        for source in ('return {id="../bad", api=1, version=1, dependencies={}}',
                       'return {id="a", api=1, version=1, dependencies={a=1.5}}',
                       'while true do end'):
            self.assertNotEqual(self.validate(source).returncode, 0)
        result = self.validate('return {id="a", api=1, version=1, dependencies={}}', 'return function(')
        self.assertNotEqual(result.returncode, 0)

    def test_factory_is_not_executed(self):
        result = self.validate('return {id="a", api=1, version=1, dependencies={}}', 'error("executed")')
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()

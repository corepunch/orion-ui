import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location("sdk", Path(__file__).parents[1] / "orion_sdk.py")
sdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)


class SDKTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_application_and_plugin(self):
        for kind in ("application", "plugin"):
            path = self.root / kind
            sdk.create(path, "example", kind)
            self.assertTrue(sdk.check(path))
            with self.assertRaises(FileExistsError):
                sdk.create(path, "example", kind)

    def test_reproducible_archive_and_inventory(self):
        app = self.root / "app"
        sdk.create(app, "example", "application")
        first, second = self.root / "first.zip", self.root / "second.zip"
        sdk.pack(app, first)
        sdk.pack(app, second)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        with zipfile.ZipFile(first) as archive:
            for name, digest in json.loads(archive.read("SHA256SUMS.json")).items():
                self.assertEqual(hashlib.sha256(archive.read(name)).hexdigest(), digest)
        with self.assertRaises(ValueError):
            sdk.pack(app, first)
        with self.assertRaises(ValueError):
            sdk.pack(app, app / "release.zip")

    def test_reject_escape_cycle_missing_form_and_symlink(self):
        app = self.root / "app"
        sdk.create(app, "example", "application")
        manifest = app / "application.orion"
        original = manifest.read_text()
        for include in ("../outside.orion", "application.orion"):
            manifest.write_text(original.replace("<orion>", f'<orion><include file="{include}"/>'))
            with self.assertRaises(ValueError):
                sdk.check(app)
        manifest.write_text(original.replace('form="records"', 'form="missing"'))
        with self.assertRaises(ValueError):
            sdk.check(app)
        manifest.write_text(original)
        (app / "linked.lua").symlink_to(self.root / "outside.lua")
        with self.assertRaises(ValueError):
            sdk.check(app)

    def test_reject_invalid_plugin_version_and_lua(self):
        plugin = self.root / "plugin"
        sdk.create(plugin, "example", "plugin")
        manifest = plugin / "manifest.lua"
        original = manifest.read_text()
        manifest.write_text(original.replace("version = 1,", ""))
        with self.assertRaises(subprocess.CalledProcessError):
            sdk.check(plugin)
        manifest.write_text(original)
        (plugin / "main.lua").write_text("not valid Lua!")
        with self.assertRaises(subprocess.CalledProcessError):
            sdk.check(plugin)

    def test_existing_lodge_and_bundled_plugins(self):
        self.assertTrue(sdk.check(sdk.ROOT / "apps/lodge/share"))
        for manifest in (sdk.ROOT / "share/lua/orion/plugins").glob("*/manifest.lua"):
            self.assertTrue(sdk.check(manifest.parent))


if __name__ == "__main__":
    unittest.main()

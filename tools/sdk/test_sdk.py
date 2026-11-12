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

    def test_reject_names_and_count_not_accepted_by_package_manager(self):
        app = self.root / "app"; sdk.create(app, "example", "application")
        (app / "A.lua").write_text("return true")
        collision = app / "a.lua"
        if not collision.exists():
            collision.write_text("return true")
            with self.assertRaises(ValueError): sdk.pack(app, self.root / "collision.zip")
            collision.unlink()
        (app / "A.lua").unlink()
        for name in ("bad:name.lua", "bad\\name.lua", "sha256sums.json"):
            path = app / name
            try: path.write_text("{}")
            except OSError: continue
            with self.assertRaises(ValueError): sdk.pack(app, self.root / "invalid.zip")
            self.assertFalse((self.root / "invalid.zip").exists())
            path.unlink()
        for index in range(4094): (app / f"f{index}.txt").touch()
        with self.assertRaisesRegex(ValueError, "4095"): sdk.files(app)

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

    def test_plugin_package_contract_checked_before_archive(self):
        plugin = self.root / "plugin"; sdk.create(plugin, "example", "plugin")
        package = plugin / "package.json"; original = json.loads(package.read_text())
        output = self.root / "release.zip"
        for change in ({"id": "other"}, {"interface": 2}, {"dependencies": {}}, {"runtime_api": 99}):
            info = dict(original); info.update(change); package.write_text(json.dumps(info))
            with self.assertRaises(ValueError): sdk.check(plugin)
            with self.assertRaises(ValueError): sdk.pack(plugin, output)
            self.assertFalse(output.exists())
        package.write_text(json.dumps(original))
        (plugin / "settings.lua").write_text("return {}")
        with self.assertRaisesRegex(ValueError, "reserved"): sdk.pack(plugin, output)
        (plugin / "settings.lua").unlink()
        package.unlink()
        self.assertTrue(sdk.check(plugin))
        with self.assertRaisesRegex(ValueError, "requires package.json"): sdk.pack(plugin, output)
        self.assertFalse(output.exists())

    def test_existing_lodge_and_bundled_plugins(self):
        self.assertTrue(sdk.check(sdk.ROOT / "apps/lodge/share"))
        for manifest in (sdk.ROOT / "share/lua/orion/plugins").glob("*/manifest.lua"):
            self.assertTrue(sdk.check(manifest.parent))


if __name__ == "__main__":
    unittest.main()

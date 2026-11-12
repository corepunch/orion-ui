import importlib.util
import json
import shutil
import subprocess
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result
sdk = module("sdk_tools", "orion_sdk.py")
packages = module("packages", "orion_packages.py")


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.store = self.root / "store"

    def release(self, name="example", release="0.1.0", dependencies=None):
        source = self.root / (name + release)
        sdk.create(source, name, "plugin")
        path = source / "package.json"
        info = json.loads(path.read_text()); info["release"] = release
        if dependencies is not None: info["dependencies"] = dependencies
        path.write_text(json.dumps(info))
        archive = self.root / (name + release + ".zip")
        sdk.pack(source, archive)
        return archive

    def test_install_update_disable_rollback(self):
        packages.install(self.store, self.release())
        first = packages.state(self.store)
        packages.install(self.store, self.release(release="0.2.0"))
        self.assertEqual(packages.state(self.store)["active"]["example"], "0.2.0")
        packages.select(self.store, rollback=True)
        self.assertEqual(packages.state(self.store)["active"], first["active"])
        packages.select(self.store, "example", disable=True)
        self.assertFalse(packages.state(self.store)["active"])
        self.assertTrue((self.store / "snapshots" / first["generation"] / "orion/plugins/example/main.lua").exists())

    def test_failed_activation_preserves_profile(self):
        packages.install(self.store, self.release())
        previous = (self.store / "state.json").read_bytes()
        archive = self.release(release="0.2.0")
        replace = packages.os.replace
        def fail_registry(source, destination):
            if Path(destination).name == "state.json": raise OSError("injected publication failure")
            return replace(source, destination)
        with patch.object(packages.os, "replace", side_effect=fail_registry):
            with self.assertRaises(OSError): packages.install(self.store, archive)
        self.assertEqual((self.store / "state.json").read_bytes(), previous)

    def test_configuration_survives_disable_and_lock_rejects_writers(self):
        packages.install(self.store, self.release())
        values = {"label": "Гость\n\"quoted\"", "enabled": True, "amount": 123}
        packages.configure(self.store, "example", values)
        current = packages.state(self.store)
        settings = self.store / "snapshots" / current["generation"] / "orion/plugins/example/settings.lua"
        self.assertTrue(settings.read_text().startswith("return {"))
        packages.select(self.store, "example", disable=True)
        packages.select(self.store, "example", "0.1.0")
        self.assertEqual(packages.state(self.store)["configuration"]["example"], values)
        report = json.dumps(packages.diagnose(self.store))
        self.assertNotIn("label", report)
        self.assertNotIn(str(self.store), report)
        with packages.locked(self.store):
            with self.assertRaises(OSError): packages.select(self.store, "example", disable=True)

    def test_metadata_agreement_and_configuration_rollback(self):
        packages.install(self.store, self.release())
        packages.configure(self.store, "example", {"label": "old"})
        packages.configure(self.store, "example", {"label": "new"})
        packages.select(self.store, rollback=True)
        self.assertEqual(packages.state(self.store)["configuration"]["example"]["label"], "old")
        info, contents = packages.read_archive(self.release("mismatch"))
        info["interface"] = 2
        with self.assertRaises(ValueError): packages.validate_contents(info, contents)

    @unittest.skipUnless(shutil.which("openssl"), "OpenSSL is required")
    def test_signatures_policy_and_tampering(self):
        private = self.root / "publisher.pem"; public = self.root / "publisher.pub"
        subprocess.run(["openssl", "genpkey", "-algorithm", "ED25519", "-out", str(private)], check=True, capture_output=True)
        subprocess.run(["openssl", "pkey", "-in", str(private), "-pubout", "-out", str(public)], check=True, capture_output=True)
        archive = self.release(); signature = self.root / "release.sig"
        packages.sign_archive(archive, private, signature)
        fingerprint = packages.trust_publisher(self.store, "local", public)
        self.assertEqual(len(fingerprint), 64)
        packages.set_signature_policy(self.store, "require")
        with self.assertRaisesRegex(ValueError, "signature required"): packages.install(self.store, archive)
        packages.install(self.store, archive, signature)
        current = packages.state(self.store)
        packages.verify_selected(self.store, current); packages.verify_snapshot(self.store, current)
        wrong = self.release(release="0.2.0")
        with self.assertRaisesRegex(ValueError, "signature"): packages.install(self.store, wrong, signature)
        with self.assertRaisesRegex(ValueError, "trusted"): packages.untrust_publisher(self.store, "local")
        self.assertEqual(packages.state(self.store)["active"]["example"], "0.1.0")
        snapshot = self.store / "snapshots" / current["generation"] / "orion/plugins/example/main.lua"
        snapshot.write_text("return nil")
        with self.assertRaisesRegex(ValueError, "snapshot"): packages.verify_snapshot(self.store, current)
        item = current["installed"]["example"]["0.1.0"]
        obj = self.store / "objects" / item["digest"] / "main.lua"
        obj.write_text("return nil")
        with self.assertRaisesRegex(ValueError, "checksum"): packages.verify_selected(self.store, current)
        with self.assertRaisesRegex(ValueError, "checksum"): packages.select(self.store, "example", "0.1.0")

    @unittest.skipUnless(shutil.which("openssl"), "OpenSSL is required")
    def test_wrong_key_and_modified_signature_are_rejected(self):
        keys = []
        for number in (1, 2):
            private = self.root / f"private{number}.pem"; public = self.root / f"public{number}.pem"
            subprocess.run(["openssl", "genpkey", "-algorithm", "ED25519", "-out", str(private)], check=True, capture_output=True)
            subprocess.run(["openssl", "pkey", "-in", str(private), "-pubout", "-out", str(public)], check=True, capture_output=True)
            keys.append((private, public))
        archive = self.release(); signature = self.root / "release.sig"
        packages.sign_archive(archive, keys[0][0], signature)
        packages.trust_publisher(self.store, "local", keys[1][1])
        with self.assertRaisesRegex(ValueError, "trusted"): packages.install(self.store, archive, signature)
        packages.untrust_publisher(self.store, "local")
        packages.trust_publisher(self.store, "local", keys[0][1])
        envelope = json.loads(signature.read_text()); envelope["signature"] = "A" * 88
        signature.write_text(json.dumps(envelope))
        with self.assertRaisesRegex(ValueError, "signature"): packages.install(self.store, archive, signature)
        self.assertFalse(packages.state(self.store)["active"])

    @unittest.skipUnless(shutil.which("openssl"), "OpenSSL is required")
    def test_required_policy_rejects_existing_unsigned_release(self):
        packages.install(self.store, self.release())
        previous = (self.store / "state.json").read_bytes()
        with self.assertRaisesRegex(ValueError, "signature required"):
            packages.set_signature_policy(self.store, "require")
        self.assertEqual((self.store / "state.json").read_bytes(), previous)

    def test_run_refuses_tampered_snapshot_before_launch(self):
        packages.install(self.store, self.release())
        runtime = self.root / "runtime"; binary = runtime / "bin" / "orion"
        binary.parent.mkdir(parents=True); binary.write_text("")
        lua_root = runtime / "share" / "orion" / "lua"
        shutil.copytree(ROOT.parent / "share" / "lua", lua_root)
        current = packages.state(self.store)
        snapshot = self.store / "snapshots" / current["generation"] / "orion/plugins/example/main.lua"
        snapshot.write_text("return nil")
        with patch("sys.argv", ["orion-packages", "--store", str(self.store), "run", str(binary)]), \
             patch.object(packages.shutil, "which", return_value=str(binary)), \
             patch.object(packages.subprocess, "run") as launch:
            self.assertEqual(packages.main(), 1)
            launch.assert_not_called()

    def test_dependencies_and_corrupt_archive(self):
        packages.install(self.store, self.release())
        before = (self.store / "state.json").read_bytes()
        bad = self.release("consumer", dependencies={"missing": 1})
        with self.assertRaises(ValueError): packages.install(self.store, bad)
        self.assertEqual((self.store / "state.json").read_bytes(), before)
        corrupt = self.root / "corrupt.zip"
        with zipfile.ZipFile(corrupt, "w") as archive:
            archive.writestr("../escape.lua", "return {}")
        with self.assertRaises(ValueError): packages.install(self.store, corrupt)
        self.assertFalse((self.root / "escape.lua").exists())


if __name__ == "__main__": unittest.main()

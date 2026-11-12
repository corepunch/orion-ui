import importlib.util
import json
import os
import signal
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
        if dependencies is not None:
            info["dependencies"] = dependencies
            (source / "manifest.lua").write_text("return " + packages.lua_value({
                "id": name, "api": 1, "version": info["interface"], "dependencies": dependencies}) + "\n")
        path.write_text(json.dumps(info))
        archive = self.root / (name + release + ".zip")
        if dependencies is None:
            sdk.pack(source, archive)
        else:
            # Construct a well-formed package even when the SDK cannot resolve its
            # dependencies locally; installation must independently reject it.
            with patch.object(sdk, "check", return_value=sorted(p.resolve() for p in source.rglob("*") if p.is_file())):
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

    def test_remove_release_preserves_active_and_rollback(self):
        packages.install(self.store, self.release())
        first = packages.state(self.store)
        first_digest = first["installed"]["example"]["0.1.0"]["digest"]
        packages.configure(self.store, "example", {"label": "retained"})
        packages.install(self.store, self.release(release="0.2.0"))
        before = (self.store / "state.json").read_bytes()
        for release in ("0.1.0", "0.2.0"):
            with self.assertRaisesRegex(ValueError, "cannot remove"):
                packages.remove_release(self.store, "example", release)
        self.assertEqual((self.store / "state.json").read_bytes(), before)
        packages.configure(self.store, "example", {"label": "retained"})
        with patch("sys.argv", ["orion-packages", "--store", str(self.store), "remove", "example", "0.1.0"]):
            self.assertEqual(packages.main(), 0)
        current = packages.state(self.store)
        self.assertNotIn("0.1.0", current["installed"]["example"])
        self.assertTrue((self.store / "objects" / first_digest).is_dir())
        self.assertEqual(current["configuration"]["example"]["label"], "retained")
        packages.select(self.store, rollback=True)
        self.assertEqual(packages.state(self.store)["active"]["example"], "0.2.0")
        with self.assertRaisesRegex(ValueError, "not installed"):
            packages.remove_release(self.store, "example", "0.1.0")
        packages.select(self.store, "example", disable=True)
        with self.assertRaisesRegex(ValueError, "rollback"):
            packages.remove_release(self.store, "example", "0.2.0")
        packages.configure(self.store, "example", {"label": "retained"})
        packages.remove_release(self.store, "example", "0.2.0")
        current = packages.state(self.store)
        self.assertNotIn("example", current["installed"])
        self.assertEqual(current["configuration"]["example"]["label"], "retained")
        packages.verify_snapshot(self.store, current)
        packages.install(self.store, self.root / "example0.2.0.zip")
        self.assertEqual(packages.state(self.store)["configuration"]["example"]["label"], "retained")

    def test_check_install_validates_without_publishing(self):
        packages.install(self.store, self.release())
        before = (self.store / "state.json").read_bytes()
        objects = set((self.store / "objects").iterdir())
        snapshots = set((self.store / "snapshots").iterdir())
        candidate = self.release(release="0.2.0")
        report = packages.install(self.store, candidate, check_only=True)
        self.assertEqual((report["id"], report["release"]), ("example", "0.2.0"))
        with patch("sys.argv", ["orion-packages", "--store", str(self.store), "check-install", str(candidate)]):
            self.assertEqual(packages.main(), 0)
        bad = self.release("consumer", dependencies={"missing": 1})
        with self.assertRaisesRegex(ValueError, "consumer requires missing@1"):
            packages.install(self.store, bad, check_only=True)
        self.assertEqual((self.store / "state.json").read_bytes(), before)
        self.assertEqual(set((self.store / "objects").iterdir()), objects)
        self.assertEqual(set((self.store / "snapshots").iterdir()), snapshots)
        packages.install(self.store, candidate)
        self.assertEqual(packages.state(self.store)["active"]["example"], "0.2.0")

    def test_registry_write_failure_cleans_staging_and_retains_active_profile(self):
        packages.install(self.store, self.release())
        before = (self.store / "state.json").read_bytes()
        snapshots = set((self.store / "snapshots").iterdir())
        files = set(self.store.iterdir())
        def fail_dump(value, handle, **kwargs):
            handle.write("{partial")
            raise OSError("injected write failure")
        with patch.object(packages.json, "dump", side_effect=fail_dump):
            with self.assertRaisesRegex(OSError, "write failure"):
                packages.configure(self.store, "example", {"revision": 2})
        self.assertEqual((self.store / "state.json").read_bytes(), before)
        self.assertEqual(set((self.store / "snapshots").iterdir()), snapshots)
        self.assertEqual(set(self.store.iterdir()), files)
        packages.verify_snapshot(self.store, packages.state(self.store))

    def test_final_sync_failure_reports_published_profile(self):
        packages.install(self.store, self.release())
        sync = packages.sync_directory
        def fail_final(directory):
            if directory == self.store:
                raise OSError("injected directory failure")
            sync(directory)
        with patch.object(packages, "sync_directory", side_effect=fail_final):
            with self.assertRaisesRegex(packages.PublicationUncertainError, "inspect the active profile"):
                packages.configure(self.store, "example", {"revision": 2})
        current = packages.state(self.store)
        self.assertEqual(current["configuration"]["example"], {"revision": 2})
        packages.verify_snapshot(self.store, current)

    @unittest.skipUnless(hasattr(os, "fork"), "requires POSIX process termination")
    def test_killed_publication_keeps_complete_active_snapshot(self):
        archive = self.release()
        for stage in ("snapshot_sync", "snapshot_directory", "registry_write", "registry_replace", "registry_directory"):
            for after in (False, True):
                with self.subTest(stage=stage, after=after):
                    store = self.root / (stage + str(after))
                    packages.install(store, archive)
                    previous = packages.state(store)
                    child = os.fork()
                    if child == 0:
                        def stop(): os.kill(os.getpid(), signal.SIGKILL)
                        def wrap(function, match):
                            def call(*args, **kwargs):
                                selected = match(*args, **kwargs)
                                if selected and not after: stop()
                                result = function(*args, **kwargs)
                                if selected and after: stop()
                                return result
                            return call
                        tree = wrap(packages.sync_tree, lambda path: stage == "snapshot_sync")
                        directory = wrap(packages.sync_directory, lambda path: (
                            stage == "snapshot_directory" and path == store / "snapshots") or (
                            stage == "registry_directory" and path == store))
                        dump = wrap(packages.json.dump, lambda *args, **kwargs: stage == "registry_write")
                        replace = wrap(packages.os.replace, lambda source, target: stage == "registry_replace" and Path(target) == store / "state.json")
                        with patch.object(packages, "sync_tree", tree), patch.object(packages, "sync_directory", directory), \
                             patch.object(packages.json, "dump", dump), patch.object(packages.os, "replace", replace):
                            packages.configure(store, "example", {"revision": 2})
                        os._exit(3)
                    _, status = os.waitpid(child, 0)
                    self.assertTrue(os.WIFSIGNALED(status) and os.WTERMSIG(status) == signal.SIGKILL)
                    current = packages.state(store)
                    published = stage == "registry_directory" or (stage == "registry_replace" and after)
                    if published:
                        self.assertEqual(current["configuration"]["example"], {"revision": 2})
                    else:
                        self.assertEqual(current, previous)
                    packages.verify_selected(store, current)
                    packages.verify_snapshot(store, current)

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
        with self.assertRaisesRegex(ValueError, "signature required"): packages.install(self.store, archive, check_only=True)
        self.assertEqual(packages.install(self.store, archive, signature, check_only=True)["id"], "example")
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

    def test_verify_command_detects_snapshot_damage_without_launch(self):
        packages.install(self.store, self.release())
        with patch("sys.argv", ["orion-packages", "--store", str(self.store), "verify"]):
            self.assertEqual(packages.main(), 0)
            current = packages.state(self.store)
            snapshot = self.store / "snapshots" / current["generation"] / "orion/plugins/example/main.lua"
            snapshot.write_text("return nil")
            self.assertEqual(packages.main(), 1)

    def test_run_refuses_tampered_snapshot_before_launch(self):
        packages.install(self.store, self.release())
        runtime = self.root / "runtime"; binary = runtime / "bin" / "orion"
        binary.parent.mkdir(parents=True); binary.write_text(""); binary.chmod(0o755)
        lua_root = runtime / "share" / "orion" / "lua"
        shutil.copytree(ROOT.parent / "share" / "lua", lua_root)
        current = packages.state(self.store)
        snapshot = self.store / "snapshots" / current["generation"] / "orion/plugins/example/main.lua"
        snapshot.write_text("return nil")
        run = packages.subprocess.run
        with patch("sys.argv", ["orion-packages", "--store", str(self.store), "run", str(binary)]), \
             patch.object(packages.subprocess, "run", wraps=run) as launch:
            self.assertEqual(packages.main(), 1)
            self.assertFalse(any(call.args[0][0] == str(binary) for call in launch.call_args_list))

    def test_bundled_versions_use_validated_manifest(self):
        lua_root = self.root / "lua"
        plugin = lua_root / "orion/plugins/calendar"
        plugin.mkdir(parents=True)
        (plugin / "main.lua").write_text("return {}")
        manifest = plugin / "manifest.lua"
        manifest.write_text('-- version = 99\nreturn {id="calendar", api=1, version=1+1, dependencies={}}')
        self.assertEqual(packages.builtin_versions(lua_root), {"calendar": 2})
        selection = {"active": {"external": "1.0.0"}, "installed": {"external": {"1.0.0": {
            "metadata": {"interface": 1, "dependencies": {"calendar": 1}}}}}}
        with self.assertRaisesRegex(ValueError, "calendar@1"):
            packages.validate_selection(selection, lua_root)
        manifest.write_text('return {id="other", api=1, version=1, dependencies={}}')
        with self.assertRaisesRegex(ValueError, "invalid bundled plugin manifest"):
            packages.validate_selection(selection, lua_root)
        manifest.unlink()
        with self.assertRaisesRegex(ValueError, "invalid bundled plugin manifest"):
            packages.builtin_versions(lua_root)
        with self.assertRaisesRegex(ValueError, "directory is missing"):
            packages.builtin_versions(self.root / "missing")

    def test_dependencies_and_corrupt_archive(self):
        packages.install(self.store, self.release())
        before = (self.store / "state.json").read_bytes()
        bad = self.release("consumer", dependencies={"missing": 1})
        info, contents = packages.read_archive(bad)
        packages.validate_contents(info, contents)
        objects = set((self.store / "objects").iterdir())
        with self.assertRaisesRegex(ValueError, "consumer requires missing@1"):
            packages.install(self.store, bad)
        self.assertEqual((self.store / "state.json").read_bytes(), before)
        self.assertEqual(set((self.store / "objects").iterdir()), objects)
        corrupt = self.root / "corrupt.zip"
        with zipfile.ZipFile(corrupt, "w") as archive:
            archive.writestr("../escape.lua", "return {}")
        with self.assertRaises(ValueError): packages.install(self.store, corrupt)
        self.assertFalse((self.root / "escape.lua").exists())


if __name__ == "__main__": unittest.main()

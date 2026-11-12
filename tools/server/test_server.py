import argparse
import hashlib
import io
from email.message import Message
from types import SimpleNamespace
import importlib.util
import json
from pathlib import Path
import tempfile
import sqlite3
import subprocess
import threading
import time
import unittest
from unittest.mock import patch
import sys
import urllib.error
import urllib.request

SCRIPT = Path(__file__).resolve().parents[1] / "orion_server.py"
spec = importlib.util.spec_from_file_location("orion_server", SCRIPT)
server = importlib.util.module_from_spec(spec)
spec.loader.exec_module(server)
sys.modules["orion_server"] = server
admin_spec = importlib.util.spec_from_file_location("orion_server_admin", SCRIPT.with_name("orion_server_admin.py"))
admin = importlib.util.module_from_spec(admin_spec)
admin_spec.loader.exec_module(admin)


class FakeHost:
    def __init__(self):
        self.calls = []
        self.mode = "ok"

    def __call__(self, stage, payload):
        self.calls.append(payload)
        if self.mode == "raise":
            (stage / "partial.xml").write_text("partial")
            raise OSError("injected host failure")
        if self.mode == "refuse":
            (stage / "partial.xml").write_text("partial")
            return {"ok": False, "error": "refused"}
        if self.mode == "slow":
            time.sleep(0.05)
        if payload["command"] not in server.READS or self.mode == "mutate_read":
            path = stage / "hotel.xml"
            prior = path.read_text() if path.exists() else ""
            path.write_text(prior + payload["args"].get("marker", "x"))
        return {"ok": True, "result": {"command": payload["command"]}}


class ServerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "company"
        args = argparse.Namespace(state=str(self.root), application="lodge", country="DE", executable="unused",
                                  resources="unused", configuration=None, source=None)
        with patch.object(server.Company, "run_host", return_value={"ok": True, "result": {}}):
            server.initialize(args)
        self.host = FakeHost()
        self.company = server.Company(self.root, self.host)
        self.addCleanup(self.close_company)

    def close_company(self):
        if self.company:
            self.company.close()
            self.company = None

    def reopen(self):
        self.close_company()
        self.company = server.Company(self.root, self.host)

    @staticmethod
    def request(key, revision=0, command="reserve", **args):
        return {"request_id": key, "expected_revision": revision, "command": command, "args": args}

    def refusal(self, expected_status, actor, role, request):
        with self.assertRaises(server.Refusal) as caught:
            self.company.execute(actor, role, request)
        self.assertEqual(caught.exception.status, expected_status)

    def test_initialization_rejects_mismatched_jurisdiction(self):
        target = Path(self.temp.name) / "wrong-country"
        args = argparse.Namespace(state=str(target), application="ledger", country="DE", configuration="ru-2026",
                                  executable="unused", resources="unused", source=None)
        with self.assertRaises(ValueError):
            server.initialize(args)
        self.assertFalse(target.exists())

    def test_company_identity_is_stable_and_new_companies_are_distinct(self):
        identity = self.company.company_id
        self.reopen(); self.assertEqual(self.company.company_id, identity)
        other = self.root.parent / "other-company"
        args = argparse.Namespace(state=str(other), application="lodge", country="DE", executable="unused",
                                  resources="unused", configuration=None, source=None)
        with patch.object(server.Company, "run_host", return_value={"ok": True, "result": {}}): server.initialize(args)
        second = server.Company(other, self.host)
        try: self.assertNotEqual(second.company_id, identity)
        finally: second.close()
        self.company.db.execute("DROP TABLE company_identity")
        self.reopen(); legacy_identity = self.company.company_id
        self.assertNotEqual(legacy_identity, identity)
        self.reopen(); self.assertEqual(self.company.company_id, legacy_identity)

    def test_cas_and_idempotency_actor_isolation(self):
        request = self.request("order1", marker="A")
        first = self.company.execute("alice", "reception", request)
        self.assertEqual(first["revision"], 1)
        self.assertEqual(self.company.execute("alice", "reception", request), first)
        self.assertEqual(len(self.host.calls), 1)
        self.refusal(409, "alice", "reception", self.request("order1", marker="B"))
        self.refusal(409, "bob", "reception", request)
        second = self.company.execute("bob", "reception", self.request("order1", 1, marker="B"))
        self.assertEqual(second["revision"], 2)
        self.assertEqual(len(self.host.calls), 2)

    def test_host_failure_does_not_publish(self):
        self.host.mode = "raise"
        self.refusal(503, "alice", "reception", self.request("failed"))
        self.assertEqual(self.company.status()["revision"], 0)
        self.assertEqual([p.name for p in (self.root / "snapshots").iterdir()], ["initial"])
        self.host.mode = "refuse"
        result = self.company.execute("alice", "reception", self.request("refused"))
        self.assertFalse(result["ok"])
        self.assertEqual(self.company.status()["revision"], 0)
        self.assertFalse((self.root / "snapshots" / "initial" / "partial.xml").exists())

    def test_process_crash_before_and_after_commit(self):
        self.close_company()
        child = r"""
import os, sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
import orion_server as server
mode = sys.argv[3]
def run(stage, payload):
    (stage / "hotel.xml").write_text("committed company data")
    if mode == "before": os._exit(17)
    return {"ok": True, "result": {"id": 42}}
company = server.Company(sys.argv[2], run)
if mode == "after":
    class CrashAfterCommit:
        def __init__(self, db): self.db = db
        def execute(self, sql, *args):
            result = self.db.execute(sql, *args)
            if sql == "COMMIT": os._exit(18)
            return result
        @property
        def in_transaction(self): return self.db.in_transaction
    company.db = CrashAfterCommit(company.db)
company.execute("alice", "reception", {"request_id": "crash", "expected_revision": 0, "command": "reserve", "args": {}})
"""
        for mode, code in (("before", 17), ("after", 18)):
            result = subprocess.run([sys.executable, "-c", child, str(SCRIPT.parent), str(self.root), mode], timeout=10)
            self.assertEqual(result.returncode, code)
            self.reopen()
            if mode == "before":
                self.assertEqual(self.company.status()["revision"], 0)
                self.assertEqual([p.name for p in (self.root / "snapshots").iterdir()], ["initial"])
                self.close_company()
            else:
                self.assertEqual(self.company.status()["revision"], 1)
                receipt = self.company.execute("alice", "reception", self.request("crash"))
                self.assertEqual(receipt["result"], {"id": 42})
                self.assertEqual(len(self.host.calls), 0)

    def test_read_is_pure_and_roles_are_enforced(self):
        self.refusal(403, "cleaner", "housekeeping", self.request("no", command="reserve"))
        self.assertEqual(len(self.host.calls), 0)
        denied = self.company.db.execute("SELECT actor,command,outcome,revision FROM audit ORDER BY id DESC LIMIT 1").fetchone()
        self.assertEqual(denied, ("cleaner", "reserve", "denied", 0))
        self.host.mode = "mutate_read"
        self.refusal(502, "alice", "reception", self.request("read", command="snapshot"))
        self.assertEqual(self.company.status()["revision"], 0)
        self.assertFalse((self.root / "snapshots" / "initial" / "hotel.xml").exists())

    def test_failed_metadata_commit_discards_stage_and_receipt(self):
        connection = self.company.db
        class FaultConnection:
            def __init__(self, inner): self.inner = inner
            def execute(self, sql, *args):
                if sql == "COMMIT": raise sqlite3.OperationalError("injected commit failure")
                return self.inner.execute(sql, *args)
            @property
            def in_transaction(self): return self.inner.in_transaction
            def close(self): self.inner.close()
        self.company.db = FaultConnection(connection)
        with self.assertRaises(sqlite3.OperationalError):
            self.company.execute("alice", "reception", self.request("failure"))
        self.assertEqual(self.company.status()["revision"], 0)
        self.assertEqual([p.name for p in (self.root / "snapshots").iterdir()], ["initial"])
        self.reopen()
        self.assertEqual(self.company.status()["revision"], 0)
        self.assertEqual(self.company.db.execute("SELECT COUNT(*) FROM receipts").fetchone()[0], 0)
        self.assertTrue(self.company.execute("alice", "reception", self.request("failure"))["ok"])

    def test_reopen_receipt_and_recovery_of_uncommitted_stage(self):
        request = self.request("saved")
        result = self.company.execute("alice", "reception", request)
        stray = self.root / "snapshots" / "revision-interrupted"
        stray.mkdir(); (stray / "partial.xml").write_text("partial")
        self.reopen()
        self.assertFalse(stray.exists())
        self.assertEqual(self.company.execute("alice", "reception", request), result)
        self.assertEqual(len(self.host.calls), 1)
        self.assertEqual(self.company.status()["revision"], 1)

    def test_corrupted_snapshot_is_rejected(self):
        self.company.execute("alice", "reception", self.request("first"))
        current = self.company.db.execute("SELECT snapshot FROM current").fetchone()[0]
        (self.root / "snapshots" / current / "hotel.xml").write_text("tampered")
        self.refusal(503, "alice", "reception", self.request("second", 1))
        self.assertEqual(self.company.status()["revision"], 1)

    def test_simultaneous_compare_and_swap(self):
        self.host.mode = "slow"
        start = threading.Barrier(3)
        results = []
        def submit(actor):
            start.wait()
            try:
                results.append(self.company.execute(actor, "reception", self.request(actor)))
            except server.Refusal as error:
                results.append(error.status)
        threads = [threading.Thread(target=submit, args=(name,)) for name in ("alice", "bob")]
        for thread in threads: thread.start()
        start.wait()
        for thread in threads: thread.join(timeout=5); self.assertFalse(thread.is_alive())
        self.assertEqual(sum(isinstance(result, dict) for result in results), 1)
        self.assertIn(409, results)
        self.assertEqual(len(self.host.calls), 1)
        self.assertEqual(self.company.status()["revision"], 1)

    def test_handler_authentication_and_command_permissions(self):
        token = "test-token-with-entropy"
        users = [{"id": "cleaner", "role": "housekeeping", "token_sha256": hashlib.sha256(token.encode()).hexdigest()}]
        (self.root / "users.json").write_text(json.dumps(users))
        def invoke(token_value, payload=None):
            handler = object.__new__(server.Handler)
            handler.headers = Message()
            if token_value: handler.headers["Authorization"] = "Bearer " + token_value
            body = json.dumps(payload).encode() if payload is not None else b""
            if payload is not None: handler.headers["Content-Length"] = str(len(body))
            handler.rfile = io.BytesIO(body)
            handler.connection = SimpleNamespace(settimeout=lambda value: None)
            handler.path = "/v1/command" if payload is not None else "/v1/status"
            handler.server = SimpleNamespace(company=self.company)
            result = []
            handler.reply = lambda status, value: result.append((status, value))
            handler.perform(payload is not None)
            return result[0]
        self.assertEqual(invoke(None)[0], 401)
        self.assertEqual(invoke("invalid")[0], 401)
        status, details = invoke(token)
        self.assertEqual(status, 200); self.assertEqual(details["actor"], "cleaner")
        self.assertEqual(details["company_id"], self.company.company_id)
        self.assertEqual(invoke(token, self.request("denied"))[0], 403)
        self.assertEqual(len(self.host.calls), 0)
        (self.root / "users.json").write_text("[]")
        self.assertEqual(invoke(token)[0], 401)

    def test_offline_backup_verify_restore_and_no_credentials(self):
        request = self.request("saved")
        self.company.execute("alice", "reception", request)
        identity = self.company.company_id
        (self.root / "users.json").write_text('[{"id":"alice","token_sha256":"secret"}]')
        self.close_company()
        backup = self.root.parent / "backup"
        admin.backup(self.root, backup)
        info = admin.verify(backup)
        self.assertEqual(info["revision"], 1)
        self.assertFalse((backup / "users.json").exists())
        self.assertNotIn("token_sha256", (backup / "manifest.json").read_text())
        restored = self.root.parent / "restored"
        admin.restore(backup, restored, sys.executable, self.root)
        self.assertEqual(json.loads((restored / "users.json").read_text()), [])
        restored_company = server.Company(restored, self.host)
        try:
            self.assertEqual(restored_company.status()["revision"], 1)
            self.assertEqual(restored_company.company_id, identity)
            self.assertEqual(restored_company.execute("alice", "reception", request)["revision"], 1)
        finally:
            restored_company.close()
        with server.exclusive(self.root / "server.lock"):
            with self.assertRaises(OSError): admin.backup(self.root, self.root.parent / "blocked")
        self.assertFalse((self.root.parent / "blocked").exists())

    def test_backup_verification_detects_snapshot_and_audit_tampering(self):
        self.company.execute("alice", "reception", self.request("saved")); self.close_company()
        backup = self.root.parent / "backup"; admin.backup(self.root, backup)
        info = admin.verify(backup)
        current = next(name for name in info["snapshots"] if name != "initial")
        path = backup / "snapshots" / current / "hotel.xml"
        original = path.read_text(); path.write_text("tampered")
        with self.assertRaises(ValueError): admin.verify(backup)
        path.write_text(original)
        db_path = backup / "state.sqlite"
        with sqlite3.connect(db_path) as db: db.execute("UPDATE audit SET actor='mallory'")
        info["database_sha256"] = admin.hash_file(db_path)
        (backup / "manifest.json").write_text(json.dumps(info))
        with self.assertRaisesRegex(ValueError, "audit chain"): admin.verify(backup)

    def test_http_requires_token_and_rechecks_revocation(self):
        token = "high-entropy-test-token"
        users = [{"id": "alice", "role": "reception", "token_sha256": hashlib.sha256(token.encode()).hexdigest()}]
        (self.root / "users.json").write_text(json.dumps(users))
        try:
            httpd = server.http.server.ThreadingHTTPServer(("127.0.0.1", 0), server.Handler)
        except PermissionError:
            self.skipTest("sandbox does not permit local TCP listeners")
        httpd.company = self.company
        worker = threading.Thread(target=httpd.serve_forever, daemon=True); worker.start()
        self.addCleanup(lambda: (httpd.shutdown(), httpd.server_close(), worker.join()))
        url = f"http://127.0.0.1:{httpd.server_port}/v1/status"
        with self.assertRaises(urllib.error.HTTPError) as caught: urllib.request.urlopen(url, timeout=3)
        self.assertEqual(caught.exception.code, 401)
        request = urllib.request.Request(url, headers={"Authorization": "Bearer " + token})
        with urllib.request.urlopen(request, timeout=3) as response:
            self.assertEqual(json.load(response)["revision"], 0)
        (self.root / "users.json").write_text("[]")
        with self.assertRaises(urllib.error.HTTPError) as caught: urllib.request.urlopen(request, timeout=3)
        self.assertEqual(caught.exception.code, 401)


if __name__ == "__main__":
    unittest.main()

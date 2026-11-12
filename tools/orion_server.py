#!/usr/bin/env python3
"""Single-company business command server, with durable snapshot publication.

Only reviewed host executables are invoked. The network cannot supply code,
paths, actors or roles. Clients never open the company's XML files.
"""
from __future__ import annotations
import argparse
import contextlib
import hashlib
import hmac
import http.server
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import sqlite3
import ssl
import stat
import subprocess
import sys
import tempfile
import threading
import time

MAX_REQUEST = 1024 * 1024
MAX_RESPONSE = 4 * 1024 * 1024
MAX_COMPANY = 32 * 1024 * 1024
IDENTIFIER = re.compile(r"[a-zA-Z0-9][a-zA-Z0-9_.:-]{0,99}\Z")
POLICIES = {
    "lodge": {
        "snapshot": {"admin", "reception", "housekeeping", "auditor"},
        **{c: {"admin", "reception"} for c in (
            "save_room", "save_guest", "delete_room", "delete_guest", "reserve", "check_in", "check_out", "cancel", "payment", "set_night", "reset_night", "group_reserve", "group_cancel", "group_settle")},
        "set_room_cleaning": {"admin", "housekeeping"},
        "day_preview": {"admin", "reception", "auditor"}, "close_day": {"admin"},
        "configure_hotel": {"admin"},
    },
    "ledger": {
        **{c: {"admin", "accountant", "auditor"} for c in ("documents.list", "documents.lines", "documents.history", "documents.history_entries", "journal.list", "catalogs.list", "period.history")},
        **{c: {"admin", "accountant"} for c in ("documents.create", "documents.update", "documents.delete", "documents.post", "documents.reverse", "documents.replace_lines", "counterparties.save", "items.save")},
        "period.close": {"admin"},
    },
}
for _commands in POLICIES.values():
    _commands["maintenance.upgrade"] = {"admin"}
READS = {"snapshot", "day_preview", "documents.list", "documents.lines", "documents.history", "documents.history_entries", "journal.list", "catalogs.list", "period.history"}


def canonical(value):
    return json.dumps(value, ensure_ascii=False, allow_nan=False, sort_keys=True, separators=(",", ":"))


def decode(data):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("duplicate JSON key")
            result[key] = value
        return result
    def invalid(_):
        raise ValueError("non-finite JSON number")
    return json.loads(data, object_pairs_hook=pairs, parse_constant=invalid)


def load_users(root, application):
    try:
        users = decode((root / "users.json").read_bytes())
    except (ValueError, UnicodeError, RecursionError) as error:
        raise ValueError("invalid user directory") from error
    roles = set().union(*POLICIES[application].values())
    if not isinstance(users, list):
        raise ValueError("invalid user directory")
    ids, fingerprints = set(), set()
    for user in users:
        if (not isinstance(user, dict) or not isinstance(user.get("id"), str) or
                not IDENTIFIER.fullmatch(user["id"]) or user["id"] in ids or
                not isinstance(user.get("role"), str) or user["role"] not in roles or
                not isinstance(user.get("token_sha256"), str) or
                not re.fullmatch(r"[0-9a-f]{64}", user["token_sha256"]) or
                user["token_sha256"] in fingerprints):
            raise ValueError("invalid user directory")
        ids.add(user["id"]); fingerprints.add(user["token_sha256"])
    return users


def sync_directory(path):
    if os.name != "nt":
        fd = os.open(path, os.O_RDONLY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)


def private_write(path, data):
    with open(path, "x", encoding="utf-8", opener=lambda p, f: os.open(p, f, 0o600)) as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


@contextlib.contextmanager
def exclusive(path):
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    with open(path, "a+b") as stream:
        if os.name == "nt":
            import msvcrt
            stream.seek(0)
            if not stream.read(1):
                stream.write(b"\0"); stream.flush()
            stream.seek(0)
            msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        try:
            yield
        finally:
            if os.name == "nt":
                stream.seek(0); msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(stream, fcntl.LOCK_UN)


def files(directory, changing=False):
    total, count = 0, 0
    for parent, directories, names in os.walk(directory, followlinks=False):
        directories.sort(); names.sort()
        if len(Path(parent).relative_to(directory).parts) > 16:
            raise ValueError("company directory nesting limit")
        count += len(directories) + len(names)
        if count > 4096:
            raise ValueError("company file count limit")
        for name in directories + names:
            path = Path(parent) / name
            try:
                info = path.lstat()
            except FileNotFoundError:
                if changing: continue
                raise
            if stat.S_ISLNK(info.st_mode):
                raise ValueError("company data cannot contain symbolic links")
            if name in directories:
                if not stat.S_ISDIR(info.st_mode):
                    raise ValueError("company data must contain ordinary files")
                continue
            if not stat.S_ISREG(info.st_mode):
                raise ValueError("company data must contain ordinary files")
            total += info.st_size
            if total > MAX_COMPANY * 2:
                raise ValueError("company working directory exceeds 64 MiB")
            if not name.endswith((".lock", ".bak")):
                yield path


def run_limited(command, environment, stage):
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment)
    result, overflow = [bytearray(), bytearray()], threading.Event()
    def read(stream, target):
        try:
            while True:
                chunk = stream.read(8192)
                if not chunk:
                    break
                if len(target) + len(chunk) > MAX_RESPONSE:
                    overflow.set(); process.kill(); break
                target.extend(chunk)
        finally:
            stream.close()
    readers = [threading.Thread(target=read, args=(stream, result[i]), daemon=True)
               for i, stream in enumerate((process.stdout, process.stderr))]
    for reader in readers:
        reader.start()
    deadline = time.monotonic() + 120
    try:
        while process.poll() is None:
            if time.monotonic() > deadline:
                raise Refusal(504, "Business host timed out; no changes published")
            # Trusted hosts only; production additionally needs an OS disk quota.
            for _ in files(stage, changing=True):
                pass
            time.sleep(0.05)
    finally:
        if process.poll() is None:
            process.kill()
        process.wait()
        for reader in readers:
            reader.join(timeout=2)
    if overflow.is_set() or process.returncode or any(reader.is_alive() for reader in readers):
        raise Refusal(502, "Business host failed or exceeded output limits; no changes published")
    return bytes(result[0])


def copy_company(source, target):
    for path in files(source):
        dest = target / path.relative_to(source)
        dest.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        shutil.copyfile(path, dest)
        os.chmod(dest, 0o600)


def seal(directory):
    digest = hashlib.sha256()
    total = 0
    for path in files(directory):
        total += path.stat().st_size
        if total > MAX_COMPANY:
            raise ValueError("company snapshot exceeds 32 MiB")
        name = path.relative_to(directory).as_posix().encode()
        digest.update(len(name).to_bytes(4, "big")); digest.update(name)
        with path.open("rb") as stream:
            content = stream.read(MAX_COMPANY + 1)
            digest.update(len(content).to_bytes(8, "big")); digest.update(content)
            os.fsync(stream.fileno())
    for path in sorted((p for p in directory.rglob("*") if p.is_dir()), reverse=True):
        sync_directory(path)
    sync_directory(directory)
    return digest.hexdigest()


class Refusal(Exception):
    def __init__(self, status, message):
        self.status, self.message = status, message


class Company:
    def __init__(self, root, runner=None):
        self.root = Path(root).resolve()
        self.config = decode((self.root / "server.json").read_bytes())
        self.kind = self.config["application"]
        if self.kind not in POLICIES or self.config["country"] not in ("RU", "DE"):
            raise ValueError("unsupported application or jurisdiction")
        self.lock = threading.RLock()
        self.runner = runner or self.run_host
        self.db = sqlite3.connect(self.root / "state.sqlite", check_same_thread=False, isolation_level=None)
        self.db.execute("PRAGMA synchronous=FULL")
        self.db.execute("PRAGMA foreign_keys=ON")
        self.db.execute("CREATE TABLE IF NOT EXISTS current (id INTEGER PRIMARY KEY CHECK(id=1), revision INTEGER, snapshot TEXT, digest TEXT, previous TEXT)")
        self.db.execute("CREATE TABLE IF NOT EXISTS company_identity (id INTEGER PRIMARY KEY CHECK(id=1), company_id TEXT NOT NULL)")
        self.db.execute("INSERT OR IGNORE INTO company_identity(id,company_id) VALUES(1,?)", (secrets.token_hex(16),))
        identity = self.db.execute("SELECT company_id FROM company_identity WHERE id=1").fetchone()
        if not identity or not isinstance(identity[0], str) or not re.fullmatch(r"[0-9a-f]{32}", identity[0]):
            raise ValueError("invalid company identity")
        self.company_id = identity[0]
        self.db.execute("CREATE TABLE IF NOT EXISTS receipts (actor TEXT, request_id TEXT, fingerprint TEXT, response TEXT, PRIMARY KEY(actor,request_id))")
        self.db.execute("CREATE TABLE IF NOT EXISTS audit (id INTEGER PRIMARY KEY, utc TEXT, actor TEXT, command TEXT, outcome TEXT, revision INTEGER, previous TEXT, digest TEXT)")
        os.chmod(self.root / "state.sqlite", 0o600)
        if not self.db.execute("SELECT 1 FROM current").fetchone():
            initial = self.root / "snapshots" / "initial"
            self.db.execute("INSERT INTO current VALUES(1,0,?,?,NULL)", ("initial", seal(initial)))
        self.cleanup()

    def close(self):
        self.db.close()

    def cleanup(self):
        row = self.db.execute("SELECT snapshot,previous FROM current").fetchone()
        for path in (self.root / "snapshots").iterdir():
            if path.name not in row and path.is_dir() and not path.is_symlink():
                shutil.rmtree(path)

    def authenticate(self, token):
        if not token or len(token) > 256:
            raise Refusal(401, "Authentication required")
        fingerprint = hashlib.sha256(token.encode()).hexdigest()
        # Reload credentials on every request so revocation is immediate.
        try:
            credentials = load_users(self.root, self.kind)
        except (ValueError, TypeError, UnicodeError, RecursionError):
            print("[server] invalid user directory", file=sys.stderr, flush=True)
            raise Refusal(503, "User directory unavailable")
        for user in credentials:
            if hmac.compare_digest(user["token_sha256"], fingerprint):
                return user["id"], user["role"]
        raise Refusal(401, "Authentication required")

    def audit(self, actor, command, outcome, revision):
        row = self.db.execute("SELECT digest FROM audit ORDER BY id DESC LIMIT 1").fetchone()
        previous = row[0] if row else "0" * 64
        stamp = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        digest = hashlib.sha256(canonical([stamp, actor, command, outcome, revision, previous]).encode()).hexdigest()
        self.db.execute("INSERT INTO audit(utc,actor,command,outcome,revision,previous,digest) VALUES(?,?,?,?,?,?,?)",
                        (stamp, actor, command, outcome, revision, previous, digest))

    def status(self):
        with self.lock:
            revision = self.db.execute("SELECT revision FROM current").fetchone()[0]
            return {"protocol": 1, "application": self.kind, "country": self.config["country"],
                    "company_id": self.company_id, "revision": revision}

    def execute(self, actor, role, request):
        if not isinstance(request, dict) or set(request) != {"request_id", "expected_revision", "command", "args"}:
            raise Refusal(400, "Expected request_id, expected_revision, command and args")
        key, expected, command, args = (request[k] for k in ("request_id", "expected_revision", "command", "args"))
        if not isinstance(key, str) or not IDENTIFIER.fullmatch(key) or type(expected) is not int or expected < 0 or not isinstance(command, str) or not isinstance(args, dict):
            raise Refusal(400, "Invalid request fields")
        fingerprint = hashlib.sha256(canonical([role, request]).encode()).hexdigest()
        with self.lock:
            revision, current, digest = self.db.execute("SELECT revision,snapshot,digest FROM current").fetchone()
            if role not in POLICIES[self.kind].get(command, set()):
                self.audit(actor, command[:100], "denied", revision)
                raise Refusal(403, "Permission denied")
            receipt = self.db.execute("SELECT fingerprint,response FROM receipts WHERE actor=? AND request_id=?", (actor, key)).fetchone()
            if receipt:
                if not hmac.compare_digest(receipt[0], fingerprint):
                    raise Refusal(409, "Request ID was already used for different parameters")
                return decode(receipt[1])
            if expected != revision:
                raise Refusal(409, "Data changed; refresh before submitting a new request")
            if seal(self.root / "snapshots" / current) != digest:
                raise Refusal(503, "Snapshot integrity check failed; administrator recovery required")
            stage = Path(tempfile.mkdtemp(prefix="revision-", dir=self.root / "snapshots"))
            committed = False
            try:
                copy_company(self.root / "snapshots" / current, stage)
                host_args = dict(args)
                if self.kind == "lodge" and command == "payment":
                    payment_key = host_args.get("request_key")
                    if not isinstance(payment_key, str) or not payment_key.strip() or len(payment_key) > 100:
                        raise Refusal(400, "Payment requires a stable request_key of 1–100 characters")
                    host_args["request_key"] = "server:" + hashlib.sha256(canonical([actor, payment_key]).encode()).hexdigest()
                if self.kind == "lodge" and command == "group_settle":
                    group_key = host_args.get("request_key")
                    if not isinstance(group_key, str) or not re.fullmatch(r"[a-zA-Z0-9_.-]{1,70}", group_key):
                        raise Refusal(400, "Group settlement requires a stable request_key of 1–70 ASCII letters, digits, dots, underscores or hyphens")
                    host_args["request_key"] = "srv-" + hashlib.sha256(canonical([actor, group_key]).encode()).hexdigest()
                payload = {"command": command, "args": host_args, "actor": actor, "role": role}
                response = self.runner(stage, payload)
                if not isinstance(response, dict) or type(response.get("ok")) is not bool:
                    raise Refusal(502, "Invalid business host response")
                if not response["ok"]:
                    self.audit(actor, command, "refused", revision)
                    return {"ok": False, "error": str(response.get("error", "Operation refused")), "revision": revision}
                new_digest = seal(stage)
                if command in READS and new_digest != digest:
                    raise Refusal(502, "Read command attempted to change company data")
                changed = command not in READS
                result = {"ok": True, "result": response.get("result"), "revision": revision + int(changed), "request_id": key}
                encoded = canonical(result)
                if len(encoded.encode()) > MAX_RESPONSE:
                    raise Refusal(502, "Business response is too large")
                sync_directory(stage.parent)
                self.db.execute("BEGIN IMMEDIATE")
                try:
                    if changed:
                        self.db.execute("UPDATE current SET revision=?,snapshot=?,digest=?,previous=? WHERE id=1", (revision + 1, stage.name, new_digest, current))
                    self.db.execute("INSERT INTO receipts VALUES(?,?,?,?)", (actor, key, fingerprint, encoded))
                    self.audit(actor, command, "committed" if changed else "read", result["revision"])
                    self.db.execute("COMMIT")
                    committed = changed
                except BaseException:
                    if self.db.in_transaction:
                        self.db.execute("ROLLBACK")
                    # A failed COMMIT can have an uncertain outcome. Never remove
                    # a snapshot that may be named by durable metadata.
                    committed = self.db.execute("SELECT snapshot FROM current").fetchone()[0] == stage.name
                    raise
                if changed:
                    self.cleanup()
                return result
            except (OSError, ValueError, subprocess.SubprocessError) as error:
                print(f"[server] host/storage failure: {type(error).__name__}", file=sys.stderr)
                raise Refusal(503, "Command could not be completed; retry with the same request ID") from error
            finally:
                if not committed and stage.exists():
                    shutil.rmtree(stage)

    def run_host(self, stage, payload):
        upgrade = payload["command"] == "maintenance.upgrade"
        if upgrade:
            if payload["args"]:
                raise Refusal(400, "Upgrade does not accept parameters")
            payload = {**payload, "command": "snapshot" if self.kind == "lodge" else "documents.list"}
        with tempfile.TemporaryDirectory(prefix="request-", dir=self.root) as directory:
            request_path = Path(directory) / "request.json"
            private_write(request_path, canonical(payload))
            if self.kind == "lodge":
                command = [self.config["executable"], "--application", self.config["resources"], "--data", str(stage),
                           "--request", str(request_path), "--actor", payload["actor"], "--role", payload["role"]]
            else:
                command = [self.config["executable"], "--share", self.config["resources"], "--data", str(stage),
                           "--config", self.config["configuration"], "--request", str(request_path), "--write"]
            environment = os.environ.copy()
            environment.pop("ORION_PLUGIN_ROOT", None)
            result = decode(run_limited(command, environment, stage))
            if upgrade and isinstance(result, dict) and result.get("ok") is True:
                return {"ok": True, "result": {"application": self.kind, "validated": True}}
            return result



class Handler(http.server.BaseHTTPRequestHandler):
    server_version = "Orion/1"
    sys_version = ""

    def setup(self):
        self.request.settimeout(15)
        super().setup()

    def log_message(self, *_):
        pass  # No tokens, company records or routine requests in process logs.

    def reply(self, status, value):
        data = canonical(value).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(data)

    def perform(self, write):
        try:
            self.connection.settimeout(15)
            header = self.headers.get("Authorization", "")
            token = header[7:] if header.startswith("Bearer ") else ""
            self.server.company.authenticate(token)
            if not write and self.path == "/v1/status":
                with self.server.company.lock:
                    actor, role = self.server.company.authenticate(token)
                    result = {**self.server.company.status(), "actor": actor, "role": role,
                              "commands": sorted(command for command, roles in POLICIES[self.server.company.kind].items() if role in roles)}
                self.reply(200, result); return
            if not write or self.path != "/v1/command":
                raise Refusal(404, "Unknown endpoint")
            if self.headers.get("Transfer-Encoding") or len(self.headers.get_all("Content-Length", [])) != 1:
                raise Refusal(400, "A single Content-Length is required")
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > MAX_REQUEST:
                raise Refusal(413, "Invalid request size")
            body = self.rfile.read(length)
            if len(body) != length:
                raise Refusal(400, "Incomplete request")
            request = decode(body)
            with self.server.company.lock:
                actor, role = self.server.company.authenticate(token)
                result = self.server.company.execute(actor, role, request)
            self.reply(200, result)
        except Refusal as error:
            if error.status in (401, 403):
                print(f"[server] access denied status={error.status}", file=sys.stderr)
            self.reply(error.status, {"ok": False, "error": error.message})
        except (ValueError, TypeError, RecursionError, UnicodeError):
            self.reply(400, {"ok": False, "error": "Invalid JSON request"})
        except (OSError, sqlite3.Error):
            self.reply(503, {"ok": False, "error": "Storage unavailable; retry with the same request ID"})

    def do_GET(self):
        self.perform(False)

    def do_POST(self):
        self.perform(True)


class BusinessServer(http.server.ThreadingHTTPServer):
    daemon_threads = False
    def __init__(self, address, handler):
        self.slots = threading.BoundedSemaphore(16)
        super().__init__(address, handler)

    def process_request(self, request, address):
        if not self.slots.acquire(blocking=False):
            self.shutdown_request(request)
            return
        try:
            super().process_request(request, address)
        except BaseException:
            self.slots.release()
            raise

    def process_request_thread(self, request, address):
        try:
            super().process_request_thread(request, address)
        finally:
            self.slots.release()


def initialize(args):
    configuration = args.configuration or ("ru-2026" if args.country == "RU" else "de-2026")
    if args.application == "ledger" and (not IDENTIFIER.fullmatch(configuration) or not configuration.startswith(args.country.lower() + "-")):
        raise ValueError("Ledger configuration must match the selected country")
    root = Path(args.state).resolve()
    if root.exists():
        raise ValueError("state directory must be new")
    root.mkdir(mode=0o700, parents=True)
    initial = root / "snapshots" / "initial"
    initial.mkdir(parents=True, mode=0o700)
    if args.source:
        copy_company(Path(args.source).resolve(), initial)
    config = {"application": args.application, "country": args.country,
              "executable": str(Path(args.executable).resolve()), "resources": str(Path(args.resources).resolve()),
              "configuration": configuration}
    private_write(root / "server.json", canonical(config))
    private_write(root / "users.json", "[]")
    company = Company(root)
    try:
        # Complete schema/model adoption before exposing a revision to readers.
        if args.application == "lodge":
            configured = company.run_host(initial, {"command": "configure_hotel", "args": {
                "country": args.country, "currency": "EUR" if args.country == "DE" else "RUB"},
                "actor": "bootstrap", "role": "admin"})
            if not isinstance(configured, dict) or configured.get("ok") is not True:
                raise ValueError("hotel country/currency adoption failed")
        command = "snapshot" if args.application == "lodge" else "documents.list"
        result = company.run_host(initial, {"command": command, "args": {}, "actor": "bootstrap", "role": "admin"})
        if not isinstance(result, dict) or result.get("ok") is not True:
            raise ValueError("initial business validation failed")
        company.db.execute("UPDATE current SET digest=? WHERE id=1", (seal(initial),))
    finally:
        company.close()
    private_write(root / "READY", "1\n")
    sync_directory(root)


def add_user(args):
    root = Path(args.state).resolve()
    with exclusive(root / "users.lock"):
        config = decode((root / "server.json").read_bytes())
        if not IDENTIFIER.fullmatch(args.user) or args.role not in set().union(*POLICIES[config["application"]].values()):
            raise ValueError("invalid user or role")
        users = load_users(root, config["application"])
        users = [user for user in users if user["id"] != args.user]
        if not args.revoke:
            token = secrets.token_urlsafe(32)
            users.append({"id": args.user, "role": args.role, "token_sha256": hashlib.sha256(token.encode()).hexdigest()})
        temporary = root / ("users-" + secrets.token_hex(8) + ".json")
        private_write(temporary, canonical(users)); os.replace(temporary, root / "users.json"); sync_directory(root)
        if not args.revoke:
            print(token)  # Deliberate one-time credential output to the operator.



def list_users(args):
    root = Path(args.state).resolve()
    config = decode((root / "server.json").read_bytes())
    users = load_users(root, config["application"])
    result = [{"id": user["id"], "role": user["role"]} for user in users]
    print(json.dumps(sorted(result, key=lambda user: user["id"]), ensure_ascii=False, indent=2))


def serve(args):
    if not (Path(args.state) / "READY").is_file():
        raise ValueError("server initialization has not completed")
    if args.bind not in ("127.0.0.1", "localhost") and not (args.cert and args.key):
        raise ValueError("remote connections require --cert and --key for TLS")
    with exclusive(Path(args.state) / "server.lock"):
        company = Company(args.state)
        server = BusinessServer((args.bind, args.port), Handler)
        server.company = company
        if args.cert or args.key:
            if not args.cert or not args.key:
                raise ValueError("both TLS certificate and key are required")
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.minimum_version = ssl.TLSVersion.TLSv1_2
            context.load_cert_chain(args.cert, args.key)
            server.socket = context.wrap_socket(server.socket, server_side=True, do_handshake_on_connect=False)
        try:
            server.serve_forever()
        finally:
            server.server_close(); company.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    init = commands.add_parser("init")
    init.add_argument("--state", required=True)
    init.add_argument("--application", choices=POLICIES, required=True)
    init.add_argument("--country", choices=("RU", "DE"), required=True)
    init.add_argument("--executable", required=True)
    init.add_argument("--resources", required=True)
    init.add_argument("--configuration")
    init.add_argument("--source")
    user = commands.add_parser("user", help="create/rotate or revoke a user's token")
    user.add_argument("--state", required=True)
    user.add_argument("--user", required=True)
    user.add_argument("--role", required=True)
    user.add_argument("--revoke", action="store_true")
    users = commands.add_parser("users", help="list active user IDs and roles without credentials")
    users.add_argument("--state", required=True)
    server = commands.add_parser("serve")
    server.add_argument("--state", required=True)
    server.add_argument("--bind", default="127.0.0.1")
    server.add_argument("--port", type=int, default=8790)
    server.add_argument("--cert"); server.add_argument("--key")
    args = parser.parse_args()
    try:
        {"init": initialize, "user": add_user, "users": list_users, "serve": serve}[args.action](args)
    except (ValueError, OSError, sqlite3.Error) as error:
        parser.exit(1, f"[server] {error}\n")


if __name__ == "__main__":
    main()

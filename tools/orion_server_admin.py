#!/usr/bin/env python3
"""Offline backup, verification and restore for an Orion company server."""
import argparse
import contextlib
import hashlib
import json
import os
import platform
from pathlib import Path
import re
import shutil
import sqlite3
import sys
import tempfile

import orion_server as server

MAX_LOCAL_COPY = 500_000_000
SNAPSHOT = re.compile(r"(?:initial|revision-[A-Za-z0-9_-]+)\Z")


def hash_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def snapshot_path(root, name):
    if not isinstance(name, str) or not SNAPSHOT.fullmatch(name):
        raise ValueError("invalid snapshot name")
    path = root / "snapshots" / name
    if path.is_symlink() or not path.is_dir():
        raise ValueError("missing or linked snapshot")
    return path


def digest_snapshot(path):
    digest = hashlib.sha256()
    for entry in server.files(path):
        name = entry.relative_to(path).as_posix().encode()
        digest.update(len(name).to_bytes(4, "big")); digest.update(name)
        with entry.open("rb") as stream:
            data = stream.read(server.MAX_COMPANY + 1)
        digest.update(len(data).to_bytes(8, "big")); digest.update(data)
    return digest.hexdigest()


def open_readonly(path):
    if path.is_symlink() or not path.is_file():
        raise ValueError("missing or linked state database")
    return sqlite3.connect(path.resolve().as_uri() + "?mode=ro&immutable=1", uri=True)


def inspect_database(path):
    with contextlib.closing(open_readonly(path)) as db:
        if db.execute("PRAGMA integrity_check").fetchone() != ("ok",):
            raise ValueError("SQLite integrity check failed")
        try:
            identities = db.execute("SELECT id,company_id FROM company_identity").fetchall()
        except sqlite3.Error as error:
            raise ValueError("missing company identity") from error
        if (len(identities) != 1 or identities[0][0] != 1 or
                not isinstance(identities[0][1], str) or not re.fullmatch(r"[0-9a-f]{32}", identities[0][1])):
            raise ValueError("invalid company identity")
        rows = db.execute("SELECT revision,snapshot,digest,previous FROM current WHERE id=1").fetchall()
        if len(rows) != 1:
            raise ValueError("invalid current revision")
        revision, current, digest, previous = rows[0]
        if type(revision) is not int or revision < 0 or not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise ValueError("invalid current metadata")
        if not SNAPSHOT.fullmatch(current) or (previous is not None and not SNAPSHOT.fullmatch(previous)):
            raise ValueError("invalid snapshot reference")
        prior = "0" * 64
        last_id, count = 0, 0
        for row in db.execute("SELECT id,utc,actor,command,outcome,revision,previous,digest FROM audit ORDER BY id"):
            row_id, stamp, actor, command, outcome, rev, recorded_previous, recorded_digest = row
            if row_id <= last_id or recorded_previous != prior:
                raise ValueError("audit chain is broken")
            expected = hashlib.sha256(server.canonical([stamp, actor, command, outcome, rev, prior]).encode()).hexdigest()
            if recorded_digest != expected:
                raise ValueError("audit chain is broken")
            prior, last_id = expected, row_id
            count += 1
        return revision, current, digest, previous, prior, count


def verify(bundle):
    bundle = Path(bundle).resolve()
    if not bundle.is_dir(): raise ValueError("backup directory does not exist")
    manifest_path = bundle / "manifest.json"
    if manifest_path.is_symlink() or not manifest_path.is_file(): raise ValueError("missing backup manifest")
    if manifest_path.stat().st_size > server.MAX_REQUEST: raise ValueError("backup manifest too large")
    info = server.decode(manifest_path.read_bytes())
    if not isinstance(info, dict) or info.get("format") != 1:
        raise ValueError("unsupported backup format")
    config, snapshots = info.get("company"), info.get("snapshots")
    if not isinstance(config, dict) or config.get("application") not in server.POLICIES or config.get("country") not in ("RU", "DE"):
        raise ValueError("invalid company metadata")
    if config.get("configuration") is not None and (not isinstance(config["configuration"], str) or
                                                     not server.IDENTIFIER.fullmatch(config["configuration"])):
        raise ValueError("invalid configuration identifier")
    if not isinstance(snapshots, dict) or not snapshots:
        raise ValueError("invalid snapshot inventory")
    if set(p.name for p in bundle.iterdir()) != {"manifest.json", "state.sqlite", "snapshots"}:
        raise ValueError("unexpected backup entries")
    if (bundle / "snapshots").is_symlink() or not (bundle / "snapshots").is_dir():
        raise ValueError("invalid snapshot directory")
    if set(p.name for p in (bundle / "snapshots").iterdir()) != set(snapshots):
        raise ValueError("snapshot inventory mismatch")
    database = bundle / "state.sqlite"
    if database.is_symlink() or not database.is_file() or hash_file(database) != info.get("database_sha256"):
        raise ValueError("state database checksum mismatch")
    revision, current, digest, previous, audit_head, audit_count = inspect_database(database)
    expected_names = {current} | ({previous} if previous else set())
    if set(snapshots) != expected_names:
        raise ValueError("metadata references do not match backup snapshots")
    for name, expected in snapshots.items():
        path = snapshot_path(bundle, name)
        if digest_snapshot(path) != expected:
            raise ValueError("snapshot checksum mismatch")
    if snapshots[current] != digest:
        raise ValueError("current snapshot does not match database")
    if info.get("revision") != revision or info.get("audit_head") != audit_head or info.get("audit_count") != audit_count:
        raise ValueError("backup metadata mismatch")
    return info


def diagnose_backup(bundle):
    report = {"format": 1, "subject": "backup", "verification": "failed",
              "environment": {"os": platform.system(), "python": platform.python_version()}}
    try:
        info = verify(bundle)
    except (ValueError, OSError, sqlite3.Error, KeyError, TypeError, UnicodeError, RecursionError) as error:
        known = {
            "SQLite integrity check failed": "database_integrity",
            "state database checksum mismatch": "database_checksum",
            "snapshot checksum mismatch": "snapshot_checksum",
            "current snapshot does not match database": "snapshot_metadata",
            "audit chain is broken": "audit_chain",
            "unsupported backup format": "unsupported_format",
        }
        report["failure"] = known.get(str(error), "invalid_or_unavailable")
        return report
    report["verification"] = "passed"
    report["application"] = info["company"]["application"]
    report["country"] = info["company"]["country"]
    report["backup_format"] = info["format"]
    return report


def staged_directory(destination):
    destination = Path(destination).resolve()
    if destination.exists(): raise ValueError("destination must not exist")
    if not destination.parent.is_dir(): raise ValueError("destination parent does not exist")
    return destination, Path(tempfile.mkdtemp(prefix=".orion-admin-", dir=destination.parent))


def backup(source, destination):
    source = Path(source).resolve()
    if not source.is_dir() or not (source / "server.json").is_file() or not (source / "state.sqlite").is_file():
        raise ValueError("company directory is incomplete")
    destination = Path(destination).resolve()
    if destination.is_relative_to(source): raise ValueError("backup destination must be outside company")
    with server.exclusive(source / "server.lock"):
        config = server.decode((source / "server.json").read_bytes())
        if config.get("application") not in server.POLICIES or config.get("country") not in ("RU", "DE"):
            raise ValueError("invalid company configuration")
        db_path = source / "state.sqlite"
        # Include rollback journal/WAL and an extra database-sized copy in the estimate.
        footprint = sum(p.stat().st_size for p in (db_path, Path(str(db_path) + "-wal"), Path(str(db_path) + "-journal")) if p.exists()) * 2
        if db_path.is_symlink(): raise ValueError("linked state database")
        with contextlib.closing(sqlite3.connect(db_path)) as live:
            live.execute("PRAGMA query_only=ON")
            row = live.execute("SELECT revision,snapshot,digest,previous FROM current WHERE id=1").fetchone()
            if not row: raise ValueError("missing current metadata")
            names = {row[1]} | ({row[3]} if row[3] else set())
            for name in names:
                footprint += sum(p.stat().st_size for p in server.files(snapshot_path(source, name)))
            if footprint > MAX_LOCAL_COPY:
                raise ValueError("backup exceeds 500 MB local working limit; use cloud storage")
            destination, staging = staged_directory(destination)
            try:
                snapshots = staging / "snapshots"; snapshots.mkdir(mode=0o700)
                for name in names:
                    target = snapshots / name; target.mkdir(mode=0o700)
                    server.copy_company(snapshot_path(source, name), target)
                with contextlib.closing(sqlite3.connect(staging / "state.sqlite")) as copy:
                    live.backup(copy)
                os.chmod(staging / "state.sqlite", 0o600)
                with (staging / "state.sqlite").open("rb") as handle: os.fsync(handle.fileno())
                revision, current, digest, previous, audit_head, audit_count = inspect_database(staging / "state.sqlite")
                hashes = {name: digest_snapshot(snapshot_path(staging, name)) for name in names}
                if hashes[current] != digest: raise ValueError("current snapshot integrity check failed")
                info = {"format": 1, "company": {"application": config["application"], "country": config["country"],
                        "configuration": config.get("configuration")}, "revision": revision,
                        "database_sha256": hash_file(staging / "state.sqlite"), "snapshots": hashes,
                        "audit_head": audit_head, "audit_count": audit_count}
                server.private_write(staging / "manifest.json", server.canonical(info))
                verify(staging)
                for name in names: server.seal(snapshot_path(staging, name))
                server.sync_directory(snapshots); server.sync_directory(staging)
                os.rename(staging, destination); server.sync_directory(destination.parent)
            finally:
                if staging.exists(): shutil.rmtree(staging)


def restore(bundle, destination, executable, resources):
    bundle = Path(bundle).resolve()
    info = verify(bundle)
    executable, resources = Path(executable).resolve(strict=True), Path(resources).resolve(strict=True)
    if not executable.is_file() or not resources.exists(): raise ValueError("invalid executable or resources")
    total = (bundle / "state.sqlite").stat().st_size
    for name in info["snapshots"]:
        total += sum(p.stat().st_size for p in server.files(snapshot_path(bundle, name)))
    if total * 2 > MAX_LOCAL_COPY: raise ValueError("restore exceeds 500 MB local working limit; use cloud storage")
    destination, staging = staged_directory(destination)
    if destination.is_relative_to(bundle):
        shutil.rmtree(staging); raise ValueError("destination must be outside backup")
    try:
        (staging / "snapshots").mkdir(mode=0o700)
        for name in info["snapshots"]:
            target = staging / "snapshots" / name; target.mkdir(mode=0o700)
            server.copy_company(snapshot_path(bundle, name), target)
        shutil.copyfile(bundle / "state.sqlite", staging / "state.sqlite")
        os.chmod(staging / "state.sqlite", 0o600)
        if hash_file(staging / "state.sqlite") != info["database_sha256"]:
            raise ValueError("backup changed during restore")
        for name, expected in info["snapshots"].items():
            if server.seal(snapshot_path(staging, name)) != expected:
                raise ValueError("backup changed during restore")
        with (staging / "state.sqlite").open("rb") as handle: os.fsync(handle.fileno())
        config = dict(info["company"], executable=str(executable), resources=str(resources))
        server.private_write(staging / "server.json", server.canonical(config))
        server.private_write(staging / "users.json", "[]")
        server.private_write(staging / "READY", "1\n")
        server.sync_directory(staging / "snapshots"); server.sync_directory(staging)
        os.rename(staging, destination); server.sync_directory(destination.parent)
    finally:
        if staging.exists(): shutil.rmtree(staging)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    command = commands.add_parser("backup"); command.add_argument("source"); command.add_argument("destination")
    command = commands.add_parser("verify"); command.add_argument("backup")
    command = commands.add_parser("diagnose"); command.add_argument("backup")
    command = commands.add_parser("restore"); command.add_argument("backup"); command.add_argument("destination")
    command.add_argument("--executable", required=True); command.add_argument("--resources", required=True)
    args = parser.parse_args()
    try:
        if args.action == "backup": backup(args.source, args.destination)
        elif args.action == "verify": print(json.dumps(verify(args.backup), sort_keys=True, indent=2))
        elif args.action == "diagnose":
            report = diagnose_backup(args.backup)
            print(json.dumps(report, sort_keys=True, indent=2))
            if report["verification"] != "passed": return 1
        else: restore(args.backup, args.destination, args.executable, args.resources)
    except (ValueError, OSError, sqlite3.Error, KeyError, TypeError) as error:
        parser.exit(1, f"[orion-server-admin] {error}\n")


if __name__ == "__main__": sys.exit(main())
